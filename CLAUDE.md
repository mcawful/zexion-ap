# Working in zexion-ap

Archipelago (AP) multiworld support for **Zexion** (Gallant Leaf, Steam app 3392510), a native
x64 exe on SDL2 and FMOD with no modding API: its mod folders are for sprites and translations
only, and build B1 does not read the sprite ones ([`docs/re/builds.md`](docs/re/builds.md)).
Everything here comes from reverse engineering, hence the evidence rules below.

**Status:** pre-release. Client and helper 0.28.0, played in game on build B1. The APWorld lives
on the local branch `apworld` (never pushed: Rules).

Where things are:

- How the pieces fit, the terms, what each feature does:
  [`docs/DEVELOPING.md`](docs/DEVELOPING.md).
- The in-game helper: [`native/README.md`](native/README.md).
- What has been measured, and how: [`docs/re/README.md`](docs/re/README.md).
- When the game is patched: [`docs/re/new-build.md`](docs/re/new-build.md).

## Rules

- **Measured vs. assumed.** Every fact in `docs/re/` (offsets, patterns, formats, item and flag
  IDs) says how it was established and on which build, as
  [`docs/re/README.md`](docs/re/README.md) defines. An assumption is labeled as one until
  measured. Nothing is "working in game" until it has run in the real game, with the date and
  build recorded.
- **No hard-coded addresses.** ASLR is on and the game is still patched. Live addresses come
  only from byte patterns checked to match exactly once. Struct offsets, and per-build offsets
  into an exe copy read offline, are allowed when `docs/re/memory.md` records them for that
  build. `Target.rvas` in `client/patterns.py` is for checks only.
- **The game install and the real save folder are read-only to us.** Hooks block direct writes.
  Save experiments work on copies in `lab/` through `tools/save_lab.py`; only its `install`
  (an empty slot, after a verified backup) and `restore` write the save folder. The client never
  writes it.
- **Ask first, every time,** before anything that changes the running game or the saves:
  `grant.py set`, `memscan.py write`, `helper.py load` and `unload`, a client run that may load
  the helper, the client's `/unload_helper`, and `save_lab.py install` and `restore`. Grants and
  memory writes go only into a lab slot; grants only while the player stands still in play.
- **Branches.** `main` plus short-lived topic branches, deleted once merged. `apworld`, and
  every branch cut from it (any branch holding `worlds/`), stays local until the owner
  publishes the world: never push one. Locally, `.git/hooks/pre-push` (not committed) refuses a
  push of any commit that holds `worlds/`.
- **This repo is public.** Never commit game files, exe or memory dumps, saves (real or
  patched), Steam account ids, or anything from `lab/` or `private/`. Facts go in `docs/re/`;
  the bytes they came from stay local.
- **Documentation describes the repo as it is.** A change that makes a doc line false fixes it
  in the same change; new behavior is described where its subject already lives. Skill and agent
  files count.
- **American English**, in prose and in names that read as prose (test names, log lines,
  errors).

## Setup

```sh
python -m pip install --user uv                # --user leaves uv off PATH: hence `python -m uv`
python -m uv python install 3.13               # AP 0.6.8 refuses 3.14
python -m uv venv --seed --python 3.13 .venv   # --seed: AP's updater uses pip
python -m uv pip install --python .venv -r requirements-dev.txt
.venv/Scripts/python tools/ap_checkout.py      # AP at AP_VERSION into .ap/ (gitignored), with its
                                               # and the worlds' requirements; links worlds/zexion
```

## Checks

CI runs the first three. The fourth, AP's world tests, needs the world (`apworld`) and `.ap/`.
`/preflight` runs all four.

```sh
.venv/Scripts/ruff format --check .
.venv/Scripts/ruff check .
.venv/Scripts/python -m pytest
cd .ap/Archipelago && AP_TEST_WORLDS=zexion ../../.venv/Scripts/python -m pytest test/general worlds/zexion/test
```

