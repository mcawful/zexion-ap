# RandoCells

`Files/RandoCells` is the built-in randomizer's logic: for every map cell, what it takes to
leave it in each direction and to collect its item. `tools/randocells.py` parses it and runs a
reachability sweep for a seed. Format per `docs/re/README.md`.

## Loading: 2,916 records of 85 bytes, read raw

| Field | |
|---|---|
| Value | The loader (function at RVA `0x1B5410`) builds the path `…RandoCells`, logs "Reading RandoBlocks file...!", and reads the file with one call of record size `0x55` (85) and count `0xB64` (2,916) straight into a global array at RVA `0xDF8690` (`Zexion.exe+0xDF8690` in memory). No header, no compression. 2,916 = 54 × 54. |
| Status | measured |
| Method | disassembly (capstone) of the function referencing the "Unable to open file RandoCells" string |
| Build | B1 |
| Evidence | `lab/data/loader.asm` (local), 2026-10-20. File size 247,860 = 2,916 × 85. |

## Grid order: row-major, index = y × 54 + x

| Field | |
|---|---|
| Value | Record *i* is the map cell (*i* mod 54, *i* div 54), the same grid as the save's map at file `0x877F` and the randomizer's location cells. |
| Status | measured |
| Method | disassembly: the load-time checker (RVA `0x1AC8E0`) indexes records as `row × 0x11EE + column × 0x55` (`0x11EE` = 54 × 85) |
| Build | B1 |
| Evidence | `lab/data/logic.asm` (local), 2026-10-20. |

## Record layout

| Field | |
|---|---|
| Value | `+0x00` four lists of 8 one-byte entries, for leaving the cell to the **N, S, W, E** (in that order); `0xFF` ends a list. `+0x20` four parallel lists of tier/count bytes. `+0x40` four passage bytes for the **S, W, E, N** (in that order): `0` open, `1` needs that direction's list, `0xFF` wall. `+0x44` 1 when collecting the cell's item has its own requirements. `+0x45` those requirements (8 entries), `+0x4D` their tier/count bytes. |
| Status | measured (offsets and sizes, list-to-passage pairing); measured statistically (direction names) |
| Method | disassembly: every function using the array reads the pairs `+0x00/+0x20`, `+0x08/+0x28`, `+0x10/+0x30`, `+0x18/+0x38`, `+0x45/+0x4D`, and bytes `+0x40` to `+0x44`. Every non-empty list sits on a cell whose matching passage byte is 1 (202/202, 195/195, 309/309, 108/108). Direction names: each passage byte's `0xFF` agrees best (82 to 85%) with "no explored room in that direction" on a fully explored map; 61 of the 63 cells with an item-requirement list are location cells. |
| Build | B1 |
| Evidence | `lab/data/6d8a94bd4d64/RandoCells`, slot 1 map, location cells from slot 28, 2026-10-20. |

## Entry values

