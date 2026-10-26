---
name: find-address
description: Locate a value in Zexion's running memory (an upgrade flag, the inventory, a location's collected flag), turn it into a byte pattern that survives game updates and ASLR, and record it in docs/re/memory.md with evidence. Use whenever the client or a spike needs to read or write game state.
---

# Find an address

The exe is ASLR'd (DynamicBase, HighEntropyVA) and still being patched, so an address is only
good for one process on one build. What gets recorded and coded is a **byte pattern** in the
exe's code plus the arithmetic from the match to the value.

## 1. Know the build

```sh
.venv/Scripts/python tools/build_id.py
```

If the hash is not in `docs/re/builds.md`, add it as a new build first. Every result below names
the build.

## 2. Find the value

With the game running and the user at the controls:

1. Attach with `client/process.py` (`Process("Zexion.exe")`, the Windows API through ctypes).
   Read the module base and size from its `module()`.
2. Snapshot the candidate memory (the module's writable sections first, then the heap regions
   from `VirtualQueryEx`), save the snapshot in `lab/` with the build and a note of the game
   state.
3. Ask the user to do one thing that changes the value (pick up the item, spend a missile).
   Snapshot again and keep only addresses that changed the expected way. Repeat with the value
   unchanged too, to drop counters and timers.
4. When a handful are left, confirm by writing one (only in a lab slot, never while playing a
   real save) and having the user check the effect.

`tools/memscan.py` does this: `find HEX` searches writable memory, `snap NAME` saves a snapshot
to `lab/mem/`, `cmp A B [--from X --to Y]` lists bytes that changed between two snapshots,
`read` dumps live memory, and `write ADDR HEX --yes` writes it and reads it back. Ask before
every `write`. It only touches the game process, never the install or save folder.
For a value that is a state rather than a quantity (in play, paused, dead), take two
`snap --module-only` snapshots in each state and run `classify play=A,B pause=C,D ...`: it keeps
bytes that hold steady within each state and tell the target state from the others, and
`--out NAME` saves them. `record NAME --candidates NAME` then logs every change of those bytes,
timestamped, while the user goes through short-lived states such as a load or a death.
For an inventory byte, `tools/grant.py set ITEM VALUE --expect N --yes` is safer: it locates
the inventory by pattern, refuses if the byte is not as expected, and logs to `lab/grants.jsonl`.

A shortcut that worked on B1: the game keeps save data in the same layout as the `.sav` file,
so searching memory for a distinctive run of save bytes (the seed, the item table) finds it.
See `docs/re/memory.md`.

## 3. Find a stable route to it

- **In the exe's static data** (`.data`, `.bss`): the offset from the module base is stable for
  this build only. Go to step 4 to find the code that uses it.
- **On the heap**: find the pointer chain. Search the module's static data for pointers to the
  object's base, then confirm the chain survives a restart of the game.

## 4. Turn it into a pattern

`tools/make_pattern.py EXE RVA [--size N]` does the steps below on a copy of the exe and prints
the best candidates; add the chosen ones to `client/patterns.py` (two per target, from
different code, with the RVA the target has on each known build in its `rvas`) and check them
with `tools/check_patterns.py EXE` and then `--live`. For a
function, it finds call sites (kind `rip`: the `call rel32` field leads to the function) and the
function's own start (kind `at`: the match itself is the address); use call sites from at least
two functions, plus the start pattern. By hand:

Disassemble the exe file read-only with `capstone` (no debugger needed) and find instructions
that reference the static address: x64 code uses RIP-relative operands, so look for
`[rip + disp32]` where `next_instruction + disp32` equals the address.

- Take 16 to 32 bytes around one such instruction. Replace every relocated or displacement byte
  (the `disp32`, any `call rel32` or `jmp rel32`) with wildcards (`??`).
- Search the whole `.text` section for the pattern. **It must match exactly once.** If not,
  widen it.
- Record how the value is reached from the match: "match + 3: disp32; target = match + 7 +
  disp32; value at target + 0x1C".
- Prefer a reference inside the function that does the thing you care about (the pickup
  routine, the inventory UI) over an arbitrary one. A pattern in meaningful code is more likely
  to survive a patch.

If finding "what writes this value" needs a debugger (watchpoints), stop and ask the user to
install x64dbg or Cheat Engine. Don't install anything yourself.

## 5. Record it

In `docs/re/memory.md`, in the format from `docs/re/README.md`:

- Value: the pattern, the arithmetic to the value, its type and size, and what its contents mean
  (bit 3 = Wall Jump Boots, and so on).
- Status: **measured** once the pattern resolves to the right value in a fresh game process.
  **working in game** only once something built on it (a client read or write) has done the
  right thing in the real game.
- Method: memory scan + disassembly, and the game actions used to change the value.
- Build, and the date.
- Evidence: the scan steps with the counts left after each, and the `lab/` snapshot names.

## 6. After a game update

Run `build_id.py`. If the build changed, re-run every pattern in `docs/re/memory.md` against
the new exe and record each one as still matching once, moved, or broken.