The helper (Visual Studio's C++ tools and CMake). CI's native job runs these on Windows, then
`tests/test_native.py`; what the tests cover: [`native/README.md`](native/README.md), "Building".

```sh
cmake -S native -B native/build -A x64
cmake --build native/build --config Release
ctest --test-dir native/build -C Release --output-on-failure
```

Nothing in `tests/` needs the game, and CI never runs it.

## Running things

### The client

```sh
.venv/Scripts/python -m client.zexion_client --nogui --verbose archipelago://NAME@localhost:38281
```

- From source it needs `.ap/` with the world linked in (`ap_checkout.py`, on `apworld`): AP's
  client looks up its game's data at startup. From the Launcher it uses the Launcher's AP.
  `ZEXION_AP_DIR` points it at another AP folder.
- `--verbose` also logs what it sets up in the game for the seed. `--keep-randomizer` leaves
  RANDOMIZER on the menu instead of offering ARCHIPELAGO.
- On attaching it uses or loads the helper (which one: [`native/README.md`](native/README.md),
  "Getting in"; `ZEXION_DLL` names a build). Without a usable helper, items are written
  directly with no popup and no seed can be started. `/unload_helper` (hidden from `/help`)
  unloads it as if it had failed; the client loads it again within 30 seconds.
- While `zexion_ap.disabled` sits in the game's folder, neither the proxy nor the client loads
  the game folder's helper, and the client says so (its own build, `native/build` or
  `ZEXION_DLL`, still loads).

### Tools

Each tool's `--help` (its docstring) has the details. Only these tools and the client open the
game or save folders.

| Command (`.venv/Scripts/python tools/...`) | What | Changes the game? |
|---|---|---|
| `build_id.py` | which build is installed | no |
| `save_lab.py slots`, `backup`, `copy`, `diff`, `patch` | saves, on copies (`/save-lab`) | no |
| `save_lab.py install`, `restore` | write the save folder | **yes: ask** |
| `memscan.py find`, `snap`, `cmp`, `read`, `classify`, `record` | search live memory (`/find-address`) | no |
| `memscan.py write ADDR HEX --yes` | write live memory | **yes: ask** |
| `grant.py show`, `watch` | the live inventory by name | no |
| `grant.py set ITEM VALUE --expect N --yes` | grant by hand (inventory byte only; logs to `lab/grants.jsonl`) | **yes: ask** |
| `helper.py status` | is the helper loaded; its frame rate | no |
| `helper.py load --yes`, `unload --yes` | load or unload the helper (logs to `lab/helper.jsonl`) | **yes: ask** |
| `check_patterns.py EXE` or `--live` | `client/patterns.py` against an exe copy or the running game | no |
| `game_data.py NAMES --exe` | copy game data into `lab/data/<exe-hash-12>/` | no |
| `make_pattern.py EXE RVA` | candidate byte patterns | no |
| `randocells.py SAVE --fresh` | reachable locations of a new seed | no |
| `gen_world_data.py` (on `apworld`) | regenerate `worlds/zexion/data.py` | no |
| `build_apworld.py` (on `apworld`) | a release into `dist/` | no |

The tools find the game through Steam's library list and the saves through `%APPDATA%`.
`ZEXION_GAME_DIR` (or `build_id.py --game-dir`), `ZEXION_SAVE_DIR` (or `save_lab.py
--save-dir`) and `ZEXION_LAB_DIR` point them elsewhere; the tests use the last two.

## Releases and versions

`tools/build_apworld.py` (on `apworld`) writes `dist/zexion.apworld` (the world without its
tests, with `client` as `worlds.zexion.client`), `zexion_ap.dll` and `version.dll`. It packs only
git-tracked files and refuses missing, stale or mismatched DLLs (its docstring): rebuild
`native/` first. The packaged client has no build of its own, so it uses the helper in the
game's folder.

- **The release version** is `VERSION` in `client/native.py` and `kVersion` in
  `native/src/dllmain.cpp`: bump both (`tests/test_cross_checks.py` compares them; the helper
  shows it on the title screen).
- `world_version` in `worlds/zexion/archipelago.json` is the world's version for AP.
- `pyproject.toml`'s `0.0.0` is unused.

