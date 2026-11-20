# Developing zexion-ap

For contributors. Players start at the [README](../README.md). The project's rules (evidence,
no hard-coded addresses, the game and save folders and the mod's offline files read-only, what
never gets committed) are in [`CLAUDE.md`](../CLAUDE.md), "Rules": read them before changing
anything.

Zexion has no modding API, so everything here rests on reverse engineering, recorded with its
evidence in [`docs/re/`](re/README.md).

## Status

The first release, 1.0.0, was published on 2026-11-05 (UTC), 1.1.0 after it (the current version is
`world_version` in `worlds/zexion/archipelago.json`). Every in-game run was on game build B1. A
release build has run from Archipelago 0.6.8's Launcher since 2026-10-25
([`docs/re/memory.md`](re/memory.md), "The client in a live game"), the 1.0.0 client from it on
2026-10-29 (`native/README.md`, "AP saves without the client"), the 1.0.0 release candidate's
builds on 2026-11-03 and 04, the 1.1.0 candidate's on 2026-11-08 (develop 5597753) and 09
(develop 88bebfb), `develop` 4205871's Area Maps on 2026-11-09, and `develop` e500ab9's traps on
2026-11-11. The runs since, of the checkpoint checks, own disks and traps following the save and
other players' items kept collected (2026-11-13 to 16), are in "Features, and when each was working
in game", below.

## How it fits together

```
Archipelago server ⇄ (websocket) ⇄ client/  (Python, outside the game)
                                     ├─ reads Zexion.exe's memory, found by byte pattern
                                     │  (game state, inventory, map); writes inventory bytes
                                     │  itself only when no helper is in use, and marks other
                                     │  players' items collected
                                     └─ named pipe \\.\pipe\zexion-ap-<pid>
                                          ⇄ zexion_ap.dll, the helper (inside Zexion.exe)
version.dll, the proxy (in the game's folder) → loads the helper as the game starts
worlds/zexion/ → generation → slot data → client
```

