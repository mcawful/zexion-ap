# Developing zexion-ap

For contributors. Players start at the [README](../README.md). The project's rules (evidence,
no hard-coded addresses, the game and save folders read-only, what never gets committed) are in
[`CLAUDE.md`](../CLAUDE.md), "Rules": read them before changing anything.

Zexion has no modding API, so everything here rests on reverse engineering, recorded with its
evidence in [`docs/re/`](re/README.md).

## Status

1.1.0; the first release, 1.0.0, was published on 2026-11-05 (UTC). Every in-game run was on game
build B1. A release build has run from Archipelago 0.6.8's Launcher since 2026-10-25
([`docs/re/memory.md`](re/memory.md), "The client in a live game"), the 1.0.0 client from it on
2026-10-29 (`native/README.md`, "AP saves without the client"), the 1.0.0 release candidate's
builds on 2026-11-03 and 04, the 1.1.0 candidate's on 2026-11-08 (develop 5597753) and 09
(develop 88bebfb), and `develop` 4205871's Area Maps on 2026-11-09 ("Features, and when each was
working in game", below).

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
  other 4; in Custom, those the settings reach from the start, 155 with Secret Shuffle), using the game's own randomizer map and rules (`docs/re/locations.md`).
  The owner's principles: follow the game's own rules and add no protection the game does not
  have (where the world departs from them: `docs/re/locations.md`, "Where the world departs
  from the game"); and in a new game the game's menu holds the seed's settings, so the game
  derives from them what it derives, and AP writes only what is AP's (`native/README.md`, "New
  games").
- **The client** attaches to the running game. While this seed's save is loaded it sends the
  locations the map marks as collected (again until the server holds them), gives the items
  other worlds send, and sends the goal once the save has reached the ending (with the OS
  Install Disks goal, with enough disks). It never counts its own grants: it gives what the
  player should own minus what the inventory holds, so an item lost to a death or a reload is
  given again. Only Energy Tanks, Missile Containers, Mega Bombs, the boxes and Scrap Metal
  count up: any other item's byte is written as 1 at most, so a second copy (a server console's
  `/send`) changes nothing in the game but its popup, except that with Guns First a weapon's
  second copy is its upgrade. It keeps a few records in the server's data storage (the received
  items handed to the helper for their popup, the follow-up popups shown (and this world's own
  traps set off), the Intel Chip hints
  made, the Energy Shields given, and this world's locations found in game, which the OS Install
  Disks count and the Area Map hints read; sent again until the server holds them). A failing
  side task (labels, disk count, DeathLink, follow-up popups) is logged once while items, checks
  and the goal go on. With no server connection, while a save it has not dressed is loaded and
  no ending is on its way, it lets the helper go, so the helper gives AP saves their look as
  with no client, and attaches it again on connecting
  ([`native/README.md`](../native/README.md), "AP saves without the client").
- **The helper** does what the client cannot from outside: popups, new games from the seed,
  the look of other players' items, and the rest of the features below. It hooks the game's
  once-per-frame call, so its work runs on the game's own thread, between frames; its own
  thread only serves the pipe. Without a helper in use, items are written directly with no
  popup, no seed can start, and the client tries the helper again within 30 seconds (one
  that doesn't match the client is left alone until the game restarts, unless the client has
  a build of its own or the old helper may unload on a later try). A helper lost while the
  client runs is attached again before items are given, for up to 30 seconds, so they keep
  their popups.
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
  (48), or one of this world's own traps (47). The helper restyles them.
- **Frame call**: one call the game makes, once per frame, through the hooked import; the
  helper runs its frame function in it.
- **B1**: the only game build supported so far (Steam build 23943567:
  [`docs/re/builds.md`](re/builds.md)).
- **Working in game (date, B1)**: ran in the real game on that date and build and did the right
  thing ([`docs/re/README.md`](re/README.md) defines it). A test on synthetic data is not that.

## Features, and when each was working in game

