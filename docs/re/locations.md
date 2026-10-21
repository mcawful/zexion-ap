# Locations and what reaching them takes

The APWorld's locations and rules are not written by hand: `tools/gen_world_data.py` generates
`worlds/zexion/data.py` from the game's `RandoCells` file, and `worlds/zexion/logic.py` checks
the requirement lists the way the game does. This page records how that data is derived and
what backs it. The format of `RandoCells` and the meaning of every code are in
[`randocells.md`](randocells.md); the save fields in [`save-format.md`](save-format.md).

## The 154 locations are the game's own list

| Field | |
|---|---|
| Value | The world's locations are the 154 cells of the randomizer's location list, in its order, which is the same in every seed (`save-format.md`, "What changes between randomizer seeds"). Each is named after its map cell as the game displays it, "Map *x*,*y*" (`randocells.md`, "Grid order"). A location whose cell has its own item requirements (byte `+0x44` of the cell's record) gets them as its access rule: 61 of the 154. |
| Status | measured (the list, its order and the display order) |
| Method | `tools/gen_world_data.py` reading the location list of a fresh default-preset save and each cell's record |
| Build | B1 |
| Evidence | `lab/backups/20261020-212018/Save Game 28.sav` and `lab/data/6d8a94bd4d64/RandoCells`, 2026-10-21. |

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
| Value | Of the game's 34 start candidates (`save-format.md`, "The continue point and the seed's start"), every one reaches all 154 locations with every item, but with only the default preset's starting inventory (5 Energy Tanks, 3 Missile Containers, the Machine Gun, the Power Cartridge, the Navcore), 25 reach fewer than 3 locations. The world offers the 9 that reach at least 3, including the three starts seen in real seeds. |
| Status | measured (by the flood fill); assumed (that 3 locations is enough for Archipelago's fill: it was in every generation tried) |
| Method | `tools/gen_world_data.py` |
| Build | B1 |
| Evidence | Generator output, 2026-10-21. |

## One-way pockets are escaped by teleporting to the start

| Field | |
|---|---|
| Value | Neither the world nor the game's own flood fill checks that a player can get back from where they go. Some passages lead into pockets whose way out needs an item: from the default start (13, 16), the open passage east from (22, 26) into (23, 26) has an empty requirement list on the way back, and the pocket's only other exit, past location "Map 27,24", needs Slide Mod. Collecting that location without Slide Mod would cut the player off from the start's other early locations. With only the starting items, 3 to 204 reachable cells per start are such pockets; with every item, 23 cells still have no way back (for example (21, 8) and (24, 6)). The game's own seeds have the same pockets, and the game provides the escape: the pause menu offers a teleport back to the start from anywhere (the owner's report). So the pockets are not soft-locks, and the logic needs no change: the teleport only adds a way back to the start, which is already reachable. It goes to the seed's start as the save records it, a patched start included: in the patched slot 26 (start moved to (23, 22)), teleporting from a few rooms away put the owner back in that save room. |
| Status | measured (the pockets, from the data); working in game (the teleport from the pause menu, from anywhere, and to a patched start: slot 26, 2026-10-21) |
| Method | the `logic-reviewer` subagent's route search over `worlds/zexion/data.py` and the cell-by-cell flood fill; the owner's account of using the teleport |
| Build | B1 |
| Evidence | 2026-10-21. |

## The goal is the core, behind the four keys

| Field | |
|---|---|
| Value | The world's goal is an event at map cell (32, 45), the core, needing all four keys: the game's main ending starts when Zexion is grabbed there, and the keys open the core (`memory.md`, "The goal: Zexion's inventory byte at the grab"). `RandoCells` reaches (32, 45) without the keys, so the key rule is the world's own addition, from the owner's account. The client detects the goal as item 31's byte going 0 → 1. |
| Status | measured (the cell, the byte); reported by the owner (the keys) |
| Method | as in `memory.md` |
| Build | B1 |
| Evidence | 2026-10-21. |

## What the world needs from the client

- **Items lost on death.** A grant the game has not saved is gone after a death
  (`memory.md`, "Granting each kind of item, and what keeps it"). The world treats every item
  as permanent once received, so the client must give items again after a reload.
- **The starting inventory and settings.** The world's logic assumes the default preset's
  starting inventory and settings (`worlds/zexion/data.py`, `START_INVENTORY`, `ENERGY_TOTAL`,
  `MODE69`, `OPTIONS`). The save the client prepares must hold exactly those, or sphere 0 is
  wrong.

## Not yet decided

- **Settings.** The world uses the default preset's settings (energy total 40, code-69 mode 2,
  its option flags) and item pool; other presets are not offered yet.
- **Missile and Bomb Boxes in groups.** As in `randocells.md`, a box adding 10 to a group's
  count is not modeled.
