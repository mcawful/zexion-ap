---
name: find-address
description: Locate a value in Zexion's running memory (an upgrade flag, the inventory, a location's collected flag), turn it into a byte pattern that survives game updates and ASLR, and record it in docs/re/memory.md with evidence. Use whenever the client or a tool needs to read or write game state.
---

# Find an address

The exe is ASLR'd (DynamicBase, HighEntropyVA) and still being patched, so an address is only
good for one process on one build. What gets recorded and coded is a **byte pattern** in the
exe's code plus the arithmetic from the match to the value.

## 1. Know the build

Run `.venv/Scripts/python tools/build_id.py`. If the hash is not in `docs/re/builds.md`, add it
as a new build first. Every result below names the build.

## 2. Find the value

With the game running and the user at the controls, use `tools/memscan.py` (its docstring has
every command). It only touches the game process, never the install or save folder.

1. `find HEX` searches writable memory. The game keeps save data in the same layout as the
   `.sav` file (B1, `docs/re/memory.md`), so a distinctive run of save bytes finds it.
2. Otherwise `snap NAME`, ask the user to do one thing that changes the value (pick up the
   item, spend a missile), `snap` again and `cmp A B --from X --to Y`. Repeat with the value
   unchanged too, to drop counters and timers.
3. For a state rather than a quantity (in play, paused, dead): two `snap --module-only` in each
   state, then `classify play=A,B pause=C,D ...`; `record NAME --candidates NAME` logs those
   bytes through short-lived states such as a load or a death.
4. Confirm by writing one with `write ADDR HEX --yes` (only in a lab slot, never on a real
   save) and having the user check the effect. Ask before every `write`. For an inventory byte,
   `tools/grant.py set` is safer (by pattern, compare-and-set, logged to `lab/grants.jsonl`).

If finding "what writes this value" needs a debugger (watchpoints), stop and ask the user to
install x64dbg or Cheat Engine. Don't install anything yourself.

## 3. Find a stable route to it

- **In the exe's static data** (`.data`, `.bss`): the offset from the module base is stable for
  this build only. Go to step 4 to find the code that uses it.
- **On the heap**: find the pointer chain. Search the module's static data for pointers to the
  object's base, then confirm the chain survives a restart of the game.

## 4. Turn it into a pattern

Run `tools/make_pattern.py EXE RVA [--size N]` on a copy of the exe (`game_data.py --exe`). It
wildcards every field that moves when code or data moves and keeps only windows that **match
exactly once** in `.text`; a pattern that matches twice is never used. Add the chosen ones to
`client/patterns.py`: two or more per target, from different code (for a function, call sites
in at least two functions plus its start pattern), with the target's RVA on each known build in
`rvas`. Prefer references inside the function that does the thing you care about (the pickup
routine, the inventory UI): they are likelier to survive a patch. Regenerate
`tests/patterns_frozen.json` (`.venv/Scripts/python tests/test_patterns.py`), then check with
`tools/check_patterns.py EXE` and `--live`.

## 5. Record it

memory.md is long: find the heading with a search for `^## ` and read only that range.

In `docs/re/memory.md`, in the format of `docs/re/README.md` ("How a fact is recorded"):

- **Value:** the target and the arithmetic from it to the value; the value's type, size and
  meaning (bit 3 = Wall Jump Boots). The patterns and their source instructions stay in
  `client/patterns.py`; the first offline and live match goes in "Byte patterns".
- **Status:** **measured** once it resolves to the right value in a fresh game process;
  **working in game** only once a client read or write built on it did the right thing in the
  real game.
- **Method:** how it was found, and the game actions used.
- **Build** and date.
- **Evidence:** the scan steps with the counts left after each, the `lab/` snapshot names.

After a game update, follow `docs/re/new-build.md`.