- **The world** places items at the game's 154 locations (150 in OHKO, which cannot reach the
  other 4; in Custom, those the settings reach from the start, 155 with Secret Shuffle), using the
  game's own randomizer map and rules (`docs/re/locations.md`).
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
  given again. Another player's item in this world stays collected once the server has its
  location as checked (found before a death or a reload, or in another save of the seed;
  collected by its owner; released): the client marks it collected in the loaded save, so it
  is not there to pick up again. Never in a room that waits for its item's pickup (Exodia
  X20 Y0, where the pickup starts the boss fight, R'Talva X34 Y51, the boss's reward,
  whose pickup turns the warp gate on, and Kresezoa X5 Y23, whose boss drops the item): there
  the item stays and shows as Scrap Metal once it is checked, and with the helper it is picked
  up as the game's own Scrap Metal: in game, the first two rooms gave "the regular scrap metal
  and the pickups from it" (the owner's words), and Kresezoa X5 Y23's boss dropped an actual
  Scrap Metal (the owner's report, 2026-11-16), and the first two rooms' squares showed as a
  minor's under the presets' Obscure settings. An
  item the game has made already is ended by the game itself once its state is written; one
  that may be there as another kind of object (in a room the game has loaded or within two
  map squares of the player) stays until the player is farther from it
  ([`native/README.md`](../native/README.md), "Other players' items"). A marked item gives
  nothing: the one Scrap Metal a stand-in gives is counted only where it was picked up,
  which the game's own pickup count tells apart. With `--verbose` each marking is logged
  ("Marked collected in this save (...)"). This world's own items follow the save.
  Only Energy Tanks, Missile Containers, Mega Bombs, the boxes and Scrap Metal count up: any
  other item's byte is written as 1 at most, so a second copy (a server console's `/send`)
  changes nothing in the game but its popup, except that with Guns First a weapon's
  second copy is its upgrade. It keeps a few records in the server's data storage (the received
  items handed to the helper for their popup, the traps held at the slot's first connection still
  to pop up, the hints shown for this world's own Area Maps found, the Intel Chip hints
  made, the Energy Shields given, and this world's locations found in game, which the Area Map
  hints read; sent again until the server holds them). This world's own OS Install Disks and
  traps follow the save instead: the disk count is the loaded save's own, and the helper sets an
  own trap off at each pickup ([`native/README.md`](../native/README.md), "The OS Install Disks
  goal", "Traps"). A failing side task (labels, disk count, DeathLink, follow-up popups) is
  logged once while items, checks and the goal go on, and so is a failing marking of other
  players' items ("Can't mark other players' items collected right now"; or, on a game build
  where the pickup counter's patterns no longer match, "Can't mark other players' items
  collected in this build of the game", after which nothing is marked), which is part of the
  poll's step, not a side task.
  With no server connection, while a save it has not dressed is loaded and no ending is on
  its way, it lets the helper go (on Linux, closes the Linux helper's connection), so the helper
  gives AP saves their look as with no client, and attaches it again on connecting
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
  the addresses it found for this build; the helper keeps them in `%LOCALAPPDATA%\ZexionAP` (the
  Linux helper the seeds alone, under `$XDG_DATA_HOME`: `native/README.md`, "The Linux helper").
  With no client, the helper uses them to give AP saves their look. With or without a client,
  they also tell it which saves are AP saves, which earn no Feats or Steam achievements.
  Deleting the folder is safe: the client sends the files again the next time it attaches to
  the game.

How the helper works: [`native/README.md`](../native/README.md).

## Glossary

- **AP save**: a randomizer save holding a seed a client has played. The helper recognizes one
  by its item table and start room, matched against the seed files in the offline cache.
  **This seed's save** is an AP save of the seed the connected client plays.
- **The helper**: `zexion_ap.dll`, built from `native/`, for the game's Windows build. **The
  proxy**: `version.dll`, built from `native/proxy/`. Players call the two together "the mod".
- **The Linux helper**: `libzexion_ap.so`, built from `native/linux/` with the sources of
  `native/src/` it shares, for the game's Linux build: preloaded by the game's launch options
  (`LD_PRELOAD`), never loaded into a running game or unloaded.
- **Stand-in**: the Scrap Energy (47) or Scrap Chunk (48) the game holds at a location whose
  item it cannot hand out itself: another player's item (48 if it is progression or a trap,
  except another Zexion player's supplies, 47 and minor as the game marks this player's own:
  `world.important_elsewhere`), or, with the OS Install Disks goal, one of this world's own disks
  (48), or one of this world's own traps (48). The helper restyles them.
- **Frame call**: one call the game makes, once per frame, through the hooked import; the
  helper runs its frame function in it. The Linux helper's is the game's `SDL_GL_SwapWindow`
  call from `SWHardwareEnd`, which the preloaded library defines in SDL's place.
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
- This world's own OS Install Disks and traps following the save: 2026-11-14 (B1; the helpers
  `zexion_ap.dll` `5f19e4d4…` from bbaf3c5 and `7859b9e7…` from 1b04f24), with the client and
  with none. What each run showed and what is not run (the disks' count with no client and on
  the file select, a death, a trap queued behind one of its kind, a load from the file select
  over a different map, no helper, and the rest): `native/README.md`, "Traps" ("This world's
  own") and "The OS Install Disks goal".
- Other players' items kept collected once the server has them as checked: 2026-11-14 (PR
  #144's branch at bbaf3c5 and 79bee7a: a reload, `!collect`), 2026-11-15 (PR #144's a17904b,
  PR #145's 399f57e, PR #146's fe4fe1b and c7186e9: the two rooms left unmarked, the hidden
  picture, a mark giving no Scrap Metal), 2026-11-16 (develop 0b01d41: 14 squares with a type-28
  event marked from afar, Kresezoa X5 Y23's boss drop, PR #153), B1. What each run showed and what
  is not run (a death, another save of the seed, `!release`, a reload beside an item, an arena's
  item marked with the player in its room, a type-715 room such as Guardia X22 Y7, the marking's
  wait through a TELEPORT TO, the map square under the other Obscure settings, a scrap-labeled
  square of item 48 that the game's on-screen write raises, nine of the 14 type-28 squares (Guardia
  X41 Y10 and X22 Y9, Pulsava X51 Y12 and X46 Y13, Phobyss X18 Y41, Kresezoa X22 Y26, Crysmoon X49
  Y32, X52 Y38 and X53 Y20), Fijedar X5 Y39's enemy's drop, and the other squares where a boss holds
  the item): `native/README.md`, "Other players' items" ("Kept collected", "This player's own Scrap
  Metal", "Run in game after the review rounds"); `docs/re/memory.md`, "What decides whether an item
  appears".
- Map markers and names under the cursor: 2026-10-25; under Obscure settings: 2026-10-27 and 28
  ("The map"; "Map markers").
- Received Energy Shields: 2026-10-23; spawned once when the client attaches in play:
  2026-11-03; given once across reconnects: 2026-11-04; the helper's check of the whole of
  event 3553: 2026-11-09 ("Received Energy Shields"; its stop after three misses: not run in
  game). One received in the pause, then TELEPORT TO START, coming up after the teleport's load:
  2026-11-14 (99dc486, `lab/ap/live62`, `lab/ingame-checkpoint-checks.md`); scrap's drops,
  received deaths and popups waiting out a TELEPORT TO's countdown: stand-in tests only ("Popups",
  "Received Energy Shields").
