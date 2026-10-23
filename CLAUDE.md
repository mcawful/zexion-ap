# Working in zexion-ap

This file guides Claude Code when working in this repository.

Archipelago (AP) multiworld support for **Zexion** (Gallant Leaf, Steam app 3392510). The game
is a native x64 exe on SDL2 and FMOD, with mod folders only for sprites and translations
([`docs/re/builds.md`](docs/re/builds.md)), so everything here comes from reverse engineering.
The rules about evidence below exist because of that.

**Status:** Phase 1. Phase 0's go/no-go spike passed. There is no APWorld yet. `client/`
holds the AP client, which has run a solo seed in the game with a local AP server, and
the module that finds and accesses game state by byte pattern. `native/` holds an in-game
helper DLL, which runs in the game and gives received items there with the game's own popup,
sound and HUD refresh (helper phase 2, live-tested on one seed), and starts a new game from the
AP seed: ARCHIPELAGO in place of RANDOMIZER after an empty slot is picked (helper phase 3,
live-tested on a Standard seed). While loaded, it also offers the game's escape skip after
Zexion is grabbed (not yet run in game).
[`docs/re/README.md`](docs/re/README.md) lists what has been measured so far.

## Commands

One-time setup. Archipelago 0.6.8 refuses Python 3.14, so the venv is 3.13, managed by `uv`.
`pip install --user` does not put `uv` on `PATH` here, hence `python -m uv`. The venv is seeded
with `pip` because AP's own updater uses it.

```sh
python -m pip install --user uv
python -m uv python install 3.13
python -m uv venv --seed --python 3.13 .venv
python -m uv pip install --python .venv -r requirements-dev.txt
.venv/Scripts/python tools/ap_checkout.py     # AP at the tag in AP_VERSION, into .ap/ (gitignored)
```

