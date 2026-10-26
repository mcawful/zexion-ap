# zexion-ap

[Archipelago](https://archipelago.gg) multiworld support for
[Zexion](https://store.steampowered.com/app/3392510/) (Gallant Leaf, 2025).

This is an unofficial fan project, not affiliated with Gallant Leaf. It contains no game files;
you need your own copy of Zexion.

## Status

**Pre-release.** The APWorld is on the local branch `apworld` and not published: the logic it
derives from the game's data stays local until the owner decides to publish it. The client and
its in-game helper have played seeds against a local Archipelago server, and a release build
(`tools/build_apworld.py`) has run from Archipelago 0.6.8's Launcher (2026-10-25, the `live27`
seed: the Launcher's entry started the client, which attached to the game and the helper, sent
the checks picked up and gave a received Missile Box with its popup).

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
  each part was working in game: [`native/README.md`](native/README.md).

## Features

Each says when it was working in game. An **AP save** is a save holding an Archipelago seed:
started from the seed, or a save of it the client has played.

- **Received items with the game's popup**: the item's popup, sound and an immediate HUD
  refresh (working in game 2026-10-22). Without a helper to use, items are written directly
  with no popup (received Energy Shields wait for a helper), and the client loads the helper
  again within 30 seconds. Each received item pops up once, whenever it is given: the server's
  data storage counts the popups shown, so a reconnect replays none, popups still queued when
  the client closes come next time, and a slot with no count yet gets no flood of old ones
  (working in game 2026-10-26, client 0.27.0 from the Launcher, in a slot played on 0.26.0: no
  old popups on connecting; two items sent with no client running popped up on connecting; of
  three sent at once with the client closed after the first popup, the other two came on
  reconnecting and the first did not repeat; a game closed with popups queued: stand-in tests
  only). The locations found are sent again until the server holds them, and a failing side
  task (the labels, the disk count, DeathLink, follow-up popups) is logged once while items,
  checks and the goal go on.
- **New games from the seed**: picking an empty slot offers ARCHIPELAGO in place of
  RANDOMIZER, which starts a new game holding the seed, and is offered again once that game is
  in play, so one seed can start several saves (working in game for all three modes, 2026-10-22
  and 2026-10-23; two saves from one connection 2026-10-24). With `--keep-randomizer`,
  RANDOMIZER stays, and a game started in the seed's mode gets the seed written in (working in
  game for all three modes, 2026-10-22).
- **Other players' items**: an Archipelago logo, and the item and its owner's name at pickup;
  another Zexion player's item shown as itself (working in game 2026-10-23). In an AP save the
  map shows this player's disks and other players' progression as important items and names
  them under the cursor (working in game 2026-10-25).
- **Received Energy Shields**: no inventory byte, so the helper spawns a received one on the
  player as its pickup does, and the client counts it once it sees the shield up (working in
  game 2026-10-23).
- **The escape skip**: while a client is connected, and in an AP save dressed offline (below),
  grabbing Zexion offers the game's SKIP ESCAPE AND FINAL BATTLE? (working in game 2026-10-23).
- **DeathLink**: for a seed with DeathLink on, the player's deaths go out, and received ones
  kill the player through the helper (working in game 2026-10-23).
- **The ASSIST lock**: for a seed that disallows assists (the world's default), the options
  menu's ASSIST is grayed out with a padlock, skipped by the cursor and kept from opening while
  that seed's save is loaded (working in game 2026-10-23; unlocked in another save
  2026-10-25). The Assist Unlock item (server console: `/send NAME Assist Unlock`) lifts the
  lock for good, as an escape hatch (working in game 2026-10-23).
- **Area Map hints**: with the world's Area Map hints on (the default), the Area Maps are Intel
  Chips, and each one the player gets hints where one of the player's progression items is,
  with the helper's popup (working in game 2026-10-24 for ones received, hints of up to five
  lines; 2026-10-25 for one picked up). In that seed's save the game's Area Map reads INTEL CHIP
  and reveals no map (working in game 2026-10-25).
- **The OS Install Disks goal** (the world's Goal option): the four keys are left out and the
  game's Data Disc, as the OS Install Disk, is shuffled in. The client counts the disks (never
  writing the game's disc byte) and pops up the count; the helper rewords the scanner's
  message, adds the disk to the equipment screen, and shows the count in the pause screen's
  KEYS row and, for the seed's saves, on the file select (working in game 2026-10-24 and
  2026-10-25). With enough disks the client pops up ENOUGH OS INSTALL DISKS FOUND. SAVE AT THE
  LANDING POD TO RETURN HOME., and a save at the landing pod offers RETURN HOME WITH THE
  DISKS?, whose YES runs the game's ending, which sends the goal (YES and NO working in game
  2026-10-24; the reminder, then worded ALL OS INSTALL DISKS FOUND., and the offer after a save
  at the pod and not elsewhere, 2026-10-25; the ENOUGH wording not yet seen in game). This goal
  needs the helper: without it nothing offers the ending.
- **Popups for this world's finds, once each**: the popup after the game's own for a find that
  has one (an OS Install Disk's count, an Intel Chip's hint) shows once per find, offline finds
  included, after the connected notice (working in game 2026-10-25).
- **The helper loaded with the game**: with `zexion_ap.dll` and the proxy `version.dll` in the
  game's folder, the game loads the helper as it starts and the client uses it (working in
  game 2026-10-25). When the client goes away the helper puts back what it changed and waits
  for the next one, apart from AP saves dressed offline (working in game 2026-10-25 for the
  item records, ASSIST and the escape skip). Once configured, with a client or not, it adds
  ARCHIPELAGO MOD, its version and ©2026 MCAWFUL under the title screen's copyright line, with
  START THE AP CLIENT TO PLAY A SEED above while no client is connected (the two lines under
  the copyright working in game 2026-10-25; the third reported up by the helper with no client,
  2026-10-25, its final layout not checked on screen).
- **AP saves without the client**: with the helper loaded with the game, an AP save keeps its
  look without the client: its item labels, INTEL CHIP, the ASSIST lock, the escape skip and
  its disk count (on the file select too), with a notice that the client is not connected (and
  another once one connects while that save is played). Received items, hints and the goal wait
  for the client, and what was picked up is sent when it connects. With the client connected
  for one seed, an AP save of another seed is told so, and gets its own seed's look if the
  helper has seen that seed (working in game 2026-10-25, from a fresh start too). How it works:
  [`native/README.md`](native/README.md), "AP saves without the client".

## Development

See [`CLAUDE.md`](CLAUDE.md) for setup, commands, the layout and the project's rules, and
[`docs/re/new-build.md`](docs/re/new-build.md) for what to do when the game is patched.