- Received scrap's drops (its pickup's event, after its popup), one received while
  disconnected dropping once: 2026-11-09 ("Received scrap"; what the drops were and gave not
  described).
- DeathLink, both ways: 2026-10-23 ("DeathLink").
- Traps (Disruption Trap and Disorient Trap: the Phazo's and the Balorb wave's touch): their first
  design 2026-11-11 (PR #138's 57381bf); the design since, 2026-11-11 (81dd8cc, `lab/ap/live60`;
  5f66f1b, `live61`; `develop` e500ab9, `live62` and `live63`; the popup order at 932411b); a
  trap waiting out RELOAD FROM CHECKPOINT, RELOAD FROM SAVE, a death's respawn and a TELEPORT TO,
  2026-11-13 (907abfd); this world's own traps set off at their pickup by
  the helper, 2026-11-14 (the line above); B1. What each run showed and what is not run (the
  ending or RETURN HOME, a key-look trap after the scanner, received traps kept through a
  refused version swap, a big release's popups split into batches, an unload refused while a
  trap waits, a trap's popup passed by others, a connection's first held-traps record beside
  another client's, the restart doubles, and a trap's popup in a translation):
  `native/README.md`, "Traps"; the Ailments' record: `docs/re/memory.md`, "Ailments: the script
  step that sets them".
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
  Those runs were of the count as it was until 2026-11-14, shared by every save of the seed
  (a found disk from the found record, the file select's from the client): the screens, the
  notices and their order stand; the count's source since, the loaded save: 2026-11-14.
  Not run in game: RELOAD FROM CHECKPOINT and
  a teleport at the pod, and the map right after saving.
- An Intel Chip's hint for each of this world's own found, once each, offline finds
  included: 2026-10-25 (then with a found disk's count, once each too; a disk's count now
  shows at each pickup: the own disks' line above).
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

The mod is for the game's Windows build. Zexion also has a native Linux build (below), with a
helper of its own that runs every part of the Windows helper's but unloading, run in the game on
2026-11-18 and 2026-11-19 (what ran, and what has not yet: below; these runs predate the socket's token, the client's keep-alive and let-go, and the review's helper fixes (the put-back of only a client that sent `config`, `write` refusing the function tables, the narrower map cache, the Steam check's retry), which have run on stand-ins only: the next in-game run re-checks the attach); the client
attaches to either build. Everything else runs on Windows, Linux and macOS, and CI runs ruff and pytest on
all three.

Windows only:

- the client attached to the Windows build (`client/process.py` is the Windows API; the rest of
  `client/` is plain Python, tested anywhere; on Linux it attaches to the Linux build: below);
- the live tools `memscan.py` and `helper.py load` and `unload` (`grant.py`,
  `check_patterns.py --live` and `helper.py status` run against the Linux build too: below);
- the helper and the proxy (`native/`, but `native/linux/`): MSVC and CMake's Visual Studio
  generator, and so a
  release, whose builder packs their DLLs. `tools/build_apworld.py` itself, and its tests, run
  anywhere;
- finding the game's install from Steam's registry entry, and the saves from `%APPDATA%`.

On Linux the tools look for the install and the saves in the usual places instead (below); on
macOS, set `ZEXION_GAME_DIR` and `ZEXION_SAVE_DIR`.

**Zexion's Linux build.** Zexion also has a native Linux build on Steam (the store's platform
list shows Linux, read 2026-11-16), which Steam runs by default on Linux and a Steam Deck
unless Proton is forced (assumed, as `re/builds.md`'s Status has it: a Deck's Properties showed
"Steam Linux Runtime 3.0", 2026-11-16). Its binary, a fixed-address
ELF executable that keeps its symbol table, and its data files, B1's, are recorded in
`re/builds.md` ("B1 on Linux"), and every target of `client/patterns.py` mapped onto it in
`re/linux.md`.
Its support is in the repo: the client attaches to it (below), but no release carries the
Linux helper yet, so it is not supported, and the setup guide says so ("Linux and Steam Deck").
What there is:

