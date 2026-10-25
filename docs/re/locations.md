# Locations and what reaching them takes

The APWorld's locations and rules are not written by hand: `tools/gen_world_data.py` generates
`worlds/zexion/data.py` from the game's `RandoCells` file, and `worlds/zexion/logic.py` checks
the requirement lists the way the game does. This page records how that data is derived and
what backs it. The format of `RandoCells` and the meaning of every code are in
[`randocells.md`](randocells.md); the save fields in [`save-format.md`](save-format.md).

## The 154 locations are the game's own list

| Field | |
|---|---|
| Value | The world's locations are the 154 cells of the randomizer's location list, in its order, which is the same in every seed (`save-format.md`, "What changes between randomizer seeds"). Each is named after its area and its map cell as the game displays them (below, "Location names"). A location whose cell has its own item requirements (byte `+0x44` of the cell's record) gets them as its access rule: 61 of the 154. |
| Status | measured (the list and its order) |
| Method | `tools/gen_world_data.py` reading the location list of the fresh Easy and Standard saves (it checks the two are identical) and each cell's record |
| Build | B1 |
| Evidence | `lab/backups/20261020-212018/Save Game 28.sav` and `Save Game 29.sav`, and `lab/data/6d8a94bd4d64/RandoCells`, 2026-10-21. |

## Location names

| Field | |
|---|---|
| Value | A location, start room or second teleport target is named "*Area* *X*,*Y*", as in "Guardia 22,7": the area the pause screen names after REGION:, then the cell's X and Y as the pause screen shows them (this repo's cell (x, y) is X = y, Y = x: `randocells.md`, "Grid order"). Start-room options are the same in lower case with underscores, as in `kresezoa_16_13`. The area comes from the pause screen's own lookup (`0x16CD7B`): the cell's area is the 16-bit value at `+0x361C58` + (X × 54 + Y) × `0x9A`, and the region's text is entry `[+0xCDE231 + area × 2]` of the `Text` file (32-byte entries after a 6-byte header). Both tables are loaded from the `MapData` file: the cell records start at file offset `0x3E5749` (the same `0x9A`-byte records; their area equal to the loaded table's, read from the running game, in all 2,916 cells) and the area table at `0x45F94D`, the region the second byte of each 2-byte entry. `tools/gen_world_data.py` reads them for every location and start room (`CELL_AREAS` in `data.py`), after checking both files' hashes. The 154 locations lie in 11 areas (Kresezoa, Machinus, Crysmoon, Guardia, Pulsava, Phobyss, Vulknor, Fijedar, Vallahar, Exodia, R'Talva: text entries 0 to 7, 18, 19 and 22), and the names are unique. |
| Status | from disassembly (the lookup) and measured (the file offsets); two of the 11 areas, Pulsava and Guardia, checked against the pause screen, the other nine from the lookup alone |
| Method | disassembly of the pause screen's REGION: line; the file offsets by matching the loaded tables to `MapData`; the owner's readings of the pause screen: X37 Y18 in Pulsava, and X22 Y7 in Guardia, both what the lookup gives |
| Build | B1 (`MapData` sha256 `79a0510f4dbbee0f…`, `Text` `3bf8cd5e07ede18f…`) |
| Evidence | `lab/data/6d8a94bd4d64/MapData` and `Text`; the owner's readings in the `live15` seed, 2026-10-23 |

## Regions are cells joined by open passages

| Field | |
|---|---|
| Value | Cells reached from a start with every item are grouped into regions: the strongly connected components of the passages that are always open (passage byte 0). Each conditional passage (byte 1) between two regions becomes an entrance whose rule is the requirement list of the cell it leaves from, checked with that cell as "where" (codes 64 to 69). A rule with codes 64 to 67 depends on whether a neighboring cell is reached; the world checks that with `can_reach_region` on the neighbor's region and registers it as an indirect condition. Where an always-open passage joins two regions, conditional passages between the same two are dropped. With the default preset this gives 446 regions and 1,081 region links (1,106 ways through). |
| Status | measured (the counts, for build B1's `RandoCells`) |
| Method | `tools/gen_world_data.py` |
| Build | B1 |
| Evidence | Generator output, 2026-10-21. |

## The translation matches the game's logic on every start

| Field | |
|---|---|
| Value | For each of the 9 valid start rooms, Archipelago generated a single-player game, and its placement, replayed through `tools/randocells.py`'s flood fill from that start with the starting inventory (the game's own algorithm, which uses no regions), reached all 154 locations and all four keys. So every placement the world accepts is completable under the game's rules. The opposite direction (that the world rejects no placement the game would accept) is not checked. Archipelago's general world tests and the world's own tests pass, and a nine-player game with one player per start room generated in about 4 s. |
| Status | measured (9 placements, one per start, seeds 100 to 108) |
| Method | `Generate.py --spoiler 1`, the spoiler's placement parsed and passed to `randocells.sweep`; `AP_TEST_WORLDS=zexion pytest worlds/zexion test/general` in the pinned checkout |
| Build | B1 |
| Evidence | 2026-10-21. Outputs in the session scratchpad (not kept). |

