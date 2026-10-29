---
name: save-lab
description: Experiment with Zexion save files safely - pause Steam Cloud, back up the real save folder, copy a slot into lab/, diff two saves, patch a copy, install a patched copy into an empty slot, and restore. Use for any work that reads, compares or changes .sav files, and before anything that could make the game write a save.
---

# Save lab

The real saves live in `%APPDATA%\Gallant Leaf\Zexion` and Steam Cloud syncs them. The user has
real progress in some slots. Nothing writes there except `tools/save_lab.py install` and
`restore` (the Archipelago client never does). The write-guard hook blocks every other route
through Bash, PowerShell and the file tools (not Monitor or the terminal tool: `CLAUDE.md`,
"Repo tooling").

## 1. Pause Steam Cloud for Zexion first

Before any session that installs a save or plays a lab slot, ask the user to turn Steam Cloud
off for Zexion, and wait until they confirm. You can't do it for them, and you can't check it.

> Steam → Library → right-click Zexion → Properties → General → turn off "Keep game saves in the
> Steam Cloud for Zexion".

With it on, Steam can upload a test save over the cloud copy, or pull the cloud copy back over a
restore. Remind the user to turn it back on only after `restore` has put everything back and
the game has been closed.

## 2. Look before changing anything

```sh
.venv/Scripts/python tools/save_lab.py slots
```

Lists every slot with size, date, hash and its state. Never assume a slot number is free; read
this. A slot is **empty** (the unused-slot template), **empty (deleted, data kept)** (a save
deleted in the game, which keeps its data but marks the slot empty), or **IN USE**. So 30 files
full of save data do not mean the folder is full: count the slots this command calls empty.
Each state: `tools/save_lab.py --help`. The two bytes a deletion sets, the evidence and what is
still assumed: `docs/re/save-format.md`, "Clearing a slot in the game sets two bytes and keeps
the rest".

## 3. Back up

```sh
.venv/Scripts/python tools/save_lab.py backup
```

Copies the whole folder to `lab/backups/<time>/` with a SHA-256 manifest, and checks every copy
against its original. `install` refuses to run unless the newest backup is from the last 12
hours and holds the target slot exactly as it is now (an older backup does not count), so take
one at the start of each session and again after the game has saved.

## 4. Work on copies

```sh
.venv/Scripts/python tools/save_lab.py copy 2                  # slot 2 -> lab/work/
.venv/Scripts/python tools/save_lab.py diff 1 2                # slots, or file paths
.venv/Scripts/python tools/save_lab.py diff lab/work/a.sav lab/work/b.sav --merge-gap 4
.venv/Scripts/python tools/save_lab.py patch lab/work/a.sav lab/work/spec.json
```

`diff` prints each changed run with its offset and both sides' first 16 bytes, plus a u8, u16,
or u32 and f32 reading for runs of exactly 1, 2 or 4 bytes. It lists 200 runs unless
`--max-runs` says otherwise; `--merge-gap N` joins runs separated by N or fewer unchanged
bytes. When sizes differ it compares the common length and says so; inserted data shifts
everything after it, so look for where the runs stop lining up.

A patch spec checks what it overwrites, so it can't land on the wrong save:

```json
{"patches": [{"offset": "0x7bae", "old": "ff ff", "new": "2e 00"}]}
```

To isolate one thing, change one thing in the game between two saves: save, do one action (pick
up one item, open one door), save again in another slot, then diff.

## 5. Install into the game

Only with the user's explicit yes, for this install. Say which lab file, which slot, and that
the slot is empty; for a deleted save, also that the data the game kept there will be
overwritten, and that the backup keeps it.

```sh
.venv/Scripts/python tools/save_lab.py install lab/work/a-patched.sav 30 --yes
```

It fills either kind of empty slot. It refuses a slot that is in use, a slot number that doesn't
exist, and a missing or stale backup. Over a deleted save it says that the old data is in the
backup, and names the file there. It writes through a temporary file (removed if the write
fails), reads the result back, and logs to `lab/installs.log`, with `was=template` or
`was=deleted` for the kind of slot it filled. Install with the game closed: the game keeps the
slots in memory, and one installed while it ran was written over by the game's own empty copy of
that slot (2026-10-28, `docs/re/memory.md`, "Feats and achievements"). Then the user starts it.

## 6. Put things back

```sh
.venv/Scripts/python tools/save_lab.py restore lab/backups/<time> --slot 30 --yes
.venv/Scripts/python tools/save_lab.py restore lab/backups/<time> --yes        # everything
```

Ask first. `restore` refuses a backup of another save folder (one taken with `--save-dir` or
`ZEXION_SAVE_DIR` set; restore it with the same setting), checks the backup against its
manifest, then backs up the current state before overwriting, so a restore can itself be undone.

## 7. Record what you learned

Every finding goes into `docs/re/save-format.md` in the format from `docs/re/README.md`: value,
status, method, build, evidence. Name the slots and lab files used and what was done in the game
between them. The `.sav` bytes themselves never get committed. save-format.md is long: find the
section with a search for `^## ` and read only that range.