- the Linux helper, `libzexion_ap.so` (`native/linux/`, `native/README.md`, "The Linux helper"),
  preloaded by the game's launch options: it counts the game's frame calls, and on a socket reads
  and writes the game's memory for the client (the client can't reach it itself: `re/linux.md`,
  "Reading the running game"), starts new games (ARCHIPELAGO on the menu, or RANDOMIZER) and gives
  the client's items with their popups, scrap's drops and Energy Shields, deals DeathLink's
  deaths, names Intel Chips, locks ASSIST, with the Windows helper's own `Game`
  (`native/src/game_calls.h`, built for both through `native/src/platform.h` and the Linux parts
  of the memory layer; it runs every part but unloading), shows other players' items as
  Archipelago items (on the map too), sets off traps, shows its text in a translation's font, and
  runs the OS Install Disks goal (its count, this world's own disks counted at their pickup with
  no client, and RETURN HOME); it keeps the seeds' files (under `$XDG_DATA_HOME`) and, with one
  and no client, configures itself from the game's symbols, so this world's own traps and disks
  work with no client, gives AP saves their look without the client and the file select's AP logo,
  disk counts and ITEMS row, shows the title screen's lines, keeps Feats and Steam achievements
  out of AP saves, with the Feats screen's note, and repairs tall popups' text. Tested against a
  stand-in game (`tests/test_native_linux.py`, run on the test PC and by CI's `native-linux` job).
  Working in game on B1 (Linux) under Flatpak Steam, 2026-11-18: loaded, counting the frame calls
  and serving reads; 2026-11-19: new games (ARCHIPELAGO), grants with their popups, both traps, an
  Energy Shield, location checks, an Intel Chip and its hint, popups in English(HD) and Japanese,
  the title's lines, the file select's AP logo and ITEMS row, the look without the client, and
  `menu_state` read in play with the mouse used; and in run 2, the OS Install Disks goal (received
  and own disks counted, the pause row, the file select's counts, RETURN HOME and its ending),
  DeathLink both ways, scrap's drops, the Feats guard at an AP ending (no Feats, no achievement,
  its note) and tall popups' text (`native/README.md`, "The Linux helper"); this world's own
  traps (with or without a client) and its own disks with no client are not run in the game yet
  (the client's lines said the ASSIST lock and the labels were set; not looked at); those runs
  predate the socket's token, the client's keep-alive and let-go, and the review's helper fixes,
  which have run on stand-ins only (the next run re-checks the attach). How to run it in the game,
  and what to try first:
  `native/README.md`, "Running it in the game". `menu_state` is the game's cursor mode on Linux
  (`gCursorMode`), which the mouse may set to 1, but only in menus: in play it stays 0 (read live,
  2026-11-19: `re/linux.md`, `menu_state`). `tools/helper.py status` finds the game, checks its
  build, makes sure the socket is the game's, and reads its memory through it
  (`client/linux_helper.py`); `helper.py load` and `unload` are Windows-only. `tools/grant.py` and
  `check_patterns.py --live` attach as the client does (`game.Game.attach`), so through the Linux
  helper, `grant.py set` writing through its `write` (no such run recorded); `--live` prints each
  target's link address after `Zexion.exe+`, the base being 0 on Linux;
- the client: on Linux `game.Game.attach()` attaches to the Linux build (`attach_linux()`), its
  targets from the binary's symbol table (`client/linux_targets.py`, the ones `game.py` reads and
  writes), every read and write through the helper, one request at a time, connecting again once a
  connection drops (a request with no reply closes it, so a late reply is never read as the next
  one's); every connection must first show the helper's token (`find_token`, `auth`, in
  `LinuxHelper._check_server`: read only after `hello` has shown the server is the game's own
  process, a failure naming the places it looked), and a second task, `keepalive_loop`, pings the
  configured connection every 10 s so the helper's 30 s of silence never closes it while the game
  loop is busy. It writes nothing through a target `re/linux.md` only assumes (every one is measured
  now; `Game.unwritable` stays for any later one), and reports play only while the player state is
  where the layout puts it. The Linux helper is configured once an attach, and again once the
  client's connection to it was made again (`ticks.linux_newgame_tick`,
  `client/linux_targets.py`'s `helper_config`, `client/linux_helper.py`'s `LinuxMod`). New games
  go through it, ARCHIPELAGO offered as on Windows, and so do the step's items, with their popups,
  traps and scrap's drops (`apply`), received Energy Shields (`ticks.shields_tick`) and
  DeathLink's deaths (`ticks.deathlink_tick`), Intel Chips (`ticks.linux_intel_tick`), the ASSIST
  lock (`ticks.linux_assists_tick`), other players' items' labels (`ticks.linux_labels_tick`),
  this seed's file (`ticks.linux_seed_tick`: at first its own traps and disks, then the whole
  file, labels included, for the look without the client), CLIENT CONNECTED and the other-seed
  notice (`linux_client_back_tick`, `linux_other_save_tick`), the language
  (`linux_language_tick`), the OS Install Disks' count (`linux_discs_tick`), the finds' follow-ups
  and RETURN HOME (`linux_finds_tick`, `linux_replay_tick`). An attach holds the step back until
  the Linux helper is first configured, or for 30 s (`LINUX_HOLD_SECONDS`, the poll), so no step
  meanwhile writes the items owed with no popup or drops a waiting trap. One lost after it was
  configured holds the step back for 30 s too (`ticks.linux_mod_lost`), as does one let go of with
  no server connection (`LinuxProcess.disconnect`, `linux_back_by`: the poll waits for it to be
  configured again, up to 30 s), then the items are written
  directly with no popup until it is back; one that failed two steps running has the next step's
  written directly. A Linux helper of this version built before traps, which answers `trap_status`
  with `error unknown request` (`linux_helper.HelperTooOld`), holds nothing back: items are
  written directly until it is replaced; any other reply but `queued …` (a frame not come in time:
  `refused no frame`) is a failed configuration, tried again. The rest of the client runs as
  without the mod: `native.attach` refuses the Linux build (`native.NoWindowsHelper`, nothing
  said: the Linux helper runs every part); checks, the goal, DeathLink's deaths out and marking
  other players' items collected go on. Tested against stand-ins; run in the game for the attach,
  the seed's new game and grants (2026-11-19: `native/README.md`, "The Linux helper"), the rest
  not yet, and the token, the keep-alive and the let-go not at all;