| Field | |
|---|---|
| Value | An entry below 50 is an item ID (`ItemNames.txt`); 50 to 69 is a special condition, drawn with its own icon (sprite table at RVA `0x20C820`), whose meaning is not known. The tier/count byte: `1` white, `2` yellow, `3` red, `4` blue (colors used by the game's built-in editor), `11` to `40` white with the number (value − 10), `41` to `90` yellow with the number (value − 40). Reading: white entries each suffice alone; yellow entries form one group that is needed together (tier 2 almost always appears two or more times in a list); counts are "at least N of that item" (Missile Container `43` = 3 containers). Tier 3 turned out to exclude an item from the check (see the evaluator below); tier 4 is unknown. |
| Status | measured (the editor's encoding: item vs special, colors, the numbers it draws); assumed (the AND/OR reading; tier 4) |
| Method | disassembly of the editor's drawing function (RVA `0x3CB3D`); counting tier patterns in multi-entry lists |
| Build | B1 |
| Evidence | `lab/data/gen.asm` (local), 2026-10-20. |

## A sweep with this reading reaches every location in three real seeds

| Field | |
|---|---|
| Value | Starting at cell (25, 25), moving only where the cell being left allows it, and collecting each location's item once reached (and once its own requirements hold), all 154 locations are reachable in each of the three randomizer seeds on hand, when special conditions count as met. With the opposite reading (the cell being entered decides) 3 locations stay out of reach even then, so the cell being left decides. With special conditions never met, only 3 to 8 locations are reachable, so they matter and must be decoded before the logic can be used. |
| Status | measured (the sweep results); assumed (the start cell, which matches where a new game begins in observation) |
| Method | `tools/randocells.py` on backup copies of slots 2, 28 and 29 |
| Build | B1 |
| Evidence | `lab/backups/20261020-224429/Save Game 2.sav`, `lab/backups/20261020-212018/Save Game 28.sav` and `Save Game 29.sav`, 2026-10-20. |

## The generator's reachability check

| Field | |
|---|---|
| Value | Function at RVA `0xE5240` is the seed generator's flood fill: recursive over the 54 × 54 grid from a cell, marking visited cells in a byte grid at RVA `0xEB21C0` (1 = reached, 2 = item collectible) and a step number per cell in a 16-bit grid at RVA `0xEB0900`. For each direction it calls the requirement evaluator at RVA `0xE3C80` with the passage byte, the list and its tier bytes: passage `+0x41` with list `+0x10`/`+0x30` leads to x − 1, `+0x42`/`+0x18` to x + 1, `+0x43`/`+0x00` to y − 1, `+0x40`/`+0x08` to y + 1, which confirms the direction names above from the code. A cell with a location (a table at RVA `0x361C4A`, 154 bytes per cell, `0xFF` when none) is marked collectible when the evaluator passes `+0x44` with list `+0x45`/`+0x4D`. It addresses everything as image base + offset, which is why a search for instruction-relative references missed it. |
| Status | measured |
| Method | disassembly, found by scanning every function for the record arithmetic (multiplication by `0x55` and `0x9A`) |
| Build | B1 |
| Evidence | `lab/data/e5240.asm` (local), 2026-10-20. |

## The evaluator works on a table of 64 counts

| Field | |
|---|---|
| Value | The evaluator (RVA `0xE3C80`) copies a global table of 64 16-bit counts (RVA `0xEB0870`, one per code 0 to 63) into a local array, then, for each list entry below 64: tier 3 sets that code's count to 0 (an item excluded from the check, not a harder alternative); tier 2 and tiers 41+ build the group needed together; a Missile Box (36) or Bomb Box (37) in the group adds 10 per box to the group's count; codes 26 and 55 get extra handling. The generator fills the table as items are collected (function at RVA `0xE5680`): each item's definition record (100 bytes, in a table behind the pointer at RVA `0xCDD068`) can redirect the count, field `+0x54` to another code once the item is already owned and field `+0x56` to another code set to 1. Codes 50 to 63 are presumably counts reached through these redirects; not yet read. |
| Status | measured (the evaluator's handling of tiers 2, 3 and boxes, the table, the redirect fields); assumed (how codes 50 to 63 get their counts) |
| Method | disassembly of RVAs `0xE3C80` and `0xE5680` |
| Build | B1 |
| Evidence | `lab/data/e3c80.asm`, `lab/data/all.asm` (local), 2026-10-20. |

## Not yet found

- The meaning of special conditions 50 to 69: read the live item definition table (pointer at
  RVA `0xCDD068`) for redirects to codes 50 to 63, and read how the evaluator treats codes
  64 to 69 (it skips them in its first pass). An x64dbg read breakpoint during generation
  was tried and the game crashed with the debugger attached.
- Tier 4, the rest of the evaluator (how standalone entries and counts are tested), and
  whether the logic depends on a difficulty option. `tools/randocells.py` still treats tier
  3 as an alternative; the evaluator shows it excludes an item instead.
