# Working in zexion-ap

This file guides Claude Code when working in this repository.

Archipelago (AP) multiworld support for **Zexion** (Gallant Leaf, Steam app 3392510). The game
is a native x64 exe on SDL2 and FMOD, with mod folders only for sprites and translations
([`docs/re/builds.md`](docs/re/builds.md)), so everything here comes from reverse engineering.
The rules about evidence below exist because of that.

**Status:** Phase 1, pre-release (Phase 0's go/no-go spike passed). The APWorld is in
development on the local-only branch `apworld` (below), not published. The client and the
in-game helper have played seeds in the game on build B1. What each feature does and when it
ran in the game: [`README.md`](README.md), and [`native/README.md`](native/README.md) for the
helper. What has been measured: [`docs/re/README.md`](docs/re/README.md). When the game is
patched: [`docs/re/new-build.md`](docs/re/new-build.md).

## Commands

One-time setup. Archipelago 0.6.8 refuses Python 3.14, so the venv is 3.13, managed by `uv`.
`pip install --user` does not put `uv` on `PATH` here, hence `python -m uv`. The venv is seeded
with `pip` because AP's own updater uses it. `tools/ap_checkout.py` clones AP, installs its
requirements (including each world's) through AP's `ModuleUpdate`, and links `worlds/zexion/`
into the checkout once that folder exists.

```sh
python -m pip install --user uv
python -m uv python install 3.13
python -m uv venv --seed --python 3.13 .venv
python -m uv pip install --python .venv -r requirements-dev.txt
.venv/Scripts/python tools/ap_checkout.py     # AP at the tag in AP_VERSION, into .ap/ (gitignored)
```

Checks. CI's Python jobs run the first three; `/preflight` runs them too, and AP's world tests
(the last, on `apworld`, from the pinned checkout `tools/ap_checkout.py` links the world into):

```sh
.venv/Scripts/ruff format --check .
.venv/Scripts/ruff check .
.venv/Scripts/python -m pytest
cd .ap/Archipelago && AP_TEST_WORLDS=zexion ../../.venv/Scripts/python -m pytest test/general worlds/zexion/test
```

The native helper (`native/`, needs Visual Studio's C++ tools and CMake). CI's native job
builds it and runs these on Windows (the tests include grants applied in the frame calls of a
stand-in game, and 300 loads and unloads of a test build while the function it hooks is called
nonstop), then `tests/test_native.py` loads it into a stand-in process (that one test skips
without a build):

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
.venv/Scripts/python tools/game_data.py RandoCells MapData Text --exe   # copy game data into lab/data/<build>/
.venv/Scripts/python tools/randocells.py SAVE --fresh   # reachable locations of a new seed
.venv/Scripts/python tools/gen_world_data.py           # regenerate worlds/zexion/data.py (on apworld)
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
standing still in play (it refuses unless `Game.settled_state()` reads in play), and only in a
lab slot (the game saves the grant into the slot). It logs every attempt to `lab/grants.jsonl`
before writing. It writes the inventory byte only; Energy Tanks, Missile Containers and Mega
Bombs also need the player-state fields in `docs/re/memory.md` ("Granting each kind of item").
`helper.py load` and `unload` change the running game too: ask before each one. They log to
`lab/helper.jsonl`, and `load` refuses a build `client/game.py` does not know.

The client, a package run from the repo root (`python client/zexion_client.py` runs the same).
From source it needs Archipelago from `.ap/` with the world installed there (AP will not
connect a client whose game it does not know); from the Launcher, the Launcher's own. Its
window shows what the player acts on (the connection, the helper, how to start, warnings);
`--verbose` also shows what it sets up in the game for the seed (the labels, the ASSIST lock,
INTEL CHIP and so on), which live tests of the client read in its log.

```sh
.venv/Scripts/python -m client.zexion_client --nogui --verbose archipelago://NAME@localhost:38281
```

- **The helper.** On attaching, the client uses a helper of its own version already in the
  game: the player may put `zexion_ap.dll` and the proxy `version.dll`
  (`native/build/proxy/Release/`) in the game's folder, which loads it as the game starts. That
  is the player's own step, with the game closed: our tools never write the game's folder.
  Otherwise it loads the one built from `native/` (`native/build/Release/zexion_ap.dll`, or
  `ZEXION_DLL`), unloading a helper of another version first; with no build it loads the game
  folder's copy, and with no build and that copy of another version it cannot use one
  (`native/README.md`, "Getting in"). A build that loads as another version (out of date) is
  refused until the file changes: rebuild `native/`. Ask before a run that loads it into the game, as for
  `helper.py load`. `--no-helper` writes items directly, with no popup (received Energy
  Shields wait for a helper);
  `/unload_helper` unloads the helper as if it had failed, to test that (ask first), and the
  client loads it again within 30 seconds. `--keep-randomizer`: README.md, "Features".
- **`/prepare SLOT`**, once connected, writes the seed into a slot holding a new randomizer
  game of the seed's mode, saved at once (a slot that already holds the seed is left alone). It
  replaces the slot file after a verified backup, which goes to `zexion_backups/` in
  Archipelago's user folder (not `lab/backups/`), and only while Zexion is closed. On the real
  save folder, pause Steam Cloud first and ask before each one, as for `save_lab.py install`.
  With `ZEXION_SAVE_DIR` pointing at a copy it touches nothing real.
- `ZEXION_AP_DIR` points the client at another Archipelago folder than `.ap/Archipelago`
  (unused from the Launcher).

A release for Archipelago's own Launcher (on `apworld`, where the world is):
`.venv/Scripts/python tools/build_apworld.py` writes `dist/`: `zexion.apworld`, `zexion_ap.dll`
and `version.dll`. The apworld is `worlds/zexion` (without its tests) with the `client` package
in `zexion/client/` (`worlds.zexion.client`, which the world's Zexion Client entry runs), and
its `archipelago.json` is the world's plus Archipelago's container keys (`version`,
`compatible_version`). It packs only files git tracks, and writes nowhere in the game's or
the saves' folder (`tools/protected.py`). The build refuses an untracked file in
`worlds/zexion` or `client`, a manifest that does not name the game Zexion, a
missing DLL, a helper whose version is not the client's, and a DLL
older than its sources (`native/src`, `native/proxy` and `native/CMakeLists.txt`; after a
checkout that touched them, too: rebuild `native/` first). The packaged client has no helper
build of its own, so it uses the helper the player copied into the game's folder.

The tools find the game through Steam's library list and the saves through `%APPDATA%`.
`ZEXION_GAME_DIR` (or `build_id.py --game-dir`), `ZEXION_SAVE_DIR` (or
`save_lab.py --save-dir`) and `ZEXION_LAB_DIR` point them elsewhere; the tests use the last two.
Nothing in `tests/` needs the game, and CI never runs it.

## Layout

- `worlds/zexion/` (on `apworld`): the APWorld, `world.py` (the `World`), `items.py`,
  `locations.py`, `options.py`, `rules.py`, `web_world.py`, the player docs in `docs/`,
  `archipelago.json` (the manifest) and `test/`. `__init__.py` also registers the **Zexion
  Client** launcher entry (and `archipelago://` links for Zexion), which runs the client
  (`run_client`; from source, this repo's `client`). `logic.py` holds the game's rules (shared
  with `tools/randocells.py`); `starting.py` the starting items the client can give
  (generation refuses the rest); `data.py` is generated by `tools/gen_world_data.py` from the
  game's `RandoCells` and must not be edited by hand (how, and what backs it:
  `docs/re/locations.md`). `test/test_client_agrees.py` checks the client's codes and slot
  data readers against it.
- `client/`: the AP client, a package (relative imports). `zexion_client.py` (`CommonClient`
  wiring, `/prepare`), `state.py` (its state, by how long each part lasts, and the pure decisions on it: the
  popup count to store, the found record, warnings said once), `session.py` (one
  poll: checks, the goal, grants), `reconcile.py` (what to give: what the player should own
  minus what the inventory holds), `shields.py` (received Energy Shields), `ids.py` (the item
  and location codes, the game's sizes), `save_patch.py` and `prepare.py` (a seed into a fresh
  save), `patterns.py` (each target's byte patterns, one `rip()` or `at()` line each, and in
  `Target.rvas` its RVA on each build, for checks only; `tests/patterns_frozen.json` is a copy
  that `tests/test_patterns.py` compares with under pytest and rewrites when run as a script),
  `game.py` (attach, locate by pattern, read and write with checks, `state()`: in play, menu,
  dead or no save loaded; the client attaches with `patterns.CLIENT_TARGETS`, the tools with
  every target, the pickup counter included), `process.py` (the
  game's memory through the Windows API, as Archipelago's release bundles no such library;
  `tools/memscan.py` and the tests use it too), `pe.py` (the PE header fields the client and the
  tools read). `native.py` loads the helper and talks to it; its requests come from `newgame.py`
  (the seed as `ap_newgame`), `aplabels.py` (other players' items as `ap_labels`),
  `deathlink.py` (when a death goes out, when a received one is dealt: `die`), `hints.py`
  (which item each Area Map, or Intel Chip, hints, and its popup), `discs.py` (the OS Install
  Disks count, its popups, `discs` and `disk_slots`) and `offline.py` (`offline_put`: what it
  leaves the helper for AP saves without it).
- `native/`: the helper, `zexion_ap.dll`, which runs inside the game: a once-per-frame hook on
  the game's thread and a command pipe. `src/dllmain.cpp` (the hook, the pipe's requests),
  `src/game_calls.cpp` (`Game`: configure, grants, popups, each part's watch),
  `src/game_offline.cpp` (the offline look), `src/requests.cpp` (the requests' parsers),
  `src/text.cpp` (popup text layout), `src/game_mem.h`/`.cpp` and `src/game_layout.h`
  (fault-safe memory access, shared layouts), one file per part; the tests share
  `tests/check.h`. `proxy/` is the proxy `version.dll` (built into `build/proxy/Release/`).
  What each part does and when it ran in the game: `native/README.md`.
- `tools/`: RE and save tooling, and `build_apworld.py`, the release builder (on `apworld`).
  `tools/protected.py` defines the protected folders for the tools and the write-guard hook.
- `docs/re/`: the reverse-engineering record. Every fact carries its evidence (below).
- `tests/`: pytest, host-only. `test_cross_checks.py` reads `native/src` for what the client and
  the helper must agree on (the helper's version, the builds, the `config` keys).
  `test_build_apworld.py` and `test_gen_world_data.py` test tools that are on `apworld` only.
- `lab/` (gitignored): save backups, working copies, memory snapshots. `private/` (gitignored):
  notes that must not be public, such as the draft to the developer. `.ap/` (gitignored): the
  pinned Archipelago checkout.

## Rules

- **Measured vs. assumed.** Every fact in `docs/re/` (save offsets, memory patterns, the
  `RandoCells` format, item and flag IDs) states how it was established (save diff, memory scan,
  disassembly, the developer, or an assumption and why) and on which game build
  ([`docs/re/builds.md`](docs/re/builds.md)). An assumption is labeled as one until something
  measures it. Nothing is called "working" until it has run in the real game, and the entry says
  when and on which build. A test passing on synthetic data is not that.
- **No hard-coded addresses.** ASLR is on (measured, `docs/re/builds.md`) and the game is still
  being patched. Code finds things by byte pattern plus offset, and checks that each pattern
  matches exactly once.
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
- **Branches:** `main` plus short-lived topic branches, deleted once merged. The exception is
  `apworld`, which stays local until the owner decides to publish the logic derived from the
  game's data: never push it.
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
    refused a live Write to a decoy `steamapps/common/Zexion` folder on 2026-10-20; rule changes
    made mid-session did not take effect, so the other two are untested.
  - `ruff_on_edit.py` (after Write, Edit and MultiEdit) runs `ruff format` and
    `ruff check --fix` on an edited `.py` file in this repo and reports what ruff could not fix.
    It uses `.venv`'s ruff or one on `PATH`; with neither, it does nothing.
  - An allowlist for read-only commands run constantly: git status/diff/log/show/branch (in
    Bash and PowerShell), `ruff check`, `ruff format --check`, pytest, `build_id.py`, and
    `save_lab.py slots` and `diff`.
- `/preflight`: ruff, pytest, and (once the world exists) AP's world tests against `.ap/`.
  Reports the first failure. Without the toolchain, the change is unverified.
- `/save-lab`: pausing Steam Cloud, then backing up, copying, diffing, patching, installing and
  restoring saves. `/find-address`: locate a value in memory, turn it into a byte pattern,
  record it with evidence.
- Subagents: `docs-reviewer` (documentation the change made wrong, or behavior nothing
  describes), `logic-reviewer` (APWorld rules not backed by `docs/re/locations.md`, and
  soft-locks), `re-auditor` (client, tool and native-helper diffs with hard-coded addresses,
  unchecked reads, unsafe hooks, or ways to double-deliver or lose an item).