## Which start rooms the world offers

| Field | |
|---|---|
| Value | Of the game's 34 start candidates (`save-format.md`, "The continue point and the seed's start"), every one reaches all 154 locations with every item, but with only Standard mode's starting inventory as seed 29 had it (3 Energy Tanks, 3 Missile Containers, the Force Blaster), 25 reach fewer than 3 locations. The world offers the 9 that reach at least 3, including the three starts seen in real seeds. The same 9 come out for Easy mode, with slot 28's starting inventory and settings (5 Energy Tanks, 3 Missile Containers, the Machine Gun, the Power Cartridge, the Navcore; energy total 40). The world offers each mode the rooms valid for it. For each mode and each of its rooms, an Archipelago placement replayed through `randocells.sweep` reached 154/154 (18 games, seeds 300 to 308, 2026-10-21). |
| Status | measured (by the flood fill); assumed (that 3 locations is enough for Archipelago's fill: it was in every generation tried) |
| Method | `tools/gen_world_data.py` |
| Build | B1 |
| Evidence | Generator output, 2026-10-21. |

## One-way pockets are escaped by teleporting to the start

| Field | |
|---|---|
| Value | Neither the world nor the game's own flood fill checks that a player can get back from where they go. Some passages lead into pockets whose way out needs an item: from the default start (13, 16), the open passage east from (22, 26) into (23, 26) has an empty requirement list on the way back, and the pocket's only other exit, past location "Machinus 27,24", needs Slide Mod. Collecting that location without Slide Mod would cut the player off from the start's other early locations. With only the starting items, 3 to 204 reachable cells per start are such pockets; with every item, 23 cells still have no way back (for example (21, 8) and (24, 6)). The game's own seeds have the same pockets, and the game provides the escape: the pause menu offers a teleport back to the start from anywhere (the owner's report). So the pockets are not soft-locks, and the logic needs no change: the teleport only adds a way back to the start, which is already reachable. It goes to the seed's start as the save records it, a patched start included: in the patched slot 26 (start moved to (23, 22)), teleporting from a few rooms away put the owner back in that save room. |
| Status | measured (the pockets, from the data); working in game (the teleport from the pause menu, from anywhere, and to a patched start: slot 26, 2026-10-21) |
| Method | the `logic-reviewer` subagent's route search over `worlds/zexion/data.py` and the cell-by-cell flood fill; the owner's account of using the teleport |
| Build | B1 |
| Evidence | 2026-10-21. |

## The core goal is the core, behind the four keys

| Field | |
|---|---|
| Value | With the Goal option's default, core (the OS Install Disks goal is below), the world's goal is an event at map cell (32, 45), the core, needing all four keys: the game's main ending starts when Zexion is grabbed there, and the keys open the core (`memory.md`, "The goal: Zexion's inventory byte at the grab"). `RandoCells` reaches (32, 45) without the keys, so the key rule is the world's own addition, from the owner's account. The client detects the goal as item 31's byte going 0 → 1. |
| Status | measured (the cell, the byte); reported by the owner (the keys) |
| Method | as in `memory.md` |
| Build | B1 |
| Evidence | 2026-10-21. |

## The OS Install Disks goal: the disks, then the ending

| Field | |
|---|---|
| Value | With the Goal option's OS Install Disks, the world has no core event: the keys are left out of the pool (their places become Scrap Metal), OS Install Disks (the game's Data Disc, item 44) are added in their number, each taking the place of, in order, Scrap Metal, Scrap Energy, Scrap Chunk, Area Map, Energy Shield, and the completion condition is having the number required. No location or passage rule reads any of these items or the keys (codes 27 to 30, 44, 46 to 50 and the Intel Chip's 101 among item codes; rule codes from 50 up are derived ones, `logic.derive`), so the pool's logic is unchanged. Room per mode (after left-out locations): Easy 31, Standard 31, OHKO 41, more being an `OptionError`; the options stop at 30 (the owner's cap), with defaults of 16 placed and 12 required, which fit Standard without taking an Area Map. Nothing places an item behind the game's secret boss: its passage (cell (22, 48)) leads to a map of its own, and none of the 154 locations is there. The client sends the goal when the game's ending begins with the player holding enough disks (`memory.md`, "The mission prompt"): once they have them and save at the landing pod (the seed's start), the helper offers RETURN HOME WITH THE DISKS?, whose YES starts the ending. With the Area Map Hints option on (the default), each Area Map left after those replacements becomes an Intel Chip (code 101, `worlds/zexion/items.py`): an Archipelago-only item, filler as the Area Map is, which `location_items` gives the game as the Area Map (49, `DRAWN_AS`); no rule reads it either. |
| Status | from the world's data (the rule codes, the pools, the room); generated and filled by Archipelago's tests in all three modes; working in game (2026-10-24, B1: a Standard seed of 3 of 20 disks, the goal sent at the ending reached from the prompt's YES, its question then END THE MISSION?; then with RETURN HOME WITH THE DISKS?, NO and the question again on the next load, helper 0.17.1). The goal needs the client's helper: without it nothing offers the ending, and with no keys the core is closed for good under Gates Vanilla (OHKO); whether Gates Unlocked (Easy, Standard) opens it without keys is not traced (`randocells.md`). |
| Method | `worlds/zexion/test/test_world.py`; Archipelago's general tests; the live runs |
| Build | B1 |
| Evidence | `lab/ap/live22/`, `lab/ap/live23/` (the seeds, their spoilers, the client and server logs), 2026-10-24 |

## OHKO: two starts, and only the locations they reach

| Field | |
|---|---|
| Value | An OHKO seed starts in one room and can teleport to a second (`save-format.md`, "The continue point and the seed's start"), and some of its locations are unreachable (`randocells.md`, "The randomizer's modes"). For OHKO the generator records every one of the 34 candidates with the locations every item reaches from it alone and those its starting items (3 Missile Containers and the Machine Gun) reach: every candidate reaches the same 150 locations with every item, and 0 to 9 with the starting items. The world takes the first room from the Start Room option and picks the second at random among those that, together with the first, let the starting items reach at least 3 locations; both connect to the start of the logic. It creates only the locations every item reaches from one of the two, and removes as many filler items from the pool (Scrap Metal first), as the game fills unreachable locations with Scrap. Combining the two rooms' single-start results is exact for the locations and regions, as every candidate reaches the same 443 regions and 150 locations with every item; for the starting items it can only undercount what the pair reaches together. That the second target can be used from the first moment is from the game's own text (`LargeText.txt` entry 283, OHKO: "No energy tanks at all. Can teleport to two locations.") and from the real OHKO seed, whose first start alone reaches no location. Gates Vanilla does not loosen anything: the gates can be switched off entirely (entries 349 to 352) while `RandoCells` stays the same file, so the logic does not count on gate travel; and the goal's key rule matches Vanilla's "require the four keys". Twelve OHKO games with random start rooms (seeds 400 to 411), replayed through `randocells.sweep` from both their starts with the left-out locations as Scrap, reached all 150 placed locations and all four keys. |
| Status | computed with the evaluator (the 150 locations; the 12 replays); working in game: a second target written at `0x12EE0` (a patched OHKO save in slot 6 teleported the owner to the patched room, 2026-10-21; `save-format.md`). Not yet tried: a whole OHKO game written by the client. |
| Method | `tools/gen_world_data.py` with the fresh OHKO save; `Generate.py --spoiler 1`, the start rooms read from the spoiler header the world writes |
| Build | B1 |
| Evidence | `lab/work/slot5-ohko-fresh.sav`; generator output and replays, 2026-10-21. |

## What the world needs from the client

- **Items lost on death.** A grant the game has not saved is gone after a death
  (`memory.md`, "Granting each kind of item, and what keeps it"). The world treats every item
  as permanent once received, so the client must give items again after a reload.
- **The starting inventory and settings.** The world's logic assumes the chosen mode's
  starting inventory and settings (`worlds/zexion/data.py`, `MODES`, sent as `mode` in the slot
  data). The save the client prepares must hold exactly those, or sphere 0 is wrong; in OHKO
  also the second teleport target (`second_start`), and Scrap at every location the world left
  out (those not in `kept_locations`).
- **What the slot data gives the client for that.** `location_items`: for each of the 154
  locations, the game's item ID to place there: this world's own item (a progressive weapon as
  the weapon, which the game turns into the upgrade on a second copy, `randocells.md`), or
  Scrap Metal (46) where another world's item is or the location was left out. `start_items`:
  inventory bytes for everything the player starts with, the mode's items and any
  `start_inventory` (a second copy of a progressive weapon as its upgrade). `amounts` and
  `options`: the mode's six Energy, missile and bomb amounts and the settings the logic depends
  on (code 68's tiers 30, 31, 32 and 37, Double Teleport 33, Safety 42 and 43, Gates 72 and 73),
  as the mode's fresh save holds them, for the client to compare with the player's new game.
  The Energy Shield can be placed in any world: it is no inventory byte (its pickup counts as
  Scrap Metal and spawns the shield), so a received one is counted as Scrap Metal and spawned by
  the client's helper (`memory.md`, "Received Energy Shields").

## Not yet decided

- **Modes and settings.** The world offers Easy, Standard and OHKO, each as one fresh save of
  that mode had it: its settings, its item pool and its starting items (in Standard, the Force
  Blaster that "Random Gun" gave that seed). The individual settings (`randocells.md`,
  "Where each setting is stored") are not separate options yet.
- **Missile and Bomb Boxes in groups.** As in `randocells.md`, a box adding 10 to a group's
  count is not modeled.
