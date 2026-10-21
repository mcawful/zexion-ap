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
| Value | Record *i* is the map cell (*i* mod 54, *i* div 54), the same grid as the save's map at file `0x877F` and the randomizer's location cells. The game's own coordinate display uses the same order, (x, y): standing next to Zexion in the core, the owner read x 32, y 45, and the save marks cell (32, 45) as explored and (45, 32) as not explored, in a save whose owner had explored every room (ITEMS 150/150), so (45, 32) is not a room at all; and in the map's per-cell object table (`save-format.md`, "The continue point and the seed's start", read from the `MapData` file) (32, 45) holds an object and (45, 32) none. An earlier reading, the save room at (23, 22) as x 22, y 23, does not fit: cell (22, 23) holds no object, and the save point saved there at tile column 333, row 449 is cell x 23, y 22 by the tile formula in the same entry. It is taken as misread. |
| Status | measured (the index; the display order, from one reading checked against the save's explored map) |
| Method | disassembly: the load-time checker (RVA `0x1AC8E0`) indexes records as `row × 0x11EE + column × 0x55` (`0x11EE` = 54 × 85); the display order from the owner's reading at the core, checked against the explored map of the save made there and against the object table in `MapData` |
| Build | B1 |
| Evidence | `lab/data/logic.asm` (local), 2026-10-20. The display order: `lab/work/slot25-after-ending.sav` (map grid), 2026-10-21. |

## Record layout

| Field | |
|---|---|
| Value | `+0x00` four lists of 8 one-byte entries, for leaving the cell to the **N, S, W, E** (in that order); `0xFF` ends a list. `+0x20` four parallel lists of tier/count bytes. `+0x40` four passage bytes for the **S, W, E, N** (in that order): `0` open, `1` needs that direction's list, `0xFF` wall. `+0x44` 1 when collecting the cell's item has its own requirements. `+0x45` those requirements (8 entries), `+0x4D` their tier/count bytes. |
| Status | measured (offsets and sizes, list-to-passage pairing); measured (which list goes with which index step, statistically and then from the generator's code) |
| Method | disassembly: every function using the array reads the pairs `+0x00/+0x20`, `+0x08/+0x28`, `+0x10/+0x30`, `+0x18/+0x38`, `+0x45/+0x4D`, and bytes `+0x40` to `+0x44`. Every non-empty list sits on a cell whose matching passage byte is 1 (202/202, 195/195, 309/309, 108/108). Direction names: each passage byte's `0xFF` agrees best (82 to 85%) with "no explored room in that direction" on a fully explored map; 61 of the 63 cells with an item-requirement list are location cells. |
| Build | B1 |
| Evidence | `lab/data/6d8a94bd4d64/RandoCells`, slot 1 map, location cells from slot 28, 2026-10-20. |

## Entry values

| Field | |
|---|---|
| Value | An entry below 50 is an item ID (`ItemNames.txt`); 50 to 63 is derived from the items, and 64 to 69 depends on the cell, the options and energy (both drawn with their own icons, sprite table at RVA `0x20C820`; see the sections on special codes below). The tier/count byte: `1` white, `2` yellow, `3` red, `4` blue (colors used by the game's built-in editor), `11` to `40` white with the number (value − 10), `41` to `90` yellow with the number (value − 40). Reading: white entries each suffice alone; yellow entries form one group that is needed together (tier 2 almost always appears two or more times in a list); counts are "at least N of that item" (Missile Container `43` = 3 containers). From the evaluator (below): tier 3 excludes the item from the check, and tier 4 is a hard requirement (the check fails if it is missing). |
| Status | measured (the editor's encoding; tiers 3 and 4 from the evaluator); measured (tier 2 as the group needed together, from the evaluator); inferred (tier 1 as enough alone, which fits the editor's colors, every list and the sweeps) |
| Method | disassembly of the editor's drawing function (RVA `0x3CB3D`); counting tier patterns in multi-entry lists |
| Build | B1 |
| Evidence | `lab/data/gen.asm` (local), 2026-10-20. |

## A sweep with the complete logic reaches every location in two fresh seeds

| Field | |
|---|---|
| Value | A new randomizer game starts in a save room chosen per seed, which the save also records, so a patched save can move it (`save-format.md`, "The continue point and the seed's start") (the only revealed map cell in a save made at once: (13, 16) for slot 28, (10, 43) for slot 29), with a starting inventory that also varies (slot 28: 5 Energy, 3 Missile Containers, Machine Gun, Power Cartridge, Navcore; slot 29: 3 Energy, 3 Missile Containers, Force Blaster). From there, with every code computed as the game does and each seed's own settings (`save-format.md`, "The randomizer section holds the seed's settings"), all 154 locations are reachable in both seeds. As a control, the same items shuffled into random locations were fully reachable in 0 of 30 shuffles of slot 28 and 2 of 30 of slot 29 (median 3 and 9 locations), so the logic tells placements apart rather than passing everything. |
| Status | measured |
| Method | `tools/randocells.py --fresh` on backup copies of slots 28 and 29 saved right after the seed was made; 30 seeded shuffles of each item table |
| Build | B1 |
| Evidence | `lab/backups/20261020-212018/Save Game 28.sav` and `Save Game 29.sav`, 2026-10-20. |

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
| Value | The evaluator (RVA `0xE3C80`) copies a global table of 64 16-bit counts (RVA `0xEB0870`, one per code 0 to 63) into a local array, then, for each list entry below 64: tier 3 sets that code's count to 0 (an item excluded from the check, not a harder alternative); tier 2 and tiers 41+ build the group needed together; a Missile Box (36) or Bomb Box (37) in the group adds 10 per box to the group's count; codes 26 and 55 get extra handling. The generator fills the table as items are collected (function at RVA `0xE5680`): each item's definition record (100 bytes, in a table behind the pointer at RVA `0xCDD068`) can redirect the count, field `+0x54` to another code once the item is already owned and field `+0x56` to another code set to 1. No item redirects to codes 50 to 63; those are derived inside the evaluator (see "Special codes 50 to 63 are derived from the items"). |
| Status | measured (the evaluator's handling of tiers 2, 3 and boxes, the table, the redirect fields) |
| Method | disassembly of RVAs `0xE3C80` and `0xE5680` |
| Build | B1 |
| Evidence | `lab/data/e3c80.asm`, `lab/data/all.asm` (local), 2026-10-20. |

## Special codes 50 to 63 are derived from the items

| Field | |
|---|---|
| Value | Before checking a list, the evaluator calls RVA `0xE3530` on its 64 counts. First, entries in the list itself adjust them: a tier-3 entry zeroes that item, code 55 zeroes the Gravity Harness ("without the Gravity Harness"), and code 26 (Hyper Jump) in the list sets code 63, which stops Hyper Jump standing in for other items. Then: the Gravity Harness counts as Jump Booster and G-Diffuser too; **50** = Wall Jump Boots, Gravity Harness or Jump Booster; **51** = Wall Jump Boots with G-Diffuser or Gravity Harness; **52** = Wall Jump Boots with Jump Booster; **53** = Wall Jump Boots with G-Diffuser or Gravity Harness, or Jump Booster; **54** = Gravity Harness with Jump Booster (each of 50 to 54 is also met by Hyper Jump unless code 63 is set); a weapon upgrade counts only with its weapon (Pulse Wave with Machine Gun, Pentashot with Force Blaster, Tunneler with Snake, Focal Crystal with Lance, Plasma Burst with Arc Plasma), and S-Blade and Rocket Slide only with Slide Mod; **57** = Tunneler, Lance, S-Blade or Plasma Burst; **58** = Plasma Burst, Force Blaster, Machine Gun or Snake; **59** = Machine Gun, Snake or Plasma Burst; **60** = Arc Plasma, Lance, Machine Gun, Snake or Force Blaster; **61** = Plasma Burst or a Missile Container; **62** = a combat tier from 0 to 4, computed from Energy Tanks (doubled by Magnus Armor), damage upgrades (Power Chip, Power Cartridge, Power Core), missiles (10 per Missile Box), Mega Bombs (10 per Bomb Box), X-Missile and the weapons, with Plasma Burst alone giving 4; **56** is always met. Hyper Jump (unless code 63) then also counts as Marine Jets, Jump Booster, G-Diffuser, Wall Jump Boots and Gravity Harness, and the X-Missile counts only with at least 5 Missile Containers. Item pickups also follow redirects in the item definitions (the `Objects` data from byte 1,028, 100 bytes per item, fields `+0x54` and `+0x56`): a weapon picked up again counts as its upgrade, and an upgrade picked up first counts as its weapon. `tools/randocells.py` implements all of this, including code 62's scaling of Energy to the seed's Energy total (see "Codes 64 to 69" below). |
| Status | measured (from the disassembly and the live item table) |
| Method | disassembly of RVAs `0xE3530`, `0xE3C80`, `0xE5680`, `0xE70A0` and `0x16DB41`; the item table read from the running game with `memscan.py` |
| Build | B1 |
| Evidence | `lab/data/all.asm`, `lab/data/e3c80.asm` (local), 2026-10-20. |

## Codes 64 to 69 depend on the cell, the options and energy

| Field | |
|---|---|
| Value | The evaluator checks a list's entries in order in a first pass, before the rest: a code from 64 to 69 that applies decides the whole check on its own, and a missing tier-4 entry fails it. **64 to 67** pass if the neighboring cell to the west, east, north or south (64, 65, 66, 67) has already been reached in this fill, and fail otherwise: a way that opens only from its other side. At the map edge they do not apply (east steps by index, so at x = 53 it looks at the first cell of the next row). **68** passes if the option chosen by its tier byte is on (RVA `0xCDDF4B` + tier; the tiers used, 30, 31, 32 and 37, are X Crystals, X Blocks, Abridged Finale and Secret Shuffle, inferred in "Where each setting is stored"); otherwise it does not apply. **69** passes if code 69's mode (`2 × byte 0xCDDF76 + byte 0xCDDF75`, the menu's Safety setting: "Where each setting is stored") is 0 or 3; otherwise its tier byte picks a test on Energy (doubled by Magnus Armor, scaled up when the seed's Energy total is under 20): 11: above 9; 12: above 9 and combat tier 1+; 13: above 9 and combat tier 2+; 14: above 19 in mode 1, above 29 in mode 2; 15: above 7 and combat tier 1+; 16: above 4 and a strong enough weapon (Snake with Tunneler, Power Core or 20 missiles; Machine Gun; Force Blaster with Power Chip and Power Cartridge; Arc Plasma; or 40 missiles). Code 62's combat tier scales Energy to 20 points of the seed's Energy total, up or down. Mode 3 also sets combat tier 4. A per-cell counter at RVA `0xEAFD00` lets the generator re-run its fill when a neighbor is not reached yet; `tools/randocells.py` instead repeats its fill until nothing changes. In the data, 64 to 67 appear 7 times, 68 42 times (options 30, 31, 32, 37) and 69 12 times. |
| Status | measured (from the disassembly; checked by the sweep above) |
| Method | disassembly of RVA `0xE3C80` (offsets `0xE4273` to `0xE49DA`) and `0xE3530`; the 20.0 and 1.0 constants at RVAs `0x217768` and `0x217440` |
| Build | B1 |
| Evidence | `lab/data/e3c80.asm` (local), 2026-10-20. |

## The randomizer menu's settings

| Field | |
|---|---|
| Value | The randomizer's menu offers, on its first screen: a 16-character seed ("The same seed and options will generate the same item placements"); a mode, Easy, Standard, OHKO or Custom; Edit Options; Copy and Paste of "the seed and letters representing the settings"; and Starting Items and Item Pool, each a list of items with Energy, missile and bomb counts. Standard shows starting 3 Energy, 3 missiles, 0 bombs and a pool of 17, 122 and 12, the figures in slot 29's seed (`save-format.md`, "The randomizer section holds the seed's settings"), so slot 29 was made in Standard; slot 28's figures (5 + 35, 3 + 199, 0 + 18) are Easy's ("The randomizer's modes"). Edit Options holds, with the game's own explanations: Obscure Majors, Obscure Minors, Obscure Class (what the map reveals about items); Random Gun ("start the game with a random gun"); Bulk Missiles and Bulk Bombs ("all … in boxes of ten"); Box Compression ("missiles and bombs will be put into boxes more"); Hints (None, Vanilla, Vanilla+, Small, Normal, Large, Full Map, Revealed: what navigation rooms reveal); Gates (Off, Vanilla, Unlocked, Open: whether the hyperspace gates need the four keys, open one by one as they are visited, or are open); Starting Location (Random Start and others); X Crystals and X Blocks ("removes crystal blocks … destroyed by plasma", "cracked blocks … destroyed by missiles"); Abridged Finale ("victory is achieved when you first grab Zexion in the core"); Double Teleport (a second, random teleport target); Secret Shuffle ("allows an item to be placed in the 7th layer"); Guns First ("guns will be found before their upgrade that specifically requires them"); Show Obtainable; Colorizer; Safety (Light, Early, Normal, Off: how far Energy Tanks and equipment are taken into account for tough fights); Start Keys (0 to 4); and No Guarantee ("not every single item is guaranteed to be accessible"). Where each is stored, and which of them the logic reads, is in "Where each setting is stored". |
| Status | measured (the names, the descriptions and Standard's figures, from the game's own text and menu) |
| Method | the menu as shown on screen by the owner; `SmallText.txt` and `LargeText.txt` in the game's English translation folder, read-only |
| Build | B1 |
| Evidence | Screenshots from the owner, 2026-10-21. |

## The randomizer's modes

| Field | |
|---|---|
| Value | The menu's mode sets the energy, missile and bomb amounts, the item choices and some settings together. In the menu they are six 16-bit numbers at `+0xDF73C8` (starting Energy, Energy in the pool, starting missiles, missiles in the pool, starting bombs, bombs in the pool), the same order as the save's at `0x12ED4`, and two arrays of 27 one-byte entries at `+0xDF7390` and `+0xDF73AB`, the save's two arrays at `0x12E9D` and `0x12EB8` (one entry per upgrade; per item the menu offers Starting Item, Random, Random Gun or Excluded, and how those map to the two arrays is not decoded). Three fresh saves, one per mode: **Easy** (slot 28): 5 + 35 Energy, 3 + 199 missiles, 0 + 18 bombs, Random Gun off, Hints Large, Safety Normal, Gates Unlocked; starting with the Machine Gun, the Power Cartridge and the Navcore. **Standard** (slot 29, the default): 3 + 17, 3 + 122, 0 + 12, Random Gun on, Hints Normal, Safety Normal, Gates Unlocked; starting with a random gun (the Force Blaster in that seed). **OHKO** (slot 5): 0 + 0, 3 + 152, 0 + 12, Random Gun off, Hints Large, Safety Light, Gates Vanilla, Double Teleport on; starting with the Machine Gun and no Energy Tank. Each seed's item table follows its mode's amounts (OHKO's has no Energy Tank, 62 Missile Containers and 5 Energy Shields). In the menu, Custom showed Easy's amounts with 0 bombs, presumably the last custom choice. Where the mode itself is stored, if anywhere, is not known. The fresh Easy and Standard seeds reach all 154 locations from their start ("A sweep with the complete logic…"); the OHKO seed, flooded from both its start and its second teleport target (`save-format.md`, "The continue point and the seed's start"), reaches 150, and the other four all hold Scrap metal: one needs two Energy Tanks to collect, which OHKO never has, and three lie where its starting rooms never reach. So the generator fills locations it cannot reach with Scrap (one seed seen). |
| Status | measured (the six numbers per mode, in the menu and in the three saves; the settings and pools of the three saves; the OHKO seed's 150 reachable locations); not decoded (the two arrays' encoding); assumed (that unreachable locations always get Scrap: one seed) |
| Method | module snapshots about once a second while the owner switched modes and changed the amounts (`lab/mem/modes-*`, sorted by time); `save_lab.py copy` of the three fresh saves, decoded with "Where each setting is stored" |
| Build | B1 |
| Evidence | `lab/backups/20261020-212018/Save Game 28.sav` (Easy), `29.sav` (Standard), `lab/work/slot5-ohko-fresh.sav` (OHKO, seed `0000 0000 0000 0001`), 2026-10-21. |

## Where each setting is stored

| Field | |
|---|---|
| Value | The settings are one array of bytes, setting *t* at file offset `0x1308F + t` in a randomizer save, at `+0xCDDF4B + t` in the loaded randomizer section, and at `+0xDF740A + t` while the menu is open. Each on/off setting is one byte (0 or 1); a setting with more values is a small binary number, one bit per byte, lowest bit first. *t*: 10 Guns First; 11 Show Obtainable; 12 Colorizer; 15 Random Gun; 16 Bulk Missiles; 17 Bulk Bombs; 18-20 Hints (0 None, 1 Vanilla, 2 Vanilla+, 3 Small, 4 Normal, 5 Large, 6 Full Map, 7 Revealed); 22-27 Starting Location; 28 Box Compression; 29 Obscure Majors; 30 X Crystals; 31 X Blocks; 32 Abridged Finale; 33 Double Teleport; 34-36 Start Keys (0 to 4); 37 Secret Shuffle; 38 Obscure Minors; 41 Obscure Class; 42-43 Safety (0 Light, 1 Early, 2 Normal, 3 Off); 72-73 Gates (0 Off, 1 Vanilla, 2 Unlocked, 3 Open). So the options code 68 reads (tiers 30, 31, 32, 37; "Codes 64 to 69") are X Crystals, X Blocks, Abridged Finale and Secret Shuffle, and code 69's mode (`2 × t43 + t42`) is Safety. Gates lies outside the 64 bytes the logic reads, as the keys lie outside `RandoCells`. Slot 29's fresh save decodes to the defaults the owner's screenshot of a fresh menu shows (Standard mode, Gates Unlocked, Safety Normal); slots 2 and 28 to the same but Random Gun off and Hints Large, which is how the menu stood during this session. Bytes 74 to 105 hold 32 printable ASCII letters in all three randomizer saves seen: identical in slots 2 and 28 (`FLCgBLLBBBBBLcHcNcNcNRNRWWFBKJBB`), whose seeds differ but whose settings match, and different in slot 29. So they are not the seed; possibly the menu's Copy/Paste settings code, not decoded. |
| Status | measured (which menu byte changed when, and through which values); assigned by order (which setting each byte is: the owner stepped the settings column by column in screen order, which the timing shows but nobody recorded per step); measured (the save and loaded-section offsets for the bytes the saves have on: 10, 11, 15, 18-20, 28, 29, 38, 42-43, 72-73, and Safety against code 69's mode bits); inferred (the same offsets for the settings that are off in every save seen, so not yet cross-checked: Colorizer, Bulk Missiles, Bulk Bombs, Obscure Class, X Crystals, X Blocks, Abridged Finale, Double Teleport, Secret Shuffle, Start Keys, Starting Location; that code 68's options are X Crystals, X Blocks, Abridged Finale and Secret Shuffle, from those offsets); menu only (the value orders other than Hints 4/5, Safety 2 and Gates 2; Starting Location's values, which were stepped but not tied to rooms) |
| Method | `memscan.py record` over the loaded randomizer section while the owner stepped Starting Location through its values; then module snapshots about once a second while the owner stepped every other setting through its values and back, column by column, and each byte's changes matched to the order; the two fresh saves decoded with the result |
| Build | B1 |
| Evidence | `lab/mem/set-rec.log`, snapshots `lab/mem/set2-001` onward, `lab/backups/20261020-212018/Save Game 28.sav` and `29.sav`, `lab/backups/20261021-101657/Save Game 2.sav`; the owner's screenshots of a fresh randomizer menu (the defaults), 2026-10-21. A seed made with, for example, X Crystals on would cross-check an inferred offset (`0x1308F + 30`). |

## Not yet found

- Whether the mode itself is stored, how the two item-choice arrays encode Starting Item,
  Random, Random Gun and Excluded, and what settings bytes 74 to 105 hold.
- Whether the four keys gate the core in a randomizer game with Gates Unlocked (the default).
- Found but not modeled in `tools/randocells.py`: Missile and Bomb Boxes adding 10 to a group's
  count (see "The evaluator works on a table of 64 counts").
- An x64dbg read breakpoint during generation was tried and the game crashed with the
  debugger attached; the code was found statically instead.
