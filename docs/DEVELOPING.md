# Developing zexion-ap

For contributors. Players start at the [README](../README.md). The project's rules (evidence,
no hard-coded addresses, the game and save folders read-only, what never gets committed) are in
[`CLAUDE.md`](../CLAUDE.md), "Rules": read them before changing anything.

Zexion has no modding API, so everything here rests on reverse engineering, recorded with its
evidence in [`docs/re/`](re/README.md).

## Status

In development. Every in-game run was on game build B1. A release build has run from Archipelago
0.6.8's Launcher (working in game 2026-10-25, B1: [`docs/re/memory.md`](re/memory.md), "The client
in a live game").

## How it fits together

```
Archipelago server ⇄ (websocket) ⇄ client/  (Python, outside the game)
                                     ├─ reads Zexion.exe's memory, found by byte pattern
                                     │  (game state, inventory, map); writes inventory bytes
                                     │  itself only when no helper is in use
                                     └─ named pipe \\.\pipe\zexion-ap-<pid>
                                          ⇄ zexion_ap.dll, the helper (inside Zexion.exe)
version.dll, the proxy (in the game's folder) → loads the helper as the game starts
worlds/zexion/ → generation → slot data → client
```

- **The world** places items at the game's 154 locations (150 in OHKO, which cannot reach the
  other 4; in Custom, those the settings reach from the start), using the game's own randomizer map and rules (`docs/re/locations.md`).
- **The client** attaches to the running game. While this seed's save is loaded it sends the
  locations the map marks as collected (again until the server holds them), gives the items
  other worlds send, and sends the goal once the save has reached the ending (with the OS
  Install Disks goal, with enough disks). It never counts its own grants:
  it gives what the player should own minus what the inventory holds, so an item lost to a
  death or a reload is given again. It keeps a few records in the server's data storage (the
  received items handed to the helper for their popup, the follow-up popups shown, the Intel
  Chip hints made; sent again until the server holds them). A
  failing side task (labels, disk count, DeathLink, follow-up popups) is logged once while items,
  checks and the goal go on.
- **The helper** does what the client cannot from outside: popups, new games from the seed,
  the look of other players' items, and the rest of the features below. It hooks the game's
  once-per-frame call, so its work runs on the game's own thread, between frames; its own
  thread only serves the pipe. Without a helper in use, items are written directly with no
  popup, no seed can start, and the client tries the helper again within 30 seconds.
- **The proxy** is a `version.dll` the player puts in the game's folder beside the helper. The
  game loads it as it starts; it passes every call on to Windows' own `version.dll` and loads
  the helper. A `zexion_ap.disabled` file beside them turns off the game folder's copy: neither
  the proxy nor the client loads it (a client run from source still loads its own build).
- **The offline cache.** The client sends the helper a small file per seed it connects to and
  the addresses it found for this build; the helper keeps them in `%LOCALAPPDATA%\ZexionAP`.
  With no client, the helper uses them to give AP saves their look. With or without a client,
  they also tell it which saves are AP saves, which earn no Feats or Steam achievements.
  Deleting the folder is safe: the client sends the files again the next time it attaches to
  the game.

How the helper works: [`native/README.md`](../native/README.md).

## Glossary

- **AP save**: a randomizer save holding a seed a client has played. The helper recognizes one
  by its item table and start room, matched against the seed files in the offline cache.
  **This seed's save** is an AP save of the seed the connected client plays.
- **The helper**: `zexion_ap.dll`, built from `native/`. **The proxy**: `version.dll`, built
  from `native/proxy/`. Players call the two together "the mod".
- **Stand-in**: the Scrap Energy (47) or Scrap Chunk (48) the game holds at a location whose
  item it cannot hand out itself: another player's item (48 if it is progression, except another
  Zexion player's supplies, 47 and minor as the game marks this player's own:
  `world.important_elsewhere`), or, with the OS Install Disks goal, one of this world's own disks
  (48). The helper restyles them.
- **Frame call**: one call the game makes, once per frame, through the hooked import; the
  helper runs its frame function in it.
- **B1**: the only game build supported so far (Steam build 23943567:
  [`docs/re/builds.md`](re/builds.md)).
- **Working in game (date, B1)**: ran in the real game on that date and build and did the right
  thing ([`docs/re/README.md`](re/README.md) defines it). A test on synthetic data is not that.

## Features, and when each was working in game

All on B1. The evidence for each run is in [`docs/re/memory.md`](re/memory.md).

