---
name: save-lab
description: Experiment with Zexion save files safely - pause Steam Cloud, back up the real save folder, copy a slot into lab/, diff two saves, patch a copy, install a patched copy into an empty slot, and restore. Use for any work that reads, compares or changes .sav files, and before anything that could make the game write a save.
---

# Save lab

The real saves live in `%APPDATA%\Gallant Leaf\Zexion` (on Linux, the folder `save_dir()` in
`tools/protected.py` finds: SDL's under `~/.local/share`, or Flatpak Steam's) and Steam Cloud
syncs them, between Windows and Linux too. The user has
real progress in some slots. Nothing of ours writes there except `tools/save_lab.py install` and
`restore` (the Archipelago client never does), and the mod's helpers, which write only
`ZexionAP-seed-<NAME>.sav` there, a copy of each seed one of the saves uses, so Steam Cloud
carries it to another machine (`CLAUDE.md`, "Rules"; `native/README.md`, "Seed files in the
save folder"). The write-guard hook blocks every other route through Bash, PowerShell, Monitor,
the terminal tool and the file tools (`CLAUDE.md`, "Repo tooling"), those copies' names too.

## 1. Pause Steam Cloud for Zexion first

Before any session that installs a save or plays a lab slot, ask the user to turn Steam Cloud
off for Zexion, and wait until they confirm. You can't do it for them, and you can't check it.

> Steam → Library → right-click Zexion → Properties → General → turn off "Keep game saves in the
> Steam Cloud for Zexion".

With it on, Steam can upload a test save over the cloud copy, or pull the cloud copy back over a
restore. Remind the user to turn it back on only after `restore` has put everything back and
the game has been closed. A restore changes the seed copies (`ZexionAP-seed-*.sav`) too, which
Steam uploads like the saves: another reason to keep Steam Cloud off until it is done.

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
against its original. The backup holds the mod's seed copies, `ZexionAP-seed-*.sav`, with the
saves; it leaves out a helper's temporary file (`*.tmp`), and warns while the game runs (a helper
may be writing a copy, and the game its saves): close the game for a backup you will restore from.
`install` refuses to run unless the newest backup is from the last 12
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

It fills either kind of empty slot. It refuses while Zexion is running, or when that can't be
told, as `restore` does (the game writes its copies of the slots back over an install, and
presumably over a restore: `save_lab.py --help`), so ask the user to close the game first. On
Linux the check has found the Linux build running (`docs/DEVELOPING.md`, "Platforms"); under
Proton it is not yet seen, so there have the user confirm the game is closed. It
also refuses a slot that is in use, a slot number that doesn't exist, and a missing or stale
backup. Over a deleted save it names the backup file that now
holds the old data. It logs to `lab/installs.log`, with `was=template` or `was=deleted` for the
kind of slot it filled. Then the user starts the game.

## 6. Put things back

```sh
.venv/Scripts/python tools/save_lab.py restore lab/backups/<time> --slot 30 --yes
.venv/Scripts/python tools/save_lab.py restore lab/backups/<time> --yes        # everything
```

Ask first, and have the user close the game: like `install`, `restore` refuses while Zexion is
running. It refuses a backup of another save folder (one taken with `--save-dir` or
`ZEXION_SAVE_DIR` set; restore it with the same setting), checks the backup against its
manifest, then backs up the current state before overwriting, so a restore can itself be undone.
A full restore (no `--slot`) puts the backup's seed copies back too, each with a fresh
modification time, so the helpers' merge (`native/README.md`, "Seed files in the save folder":
the larger disk count and the labels with more lines stand, the copy last written is the newer)
reads a restored copy as the newest, and a copy whose seed no save uses is read but not refreshed.
`--slot N` restores that save alone and leaves the copies as they are. A restore never writes a
`*.tmp` file.

## 7. Record what you learned

Every finding goes into `docs/re/save-format.md` in the format from `docs/re/README.md`: value,
status, method, build, evidence. Name the slots and lab files used and what was done in the game
between them. The `.sav` bytes themselves never get committed. save-format.md is long: find the
section with a search for `^## ` and read only that range.