- finding it: `tools/protected.py` looks for an install with `Zexion.exe` or the Linux
  `Zexion` in the libraries of a native (`~/.steam/steam`, `~/.local/share/Steam`), Flatpak
  (`~/.var/app/com.valvesoftware.Steam/.local/share/Steam`) or Snap Steam, and for the saves in
  SDL's preference folder (`$XDG_DATA_HOME/Gallant Leaf/Zexion`, else under `~/.local/share`),
  Flatpak Steam's (`~/.var/app/com.valvesoftware.Steam/.local/share/Gallant Leaf/Zexion`),
  Snap Steam's, or a library's Proton prefix (for the Windows build under Proton). For each,
  the one that exists, and an error naming them when several do (two installs, or saves synced
  into a folder the game doesn't use). Snap's and Proton's layouts are assumed, not seen;
- `tools/build_id.py` fingerprints the Linux binary from its ELF header (`client/elf.py`, which
  also reads its symbols), and `tools/game_data.py --exe` copies it, into a folder of its own
  hash. That copy is an ELF file: `check_patterns.py`, `make_pattern.py` and
  `gen_world_data.py` take the Windows exe's copy, which `--exe` makes only from a Windows
  install;
- `tools/save_lab.py` reads its saves, which have the Windows layout, and `install` and
  `restore` refuse while a process going by `Zexion` or `Zexion.exe` runs (by its `comm`, its
  executable's or a command-line entry's file name: `client/procfs.py`). They also refuse when
  that can't be told: run from a sandbox whose `/proc` hides the host's processes (a nested
  process-id namespace, a Flatpak terminal, WSL, found by its interop handler, its kernel's
  release or its variables), or with a process of the user's that can't be read. With the Linux
  build running under Flatpak's Steam it found the game by its name, `Zexion` (2026-11-18); the
  Windows exe under Proton, `Zexion.exe`, is assumed, not seen.

Running the Windows build under Proton (a forced compatibility tool), with the proxy needing
`WINEDLLOVERRIDES="version=n,b"` and the Windows Archipelago run inside the game's prefix, is
an untried route: the Steam Deck run stopped before it, at the owner's choice, with no Proton
run of the game at all.

Tests that need Windows skip elsewhere and say why (`tests/test_process.py`, parts of
`tests/test_native.py` and `tests/test_pe.py`), as does `tests/test_native.py`'s load test
without a built helper and `tests/test_hook_runner.py`'s runner tests without `sh`; tests that
need Linux skip elsewhere (`tests/test_procfs.py`'s and `tests/test_elf.py`'s tests on the
running system).

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

The Windows helper needs Visual Studio 2022 or later with the C++ workload, and CMake 3.20 or
later; on Windows `native/CMakeLists.txt` refuses another compiler or a single-config generator
(such as plain Ninja), since the DLLs are looked for under `native/build/Release/`. On Linux the
same file builds the Linux helper instead, with GCC or Clang and CMake 3.20 or later
([`CLAUDE.md`](../CLAUDE.md), "Checks").

Line endings: `.gitattributes` checks every text file out with LF, on Windows too, whatever
`core.autocrlf` says.

## Checks

The commands, and which CI job runs each: [`CLAUDE.md`](../CLAUDE.md), "Checks". The Claude
Code skill `/preflight` runs the Python ones and AP's world tests (those need `.ap/`). What the
native tests cover: [`native/README.md`](../native/README.md), "Building".

Some tests skip without what they need: the hook runner's without `sh`,
`worlds/zexion/test/test_client_agrees.py` without the repo's `client` beside the world (the
link `ap_checkout.py` makes), `tests/test_native.py`'s load test without a built helper, and
`tests/test_native_linux.py` without a Linux build of `native/`. CI
sets `ZEXION_REQUIRE` (a comma-separated list: `sh`, `client`, `helper`) in the jobs that have
them, and there a missing one fails a test as well.

