# zexion-ap

[Archipelago](https://archipelago.gg) multiworld support for
[Zexion](https://store.steampowered.com/app/3392510/) (Gallant Leaf, 2025).

This is an unofficial fan project, not affiliated with Gallant Leaf. It contains no game files;
you need your own copy of Zexion.

## Status

**Pre-release.** The APWorld is still on the local branch `apworld` and is not published:
the logic it derives from the game's data stays local until the owner decides to publish it.
Nothing has been released.

The go/no-go phase is done: items can be placed through the save, collected locations detected
and items granted while playing, and the game's randomizer logic is decoded. The client and
its in-game helper have played seeds in the game against a local Archipelago server, first a
solo seed. A release build (`tools/build_apworld.py`) has run from the owner's Archipelago
0.6.8 Launcher (2026-10-25, B1): the apworld installed, the Launcher's entry started the
client, which connected and attached to the game and the helper, and, on the `live27` seed,
sent the checks picked up and gave a received Missile Box with its popup.

Every in-game run below was on game build B1 ([`docs/re/builds.md`](docs/re/builds.md)), the
only build the client and the helper know. What has been learned about the game is recorded,
with its evidence, in [`docs/re/`](docs/re/README.md).

For players: the world's [game page](worlds/zexion/docs/en_Zexion.md) and
[setup guide](worlds/zexion/docs/setup_en.md) (on `apworld`, with the world).

## How it fits together

- **The world** (`worlds/zexion/`, on `apworld`) places items at the game's 154 locations (in
  OHKO, the 150 it can reach), using the game's own randomizer map and rules
  ([`docs/re/locations.md`](docs/re/locations.md), on `apworld`).
- **The client** (`client/`) attaches to the running game. While this seed's save is loaded it
  sends the locations the map marks as collected, gives the items other worlds send, and sends
  the goal when the ending begins. Items given and then lost to a death or a reload are given
  again.
- **The in-game helper** (`native/`, `zexion_ap.dll`) runs inside the game, on its own thread
  between frames, and does what the client cannot from outside: the game's popups, new games
  from the seed, the look of other players' items, and the rest below. How it works, and when
  each part ran in the game: [`native/README.md`](native/README.md).

## Features

Each says when it ran in the game. A test on synthetic data is not that, and says so.

- **Received items with the game's popup** (helper phase 2): the item's popup, sound and an
  immediate HUD refresh, live-tested on one seed (2026-10-22). Without a helper to use
  (none loaded in the game and neither a build nor a copy in the game's folder, or a helper
  that fails) items are written directly, with no popup, all but Energy Shields,
  which only the helper can give; the client loads the
  helper again within 30 seconds (`/unload_helper` unloads it as if it had failed, to test
  that). Each received item's popup is shown once: the server's data storage keeps how many
  have been shown (`zexion_announced_<team>_<slot>`), counted as each popup leaves the
  helper's queue (its `popups` reply), so an item received while no client ran pops up when it
  is given, a reconnect replays none, and popups still queued when the client or the game
  closes come again next time. A slot with no count stored yet counts the items it holds as
  shown and stores that at once, so an older slot gets no flood of popups (live on
  2026-10-26, build B1, helper and client 0.27.0 from the Launcher, in a slot played on 0.26.0
  with no count stored: no old popups on connecting; two items sent with no client running
  popped up on connecting; of three sent at once, the client closed after the first popup,
  the other two came on reconnecting and the first did not repeat. A game closed with popups
  queued: stand-in tests only). The record of locations found is sent again
  every 5 seconds until the server holds it, and a failing side task (the labels, the disk
  count, DeathLink, follow-up popups and the like) is logged once while items, checks and
  the goal go on.
- **New games from the seed** (helper phase 3). With the helper, the client arms it with the
  seed, and again once each new game holding it is in play, so one seed can start several
  saves. For a seed whose slot data carries the menu setup (from the world on `apworld`),
  picking an empty slot then offers ARCHIPELAGO in place of RANDOMIZER, which starts a new game
  holding the seed (run in game for all three modes, first on a Standard
  seed; for OHKO the helper also fills in the menu's start table; two saves of one seed from
  one connection run in game 2026-10-24, B1; the helper also fits the hover border round
  ARCHIPELAGO). With `--keep-randomizer`, RANDOMIZER stays: started in the seed's mode, the
  helper writes the seed in as the game starts (run in game for all three modes).
- **Other players' items** (helper phase 4): an Archipelago logo and the item's owner's name
  at pickup (live-tested 2026-10-23), and another Zexion player's item shown as itself (in
  game since 2026-10-23). In an Archipelago save the map shows the progression stand-in's
  squares (this player's disks, other players' progression) as important items and names them
  under the cursor (working in game 2026-10-25).
- **Received Energy Shields**: an Energy Shield is no inventory byte, so the helper spawns a
  received one on the player as its pickup does, and the client counts it in the server's data
  storage once it sees the shield up (working in game 2026-10-23; `native/README.md`,
  "Received Energy Shields").