## Layout

- `worlds/zexion/` (on `apworld`): the APWorld. `world.py` (the `World`), `items.py`,
  `locations.py`, `options.py`, `rules.py`, `web_world.py`, the player docs in `docs/`,
  `archipelago.json`, `test/`. `__init__.py` registers the Zexion Client launcher entry and
  `archipelago://` links. `logic.py` holds the game's rules (shared with
  `tools/randocells.py`); `starting.py` the starting items the client can give (generation
  refuses the rest). `data.py` is generated by `tools/gen_world_data.py`: never edit it by hand
  (`docs/re/locations.md`). `test/test_client_agrees.py` checks the client's codes and slot data
  readers against the world.
- `client/`: the AP client, a package (relative imports):

  | Module | Job |
  |---|---|
  | `zexion_client.py` | `CommonClient` wiring, the commands, the game loop |
  | `session.py` | one poll: checks, the goal, grants |
  | `reconcile.py` | what to give: should own minus inventory |
  | `state.py` | the client's state, by lifetime, and pure decisions on it |
  | `ticks.py` | the side tasks with the helper |
  | `storage.py` | records in the server's data storage; Area Map hints |
  | `game.py` | attach, locate by pattern, checked reads and writes, `state()` |
  | `patterns.py` | byte patterns per target; known builds; `Target.rvas` |
  | `process.py` | another process's memory through the Windows API |
  | `pe.py` | the PE header fields the client and tools read |
  | `ids.py` | item and location codes; the game's sizes |
  | `save_patch.py` | the seed's plan from slot data; the save's layout |
  | `native.py` | load the helper and talk to it over its pipe |
  | `newgame.py` | the seed as `ap_newgame` |
  | `aplabels.py` | other players' items as `ap_labels` |
  | `hints.py` | which item each Area Map or Intel Chip hints |
  | `discs.py` | the OS Install Disks count: `discs`, `disk_slots` |
  | `deathlink.py` | deaths out, and received ones to deal (`die`) |
  | `shields.py` | received Energy Shields (`shield`) |
  | `offline.py` | `offline_put`: files for AP saves without the client |

- `native/`: the helper `zexion_ap.dll` and, in `proxy/`, the proxy `version.dll`:
  [`native/README.md`](native/README.md).
- `tools/`: RE and save tooling, and the release builder. `tools/protected.py` defines the
  protected folders (for the tools and the write guard) and `append_log`.
- `docs/re/`: the reverse-engineering record.
- `tests/`: pytest, host-only. `fakes.py` holds the shared fakes (process, game, a seed's plan,
  a fresh save). `test_cross_checks.py` reads `native/src` for what the client and the helper
  must agree on (version, builds, `config` keys). `test_build_apworld.py` and
  `test_gen_world_data.py` test tools that exist only on `apworld`.
- Gitignored: `lab/` (save backups, working copies, snapshots), `private/` (notes that must not
  be public), `.ap/` (the pinned AP checkout).

## Repo tooling

Committed under `.claude/`, so they apply to anyone working here:

- `.claude/settings.json`: the hook `guard_protected.py` blocks writes into the game install,
  the save folder and Steam's cloud cache for the game (rules: its docstring; cases:
  `tests/test_guard_hook.py`). `deny` rules for Edit and Write back it up; only the
  game-install one has been seen to fire. `ruff_on_edit.py` formats and lints each edited
  `.py`. An allowlist covers read-only git, ruff, pytest, `build_id.py` and `save_lab.py
  slots`/`diff`.
- Skills: `/preflight` (the checks; without the toolchain a change is unverified), `/save-lab`
  (Steam Cloud, backups, copies, diffs, patches, install, restore), `/find-address` (a value in
  memory to a byte pattern, recorded with evidence).
- Subagents: `docs-reviewer` (docs a change made wrong, behavior nothing describes),
  `logic-reviewer` (APWorld rules not backed by `docs/re/locations.md`; soft-locks),
  `re-auditor` (hard-coded addresses, unchecked reads, unsafe hooks, double or lost items in
  client, tool and helper diffs).