All on B1. Each line gives the first run in game; later runs, the bugs found in them and the
evidence are in the part of [`native/README.md`](../native/README.md) or the
[`docs/re/memory.md`](re/memory.md) heading named (memory.md's Status and Evidence rows).

- Received items with the game's popup, sound and HUD refresh: 2026-10-22 ("Grants", "Popups").
- Tall popups opening and closing with no line flashing (the text window's cut repaired):
  2026-11-08 ("Popups"); a five-line popup not run.
- Each received item pops up once, across reconnects and client restarts (the count in data
  storage): 2026-10-26; popups the helper holds when the game closes or it unloads are lost
  ("The client in a live game"). Checks and items during a drop, each once, over three drops in
  one load; the client's "To play" lines only away from the seed's save: 2026-11-04.
- ARCHIPELAGO new games: 2026-10-22 and 23, all three modes; a second save of one seed:
  2026-10-24 ("New games"; "The game-mode panel").
- Custom mode's settings in game: 2026-10-27 to 29 ([`docs/re/randocells.md`](re/randocells.md),
  "What each setting does in the game"); Gates Vanilla made Unlocked by the disks goal:
  2026-11-02 (`docs/re/locations.md`, "The OS Install Disks goal").
- `start_inventory_from_pool`: 2026-10-28 (`docs/re/locations.md`, "What the world needs from
  the client").
- Item plando: own items in own locations, 2026-10-28 and 2026-11-04; other players' items in
  this world's and this world's items, OS Install Disks among them, in another Zexion player's:
  2026-11-04 and 08 (`docs/re/locations.md`, "What the world needs from the client"). What the
  world refuses after plando is tested only ("Where the world departs from the game").
- `--keep-randomizer` (a new game by the seed's text gets the seed): 2026-10-27 ("Starting a new
  game").
- Other players' items (logo, name at pickup): 2026-10-23; the logo's own colors and black
  outline in every area: 2026-11-04; R'Talva X34 Y51's marker hidden: 2026-11-04; another
  Zexion player's Scrap Chunk shown as itself: 2026-11-09 ("Other players' items"; "Other Zexion
  players' items, as themselves"; its Scrap Energy not run in game).
- Map markers and names under the cursor: 2026-10-25; under Obscure settings: 2026-10-27 and 28
  ("The map"; "Map markers").
- Received Energy Shields: 2026-10-23; spawned once when the client attaches in play:
  2026-11-03; given once across reconnects: 2026-11-04; the helper's check of the whole of
  event 3553: 2026-11-09 ("Received Energy Shields"; its stop after three misses: not run in
  game).
- Received scrap's drops (its pickup's event, after its popup), one received while
  disconnected dropping once: 2026-11-09 ("Received scrap"; what the drops were and gave not
  described).
- DeathLink, both ways: 2026-10-23 ("DeathLink").
- Traps (Disruption Trap and Disorient Trap: the Phazo's and the Balorb wave's touch, set off
  after the trap's popup; this world's own, found in it, after the game's popup): 2026-11-11
  (PR #138's 57381bf): both received, a Disruption Trap taken by a shield, one held through the
  pause, one received while disconnected (once), this world's own and another Zexion player's
  in this world, AILMENTS reduced and immune ("Traps"; `docs/re/memory.md`, "Ailments: the
  script step that sets them"). Not run in game: one held through a death, a reload, the
  ending or RETURN HOME, and the traps held at a slot's first connection (since 57381bf).
- The ASSIST lock and Assist Unlock: 2026-10-23; left unlocked in another save: 2026-10-25
  ("The assist menu").
- Intel Chips: received hints 2026-10-24; picked up, with the INTEL CHIP name and no map reveal,
  2026-10-25; a received chip's own popup before its hint: 2026-11-08 ("Intel Chips"; "Popups");
  one received while the client was disconnected, its hint made once after the reconnect:
  2026-11-08 (develop 5597753). Not run in game: a received chip's two popups held for the next
  load.
- Area Maps kept in their own world with Area Map Hints off, so the game's own pickup reveals
  the map around the square it lay in: 2026-11-09 (develop 4205871; `docs/re/locations.md`,
  "Where the world departs from the game"; why: `docs/re/memory.md`, "Intel Chips"). Not run in
  game: the other players' sides of that seed, and an Area Map received with the option off.
- OS Install Disks goal: the count, the scanner, the equipment, pause and file-select screens:
  2026-10-24 and 25; RETURN HOME WITH THE DISKS?: 2026-10-24, at the Landing Pod's save point
  2026-10-25 (whatever the start room since 2026-10-30: 2026-11-02), a replay past the ending
  and the ending straight from the black: 2026-11-02 ("The OS Install Disks goal"; "The mission
  prompt"). The reminder after the received disks' popups, starting disks, and the goal sent by
  a client opened during the ending: 2026-10-26 (`docs/re/locations.md`, "The OS Install Disks
  goal"). A save at the pod then a pause at once, and popups waiting at YES shown at the next
  load: 2026-11-04. A received disk's own popup before its count, two at once, and both held
  at YES for the next load: 2026-11-08 ("Popups"); a received and a found disk's counts going
  up in order, then the reminder: 2026-11-08 (develop 5597753; the counts' text not quoted).
  Not run in game: RELOAD FROM CHECKPOINT and
  a teleport at the pod, and the map right after saving.
- Follow-up popups for this world's finds (a disk's count, an Intel Chip's hint), once each,
  offline finds included: 2026-10-25.
- The helper loaded with the game by the proxy: 2026-10-25 ("The proxy").
- `/reload_mod`: 2026-10-26; refused with a menu open or the ending on its way: 2026-10-27
  ("Unloading").
- What the helper changed put back when the client goes away: 2026-10-25 ("A client going
  away").
- Title screen lines: 2026-10-25; the client line 2026-10-27; in English(HD): 2026-11-03 ("The
  title screen's lines").
- No Feats or Steam achievements in AP saves, and the note on the Feats of Natu screen:
  2026-10-26 to 28 ("Feats and achievements in AP saves"). Not run in game: Steam's stand-in
  stopping an achievement, no 15Explorer on loading a fully explored AP save, and the client's
  warnings (`state.FeatsCheck`, `ticks.feats_tick`), which could not be arranged in game.
- AP saves marked on the file select, COPY FILE and DELETE FILE: 2026-10-26; their ITEMS row:
  2026-11-04 ("AP saves on the file select"; "Grants").
- AP saves without the client: 2026-10-25; the client letting the helper go with no server
  connection, from the Launcher's 1.0.0 client: 2026-10-29 ("AP saves without the client").
- The game's translations, English(HD) above all, and the client's warning in the others:
  2026-11-02, with later fixes run 2026-11-03, and the helper's text in capitals in French and
  the default English, 2026-11-04 ("Translations", whose run bullets list what was
  seen in each and what is not run in game yet).

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

The commands here and in `CLAUDE.md` are for a POSIX shell: bash or zsh, or Git Bash on Windows, which Git for
Windows installs. Claude Code needs Git Bash on Windows here too: the hook commands in
`.claude/settings.json` are written for a POSIX shell. With Git Bash (and with `sh` on Linux and
macOS), a hook that fails with an exit code other than 2 lets the call through; without it,
Claude Code runs them in PowerShell, and a failing hook blocks the call instead (Claude Code's
hooks reference; not tried here). They also work in PowerShell 7, except `VAR=value command`: set
`$env:AP_TEST_WORLDS = "zexion"` first instead. The venv's programs are in `.venv/Scripts/` on
Windows and `.venv/bin/` on Linux and macOS: change the folder in each command to match.

## Setup

The commands (uv, the Python 3.13 venv, `tools/ap_checkout.py`) are in
[`CLAUDE.md`](../CLAUDE.md), "Setup". With `python -m pip install --user uv`, `python` is the
python.org install on Windows; on Linux and macOS use uv's own installer, since Debian's,
Ubuntu's and Homebrew's Pythons refuse `pip install --user` (they are "externally managed") and
many systems have `python3` but no `python`.

The native helper, on Windows only, needs Visual Studio 2022 or later with the C++ workload, and
CMake 3.20 or later; `native/CMakeLists.txt` refuses another compiler or a single-config
generator (such as plain Ninja), since the DLLs are looked for under `native/build/Release/`.

Line endings: `.gitattributes` checks every text file out with LF, on Windows too, whatever
`core.autocrlf` says.

## Checks

The commands, and which CI job runs each: [`CLAUDE.md`](../CLAUDE.md), "Checks". The Claude
Code skill `/preflight` runs the Python ones and AP's world tests (those need `.ap/`). What the
native tests cover: [`native/README.md`](../native/README.md), "Building".

Some tests skip without what they need: the hook runner's without `sh`,
`worlds/zexion/test/test_client_agrees.py` without the repo's `client` beside the world (the
link `ap_checkout.py` makes), and `tests/test_native.py`'s load test without a built helper. CI
sets `ZEXION_REQUIRE` (a comma-separated list: `sh`, `client`, `helper`) in the jobs that have
them, and there a missing one fails a test as well.

## Running the client from source

On Windows, with the game. The command, what it needs and its flags: [`CLAUDE.md`](../CLAUDE.md),
"The client". Running it may load the helper into the game: the one already there if it is this
version, else `native/build/Release/zexion_ap.dll` (or `ZEXION_DLL`). Beyond that:

- A new game started from RANDOMIZER (kept by `--keep-randomizer`) with the seed's text typed
  into the menu gets the seed: the helper places it again with the seed's settings, whatever
  else the menu holds (`client/newgame.py`; `native/README.md`, "New games"; working in game,
  2026-10-27). From the Launcher: `ArchipelagoLauncher.exe "Zexion Client" -- --keep-randomizer`.
- `/reload_mod` unloads the helper and loads it again 3 seconds later, giving no items
  meanwhile (popups still queued are lost; items are not); with a menu or the map open, or
  while the ending RETURN HOME WITH THE DISKS? started is on its way, it refuses and says why,
  and the mod stays loaded. While the client is not connected and has let the helper go (the
  client, above), it says so and does nothing.
- While this seed's save is loaded, the client asks the helper whether its Feats and
  achievements guard is on ([`native/README.md`](../native/README.md), "Feats and achievements
  in AP saves"). If the helper says it could not do all of it (`feats=on:<why>` in its
  `hello`), or it is still off after 10 seconds with the save in play (the mod does not take
  it for an Archipelago save), the client warns once that achievements may be earned in this
  save (the reason goes to the log).
- Every few seconds while the helper is attached, the client reads the game's language from
  its `hello` (`lang`); in any translation but English(HD) it says once per game run that the
  mod's text is made for the default English and English(HD) (`state.language_line`).

## Branches and releases

`main` holds released code (or a release waiting for approval; GitHub shows its README);
`develop` holds code ready for the next release. Work happens on short-lived topic branches cut
from `develop`, merged into `develop` by pull request, then deleted. `develop` and `main` take
changes only through pull requests with every CI check passing. Branch protection requires
every check but `ruff and pytest (macos-latest)`, which a pull request must still pass.

CI (`.github/workflows/ci.yml`) runs the checks above on every push to and pull request into
`develop` or `main`. A pull request into `main` also runs the release gate
(`.github/workflows/release-gate.yml`): it must come from this repo's `develop`, raise the
version above every release's tag and above the version on `main`, and have the version's notes
written (`tools/release_check.py`). Being above `main` matters while a merged release waits for
approval: it has no tag yet, but `main` already holds its number. If GitHub can't list the
releases or `main`'s version can't be read, the gate fails. The gate runs again on any edit to the pull request, so one retargeted from
`develop` to `main` is checked too.

A release has one version number, in three places, which the tests and the builder check
([`CLAUDE.md`](../CLAUDE.md), "Releases and versions").

To release:

1. On a topic branch, raise the version in all three places and run
   `tools/gen_sample_yamls.py`. In the same pull request, write the release's notes for players:
   copy `docs/releases/TEMPLATE.md` to `docs/releases/<version>.md` and draft it from the pull
   requests merged into `develop` since the last release, as
   [`docs/releases/README.md`](releases/README.md) says. The owner approves the wording in that
   pull request. Preview the whole message with `tools/release_notes.py <version> --previous
   v<last>`. Merge it into `develop`.
2. Open a pull request from `develop` into `main`. Only the owner merges it, with **Create a
   merge commit** (a ruleset on `main` allows nothing else: a squash or rebase would put a
   commit on `main` that `develop` lacks, and the branches would drift apart). Claude never
   merges into `main`: a rule, plus a deny rule for `gh pr merge` in Claude's settings; GitHub
   itself enforces only the release approval below.
3. The merge starts `.github/workflows/release.yml`. Its job runs in the GitHub environment
   `release`, so it waits on the Actions page until the owner approves it; nothing is built,
   tagged or published before that. It then builds the helper and proxy, runs the native
   tests, builds the release with `tools/build_apworld.py`, tags `v<VERSION>` and publishes a
   GitHub Release of the files below. Its message is the one `tools/release_notes.py` builds:
   the version's notes, then the install-and-update guide in `.github/release-footer.md`, then
   the compare link from the latest release; if the notes are missing or unfinished, nothing is
   built or published. A version already released from this same commit is skipped (a re-run).
   The job fails instead if that version's release is a draft (a publish that failed part-way:
   publish or delete it on GitHub, then re-run) or was released from another commit.
4. If the approval is rejected or times out, or the job fails, `main` holds the version with no
   release: re-run the workflow from the Actions page, or fix the cause on a topic branch into
   `develop`, raise the version (the gate wants it above `main`'s), and merge `develop` into
   `main` again.

What `tools/build_apworld.py` writes into `dist/` and what it refuses: [`CLAUDE.md`](../CLAUDE.md),
"Releases and versions". Run locally, rebuild `native/` first after a checkout that touched it.
The apworld's `archipelago.json` is the world's plus the two container keys Archipelago's own
builder adds, `version` 7 and `compatible_version` 7 (`CONTAINER_KEYS`). Without
`compatible_version`, AP 0.6.8's `APContainer.read_contents` fails, its apworld loader logs
"Invalid or missing manifest file", and the world's `world_version` and `minimum_ap_version` are
never read (read in AP 0.6.8's `worlds/Files.py` and `worlds/__init__.py`, 2026-11-04).

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
- Custom mode, every setting of the game's randomizer: the options in
  `worlds/zexion/options.py`; the game's side in [`docs/re/randocells.md`](re/randocells.md),
  from "The randomizer menu's settings"; where the world departs from it,
  [`docs/re/locations.md`](re/locations.md).