- **The escape skip**: while a client is connected, and in a save dressed offline (below),
  grabbing Zexion offers the game's SKIP ESCAPE AND FINAL BATTLE? (live-tested 2026-10-23 with
  the client).
- **DeathLink**: for a seed with DeathLink on, the client sends the player's deaths and deals
  the ones it receives through the helper, which kills the player (live-tested 2026-10-23).
- **The ASSIST lock**: for a seed that disallows assists (the world's default), the helper
  grays out the options menu's ASSIST entry, puts a padlock after it, has the cursor skip it
  and keeps it from opening while that seed's save is loaded (the lock live-tested 2026-10-23;
  unlocking in another save 2026-10-25). Sending the slot the Assist Unlock item (server
  console: `/send NAME Assist Unlock`) lifts the lock for good, as an escape hatch
  (live-tested 2026-10-23).
- **Area Map hints**: for a seed with Area Map hints (the world's default), the Area Maps are
  Intel Chips, and each one the player gets hints where one of the player's progression items
  is: the client creates the Archipelago hint and the helper's popup names it (live-tested
  2026-10-24 for ones received from the server, hints of up to five lines, and 2026-10-25 for
  one picked up in the player's own world). While that seed's save is loaded the helper names
  the game's Area Map INTEL CHIP and turns off its map reveal (`intel`; live-tested
  2026-10-25).
- **The OS Install Disks goal** (the world's Goal option): the four keys are left out and the
  game's Data Disc, named OS Install Disk, is shuffled in. The client counts them (never
  writing the game's disc byte) and pops up the count, and the helper reworks the scanner's
  message, adds the disk to the equipment screen and turns the pause screen's KEYS row into a
  disk count. The file select shows the count in place of the keys for the seed's saves
  (nothing with no disk), and the game's own again once the client disconnects (live-tested
  2026-10-24 and 2026-10-25), or, with the helper loaded with the game, each save's own count
  from the offline look below. Once the player has enough, the client pops up ENOUGH OS INSTALL
  DISKS FOUND. SAVE AT THE LANDING POD TO RETURN HOME. (once per load), and after each save at
  the landing pod (the seed's start, seen in the slot's block the game rewrites on a save, with
  the player in the start cell) the helper offers RETURN HOME WITH THE DISKS?, whose YES starts
  the game's ending, which sends the goal (YES and NO live-tested 2026-10-24; the reminder,
  then worded ALL OS INSTALL DISKS FOUND., and the offer after a save at the pod, not elsewhere,
  2026-10-25; the ENOUGH wording not yet seen in the game). This goal needs the helper:
  without it nothing offers the ending.
- **Popups for this world's finds, once each**: the popup after the game's own for each of
  this world's finds that has one (an OS Install Disk's count, an Intel Chip's hint) is shown
  once for each, whenever it was found, offline included: the server's data storage keeps
  which were shown (`zexion_popped_<team>_<slot>`), and on connecting the client shows the
  ones missing for the loaded save, after the connected notice (live-tested 2026-10-25).
- **The helper loaded with the game**: the player may put `zexion_ap.dll` and the proxy
  `version.dll` in the game's folder, and the game then loads the helper as it starts; the
  client uses that one instead of loading its own (live-tested 2026-10-25). While a client is
  connected the helper changes the game; when the client goes away it puts all of it back and
  does nothing until a client connects again (live-tested 2026-10-25 for the item records,
  ASSIST and the escape skip; [`native/README.md`](native/README.md), "A client going away"),
  except the offline look below. Once configured, with a client or without, the helper adds
  ARCHIPELAGO MOD and its version, and ©2026 MCAWFUL, under the title screen's copyright line,
  with START THE AP CLIENT TO PLAY A SEED above them while no client is connected (the two
  lines under the copyright working in game 2026-10-25; the third reported up by the helper's
  `hello` with no client, 2026-10-25, its final layout not checked on screen).
- **AP saves without the client**: with the helper loaded with the game, an AP save keeps its
  look without the client. While connected, the client sends the helper a small file per seed
  and the addresses it resolved (`offline_put`, `client/offline.py`), which the helper writes
  into `%LOCALAPPDATA%\ZexionAP` (the helper, not the client: a client run from a packaged
  app, as from the Claude desktop app, has its AppData writes there unseen by the game,
  inferred to be the package's redirect). With no client, a save of such a seed gets its item
  labels, INTEL CHIP, the ASSIST lock, the escape skip and its disk counts (on the file select
  too, each save its own), with one notice that the client is not connected (and another when
  one connects while that save is played: live-tested 2026-10-25); received items, hints and
  the goal wait for the client, and what was picked up is sent when it connects (live-tested
  2026-10-25, from a fresh start too). With the client connected for one seed, a randomizer
  save of another seed gets THE ARCHIPELAGO CLIENT IS FOR ANOTHER SEED. once a load and on a
  reconnect (`other_save`), and that seed's offline look if the helper has its file; the file
  select shows each known seed's saves their own counts (live-tested 2026-10-25).

## Development

See [`CLAUDE.md`](CLAUDE.md) for setup, commands, the layout and the project's rules, and
[`docs/re/new-build.md`](docs/re/new-build.md) for what to do when the game is patched.