## Running the client from source

With the game, on Windows or on Linux. The command, what it needs and its flags:
[`CLAUDE.md`](../CLAUDE.md), "The client". On Linux it never loads a helper: the game's launch
options preload `libzexion_ap.so` and the client attaches to it ("Platforms", above). On
Windows, running it may load the helper into the game: the one already there if it is this
version, else `native/build/Release/zexion_ap.dll` (or `ZEXION_DLL`). One of another version
that refuses to unload for it (a menu open, a trap waiting or running) is asked again every 5 s,
the client saying why, with items held back up to 30 s ([`native/README.md`](../native/README.md),
"Getting in"). Beyond that:

- A new game started from RANDOMIZER (kept by `--keep-randomizer`) with the seed's text typed
  into the menu gets the seed: the helper places it again with the seed's settings, whatever
  else the menu holds (`client/newgame.py`; `native/README.md`, "New games"; working in game,
  2026-10-27). From the Launcher: `ArchipelagoLauncher.exe "Zexion Client" -- --keep-randomizer`.
- `/reload_mod` (on Linux it only says to restart Zexion: the Linux helper is preloaded for the
  game's run) unloads the helper and loads it again 3 seconds later, giving no items meanwhile
  (popups still queued are lost; items are not, nor traps waiting in the save loaded, or received
  with none loaded, for which it refuses; one waiting for a save not loaded is lost); with a menu or
  the map open, while the ending RETURN HOME WITH THE DISKS? started is on its way, or while a trap
  runs or waits to go off, it refuses and says why, and the mod stays loaded. If the mod does not
  come back (no frame, the game in the background, say), items wait for it as after any lost helper.
  While the client is not connected and has let the helper go (the client, above), it says so and
  does nothing.
- While this seed's save is loaded, the client asks the helper whether its Feats and
  achievements guard is on ([`native/README.md`](../native/README.md), "Feats and achievements
  in AP saves"). If the helper says it could not do all of it (`feats=on:<why>` in its
  `hello`), or it is still off after 10 seconds with the save in play (the mod does not take
  it for an Archipelago save), the client warns once that achievements may be earned in this
  save (the reason goes to the log).
- Every few seconds while the helper is attached, the client reads the game's language from
  its `hello` (`lang`; on Linux, the Linux helper's `lang` request); in any translation but
  English(HD) it says once per game run that the mod's text is made for the default English and
  English(HD) (`state.language_line`).

## Branches and releases

`main` holds released code (or a release waiting for approval; GitHub shows its README);
`develop` holds code ready for the next release. Work happens on short-lived topic branches cut
from `develop`, merged into `develop` by pull request, then deleted. `develop` and `main` take
changes only through pull requests with every CI check passing. Branch protection requires
every check but `ruff and pytest (macos-latest)`, which a pull request must still pass.
The `native helper (ubuntu-latest)` check is not required until the owner adds it in GitHub's
settings (the checks that are required are listed there).

CI (`.github/workflows/ci.yml`) runs the checks above on every push to and pull request into
`develop` or `main`. A pull request into `main` also runs the release gate
(`.github/workflows/release-gate.yml`): it must come from this repo's `develop`, raise the
version above every release's tag and above the version on `main`, and have the version's notes
written (`tools/release_check.py`). Being above `main` matters while a merged release waits for
approval: it has no tag yet, but `main` already holds its number. If GitHub can't list the
releases or `main`'s version can't be read, the gate fails. The gate runs again on any edit to the pull request, so one retargeted from
`develop` to `main` is checked too.

A release has one version number, in four places, which the tests and the builder check
([`CLAUDE.md`](../CLAUDE.md), "Releases and versions").

To release:

1. On a topic branch, raise the version in all four places and run
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
   built into the release or published (the helper and proxy are built and tested first). A version
   already released from this same commit is skipped (a re-run). The job fails instead if that
   version's release is a draft (a publish that failed part-way: publish or delete it on GitHub,
   then re-run) or was released from another commit.
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