`tools/ap_checkout.py` clones AP, installs its requirements (including each world's) through
AP's `ModuleUpdate`, and links `worlds/zexion/` into the checkout once that folder exists.

Checks. CI's Python jobs run these three; `/preflight` runs them too:

```sh
.venv/Scripts/ruff format --check .
.venv/Scripts/ruff check .
.venv/Scripts/python -m pytest
```

AP's world tests join this list when `worlds/zexion/` exists.

The native helper (`native/`, needs Visual Studio's C++ tools and CMake). CI's native job
builds it and runs these on Windows (the tests include grants applied in the frame calls of a
stand-in game, and 300 loads and unloads of a test build while the function it hooks is called
nonstop), then `tests/test_native.py` loads it into a stand-in process; without a build, that
test skips:

```sh
cmake -S native -B native/build -A x64
cmake --build native/build --config Release
ctest --test-dir native/build -C Release --output-on-failure
```

Reverse-engineering tools. These and the client are the only code allowed to open the game or
save folders:

```sh
.venv/Scripts/python tools/build_id.py          # which game build is installed (read-only)
.venv/Scripts/python tools/save_lab.py slots    # see /save-lab for the rest
.venv/Scripts/python tools/memscan.py find HEX  # search the running game; see /find-address
.venv/Scripts/python tools/game_data.py RandoCells --exe   # copy game data into lab/data/<build>/
.venv/Scripts/python tools/randocells.py SAVE --fresh   # reachable locations of a new seed
.venv/Scripts/python tools/make_pattern.py EXE RVA      # candidate byte patterns for data or a function
.venv/Scripts/python tools/check_patterns.py EXE        # client/patterns.py against an exe copy
.venv/Scripts/python tools/check_patterns.py --live     # ...or against the running game (reads only)
.venv/Scripts/python tools/grant.py show                # the live inventory by name
.venv/Scripts/python tools/grant.py set ITEM VALUE --expect N --yes   # grant by hand; ask first
.venv/Scripts/python tools/grant.py watch               # print inventory changes as they happen
.venv/Scripts/python tools/helper.py status            # is the native helper loaded; its frame rate
.venv/Scripts/python tools/helper.py load --yes        # load it into the running game; ask first
.venv/Scripts/python tools/helper.py unload --yes      # unhook and unload it; ask first
```

`grant.py set` changes the running game: ask before each one, only while the person playing is
standing still in play (it refuses unless `Game.settled_state()` reads in play), and only in a lab slot (the game saves the grant into the slot). It
logs every attempt to `lab/grants.jsonl` before writing. It writes the inventory byte only;
Energy Tanks, Missile Containers and Mega Bombs also need the player-state fields in
`docs/re/memory.md` ("Granting each kind of item").

`helper.py load` and `unload` change the running game too: ask before each one. They log to
`lab/helper.jsonl`, and `load` refuses a build `client/game.py` does not know.

The client. It needs Archipelago from `.ap/` with the world installed there (AP will not connect
a client whose game it does not know):

```sh
.venv/Scripts/python client/zexion_client.py --nogui archipelago://NAME@localhost:38281
```

Once connected, its `/prepare SLOT` writes the seed into a slot holding a new randomizer game
of the seed's mode (run in game for Standard only so far), saved at once (a slot that already holds the seed is left alone). It
replaces the slot file after a verified backup, which goes to `zexion_backups/` in
Archipelago's user folder (not `lab/backups/`), and only while Zexion is closed. On the real
save folder, pause Steam Cloud first and ask before each one, as for `save_lab.py install`.
With `ZEXION_SAVE_DIR` pointing at a copy it touches nothing real. `ZEXION_AP_DIR` points the
client at another Archipelago folder than `.ap/Archipelago`.

When it attaches to the game, the client also loads the native helper built from `native/`
(`native/build/Release/zexion_ap.dll`, or `ZEXION_DLL`) and sends received items through it,
with the game's popup; ask before a run that loads it into the game, as for `helper.py load`.
Loading it also changes the game while it runs: grabbing Zexion offers the escape skip.
`--no-helper`, a missing build or a helper that fails means items are written directly, with
no popup; the client's `/unload_helper` unloads it as if it had failed, to test that (ask
first), and the client loads it again within 30 seconds. Popups are only for items received
after the server's first full list, so a reconnect does not replay them. With the helper, the client also arms it with the seed,
once per connection: for a seed whose slot data carries the menu setup (from the world on
`apworld`), picking an empty slot then offers ARCHIPELAGO, which starts a new game holding
the seed with no `/prepare` (run in game for Standard and Easy). An OHKO seed keeps
RANDOMIZER: started in the seed's mode, the helper writes the seed in as the game starts
(run in game for all three modes; `--keep-randomizer` takes this path for any seed).

The tools find the game through Steam's library list and the saves through `%APPDATA%`.
`ZEXION_GAME_DIR` (or `build_id.py --game-dir`), `ZEXION_SAVE_DIR` (or
`save_lab.py --save-dir`) and `ZEXION_LAB_DIR` point them elsewhere; the tests use the last two.

Nothing in `tests/` needs the game, and CI never runs it.

## Layout

- `worlds/zexion/`: the APWorld. Not created yet.
- `client/`: the AP client. `zexion_client.py` (`CommonClient` wiring and `/prepare`),
  `session.py` (one poll: checks, the goal, grants), `reconcile.py` (what to give: what the
  player should own minus what the inventory holds), `save_patch.py` and `prepare.py` (writing
  a seed into a fresh save), `patterns.py` (byte patterns for each piece of game state) and
  `game.py` (attach, locate by pattern, read and write with checks, and `state()`: in play,
  menu, dead or no save loaded). `native.py` loads the native helper and talks to it;
  `newgame.py` turns the seed into the helper's new-game request (`ap_newgame`).
- `native/`: the native helper, `zexion_ap.dll`, which runs inside the game: a once-per-frame
  hook on the game's thread and a command pipe (`native/README.md`). Helper phase 2 applies
  grants between frames, with the game's popup, sound and HUD refresh; helper phase 3 starts
  a new game from the AP seed (ARCHIPELAGO); both working in game. It also patches the game's
  loaded event 3665 so the escape skip is offered in any game (`hello` reports `skip=`; not
  yet run in game).
- `tools/`: RE and save tooling. `tools/protected.py` defines the protected folders for both the
  tools and the write-guard hook.
- `docs/re/`: the reverse-engineering record. Every fact carries its evidence (below).
- `tests/`: pytest, host-only.
- `lab/` (gitignored): save backups, working copies, memory snapshots.
- `private/` (gitignored): notes that must not be public, such as the draft to the developer.
- `.ap/` (gitignored): the pinned Archipelago checkout.

## Rules

- **Measured vs. assumed.** Every fact in `docs/re/` (save offsets, memory patterns, the
  `RandoCells` format, item and flag IDs) states how it was established (save diff, memory scan,
  disassembly, the developer, or an assumption and why) and on which game build
  ([`docs/re/builds.md`](docs/re/builds.md)). An assumption is labeled as one until something
  measures it. Nothing is called "working" until it has run in the real game, and the entry says
  when and on which build. A test passing on synthetic data is not that.
- **No hard-coded addresses.** ASLR is on (measured, `docs/re/builds.md`) and the game is still
  being patched. Code finds things
  by byte pattern plus offset, and checks that each pattern matches exactly once.
- **The game install and the real save folder are read-only to us.** Hooks block direct writes.
  Save experiments go through `tools/save_lab.py`, which works on copies in `lab/`. Its
  `install` fills only an empty slot, and only after a verified backup; its `restore` backs up
  the present state before putting a backup back. Ask before either one, every time. The
  client's `/prepare` is the one other writer (see Commands).
- **Documentation describes the repo as it is.** A change that makes a doc line false fixes it in
  the same change, and new behavior gets described where its subject already lives. Skill and
  agent files count as documentation.
- **American English**, in prose and in names that read as prose (test names, log lines,
  errors).
- **Branches:** `main` plus short-lived topic branches, deleted once merged.
- **This repo is public.** Never commit game files, exe or memory dumps, saves (real or patched),
  Steam account ids, or anything from `lab/` or `private/`. Facts about formats go in
  `docs/re/`; the bytes they came from stay local.

## Repo tooling

Committed under `.claude/`, so they apply to anyone working here:

- `.claude/settings.json`:
  - `guard_protected.py` (before Write, Edit, MultiEdit, NotebookEdit, Bash and PowerShell)
    blocks writes into the game install, the save folder and Steam's cloud cache for the game. A
    shell command that names one of those folders runs only if every command in it is read-only,
    or if it is a single `.venv/Scripts/python tools/<name>.py` call; command substitution,
    `::` .NET calls and redirects to variable paths next to those folders are always blocked. It
    fails closed when the guard itself breaks on a call that names those folders. Its cases are
    in `tests/test_guard_hook.py`.
  - `deny` rules for Edit and Write on the same three folders back it up. The game-install rule
    refused a live Write to a decoy `steamapps/common/Zexion` folder on 2026-10-20; rule changes made mid-session did not take effect, so
    the other two are untested until a new session.
  - `ruff_on_edit.py` (after Write, Edit and MultiEdit) runs `ruff format` and
    `ruff check --fix` on an edited `.py` file in this repo and reports what ruff could not fix.
    It uses `.venv`'s ruff or one on `PATH`; with neither, it does nothing.
  - An allowlist for read-only commands run constantly: git status/diff/log/show/branch, ruff
    check, pytest, `build_id.py`, and `save_lab.py slots` and `diff`.
- `/preflight`: ruff, pytest, and (once the world exists) AP's world tests against `.ap/`.
  Reports the first failure. Without the toolchain, the change is unverified.
- `/save-lab`: pausing Steam Cloud, then backing up, copying, diffing, patching, installing and
  restoring saves.
- `/find-address`: locate a value in memory, turn it into a byte pattern, record it with
  evidence.
- `docs-reviewer` subagent: documentation the change made wrong, or behavior nothing describes.
- `logic-reviewer` subagent: APWorld rules not backed by `docs/re/locations.md`, and soft-locks.
- `re-auditor` subagent: client, tool and native-helper diffs with hard-coded addresses,
  unchecked reads, unsafe hooks, or ways to double-deliver or lose an item.