Rechecked on 575edbf (0.30.0, after the footprint pass of #20 to #22), 2026-10-27, in a three-game
seed (`lab/ap/live35`) with the client run from source: a new ARCHIPELAGO game and the AP logo,
received items with popups, checks sent (another Zexion player's item as itself, another game's as
an AP item), the map's markers and names, the pause row and the file select's count, a reload,
Disconnect then Connect, `/reload_mod` in play and refused in the pause screen, RETURN HOME with
its fade (NO and YES), and the ending with the goal sent and no Feats. All as listed below; one
fault found later that day, the Feats screen's note missing when the assist menu is unlocked
(fixed in d451e6b, below).

- Received items with the game's popup, sound and HUD refresh: 2026-10-22.
- Each received item pops up once, across reconnects and client restarts (the count in data
  storage): 2026-10-26, with 0.27.0. Since 0.30.0 the count is of popups handed to the
  helper, which keeps its queue when the client goes away (Disconnect then Connect, the
  client closed, the save quit to the title: 2026-10-26). A game closed, or the helper
  unloaded, with popups queued loses them (2026-10-26).
- ARCHIPELAGO new games, all three modes: 2026-10-22 and 2026-10-23; two saves of one seed from
  one connection: 2026-10-24.
- `--keep-randomizer` (RANDOMIZER kept; a new game in the seed's mode gets the seed), all three
  modes: 2026-10-22. That was the earlier path, checked against the player's own placement;
  identifying the game by its seed text and placing it again: 2026-10-27, a Custom seed (the
  same as its ARCHIPELAGO game byte for byte) and a Standard one, and another seed's text
  refused.
- Other players' items (logo, name at pickup; another Zexion player's item as itself):
  2026-10-23. Map markers and names under the cursor in an AP save: 2026-10-25; in a
  three-game seed (other games' items, no owner on the map, other Zexion players' supplies
  minor, markers right in a pause just after loading): 2026-10-26. A second three-game seed,
  with the markers' wait for a settled load (two looks in a row, `native/README.md`, "The
  map"): unseen squares named as the game names its own, markers kept after a save, a return to
  the title and a load: 2026-10-26. The markers under Obscure Majors on with Minors off, with
  Obscure Class off and on (hinted stand-ins as the game's hinted majors, seen ones named; a
  stand-in's minimap dot flashing while it is on screen): 2026-10-27.
- Received Energy Shields (spawned by the helper, counted once the shield is up): 2026-10-23.
- The helper's escape skip offer (SKIP ESCAPE AND FINAL BATTLE? in any game): 2026-10-23;
  retired 2026-10-27, the owner's decision: the game's own Abridged Finale is the only way to
  skip the escape (`docs/custom-mode.md`).
- DeathLink, both ways: 2026-10-23.
- The ASSIST lock: 2026-10-23; left unlocked in another save: 2026-10-25. Assist Unlock:
  2026-10-23.
- Intel Chips: hints received, up to five lines: 2026-10-24; one picked up, the INTEL CHIP
  name and no map reveal: 2026-10-25.
- OS Install Disks goal: the count, the scanner text, the equipment and pause screens and the file
  select: 2026-10-24 and 2026-10-25. RETURN HOME WITH THE DISKS? (YES and NO): 2026-10-24; offered
  at the landing pod only: 2026-10-25; the fade before it: 2026-10-27, first timed to the escape
  skip's by disassembly (not compared in game), then started half a second after GAME SAVED
  appears (the owner's pick, 2026-10-27); the fade turned back after a save at the pod during
  it: 2026-10-27. A request held by popups lapsing once the player left the pod's room, and one
  left during GAME SAVED alone, then offered again after the next save there: 2026-10-27. Not
  seen in game: the fade turned back when the helper takes back a prompt it had not opened yet
  (stand-in tests only).
  The reminder (ENOUGH OS INSTALL DISKS FOUND...) after the received disks' popups, and the pause
  row on a pause right after loading: 2026-10-26. Starting disks counted, and the goal sent by a
  client opened after the ending began: 2026-10-26. Received items held over the ending's
  cutscenes across a Disconnect then Connect during them (the goal's collect, then two items
  sent after the Connect: no popup until the save was loaded again): 2026-10-27.
- Follow-up popups for this world's finds (a disk's count, an Intel Chip's hint), once each,
  offline finds included: 2026-10-25.
- The helper loaded with the game by the proxy, and used by the client: 2026-10-25.
- `/reload_mod` in play: the mod back 3 s later, the ASSIST lock and the map's items the game's
  for those seconds: 2026-10-26. With the pause or equipment screen open it unloaded, and the
  open screen showed the disk wrong until reopened (2026-10-26), so an unload now refuses with a
  menu open: refused with the pause screen open, then reloaded in play, 2026-10-26. A cleanup
  after that run (5681ed6) made the client take any refusal for a failed request (it dropped
  the helper and attached to it again); fixed since (`native.Helper.unload`, then
  `state.reload_reply`): refused with the pause screen open, the client keeping the helper and
  the screen unchanged, then reloaded in play, 2026-10-27. Refused while RETURN HOME WITH THE
  DISKS?'s ending was on its way (after YES, with received items' popups holding it), the helper
  kept and the ending run: 2026-10-27.
- What the helper changed put back when the client goes away (item records, ASSIST):
  2026-10-25.
- Title screen: ARCHIPELAGO MOD, the version and ©2026 MCAWFUL: 2026-10-25. START THE AP
  CLIENT TO PLAY A SEED above them with no client, gone with one: 2026-10-27.
- No Feats earned in AP saves (the ending jumps over its Feats and 14Victory), ARCHIPELAGO
  SAVE: NO FEATS EARNED on the Feats of Natu screen after, and Feats in a normal save as usual:
  2026-10-26, with the client connected; again 2026-10-27, `GameUnlocks.sav` unchanged by the
  ending. An achievement reaching Steam at the title after an AP save was quit (13SecretCode,
  with the rewritten stub hooked at that title), and a plain randomizer save left alone with a
  client connected:
  2026-10-27. AP endings through the helper's escape skip (since retired) with no client, and
  with a client of another seed, no Feat earned: 2026-10-27; the first, in a seed with Allow Assists on, showed no note
  (MISSION COMPLETE had ASSISTS USED under it, which the screen check refused; what makes the
  game add it is not traced), fixed in d451e6b, and the second, with that fix, showed it. Not
  seen in game: Steam's stand-in stopping an achievement left to earn (it was in place; stand-in
  tests only; the one left that is cheap to earn, 13SecretCode, is taken at the title only), and
  no 15Explorer when a fully explored AP save loads (by disassembly and stand-in tests)
  (`native/README.md`, "Feats and achievements in AP saves"). The client's warnings, the guard on
  without all of it or still off in play: their decisions on stand-in tests (`state.FeatsCheck`),
  the asking itself (`ticks.feats_tick`) not under test, none run in game. They could not be
  arranged in game on 2026-10-27: a connected client leaves the helper its seed's file, so the
  guard turns on in this seed's save.
- AP saves marked on the file select, COPY FILE and DELETE FILE (the AP logo right of FILE
  NN; normal and plain randomizer saves unmarked), with and without the client, through
  paging, a copy and a delete, and whitened with a new game's fade: 2026-10-26. A new
  ARCHIPELAGO game's own save getting the logo: 2026-10-27.
- AP saves without the client, from a fresh start too, and another seed's save told so:
  2026-10-25; its notice as now worded, and its look kept when a client of another seed
  attaches with it in play: 2026-10-26. A plain (not Archipelago) randomizer game left alone
  with a client connected (no notice, no look): 2026-10-27.

## Platforms

The game runs on Windows, so whatever touches it does too. Everything else runs on Windows,
Linux and macOS, and CI runs ruff and pytest on all three.

Windows only:

- the client attached to the game (`client/process.py` is the Windows API; the rest of
  `client/` is plain Python, tested anywhere);
- the live tools: `memscan.py`, `grant.py`, `helper.py`, `check_patterns.py --live`;
- the helper and the proxy (`native/`): MSVC and CMake's Visual Studio generator, and so a
  release, whose builder packs their DLLs. `tools/build_apworld.py` itself, and its tests, run
  anywhere;
- finding the game by itself: Steam's path comes from the registry. Elsewhere, set
  `ZEXION_GAME_DIR`; the saves' folder comes from `%APPDATA%`, else `ZEXION_SAVE_DIR`.

Tests that need Windows skip elsewhere and say why (`tests/test_process.py`, parts of
`tests/test_native.py` and `tests/test_pe.py`), as does `tests/test_native.py`'s load test
without a built helper and `tests/test_hook_runner.py`'s runner tests without `sh`.

The commands below are for a POSIX shell: bash or zsh, or Git Bash on Windows, which Git for
Windows installs. Claude Code needs Git Bash on Windows here too: the hook commands in
`.claude/settings.json` are written for a POSIX shell. With Git Bash (and with `sh` on Linux and
macOS), a hook that fails with an exit code other than 2 lets the call through; without it,
Claude Code runs them in PowerShell, and a failing hook blocks the call instead (Claude Code's
hooks reference; not tried here). They also work in PowerShell 7, except `VAR=value command`: set
`$env:AP_TEST_WORLDS = "zexion"` first instead. The venv's programs are in `.venv/Scripts/` on
Windows and `.venv/bin/` on Linux and macOS: change the folder in each command to match.

## Setup

Git and uv, on any of the three. Archipelago 0.6.8 refuses Python 3.14, so the venv is 3.13,
managed by uv. The venv is seeded with `pip` because Archipelago's own updater uses it.

Install uv one of two ways:

- `python -m pip install --user uv` (on Windows, where `python` is the python.org install),
  then type `python -m uv` wherever `uv` appears below: `--user` does not put `uv` on `PATH`.
- uv's own installer ([docs.astral.sh/uv](https://docs.astral.sh/uv/getting-started/installation/)),
  which does. On Linux and macOS use this one: Debian's, Ubuntu's and Homebrew's Pythons refuse
  `pip install --user` (they are "externally managed"), and many systems have `python3` but no
  `python`.

```sh
uv python install 3.13
uv venv --seed --python 3.13 .venv
uv pip install --python .venv -r requirements-dev.txt
.venv/Scripts/python tools/ap_checkout.py   # Archipelago at AP_VERSION into .ap/, world linked in
```

The native helper, on Windows only, needs Visual Studio 2022 or later with the C++ workload, and
CMake 3.20 or later; `native/CMakeLists.txt` refuses another compiler or a single-config
generator (such as plain Ninja), since the DLLs are looked for under `native/build/Release/`.

Line endings: `.gitattributes` checks every text file out with LF, on Windows too, whatever
`core.autocrlf` says.

## Checks

CI runs all of these (ruff and pytest on Windows, Ubuntu and macOS; the native build, its tests
and `tests/test_native.py` on Windows; AP's world tests after `tools/ap_checkout.py`, on
Ubuntu). The Claude Code skill `/preflight` runs the first block (AP's world tests need `.ap/`).

```sh
.venv/Scripts/ruff format --check .
.venv/Scripts/ruff check .
.venv/Scripts/python -m pytest
cd .ap/Archipelago && AP_TEST_WORLDS=zexion ../../.venv/Scripts/python -m pytest test/general worlds/zexion/test
```

```sh
cmake -S native -B native/build -A x64
cmake --build native/build --config Release
ctest --test-dir native/build -C Release --output-on-failure
```

Nothing in `tests/` needs the game. What the native tests cover: `native/README.md`,
"Building".

Some tests skip without what they need: the hook runner's without `sh`, and
`worlds/zexion/test/test_client_agrees.py` without the repo's `client` beside the world (the
link `ap_checkout.py` makes). CI sets `ZEXION_REQUIRE` (a comma-separated list: `sh`, `client`)
in the jobs that have them, and there a missing one fails a test as well.

## Running the client from source

On Windows, with the game. It needs `.ap/` with the world linked in (`tools/ap_checkout.py`).
Running it may load the helper into the game: the one already there if it is this version, else
`native/build/Release/zexion_ap.dll` (or `ZEXION_DLL`).

```sh
.venv/Scripts/python -m client.zexion_client --nogui --verbose archipelago://NAME@localhost:38281
```

- `--verbose` also shows what the client sets up in the game for the seed.
- `--keep-randomizer` keeps RANDOMIZER in place of ARCHIPELAGO; a new game started from it
  with the seed's text typed into the menu gets the seed: the helper places it again with the
  seed's settings, whatever else the menu holds (`client/newgame.py`; `native/README.md`, "New
  games"; working in game, 2026-10-27). From the Launcher:
  `ArchipelagoLauncher.exe "Zexion Client" -- --keep-randomizer`.
- `/reload_mod` unloads the helper and loads it again 3 seconds later, giving no items
  meanwhile (popups still queued are lost; items are not); with a menu or the map open, or
  while the ending RETURN HOME WITH THE DISKS? started is on its way, it refuses and says why,
  and the mod stays loaded.
- While this seed's save is loaded, the client asks the helper whether its Feats and
  achievements guard is on ([`native/README.md`](../native/README.md), "Feats and achievements
  in AP saves"). If the helper says it could not do all of it (`feats=on:<why>` in its
  `hello`), or it is still off after 10 seconds with the save in play (the mod does not take
  it for an Archipelago save), the client warns once that achievements may be earned in this
  save (the reason goes to the log).
- `ZEXION_AP_DIR` points the client at another Archipelago folder than `.ap/Archipelago`.

## Branches and releases

`main` holds released code (or a release waiting for approval; GitHub shows its README);
`develop` holds code ready for the next release. Work happens on short-lived topic branches cut
from `develop`, merged into `develop` by pull request, then deleted. `develop` and `main` take
changes only through pull requests with every CI check passing. Branch protection requires
every check but `ruff and pytest (macos-latest)`, which a pull request must still pass.

CI (`.github/workflows/ci.yml`) runs the checks above on every push to and pull request into
`develop` or `main`. A pull request into `main` also runs the release gate
(`.github/workflows/release-gate.yml`): it must come from this repo's `develop` and raise the
version above every release's tag and above the version on `main` (`tools/release_check.py`).
The second matters while a merged release waits for approval: it has no tag yet, but `main`
already holds its number. If GitHub can't list the releases or `main`'s version can't be read,
the gate fails. The gate runs again on any edit to the pull request, so one retargeted from
`develop` to `main` is checked too.

A release has one version number, in three places: `VERSION` in `client/native.py`,
`kVersion` in `native/src/dllmain.cpp` and `world_version` in
`worlds/zexion/archipelago.json`. `tests/test_cross_checks.py` compares them, and the builder
refuses a world of another number. `pyproject.toml`'s `0.0.0` is unused.

To release:

1. On a topic branch, raise the version in all three places; merge it into `develop`.
2. Open a pull request from `develop` into `main`. Only the owner merges it, with **Create a
   merge commit** (a ruleset on `main` allows nothing else: a squash or rebase would put a
   commit on `main` that `develop` lacks, and the branches would drift apart). Claude never
   merges into `main`: a rule, plus a deny rule for `gh pr merge` in Claude's settings; GitHub
   itself enforces only the release approval below.
3. The merge starts `.github/workflows/release.yml`. Its job runs in the GitHub environment
   `release`, so it waits on the Actions page until the owner approves it; nothing is built,
   tagged or published before that. It then builds the helper and proxy, runs the native
   tests, builds the release with `tools/build_apworld.py`, tags `v<VERSION>` and publishes a
   GitHub Release of the files below, with generated notes. A version already released from
   this same commit is skipped (a re-run). The job fails instead if that version's release is
   a draft (a publish that failed part-way: publish or delete it on GitHub, then re-run) or was
   released from another commit.
4. If the approval is rejected or times out, or the job fails, `main` holds the version with no
   release: re-run the workflow from the Actions page, or fix the cause on a topic branch into
   `develop`, raise the version (the gate wants it above `main`'s), and merge `develop` into
   `main` again.

`tools/build_apworld.py` writes `dist/`: `zexion.apworld` (the world without its tests, with
`client` as `worlds.zexion.client`), `zexion_ap.dll`, `version.dll`, and the player guides as
`zexion-setup-guide.md` and `zexion-game-page.md` (from `worlds/zexion/docs/`). It packs only
files git tracks and refuses a release that would not match (its docstring): run locally,
rebuild `native/` first after a checkout that touched it.

Players download the latest release from the
[Releases page](https://github.com/mcawful/zexion-ap/releases).

## Reviewing a change

A review covers the branch's own work, diffed against the branch it was cut from: on a topic
branch, `git diff develop...HEAD` (`origin/develop` if the local `develop` is behind); on
`develop`, `git diff main...develop`. On `main`, or when the base is unclear, ask for the range
rather than review a whole branch's history. For uncommitted work, `git diff HEAD`, and `git
status --short` for new untracked files. A target named instead (a branch, a pull request, a
path) is reviewed as named. The review subagents in `.claude/agents/` follow this.

## Where things are

- `client/`: the client. `native/`: the helper and the proxy. `tools/`: reverse-engineering
  and save tools, and the release builder. `tests/`: pytest, host-only. `docs/re/`: the
  reverse-engineering record. `worlds/zexion/`: the APWorld and the player docs.
  Module by module, and every tool's command: [`CLAUDE.md`](../CLAUDE.md).
- When the game is patched: [`docs/re/new-build.md`](re/new-build.md).
- Custom mode, every setting of the game's randomizer (step B, run in game 2026-10-27):
  [`docs/custom-mode.md`](custom-mode.md).
