# Save format

The save folder is `%APPDATA%\Gallant Leaf\Zexion`. Never write there except through
`tools/save_lab.py` (see `/save-lab`); the client never writes it. Map cells are this repo's
(x, y), and "row" and "column" follow it (`randocells.md`, "Grid order"). Format and statuses
per [`README.md`](README.md).

## Lookup: file offsets

Offsets in `Save Game N.sav`, first block; the second block's copy is `0x9408` later. A block
starts at file `0x0002`, so a block offset is the file offset − 2 (the slot array in memory:
`memory.md`, "Save data lives in Zexion.exe's static data"). The randomizer section's own
offsets (section `+…`) are what the code and `memory.md` use.

| File offset | Field | Heading |
|---|---|---|
| `0x0000` | `E8 03` | Saves are raw, uncompressed binary |
| `0x0002`, `0x940A` | the two blocks, `0x9408` bytes each | A save is a header and two copies of one block |
| `0x0002 + flag` | the flags | `memory.md`, "The flags" |
| `0x002A` to `0x0031` | the keys' map markers (flags 40 to 47) | What changes between randomizer seeds |
| `0x0111 + id` | flags 271 + *id*, not the inventory | Dead ends (B1, 2026-10-20) |
| `0x877F` to `0x92E2` | the map grid | The map grid marks collected locations |
| `0x92F3 + ID` | the inventory, 64 bytes | Inventory: one byte per item ID at 0x92F3 |
| `0x9321` | Scrap metal's byte (item 46) | Counters |
| `0x9333`, `0x934D` | the major-item list, its length | Major items are a list in pickup order at 0x9333 |
| `0x9350`, `0x9352` | current energy, current missiles | Current energy and missiles |
| `0x9356` | play time (assumed) | The continue point and the seed's start |
| `0x935A` | the ITEMS counter | Counters |
| `0x935C`, `0x935E` | the continue point (row, column) | The continue point and the seed's start |
| `0x9360` | the empty marker (`0xFF`) | Clearing a slot in the game sets two bytes and keeps the rest |
| `0x9362` | the run's session count, + 1 when the save is loaded | Counters |
| `0x9402` | the randomizer marker | Clearing a slot in the game sets two bytes and keeps the rest |
| `0x12812` | the randomizer section's `E8 03` | Randomizer data is appended after the two blocks |
| `0x12814` (`+0x000`) | the location list | Randomizer data is appended after the two blocks |
| `0x12A80` (`+0x26C`) | the item table | The item table is indexed in location-list order |
| `0x12B1C` (`+0x308`) | the seed | Randomizer data is appended after the two blocks |
| `0x12B24` (`+0x310`) | the start record | The continue point and the seed's start |
| `0x12B2A`, `0x12B2C` (`+0x316`, `+0x318`) | a map-rectangle count and table | `memory.md`, "Starting a new game" |
| `0x12E6C` (`+0x658`) | the 16-character seed text (then 0), whose hash is at `+0x308` (measured in six saves, 2026-10-27: each text's hash by the formula in `randocells.md`, "The pool", equals the stored value; copied from the menu at `0xF2E71`, by disassembly) | `randocells.md`, "The pool" |
| `0x12E9D`, `0x12EB8` (`+0x689`, `+0x6A4`) | the start-with and pool arrays | The randomizer section holds the seed's settings |
| `0x12ED4` (`+0x6C0`) | the amounts | The randomizer section holds the seed's settings |
| `0x12EE0` (`+0x6CC`) | the second teleport target | The continue point and the seed's start |
| `0x12EE6` (`+0x6D2`) | the second target's flag | `memory.md`, "The game-mode panel: NORMAL / RANDOMIZER" |
| `0x1308F + t` (`+0x87B + t`) | setting *t* | The randomizer section holds the seed's settings |

## Steam Cloud syncs the save folder

| Field | |
|---|---|
| Value | The folder holds `steam_autocloud.vdf`, the marker Steam's Auto-Cloud writes into folders it syncs. That Steam actually uploads and downloads these saves is assumed from that marker and from the owner's account, and has not been observed. `/save-lab` asks for Steam Cloud to be paused for Zexion on that assumption. |
| Status | measured (marker file present); assumed (syncing) |
| Method | file inspection |
| Build | not build-specific |
| Evidence | `ls -la` of the save folder, 2026-10-20. |

## Files in the save folder

| Field | |
|---|---|
| Value | `Save Game 1.sav` to `Save Game 30.sav` (all 30 exist, used or not), `GameUnlocks.sav`, `PreferencesSynced.sav`, `PreferencesLocal`, `steam_autocloud.vdf`, and `Mods/{Local,Workshop}/{Sprites,Translations}/`. `GameUnlocks.sav` holds the game-wide database unlocks and Feats; its layout and when the game writes it: `memory.md`, "Feats and achievements". |
| Status | measured |
| Method | file inspection (directory listing) |
| Build | B1 installed; files written by older builds |
| Evidence | `ls -la` of the save folder, 2026-10-20. |

## Saves are raw, uncompressed binary

| Field | |
|---|---|
| Value | No compression or container: long zero runs and readable small integers appear directly in the file. Every slot inspected starts with `E8 03` (1000 as a little-endian u16). That those two bytes are a format version is an assumption. |
| Status | measured (raw layout, header bytes); assumed (meaning of the header) |
| Method | file inspection (`xxd`) of slots 1, 2 and 3 |
| Build | B1 installed; files written by older builds |
| Evidence | `xxd -l 64` of slots 1 to 3, 2026-10-20. |

## Save sizes

| Field | |
|---|---|
| Value | As of 2026-10-20, before the experiments: 75,794 bytes for slot 1 and for every unused slot; 78,108 bytes (2,314 more) for slot 2. Slot 2 is a randomizer save and the extra bytes hold its seed ("Randomizer data is appended after the two blocks"). |
| Status | measured |
| Method | file inspection |
| Build | B1 installed; files written by older builds |
| Evidence | `ls -la` and `save_lab.py slots`, 2026-10-20. |

## Unused slots hold a fixed template

| Field | |
|---|---|
| Value | Before the experiments of 2026-10-20, slots 3 to 30, all unused, had identical content (SHA-256 prefix `8c9868d64c45`), which is not all zeros: `E8 03`, zeros to `0x7BAD`, `0xFF` from `0x7BAE` to `0x877E` inclusive (3,025 bytes), zeros, one `0xFF` at `0x9360`, then 8 records of 14 bytes from `0x9364` to `0x93D3`, each `FF FF FF FF 00 00 00 00 30 00 02 00 00 00`, then zeros to the end. What the regions mean is not known. This description is complete: built from it alone, the 75,794 bytes have the template's SHA-256. `save_lab.py` builds it so (`unused_template`) and calls a slot with this content, or with the content at least 3 slots share, "empty"; a slot holding a save deleted in the game it calls "empty (deleted, data kept)" ("Clearing a slot in the game sets two bytes and keeps the rest"). |
| Status | measured |
| Method | file inspection: `xxd -a` and a byte-value histogram of slot 3; `save_lab.py slots` for the matching hashes of slots 3 to 30; the template built from this description by `save_lab.unused_template`, its SHA-256 compared with a backup's manifest |
| Build | B1 installed; files written 2025-12-19 by an older build |
| Evidence | 2026-10-20. Histogram of slot 3: 72,718 × `00`, 3,058 × `FF`, 8 × `30`, 8 × `02`, one each of `E8` and `03`. 2026-10-27: the built template's SHA-256 equals the one `lab/backups/20261020-211239/manifest.json` records for 28 slots, 3 to 30 (`tests/test_save_lab.py` checks the prefix). |

## Clearing a slot in the game sets two bytes and keeps the rest

| Field | |
|---|---|
| Value | When the owner cleared slots 27 to 30 in the game, each file kept its size (78,108 bytes) and its contents, including the randomizer section, except for two bytes: `0x9360` went from 0 to `0xFF`, the value the unused-slot template has there, and `0x9402` went from 1 to 0. `0x9402` is the randomizer marker: 1 in every randomizer save seen, 0 in the template and in the owner's normal-mode slot 1 (`memory.md`, "The loaded save's randomizer section is at +0xCDD6D0"). So the game marks a slot empty with `0x9360 = 0xFF`: a file-select entry takes `0xFF` there as an empty slot (`memory.md`, "The file select", by disassembly), and loading a save copies this byte into live memory, where `0xFF` means no save is loaded (`memory.md`, "Telling play from menus, the title screen and death"); whether the file-select screen reads anything else to show a slot as empty is not known. A save deleted in the game therefore keeps all its data, and the game marks it empty. Only randomizer saves (78,108 bytes) have been seen deleted; that a normal-mode save (75,794 bytes) is deleted the same way is assumed. `save_lab.py` knows this: `slots` lists such a slot as "empty (deleted, data kept)", apart from the template's "empty", and `install` may fill it under the same guards, saying that the replaced data is in the backup it required. It counts a slot as deleted only in a file of a save's size ("Save sizes") that starts with `E8 03`, and only with both bytes as every clear here left them, `0x9360 = 0xFF` and `0x9402 = 0`; a slot with the empty marker but a randomizer marker of 1, a state never seen, stays "IN USE", erring on the safe side while the file select's own test is assumed. In the same rewrite, every slot in use also changed its 16-bit value at `0x9362` (slot 2: `0xFFFF` → 2; slots 27 and 28 rose by 1 and 3; slots 29 and 30 kept theirs); what it holds is not known. Slot 1 did not change. |
| Status | measured (the bytes that changed, for randomizer saves; `0x9360` matching the template; the load copying `0x9360`, by disassembly); assumed (that file select shows a slot as empty from `0x9360` alone; that deleting a normal-mode save sets the same two bytes) |
| Method | `save_lab.py diff` of each slot against the backup taken before the clear |
| Build | B1 |
| Evidence | `lab/backups/20261021-091726/` (before) against copies `lab/work/fresh-slot{2,27,28,29,30}.sav` and `lab/backups/20261021-101657/` (after), 2026-10-21. Slot 28's other differences come from the grant session that day. The load copying the byte: disassembly at RVA `0x9D7EC` (`memory.md`, "Telling play from menus, the title screen and death"). |

## A save is a header and two copies of one block

| Field | |
|---|---|
| Value | A 75,794-byte save is `E8 03` followed by two 37,896-byte blocks: `0x0002` to `0x9409` and `0x940A` to `0x12811`. In slots 1 and 2 the same structures appear in both blocks, `0x9408` bytes apart (FF runs at `0x7BBA` and `0x10FC2`, the 14-byte records at `0x9388` and `0x12790`); these used slots have them at other places than the unused-slot template (FF from `0x7BAE`, records from `0x9364`), for reasons not known. In the template the second block is all zeros. Why there are two copies (a backup, or two states) is not known. |
| Status | measured (layout); open (its purpose) |
| Method | file inspection of backup copies: positions of FF runs of 16+ bytes and of the bytes `30 00 02 00`, in slots 1, 2 and 3 |
| Build | B1 installed; files written by older builds |
| Evidence | Script over `lab/backups/20261020-211239/`, 2026-10-20. |

## Randomizer data is appended after the two blocks

| Field | |
|---|---|
| Value | Slot 2's extra 2,314 bytes are appended at `0x12812`, after both blocks, which sit at the same offsets as in slot 1. The appended part starts with its own `E8 03`, then the randomizer section, `0x908` bytes (section offsets after each file offset):<br>• `0x12814` (`+0x000`): 154 little-endian u16 pairs, all distinct, sorted by the second value then the first, both in 0 to 53; then 4 zero bytes.<br>• `0x12A80` (`+0x26C`): 154 bytes, each 0 to 50; then 2 zero bytes.<br>• `0x12B1C` (`+0x308`): 8 bytes, `da 76 2e 8f ab 2b 1d e3`.<br>• `0x12B24` (`+0x310`) onward: the start record and more, including the seed's randomizer settings, up to `0x130F8`, then zeros to the end (`0x1311C`).<br>Meanings, established in later sections: the pairs are the (x, y) cells of the item locations ("The map grid marks collected locations"), and the bytes are the item at each location, in list order ("The item table is indexed in location-list order"); item IDs are those of `ItemNames.txt`, which runs 0 to 50 (50 is the Energy Shield). The 8 bytes are the seed: a game started with the menu's seed `0000 0000 0000 0001` stored `00 00 00 00 00 00 00 10`, each byte holding two of the displayed digits, the later one in the high half. The rest: "The continue point and the seed's start" and "The randomizer section holds the seed's settings". When a randomizer save loads, the game copies the section into memory (`memory.md`, "The loaded save's randomizer section is at +0xCDD6D0"). |
| Status | measured (layout, counts, value ranges; that the 8 bytes are the seed, for the last byte only: one seed entered in the menu, with one nonzero digit, puts the last displayed pair in the last byte, its later digit in the high half); working in game (the item table, below); assumed (the digits' base, and the same order for the other bytes) |
| Method | file inspection and a parsing script over the backup copy of slot 2; the seed: a game started with a seed typed in the menu, saved at once, and its bytes read |
| Build | B1 installed; slot 2 written 2026-06-13 by an older build |
| Evidence | `lab/backups/20261020-211239/Save Game 2.sav`, 2026-10-20. Byte-value counts over the 154 bytes: 35 × 0, 29 × 1, 18 × 2, 17 × 36, 17 × 46, 6 × 49. The seed: `lab/work/slot5-ohko-fresh.sav` (a copy of slot 5), build B1, 2026-10-21. |

## What changes between randomizer seeds

| Field | |
|---|---|
| Value | Across three randomizer saves (slots 2, 28, 29; different seeds, 28 and 29 saved before any pickup):<br>• The 154 u16 pairs at `0x12814` are identical in all three. They are the location cells, so the location list is fixed and only the placement varies. With Secret Shuffle the list holds 155 and the table at `0x12A80` 155 bytes (`randocells.md`, "The pool").<br>• The 154 bytes at `0x12A80` differ in 136 positions between slots 28 and 29, and their value counts differ too (slot 29 has 62 × 1 against slot 28's 29 × 1). Slot 29 was made with different options ("The randomizer section holds the seed's settings").<br>• The 8 bytes at `0x12B1C` differ in every save.<br>• The data after them (`0x12B24` to `0x130F8`) differs too: the start record (`06 00 bf 00 49 01` in 28, `04 00 94 00 65 03` in 29) and the settings (below).<br>• 27 bytes differ between 28 and 29 outside the randomizer section: 25 in the first block and 2 in the second. Eight of them, `0x2A` to `0x31`, hold values 1 to 51 that change with the seed (28: `33 15 28 11 16 01 05 27`): they are the four keys' map markers, flags 40 to 47, each the (y, x) cell where the seed puts that key (`memory.md`, "Keys, the gates and the key markers"; checked against the item tables of slots 28 and 29 and of the fresh OHKO save; slot 2 was not checked).<br>• Re-saving slot 1 while making these changed its timestamp but not its contents. |
| Status | measured |
| Method | parsing script over backup copies; for the key markers, each save's item table compared with its bytes `0x2A` to `0x31` |
| Build | B1; slots 28 and 29 written 2026-10-20 by B1 |
| Evidence | `lab/backups/20261020-212018/`, 2026-10-20 (the 27 bytes recounted 2026-10-21; the first count put all 27 in the first block); for the key markers also `lab/work/slot5-ohko-fresh.sav`, 2026-10-22. |

## The game loads and keeps a patched placement table

| Field | |
|---|---|
| Value | A copy of slot 28 with all 154 bytes at `0x12A80` set to 46 (Scrap metal), and nothing else changed, loaded without complaint. The first pickup gave Scrap metal. After the game saved over it, the whole appended section (`0x12812` to the end) was byte-for-byte what was installed, so the game neither rejected the change, nor checks a checksum over that section, nor regenerates the table from the seed when it saves. Control: the same first pickup in the unpatched slot 28 gave a normal item, not Scrap. So the game takes each location's item from this table ("The item table is indexed in location-list order"). |
| Status | working in game (one location: patched item granted, table kept on save, control differs; 2026-10-20) |
| Method | `save_lab.py patch` and `install` into slot 30; the same first pickup by the owner in slot 30 (patched) and slot 28 (unpatched); a parsing script over the backups taken after each save |
| Build | B1 |
| Evidence | 2026-10-20. Spec `lab/work/all-scrap.json`, installed 21:24 (`lab/installs.log`, sha256 `50cdac33…`); slot 30 after play: `lab/backups/20261020-213055/`; slot 28 after the control pickup: `lab/backups/20261020-213828/`. |

## The second block is a copy of the first

| Field | |
|---|---|
| Value | In a randomizer save made and saved at once (slot 28 before any pickup), the second block has a single nonzero byte. After the next save it matches the first block in all but 14 of 37,896 bytes. So the second block looks like a copy written on later saves (a backup, perhaps); which one the game reads is not known. |
| Status | measured |
| Method | byte comparison of the two blocks in backup copies |
| Build | B1 |
| Evidence | Slot 28 in `lab/backups/20261020-212018/` (before) and `lab/backups/20261020-213828/` (after), 2026-10-20. |

## Dead ends (B1, 2026-10-20)

| Field | |
|---|---|
| Value | **"Has item" candidates.** After the same single pickup from the same start, the save that received a normal item (slot 28) and the one that received Scrap (slot 30) differ in the first block at 42 bytes, mostly map exploration and stats (slot 28 explored a little further). Two went 0 → 1 only where the normal item was received: `0x0129` and `0x39A7`. Neither is the inventory, which is at `0x92F3 + item ID` ("Inventory: one byte per item ID at 0x92F3"); `0x0129` belongs to the `0x0111` array below. |
| | **A byte array also flags collected locations.** Between `0x3000` and `0x7000`, the only bytes set to 1 are `0x4589` after location #52 was collected, and `0x4589` and `0x43C7` after #52 and #48; neither is set in the fork save without the pickup. How an offset here maps to a location is not known (the two are 450 bytes apart, which no simple grid width explains); the map grid ("The map grid marks collected locations") is the usable source. |
| | **An item-ID-indexed byte array at `0x0111`, not the inventory.** File `0x0111 + id` (first block): slot 28's fresh randomizer save has entries 7, 15, 16 and 22 set (Arc Plasma, Tunneler, Slide Mod, Jump Booster), yet that save does not own Slide Mod (it had to be granted, `memory.md`). In the control run entry 24 was set after the Gravity Harness pickup; in a later run of the same pickup and a save at the same save point it was **not**, though the item was collected. So this array does not record ownership. It lies in the flags (`0x0002 + flag`, `memory.md`, "The flags"): entry *id* is flag 271 + *id*, so entry 7 is flag 278, which every randomizer game sets, and entries 33 to 36 are flags 304 to 307, the gates' visited flags. Slot 1 (far into the normal game) has many entries set, and entry 37 is 4. |
| Status | measured (the 42 bytes; the two collected-location bytes, two locations; the `0x0111` array's layout over the two runs; its place in the flags, by arithmetic, 2026-10-26); superseded ("has item": the inventory was found since); open (how the collected-location bytes map to locations; what sets the other entries of the `0x0111` array) |
| Method | byte comparison of the two saves after their pickup, against the save before it; the fork test and the two-pickup save, as in "The map grid marks collected locations"; byte comparison of slot 28 saves |
| Build | B1 |
| Evidence | "Has item": `lab/backups/20261020-213828/Save Game 28.sav` against `lab/backups/20261020-213055/Save Game 30.sav` and `lab/backups/20261020-212018/Save Game 28.sav`. The collected-location bytes: `lab/backups/20261020-221347/` (slots 27 and 30 after the fork run) and `lab/backups/20261020-215709/` (two pickups). The `0x0111` array: `lab/backups/20261020-212018/`, `20261020-213828/` (control), `20261020-223111/` and `20261020-224429/` (second run). All 2026-10-20. |

## The map grid marks collected locations

| Field | |
|---|---|
| Value | A 54 × 54 grid, one byte per map cell, row-major (`index = y × 54 + x`), at file offset `0x877F` to `0x92E2` in the first block (so `0x11B87` onward in the second). Each value is the cell's map marker, decoded in `memory.md`, "Map markers": `0x1B` is collected; values seen in saves include `0x00`, `0x0D`, `0x0E`, `0x13`, `0x15`, `0x18`, `0x19`, `0x1A`, `0x22` (the start room in fresh saves, and rooms saved in) and others up to `0x2C` in a full save. In the owner's randomizer save (slot 2, 63 pickups counted), 65 location cells read `0x1B`, and the items the item table holds there account for its inventory exactly, less that seed's starting items, apart from the filler noted in "Inventory: one byte per item ID at 0x92F3" (the two over the counter fit the keys, which it skips); the other location cells read 0, `0x13` (38, all minor items), `0x15` (4, all majors), `0x19` (3), `0x0D` or `0x0E`, and none of those items is owned, so they are uncollected. The cells marked `0x19`/`0x1B` are cells from the randomizer's location list at `0x12814`, so location *i* (its *i*-th (x, y) pair) is collected when the grid holds `0x1B` at that cell. Location numbers seen: the first pickup from the start is #52 at (25, 25), the second #48 at (29, 24). |
| Status | measured (two locations, in the save; the values' meaning below `0x24`, `memory.md`, "Map markers"); assumed (that every location behaves the same); open (`0x22` and the values from `0x24` up) |
| Method | fork test: the fresh all-Scrap save installed into slots 27 and 30, the same route played in each, the pickup taken only in slot 30; then `save_lab.py diff`. The grid layout was chosen as the one that puts both collected cells on location cells, starts at the end of the template's FF block, and has the width of the location coordinates (0 to 53); under it the explored cells form one connected route and the owner's slot 1 fills the whole grid. |
| Build | B1 |
| Evidence | `lab/backups/20261020-221347/` (slots 27 and 30 after the fork run): they differ only at `0x4589`, `0x8CDE` (`0x19` → `0x1B`), the counters `0x9321` and `0x935A`, play time, and the second block. Earlier two-pickup save: `lab/backups/20261020-215709/`. 2026-10-20. |

## The continue point and the seed's start

| Field | |
|---|---|
| Value | **Tiles.** Positions are counted in 16-pixel tiles over the whole map, each map cell 14 tiles along x and 20 along y: tile column = x × 14 + pixel x / 16, tile row = y × 20 + pixel y / 16, where (x, y) is the cell and the pixel position is the save point's within its room, both along this repo's axes. On screen the column runs down and the row across: a cell is 224 pixels high (14 tiles) and 320 wide (20) (`randocells.md`, "Grid order"). **Continue point:** the 16-bit values at `0x935C` (row) and `0x935E` (column) are the save point the save continues from. Saving in the save room at (23, 22) stored 449 / 333; teleporting back to the start and saving there stored the start's 329 / 191 again. |
| | **The seed's start:** the randomizer section opens, at `0x12B24` (section `+0x310`), with three 16-bit values: the start's 0-based index in the start candidates (below; the menu's Starting Location value − 1), then its tile column and row (slot 28: 6, 191, 329 for cell (13, 16); slot 29: 4, 148, 869 for (10, 43); the owner's slot 2: 8, 202, 27). They stay the same however far the game goes. Fresh Easy and Standard saves reveal only the start cell on the map grid, with value `0x22`, the value saved-in rooms show (that `0x22` means a save room is not established); a fresh OHKO save reveals no cell at all, so the start record is the reliable source. That OHKO seed's record is (22, 402, 989): candidate 22, cell (28, 49). `tools/randocells.py` reads the start from the record. |
| | **The second teleport target:** with Double Teleport on, the seed records a second start in the same form at `0x12EE0` (section `+0x6CC`): the OHKO seed holds (3, 151, 464), cell (10, 23). Easy and Standard seeds, with Double Teleport off, hold (0, 20, 509), candidate 0's cell (1, 25): what START GAME writes with one start (`memory.md`, "The game-mode panel: NORMAL / RANDOMIZER"); with Double Teleport off the pause menu does not offer the teleport, and the logic does not flood from it (`randocells.md`, "Starting Location and the second teleport target"). **A second target can be written too:** a copy of the fresh OHKO save with only this record changed to (6, 191, 329), installed in slot 6 and loaded, held that record in memory, and the pause menu's second teleport took the owner to the save room at (13, 16). |
| | **The candidates** are the game's start table (`memory.md`, "The game-mode panel: NORMAL / RANDOMIZER", its start table): 34 rooms, in map order, `lab/data/6d8a94bd4d64/start-candidates.json`; candidates 6, 4 and 8 are exactly the three starts seen in fresh saves (cells (13, 16), (10, 43), (14, 1) with their columns and rows). The table is built from a per-cell table at `+0x361C10`, 154 bytes per cell, which is also in the `MapData` file from byte 4,085,505 (`0x3E5701`; the records of (13, 16), (10, 43) and (23, 22) found there at that spacing): up to 8 object numbers at `+0x00` (`0xFFFF` ends them), then the first object's pixel position at `+0x10` and `+0x12` (the generator reads `+0x361C20`); the location table at `+0x361C4A` in `randocells.md` is the same table's field `+0x3A`, and the area its field `+0x48` (`locations.md`, "Location names"). The builder also flags cells holding object `0x19`; none of the three starts has one. How the generator picks among the 34, and when it takes a flagged room: `randocells.md`, "Starting Location and the second teleport target". |
| | **A start can be written:** a copy of the fresh slot 28 save with the start record set to candidate 17 (333, 449), the continue point set to the same, and the revealed map cell moved from (13, 16) to (23, 22), loaded in play at the save room in (23, 22), the same room the owner had saved in before, with only that cell revealed in the live map and the loaded randomizer section's start record reading (17, 333, 449). The three changes were made together; which of them the game uses to place the player was not separated. Only the first block was patched: in a fresh save the second block is still nearly empty ("The second block is a copy of the first"). |
| | **On a save** the game rewrites the loaded slot's first block in memory at once (`memory.md`, "Save data lives in Zexion.exe's static data"): in the `live26` save (slot 4), a save at the station in (10, 23) changed its continue point from the start's 329 / 191 to 464 / 151, the 16-bit value at file `0x9356` from 32311 to 36589, the byte at `0x9362` from 9 to 10 and the map cells explored since; a save at the start next, after teleporting there, changed only the continue point (back to 329 / 191) and `0x9356` (to 39567). `0x9356` changed with each save by a few thousand over a minute or two: play time in frames, or its low half (assumed). |
| Status | measured (the continue point over four saves; the start record against three fresh seeds, one of them OHKO; the second teleport target, one seed; the tile formula from the generator, checked on three rooms; the candidates, in snapshots with the randomizer menu open, 2026-10-21, and live, 2026-10-23; the block rewritten on a save, two saves, 2026-10-25); working in game (a patched start and a patched second target, one room each, 2026-10-21); assumed (that first objects 2 and `0x1C` are save rooms; `0x9356` as play time; the one-start second target unused) |
| Method | byte comparison of the fresh slot 28 and 29 saves (25 bytes differ in the first block) and of later saves; the fresh OHKO save `lab/work/slot5-ohko-fresh.sav` (a copy of slot 5), its record and `0x12EE0` compared with the others; disassembly of RVAs `0xF2F30` (writes the start record) and `0x14B910` (builds the table); `save_lab.py patch` and `install` into the empty slot 26, loaded by the owner, then the live map grid and randomizer section read through `client/game.py`; the same for a patched second target in slot 6, with the owner teleporting and reading the position the map shows; slot 4's block read live before and after two saves |
| Build | B1 |
| Evidence | `lab/backups/20261020-212018/` (fresh slots 28 and 29), `lab/work/slot28-after-grants.sav`, `lab/work/slot28-after-boss.sav`, `lab/backups/20261021-091726/Save Game 27.sav`, `lab/backups/20261021-101657/Save Game 2.sav`, `lab/work/slot5-ohko-fresh.sav` (a copy of slot 5), 2026-10-21. The candidates: `lab/data/6d8a94bd4d64/start-candidates.json` (first derived from the builder's rules applied to the object table read at the main menu, where the game's own count read 0; then matched by the snapshots, `memory.md`). The patched second target: spec `lab/work/ohko-second-13-16.json`, file `lab/work/ohko-second-13-16.sav`, slot 6. The patched start: spec `lab/work/start-23-22.json`, file `lab/work/slot28-fresh-start-23-22.sav`, installed in slot 26 (backup before: `lab/backups/20261021-102032-1/`). The save in memory: `lab/save-probe-before-i3.bin`, `-after-i3.bin` and `-pod-i3.bin` (slot 4's block before a save, after one at (10, 23), and after one at the start; `lab/save-probe.py`, read-only), 2026-10-25. |

## Counters

| Field | |
|---|---|
| Value | `0x935A` (16-bit) is the pause screen's `ITEMS` counter (`memory.md`, "A pickup counter updates the instant an item is collected"): 0 with no pickup, 1 after one, 2 after two, 150 in the owner's far-along slot 1 (ITEMS 150/150), and equal to the live counter. `0x9321` went 0 → 1 with one Scrap pickup, was 2 after two Scrap and 0 after one normal item in slot 28: it is Scrap metal's inventory byte (item 46, "Inventory: one byte per item ID at 0x92F3"). |
| | **The session count.** `0x9362` (8-bit) is the run's SESSIONS, shown by the pause screen's OPTION MENU, RANDO stats, and the game writes it to the file without a save point being used: slots 2 and 3, each loaded from the file screen and left through the pause menu without saving, were rewritten with `0x9362` up by one (slot 3 33 → 34, its stats reading SESSIONS: 34 after the load; slot 2 13 → 14, with `0x9356`, play time, assumed, changed too) and nothing else changed. That a load adds one and writes the file is inferred from those two; when in the load or exit the write happens was not pinned down (the files' times did not follow the order of the loads). |
| Status | measured (the session count's place and its rise by one in two saves, against the backups and the stats page); assumed (one per load, and the moment of the write) |
| Method | fork test and earlier saves; the session count, `save_lab.py copy` of slots 2 and 3 after loading each once (and leaving through the pause menu without saving), `save_lab.py diff` against the backups |
| Build | B1 |
| Evidence | The fork run, `lab/backups/20261020-221347/` (slots 27 and 30), and the two-pickup save, `lab/backups/20261020-215709/` ("The map grid marks collected locations"); slot 30 after one Scrap pickup, `lab/backups/20261020-213055/`, and slot 28 after one normal item, `lab/backups/20261020-213828/` ("The game loads and keeps a patched placement table"), 2026-10-20. Which save gave slot 1's 150 is not recorded. The session count: `lab/backups/20261027-151329/Save Game 2.sav` and `Save Game 3.sav` against `lab/work/slot2-after-view.sav` and `slot3-after-view.sav`, both files dated 17:22 after Claude loaded the saves to read their randomizer settings, 2026-10-27 (`randocells.md`, "The settings code"). |

## Current energy and missiles

| Field | |
|---|---|
| Value | The save keeps current energy as a 16-bit value at `0x9350` and current missiles at `0x9352`; loading a save copies them into the player state (`memory.md`, "Granting each kind of item, and what keeps it"). The maximums are not read from the save: loading recomputes max energy (Energy Tanks × 16), max missiles and max bombs from the inventory counts. Seen: slot 28 before the grant session held 80 and 2; saved after it, with 6 tanks (96/96) and 4 missiles, it held 96 and 4. Where current bombs are saved was not checked. **In a fresh save** both are full: current energy is Energy Tanks × 16 and current missiles Missile Containers + 10 × Missile Boxes. Easy holds 80 and 3 (5 tanks, 3 containers), Standard 48 and 3 (3 tanks), OHKO 0 and 3 (no tank); the 16-bit value at `0x9354` is 0 in all three. No fresh save started with a Missile Box, so the Box term is from the load and pickup routines, assumed for a seed that starts with Boxes. `save_patch.patch` (a test reference now: the client writes no saves) follows this rule; an ad-hoc check (not kept in the repo) ran it on each of the three fresh saves with a plan built from that save's own item table, starting items, start and settings, and every byte came out as it was. |
| Status | measured (the load routine; slot 28 over the grant session; three fresh saves, all with 0 Missile Boxes); assumed (the Box term for a seed starting with Boxes) |
| Method | disassembly of the load routine (RVA `0x9C630`: `0x9DA4D` to `0x9DA68` read file `0x9350` and `0x9352`, `0x9E02C` to `0x9E064` compute the maximums); `save_lab.py diff`; reading the three fresh saves; the ad-hoc no-op patch above |
| Build | B1 |
| Evidence | `lab/backups/20261021-091726/Save Game 28.sav` against `lab/work/slot28-after-grants.sav`; the fresh `lab/backups/20261020-212018/Save Game 28.sav` (Easy) and `Save Game 29.sav` (Standard) and `lab/work/slot5-ohko-fresh.sav` (OHKO), 2026-10-21. |

## The item table is indexed in location-list order

| Field | |
|---|---|
| Value | Item byte *i* at `0x12A80` is the item at location *i* of the list at `0x12814`. In slot 28's original seed, location #52 (25, 25) holds item 24, the only Gravity Harness in its table, and picking up that location gave the Gravity Harness. Item IDs are the numbers in `Files/Translations/English(HD)/ItemNames.txt` (24 = Gravity Harness, 46 = Scrap metal). |
| Status | working in game (one location, 2026-10-20) |
| Method | slot 28 restored to its fresh state, the first pickup taken by the owner, compared with the table |
| Build | B1 |
| Evidence | `lab/backups/20261020-212018/Save Game 28.sav` (table), pickup 2026-10-20 22:3x. |

## Major items are a list in pickup order at 0x9333

| Field | |
|---|---|
| Value | `0x9333` to `0x934C` hold up to 26 one-byte codes, in pickup order; `0x934D` is the number of entries. The Gravity Harness (item 24) is code `0xC7`. Scrap is not listed. Seen: slot 28 after the Gravity Harness: `C7`, length 1; the control run: `C7 E1`, length 2; the owner's slot 2: 10 codes (`C2 C1 CC C4 C0 CA C5 C6 CB E1`), length 10; the owner's slot 1: 26 distinct codes between `0xC0` and `0xE3`, length 26. How codes map to item IDs, apart from `0xC7` = 24, is not known. The list is a record of pickups, not the inventory: adding a code to the live list grants nothing (`memory.md`, "The pickup record holds the major-item list", which also gives its live address). |
| Status | measured (layout, the code for one item, and that the list does not grant) |
| Method | comparing `0x9300` to `0x935F` across six saves, and against the live recording of the Gravity Harness pickup |
| Build | B1 |
| Evidence | Saves in `lab/backups/20261020-223111/`, `20261020-224429/`, `20261020-213828/`, `20261020-221347/`; `lab/mem/p3-record.log`, 2026-10-20. |

## Inventory: one byte per item ID at 0x92F3

| Field | |
|---|---|
| Value | The save holds one byte per item ID at `0x92F3 + ID` (first block; 64 bytes, to `0x9332`), and the game reads the live player state the same way (RVA `0x16DB41` at offset `0x108E + ID`, filling the logic's count table; live: `memory.md`, "Live inventory: writing an item's byte grants it"). IDs 0 to 50 are those of `ItemNames.txt`; bytes 50 to 63 are 0 in every save seen (a collected Energy Shield, 50, left its byte at 0 in a live game, 2026-10-22: its record names byte 46, Scrap metal, as the one its pickup increments, `memory.md`, "Other players' items"). The names this repo uses are the game's item records, which its popups show; `ItemNames.txt` calls item 26 "HYPER JUMP" and item 37 "10X Bomb Pack", where the records say ZENITH THRUSTERS and BOMB BOX. Energy Tank (0), Missile Container (1), Mega Bomb (2), the Missile Box (36) and the Bomb Box (37) are counts; the upgrades are 0 or 1; Scrap metal (46) counts pickups (`0x9321`). Every other item is 0 or 1 too, as the pools hold one of each at most (`randocells.md`, "The pool"): across the 1,509 saves in `lab/backups/` and `lab/work/` (2026-10-28; mostly B1's, though the backups also hold copies of saves that older builds wrote, `builds.md`, "Older builds") only bytes 0, 1, 2, 36 and 46 were above 1, apart from slot 22, an Archipelago save first seen 2026-10-26 with Slide Mod (16) and Zenith Thrusters (26) at 2 (how they got there is not recorded); in a live game (B1, 2026-10-28, `lab/ap/live43`) a server console's second Zenith Thrusters, after the first was picked up, left byte 26 at 2: the client's grant added the copy to the byte (the pickup routine also only increments it, `memory.md`, "Granting each kind of item, and what keeps it"). What the game does with a 2 in such a byte is not measured; the client now writes none above 1 (`client/reconcile.py`, `COUNTED`): in game, two console Magnus Armors in a row left byte 25 at 1, one more Zenith Thrusters left byte 26 at 1, and a Missile Container still raised byte 1 from 10 to 11 (max missiles 11 on screen), each with its popup. This replaces an earlier reading of `0x9300` as "ability flags, flag = item ID − 13": `0x9300` is just item 13's byte. Seen: slot 28's fresh seed starts with 5 Energy, 3 Missile Containers, the Machine Gun (3), Power Cartridge (13) and Navcore (32), and the Gravity Harness pickup set item 24; slot 29's fresh seed starts with 3 Energy, 3 Missile Containers and the Force Blaster (4), so the starting weapon varies by seed; slot 1 owns nearly everything. In the normal game the keys (27 to 30) stay at 1 once used (slot 1's copy held all four before the core, after the grab and after the ending: `memory.md`, "The goal: Zexion's inventory byte at the grab"); under the randomizer's Gates settings this was not checked. Some filler is not kept in its own byte: in the owner's randomizer save (slot 2) two collected Scrap Energy (47) and three Area Maps (49) left bytes 47 and 49 at 0, while Scrap metal's byte (46) is 5 above the Scrap metal collected, so those pickups count as Scrap metal, as the item records say; that save also owns one item 38 (an egg), which no randomizer location holds. |
| Status | measured (layout, from the game's code and from saves; item 24's byte seen set by a pickup; byte 50 still 0 after a live Energy Shield pickup, 2026-10-22; every byte's largest value across the lab's saves, 2026-10-28; byte 26 at 2 after a console copy, live, 2026-10-28); working in game (the client's cap at 1, with a count still raised, B1, 2026-10-28); working in game (items 16 and 17 granted live through it, 2026-10-20, `memory.md`) |
| Method | disassembly of RVA `0x16DB41`; save comparison; live grants (`memory.md`); `grant.py show` after a live Energy Shield pickup; each byte's largest value over every save in `lab/backups/` and `lab/work/`, read with a one-off script (not in the repo); the live console copy, the byte read in the running game after it; the item records' names read from the `Objects` file |
| Build | B1 |
| Evidence | `lab/backups/20261020-223111/`, `20261020-224429/`, `20261020-213828/` (slot 28), 2026-10-20; the keys: `lab/work/slot1-for-core.sav`, `slot25-after-zexion.sav` and `slot25-after-ending.sav`, 2026-10-21; the filler: slot 2 in `lab/backups/20261021-102032-1/`, 2026-10-21; the names: records 26 and 37 of `lab/data/6d8a94bd4d64/Objects`, B1, 2026-10-22; the Energy Shield: picked up at Kresezoa 20,16 in slot 9 (`lab/ap/live4/client.log`), then `grant.py show` listing no item 50, 2026-10-22. The scan: every `.sav` under `lab/backups/` and `lab/work/`, bytes `0x92F3` to `0x9325` read, 2026-10-28; slot 22's 2s in every backup from `lab/backups/20261026-184341/` to `20261028-195619/`. The console copy: `lab/ap/live43/server.log` ("Cheat console: sending "Zenith Thrusters" to Hana", after the pickup at Machinus 24,29), byte 26 read as 2 afterwards (that reading is in no log), 2026-10-28. The cap: `lab/ap/live44` (slot 4, Iris; `server.log` for the four console sends, `iris2.log` for the client from `claude/cap-single-items`), the bytes read with `grant.py show` before and after (not logged), the popups and the missile count reported by the owner, 2026-10-28. |

## The randomizer section holds the seed's settings

| Field | |
|---|---|
| Value | Settings the logic reads, by file offset: starting amount and amount in the pool, as 16-bit pairs, for Energy (`0x12ED4`), missiles counting each Missile Box as 10 (`0x12ED8`) and Mega Bombs (`0x12EDC`); two 27-byte starting-item arrays (`0x12E9D` start-with, `0x12EB8` pool; their encoding: `memory.md`, "The game-mode panel: NORMAL / RANDOMIZER"), one byte per upgrade in the order listed at RVA `0x20DA68`; one byte per setting from `0x1308F` (setting *t* at `0x1308F + t`, the options code 68 reads); and code 69's two mode bits at `0x130BA` and `0x130B9` (settings 43 and 42). Which menu setting each byte holds: `randocells.md`, "Where each setting is stored"; each mode's amounts and settings: `randocells.md`, "The randomizer's modes". |
| | **Seen in four saves:** amounts as each mode's (slots 2 and 28 Easy's, slot 29 Standard's, the OHKO seed OHKO's); settings on: slots 2 and 28 have 10, 11, 18, 20, 28, 29, 38, 43 and 73, slot 29 has 10, 11, 15, 20, 28, 29, 38, 43 and 73 (Hints Large against Normal, and Random Gun on in 29); the OHKO seed has 10, 11, 18, 20, 28, 29, 33, 38 and 72 on (mode bits (0, 0), Safety Light); code 68's options (30, 31, 32, 37) are off in all four, and the mode bits are (1, 0) in the other three. The pool amounts match each seed's item table (slot 28: 35 Energy Tanks; 29 Missile Containers and 17 Missile Boxes = 199; 18 Mega Bombs). |
| Status | measured (layout from the code; values in four saves) |
| Method | disassembly of RVAs `0x9C630` (the copy), `0xE5450` and `0xE70A0` (starting counts), `0x90260` (presets); reading backup copies of the saves |
| Build | B1 |
| Evidence | `lab/backups/20261020-224429/Save Game 2.sav`, `lab/backups/20261020-212018/Save Game 28.sav` and `Save Game 29.sav`, 2026-10-20; `lab/work/slot5-ohko-fresh.sav` (a copy of slot 5), 2026-10-21. |
