# Save format

The save folder is `%APPDATA%\Gallant Leaf\Zexion`. Never write there except through
`tools/save_lab.py` (see `/save-lab`) and the client's `/prepare` (`client/prepare.py`, which
backs up the slot first and works only while Zexion is closed).

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
| Value | `Save Game 1.sav` to `Save Game 30.sav` (all 30 exist, used or not), `GameUnlocks.sav`, `PreferencesSynced.sav`, `PreferencesLocal`, `steam_autocloud.vdf`, and `Mods/{Local,Workshop}/{Sprites,Translations}/`. |
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
| Value | As of 2026-10-20, before the experiments: 75,794 bytes for slot 1 and for every unused slot; 78,108 bytes (2,314 more) for slot 2. Slot 2 is a randomizer save and the extra bytes hold its seed (see "Randomizer data is appended after the two blocks" and the sections after it). |
| Status | measured |
| Method | file inspection |
| Build | B1 installed; files written by older builds |
| Evidence | `ls -la` and `save_lab.py slots`, 2026-10-20. |

## Unused slots hold a fixed template

| Field | |
|---|---|
| Value | Before the experiments of 2026-10-20, slots 3 to 30, all unused, had identical content (SHA-256 prefix `8c9868d64c45`), which is not all zeros: `E8 03`, zeros to `0x7BAD`, `0xFF` from `0x7BAE` to `0x877E` inclusive (3,025 bytes), zeros, one `0xFF` at `0x9360`, then 8 records of 14 bytes from `0x9364` to `0x93D3`, each `FF FF FF FF 00 00 00 00 30 00 02 00 00 00`, then zeros to the end. What the regions mean is not known. `save_lab.py` treats a slot as empty when its content is the one at least 3 slots share. |
| Status | measured |
| Method | file inspection: `xxd -a` and a byte-value histogram of slot 3; `save_lab.py slots` for the matching hashes of slots 3 to 30 |
| Build | B1 installed; files written 2025-12-19 by an older build |
| Evidence | 2026-10-20. Histogram of slot 3: 72,718 × `00`, 3,058 × `FF`, 8 × `30`, 8 × `02`, one each of `E8` and `03`. |

## Clearing a slot in the game sets two bytes and keeps the rest

| Field | |
|---|---|
| Value | When the owner cleared slots 27 to 30 in the game, each file kept its size (78,108 bytes) and its contents, including the randomizer section, except for two bytes: `0x9360` went from 0 to `0xFF`, the value the unused-slot template has there (above), and `0x9402` went from 1 to 0. `0x9402` is 1 in every randomizer save seen and 0 in the template and in the owner's normal-mode slot 1. So the game marks a slot empty with `0x9360 = 0xFF`. Loading a save copies this byte into live memory, where `0xFF` means no save is loaded (`memory.md`, "Telling play from menus, the title screen and death"); whether the file-select screen reads anything else to show a slot as empty is not known. `save_lab.py` does not know this marker: it lists a cleared slot as in use (its content is not the template), so its `install` refuses one, which errs on the safe side. In the same rewrite, every slot in use also changed its 16-bit value at `0x9362` (slot 2: `0xFFFF` → 2; slots 27 and 28 rose by 1 and 3; slots 29 and 30 kept theirs); what it holds is not known. Slot 1 did not change. |
| Status | measured (the bytes that changed; `0x9360` matching the template; the load copying `0x9360`, from disassembly); assumed (that file select shows a slot as empty from `0x9360` alone) |
| Method | `save_lab.py diff` of each slot against the backup taken before the clear |
| Build | B1 |
| Evidence | `lab/backups/20261021-091726/` (before) against copies `lab/work/fresh-slot{2,27,28,29,30}.sav` and `lab/backups/20261021-101657/` (after), 2026-10-21. Slot 28's other differences come from the grant session that day. The load copying the byte: disassembly at RVA `0x9D7EC` (`memory.md`, "Telling play from menus, the title screen and death"). |

## A save is a header and two copies of one block

| Field | |
|---|---|
| Value | A 75,794-byte save is `E8 03` followed by two 37,896-byte blocks: `0x0002` to `0x9409` and `0x940A` to `0x12811`. In slots 1 and 2 the same structures appear in both blocks, `0x9408` bytes apart (FF runs at `0x7BBA` and `0x10FC2`, the 14-byte records at `0x9388` and `0x12790`). In the unused-slot template the second block is all zeros. Why there are two copies (a backup, or two states) is not known. |
| Status | measured (layout); purpose unknown |
| Method | file inspection of backup copies: positions of FF runs of 16+ bytes and of the bytes `30 00 02 00`, in slots 1, 2 and 3 |
| Build | B1 installed; files written by older builds |
| Evidence | Script over `lab/backups/20261020-211239/`, 2026-10-20. |

## Randomizer data is appended after the two blocks

| Field | |
|---|---|
| Value | Slot 2's extra 2,314 bytes are appended at `0x12812`, after both blocks, which sit at the same offsets as in slot 1. The appended part starts with its own `E8 03`, then:<br>• `0x12814`: 154 little-endian u16 pairs, all distinct, sorted by the second value then the first, both in 0 to 53; then 4 zero bytes.<br>• `0x12A80`: 154 bytes, each 0 to 50; then 2 zero bytes.<br>• `0x12B1C`: 8 bytes, `da 76 2e 8f ab 2b 1d e3`.<br>• `0x12B24` onward: more data, including the seed's randomizer settings (below), up to `0x130F8`, then zeros to the end.<br>Meanings, established in later sections: the pairs are the (x, y) cells of the item locations ("The map grid marks collected locations"), and the bytes are the item at each location, in list order ("The item table is indexed in location-list order"); item IDs are those of `ItemNames.txt`, which runs 0 to 50 (50 is the Energy Shield). The 8 bytes are the seed: a game started with the menu's seed `0000 0000 0000 0001` stored `00 00 00 00 00 00 00 10`, each byte holding two of the displayed digits, the later one in the high half (`lab/work/slot5-ohko-fresh.sav`, 2026-10-21). |
| Status | measured (layout, counts, value ranges); working in game (the item table, see below); measured for the last byte only (that the 8 bytes are the seed: one seed entered in the menu, with one nonzero digit, puts the last displayed pair in the last byte, its later digit in the high half); assumed (the digits' base, and the same order for the other bytes) |
| Method | file inspection and a parsing script over the backup copy of slot 2; the seed: a game started with a seed typed in the menu, saved at once, and its bytes read |
| Build | B1 installed; slot 2 written 2026-06-13 by an older build |
| Evidence | `lab/backups/20261020-211239/Save Game 2.sav`, 2026-10-20. Byte-value counts over the 154 bytes: 35 × 0, 29 × 1, 18 × 2, 17 × 36, 17 × 46, 6 × 49. The seed: `lab/work/slot5-ohko-fresh.sav` (a copy of slot 5), build B1, 2026-10-21. |

## What changes between randomizer seeds

| Field | |
|---|---|
| Value | Across three randomizer saves (slots 2, 28, 29; different seeds, 28 and 29 saved before any pickup):<br>• The 154 u16 pairs at `0x12814` are identical in all three. They are the location cells ("The map grid marks collected locations"), so the location list is fixed and only the placement varies.<br>• The 154 bytes at `0x12A80` differ in 136 positions between slots 28 and 29, and their value counts differ too (slot 29 has 62 × 1 against slot 28's 29 × 1). Slot 29 was made with different options: its settings match a preset built into the game ("The randomizer section holds the seed's settings").<br>• The 8 bytes at `0x12B1C` differ in every save.<br>• The data after them (`0x12B24` to `0x130F8`) differs too: a short header (`06 00 bf 00 49 01` in 28, `04 00 94 00 65 03` in 29), then what look like records of four u16 values. The short header turned out to be the seed's start ("The continue point and the seed's start"); the records after it are partly decoded later and hold the seed's settings ("The randomizer section holds the seed's settings").<br>• 27 bytes differ between 28 and 29 outside the randomizer section: 25 in the first block and 2 in the second (recounted 2026-10-21; the first count put all 27 in the first block). Eight of them, `0x2A` to `0x31`, hold values 1 to 51 that change with the seed (28: `33 15 28 11 16 01 05 27`): they are the four keys' map markers, each the (y, x) cell where the seed puts that key (`memory.md`, "Keys, the gates and the key markers"; checked against the item tables of slots 28 and 29 and of the fresh OHKO save, `lab/work/slot5-ohko-fresh.sav`; slot 2 was not checked).<br>• Re-saving slot 1 while making these changed its timestamp but not its contents. |
| Status | measured |
| Method | parsing script over backup copies; for the key markers, each save's item table compared with its bytes `0x2A` to `0x31` |
| Build | B1; slots 28 and 29 written 2026-10-20 by B1 |
| Evidence | `lab/backups/20261020-212018/`, 2026-10-20; for the key markers also `lab/work/slot5-ohko-fresh.sav`, 2026-10-22. |

## The game loads and keeps a patched placement table

| Field | |
|---|---|
| Value | A copy of slot 28 with all 154 bytes at `0x12A80` set to 46 (Scrap metal), and nothing else changed, loaded without complaint. The first pickup gave Scrap metal. After the game saved over it, the whole appended section (`0x12812` to the end) was byte-for-byte what was installed, so the game neither rejected the change, nor checks a checksum over that section, nor regenerates the table from the seed when it saves. Control: the same first pickup in the unpatched slot 28 gave a normal item, not Scrap. So the game takes each location's item from this table. That the table is indexed in the order of the cell list at `0x12814` was confirmed later ("The item table is indexed in location-list order"). |
| Status | working in game (one location: patched item granted, table kept on save, control differs) |
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

## Candidates for "has item" flags

| Field | |
|---|---|
| Value | After the same single pickup from the same start, the save that received a normal item (slot 28) and the one that received Scrap (slot 30) differ in the first block at 42 bytes. Most are map exploration and stats (slot 28 explored a little further). Two bytes went 0 → 1 only where the normal item was received: `0x0129` and `0x39A7`. Either may be the flag for owning that item; neither is confirmed, and which item it was is not yet recorded. |
| Status | superseded: the inventory is at `0x92F3 + item ID` (below); `0x0129` belongs to the array at `0x0111`, which is not the inventory |
| Method | byte comparison of the two saves after their pickup, against the save before it |
| Build | B1 |
| Evidence | `lab/backups/20261020-213828/Save Game 28.sav` against `lab/backups/20261020-213055/Save Game 30.sav` and `lab/backups/20261020-212018/Save Game 28.sav`, 2026-10-20. |

## The map grid marks collected locations

| Field | |
|---|---|
| Value | A 54 × 54 grid, one byte per map cell, row-major (`index = y × 54 + x`), at file offset `0x877F` to `0x92E2` in the first block (so `0x9B87` onward in the second). Values seen: `0x00` unexplored, `0x18` (24) explored room, `0x19` (25) a cell with an item not yet collected, `0x1B` (27) a cell whose item was collected, `0x22` (34) a special room (perhaps a save room), and others up to `0x2C` in a full save. In the owner's randomizer save (slot 2, 63 pickups counted), 65 location cells read `0x1B`, and the items the item table holds there account for its inventory exactly, less that seed's starting items, apart from the filler noted in "Inventory: one byte per item ID at 0x92F3" (the two over the counter fit the keys, which it skips); the other location cells read 0, `0x13` (38, all minor items), `0x15` (4, all majors), `0x19` (3), `0x0D` or `0x0E`, and none of those items is owned, so they are uncollected. The cells marked `0x19`/`0x1B` are cells from the randomizer's location list at `0x12814`, so location *i* (its *i*-th (x, y) pair) is collected when the grid holds `0x1B` at that cell. Location numbers seen: the first pickup from the start is #52 at (25, 25), the second #48 at (29, 24). |
| Status | measured (two locations, in the save); assumed (that every location behaves the same, and the meanings of the other values) |
| Method | fork test: the fresh all-Scrap save installed into slots 27 and 30, the same route played in each, the pickup taken only in slot 30; then `save_lab.py diff`. The grid layout was chosen as the one that puts both collected cells on location cells, starts at the end of the template's FF block, and has the width of the location coordinates (0 to 53); under it the explored cells form one connected route and the owner's slot 1 fills the whole grid. |
| Build | B1 |
| Evidence | `lab/backups/20261020-221347/` (slots 27 and 30 after the fork run): they differ only at `0x4589`, `0x8CDE` (`0x19` → `0x1B`), the counters `0x9321` and `0x935A`, play time, and the second block. Earlier two-pickup save: `lab/backups/20261020-215709/`. 2026-10-20. |

## A byte array also flags collected locations

| Field | |
|---|---|
| Value | Between `0x3000` and `0x7000`, the only bytes set to 1 are `0x4589` after location #52 was collected, and `0x4589` and `0x43C7` after #52 and #48. Neither is set in the fork save without the pickup. How an offset here maps to a location is not known (the two are 450 bytes apart, which no simple grid width explains). The map grid above is the usable source for now. |
| Status | measured (two locations); mapping unknown |
| Method | as above |
| Build | B1 |
| Evidence | as above. |

## The continue point and the seed's start

| Field | |
|---|---|
| Value | Positions are counted in 16-pixel tiles over the whole map, with each map cell 14 tiles along x and 20 along y: tile column = x × 14 + pixel x / 16, tile row = y × 20 + pixel y / 16, where (x, y) is the cell and the pixel position is the save point's within its room. **Continue point:** the 16-bit values at `0x935C` (row) and `0x935E` (column) are the save point the save continues from. Saving in the save room at (23, 22) stored 449 / 333; teleporting back to the start and saving there stored the start's 329 / 191 again. **The seed's start:** the randomizer section opens, at `0x12B24`, with three 16-bit values: an index into the game's list of start candidates minus 1, then the start's column and row (slot 28: 6, 191, 329 for cell (13, 16); slot 29: 4, 148, 869 for (10, 43); the owner's slot 2: 8, 202, 27). They stay the same however far the game goes. Fresh Easy and Standard saves reveal only the start cell on the map grid, with value `0x22`, the value saved-in rooms show ("The map grid marks collected locations"; that `0x22` means a save room is not established); a fresh OHKO save reveals no cell at all, so the start record is the reliable source. That OHKO seed's record is (22, 402, 989): entry 22, cell (28, 49), whose first object is `0x1C` in the candidate list derived below (`lab/data/6d8a94bd4d64/start-candidates.json`). `tools/randocells.py` reads the start from the record. **The second teleport target:** with Double Teleport on, the seed records a second start in the same form at `0x12EE0` (candidate index, tile column, tile row): the OHKO seed holds (3, 151, 464), cell (10, 23); Easy and Standard seeds, with Double Teleport off, hold (0, 20, 509), entry 0, cell (1, 25), presumably unused. **A second target can be written too:** a copy of the fresh OHKO save with only this record changed to (6, 191, 329), installed in slot 6 and loaded, held that record in memory, and the pause menu's second teleport took the owner to the save room at (13, 16). **The candidates:** at seed generation (RVA `0xF2F30`) the start comes from a list the game builds at run time (`0x14B910`): every map cell whose first object is 2 or `0x1C` and that holds no object `0x16`, up to 64, counted at `+0xEAF7CC` with the cells at `+0xEB2070`. Both come from a per-cell table at `+0x361C10`, 154 bytes per cell, which is also in the `MapData` file from byte 4,085,505 (the records of (13, 16), (10, 43) and (23, 22) found there at that spacing): up to 8 object numbers at `+0x00` (`0xFFFF` ends them), then the first object's pixel position at `+0x10` and `+0x12` (the generator reads `+0x361C20`); the location table at `+0x361C4A` in `randocells.md` is the same table's field `+0x3A`. The game's own list read empty at the main menu (count 0), so it was not read; applying the builder's rules, as disassembled, to the object table read at the main menu gives 34 entries, and entries 6, 4 and 8 are exactly the three starts seen (cells (13, 16), (10, 43), (14, 1) with their columns and rows). The builder also flags cells holding object `0x19`; none of the three starts has one, and whether the generator avoids them is not known. How the generator picks among the 34 is not traced. **A start can be written:** a copy of the fresh slot 28 save with the header set to entry 17 (333, 449), the continue point set to the same, and the revealed map cell moved from (13, 16) to (23, 22), loaded in play at the save room in (23, 22), the same room the owner had saved in before, with only that cell revealed in the live map and the loaded randomizer section's header reading (17, 333, 449). The three changes were made together; which of them the game uses to place the player was not separated. Only the first block was patched: in a fresh save the second block is still nearly empty ("The second block is a copy of the first"). |
| Status | working in game (a patched start, one room, 2026-10-21); measured (the continue point over four saves; the header against three fresh seeds, one of them OHKO; the second teleport target, one seed; working in game (a patched second target, one room, 2026-10-21); the tile formula from the generator, checked on three rooms); derived (the list of 34, from the disassembled rules, consistent with three starts); assumed (that first objects 2 and `0x1C` are save rooms) |
| Method | byte comparison of the fresh slot 28 and 29 saves (25 bytes differ in the first block) and of later saves; the fresh OHKO save `lab/work/slot5-ohko-fresh.sav` (a copy of slot 5), its record and `0x12EE0` compared with the others; disassembly of RVAs `0xF2F30` (writes the header) and `0x14B910` (builds the list); the game's list count read at `+0xEAF7CC` (0) and the object table read at `+0x361C10` with the game at the main menu, then the builder's rules applied; `save_lab.py patch` and `install` into the empty slot 26, loaded by the owner, then the live map grid and randomizer section read through `client/game.py`; the same for a patched second target in slot 6, with the owner teleporting and reading the position the map shows |
| Build | B1 |
| Evidence | `lab/backups/20261020-212018/` (fresh slots 28 and 29), `lab/work/slot28-after-grants.sav`, `lab/work/slot28-after-boss.sav`, `lab/backups/20261021-091726/Save Game 27.sav`, `lab/backups/20261021-101657/Save Game 2.sav`, `lab/work/slot5-ohko-fresh.sav` (a copy of slot 5), 2026-10-21. The list: `lab/data/6d8a94bd4d64/start-candidates.json`. The patched second target: spec `lab/work/ohko-second-13-16.json`, file `lab/work/ohko-second-13-16.sav`, slot 6. The patched start: spec `lab/work/start-23-22.json`, file `lab/work/slot28-fresh-start-23-22.sav`, installed in slot 26 (backup before: `lab/backups/20261021-102032-1/`). |

## Counters

| Field | |
|---|---|
| Value | `0x935A` (16-bit) is the pause screen's `ITEMS` counter, the number of locations collected except those whose item is 27 to 31 or 38 to 45, which the pickup routine does not count (`memory.md`, "Granting each kind of item, and what keeps it"; the Zexion grab left it unchanged, "The goal: Zexion's inventory byte at the grab"): 0 with no pickup, 1 after one, 2 after two, 150 in the owner's far-along slot 1 (ITEMS 150/150), and equal to the live counter (`memory.md`). `0x9321` went 0 → 1 with one Scrap pickup, was 2 after two Scrap and 0 after one normal item in slot 28: it is Scrap metal's inventory byte (item 46, see "Inventory: one byte per item ID at 0x92F3"). |
| Status | measured |
| Method | fork test and earlier saves |
| Build | B1 |
| Evidence | as above. |

## Current energy and missiles

| Field | |
|---|---|
| Value | The save keeps current energy as a 16-bit value at `0x9350` and current missiles at `0x9352`; loading a save copies them into the player state (`memory.md`, "Granting each kind of item, and what keeps it"). The maximums are not read from the save: loading recomputes max energy (Energy Tanks × 16), max missiles and max bombs from the inventory counts. Seen: slot 28 before the grant session held 80 and 2; saved after it, with 6 tanks (96/96) and 4 missiles, it held 96 and 4. Where current bombs are saved was not checked. **In a fresh save** both are full: current energy is Energy Tanks × 16 and current missiles Missile Containers + 10 × Missile Boxes. Easy holds 80 and 3 (5 tanks, 3 containers), Standard 48 and 3 (3 tanks), OHKO 0 and 3 (no tank); the 16-bit value at `0x9354` is 0 in all three. No fresh save started with a Missile Box, so the Box term is from the load and pickup routines, assumed for a seed that starts with Boxes. Current bombs were not checked; `client/save_patch.py` writes none. It writes energy and missiles by this rule: an ad-hoc check (not kept in the repo) ran `save_patch.patch` on each of the three fresh saves with a plan built from that save's own item table, starting items, start and settings, and every byte came out as it was. |
| Status | measured (the load routine; slot 28 over the grant session; three fresh saves, all with 0 Missile Boxes) |
| Method | disassembly of the load routine (RVA `0x9C630`: `0x9DA4D` to `0x9DA68` read file `0x9350` and `0x9352`, `0x9E02C` to `0x9E064` compute the maximums); `save_lab.py diff`; reading the three fresh saves; the ad-hoc no-op patch above |
| Build | B1 |
| Evidence | `lab/backups/20261021-091726/Save Game 28.sav` against `lab/work/slot28-after-grants.sav`; the fresh `lab/backups/20261020-212018/Save Game 28.sav` (Easy) and `Save Game 29.sav` (Standard) and `lab/work/slot5-ohko-fresh.sav` (OHKO), 2026-10-21. |

## The item table is indexed in location-list order

| Field | |
|---|---|
| Value | Item byte *i* at `0x12A80` is the item at location *i* of the list at `0x12814`. In slot 28's original seed, location #52 (25, 25) holds item 24, the only Gravity Harness in its table, and picking up that location gave the Gravity Harness. Item IDs are the numbers in `Files/Translations/English(HD)/ItemNames.txt` (24 = Gravity Harness, 46 = Scrap metal). |
| Status | working in game (one location) |
| Method | slot 28 restored to its fresh state, the first pickup taken by the owner, compared with the table |
| Build | B1 |
| Evidence | `lab/backups/20261020-212018/Save Game 28.sav` (table), pickup 2026-10-20 22:3x. |

## An item-ID-indexed byte array at 0x0111 (not the inventory)

| Field | |
|---|---|
| Value | File `0x0111 + id` (first block): slot 28's fresh randomizer save has entries 7, 15, 16 and 22 set (Arc Plasma, Tunneler, Slide Mod, Jump Booster), yet that save does not own Slide Mod (it had to be granted, see `memory.md`), so these are not owned items. In the control run, entry 24 was set after the Gravity Harness pickup; in a later run of the same pickup and a save at the same save point, it was **not**, though the item was collected. So this array is not what records ownership; what sets it is not known. Slot 1 (far into the normal game) has many entries set, and entry 37 is 4. |
| Status | measured (layout, the two runs); meaning unknown |
| Method | byte comparison of slot 28 saves |
| Build | B1 |
| Evidence | `lab/backups/20261020-212018/`, `20261020-213828/` (control), `20261020-223111/` and `20261020-224429/` (second run), 2026-10-20. |

## Major items are a list in pickup order at 0x9333

| Field | |
|---|---|
| Value | `0x9333` to `0x934C` hold up to 26 one-byte codes, in pickup order; `0x934D` is the number of entries. The Gravity Harness (item 24) is code `0xC7`. Scrap is not listed. Seen: slot 28 after the Gravity Harness: `C7`, length 1; the control run: `C7 E1`, length 2; the owner's slot 2: 10 codes (`C2 C1 CC C4 C0 CA C5 C6 CB E1`), length 10; the owner's slot 1: 26 distinct codes between `0xC0` and `0xE3`, length 26. How codes map to item IDs, apart from `0xC7` = 24, is not known. The list and its length sit at a fixed distance from their live copies (`memory.md`); the other fields around them are saved one by one. Adding a code to the live list grants nothing, so the list is a record of pickups, not the inventory. |
| Status | measured (layout, the code for one item, and that the list does not grant) |
| Method | comparing `0x9300` to `0x935F` across six saves, and against the live recording of the Gravity Harness pickup |
| Build | B1 |
| Evidence | Saves in `lab/backups/20261020-223111/`, `20261020-224429/`, `20261020-213828/`, `20261020-221347/`; `lab/mem/p3-record.log`, 2026-10-20. |

## Inventory: one byte per item ID at 0x92F3

| Field | |
|---|---|
| Value | The player state holds one byte per code, at file `0x92F3 + code` (first block): codes 0 to 49 are item IDs from `ItemNames.txt`, so this is the inventory (the names this repo uses are the game's item records, what its popups show: `ItemNames.txt` still calls item 26 "HYPER JUMP" and item 37 "10X Bomb Pack", where the records, from the `Objects` file, say ZENITH THRUSTERS and BOMB BOX) (the list runs to 50, the Energy Shield; whether item 50 uses the byte the logic treats as code 50 is not known). Energy Tank (0), Missile Container (1), Mega Bomb (2) and the Missile Box (36) are counts; the upgrades are 0 or 1; Scrap metal (46) counts pickups (`0x9321`). Codes 50 to 63 are 0 in every save seen. This replaces an earlier reading of `0x9300` as "ability flags, flag = item ID − 13": `0x9300` is just item 13's byte. Seen: slot 28's fresh seed starts with 5 Energy, 3 Missile Containers, the Machine Gun (3), Power Cartridge (13) and Navcore (32), and the Gravity Harness pickup set item 24; slot 29's fresh seed starts with 3 Energy, 3 Missile Containers and the Force Blaster (4), so the starting weapon varies by seed; slot 1 owns nearly everything. The keys (27 to 30) stay at 1 once used: slot 1's copy in slot 25 held all four before the owner went into the core, after the grab, and after the ending (`memory.md`, "The goal: Zexion's inventory byte at the grab"), so getting through the key gates does not take them away. That is the normal game; whether the randomizer's Gates settings behave the same was not checked. Some filler is not kept in its own byte: in the owner's randomizer save (slot 2) two collected Scrap Energy (47) and three Area Maps (49) left bytes 47 and 49 at 0, while Scrap metal's byte (46) is 5 above the Scrap metal collected, so those pickups probably count as Scrap metal; that save also owns one item 38 (an egg), which no randomizer location holds. The game fills the logic's count table from these bytes (RVA `0x16DB41` reads the live player state at offset `0x108E + code`). |
| Status | measured (layout, from the game's code and from saves; item 24's byte seen set by a pickup); working in game (items 16 and 17 granted live through it, see `memory.md`) |
| Method | disassembly of RVA `0x16DB41`; save comparison; live grants (`memory.md`) |
| Build | B1 |
| Evidence | `lab/backups/20261020-223111/`, `20261020-224429/`, `20261020-213828/` (slot 28), 2026-10-20; the keys: `lab/work/slot1-for-core.sav`, `slot25-after-zexion.sav` and `slot25-after-ending.sav`, 2026-10-21; the filler: slot 2 in `lab/backups/20261021-102032-1/`, 2026-10-21. |

## The randomizer section holds the seed's settings

| Field | |
|---|---|
| Value | The game copies the 2,312 bytes from `0x12814` (the location list onward) to RVA `0xCDD6D0` when a randomizer save loads (marker `0x9402` = 1; a normal save leaves the previous section in place, `memory.md`, "The loaded save's randomizer section is at +0xCDD6D0"), and keeps a copy per slot at RVA `0x347830 + (slot − 1) × 0x908`. Settings the logic reads, by file offset: starting amount and amount in the pool, as 16-bit pairs, for Energy (`0x12ED4`), missiles counting each Missile Box as 10 (`0x12ED8`) and Mega Bombs (`0x12EDC`); two 27-byte starting-item option arrays (`0x12E9D`, `0x12EB8`), one byte per upgrade in the order listed at RVA `0x20DA68`; one byte per option from `0x1308F` (option *t* at `0x1308F + t`, the options code 68 reads); and code 69's two mode bits at `0x130BA` and `0x130B9`. Which menu setting each option byte holds is in `randocells.md`, "Where each setting is stored". Seen: slots 2 and 28 hold Energy 5 + 35, missiles 3 + 199, bombs 0 + 18; slot 29 holds 3 + 17, 3 + 122, 0 + 12, Standard mode's preset; slot 28 is Easy's (`randocells.md`, "The randomizer's modes"); an OHKO seed holds 0 + 0, 3 + 152, 0 + 12; options on: slots 2 and 28 have 10, 11, 18, 20, 28, 29, 38, 43 and 73, slot 29 has 10, 11, 15, 20, 28, 29, 38, 43 and 73 (Hints Large against Normal, and Random Gun on in 29; `randocells.md`, "Where each setting is stored"); code 68's options (30, 31, 32, 37) are off and the mode bits (1, 0) in all three; the OHKO seed (`lab/work/slot5-ohko-fresh.sav` (a copy of slot 5)) has 10, 11, 18, 20, 28, 29, 33, 38 and 72 on and mode bits (0, 0), Safety Light. The pool amounts match each seed's item table (slot 28: 35 Energy Tanks; 29 Missile Containers and 17 Missile Boxes = 199; 18 Mega Bombs). |
| Status | measured (layout from the code; values in four saves) |
| Method | disassembly of RVAs `0x9C630` (the copy), `0xE5450` and `0xE70A0` (starting counts), `0x90260` (presets); reading backup copies of the saves |
| Build | B1 |
| Evidence | `lab/backups/20261020-224429/Save Game 2.sav`, `lab/backups/20261020-212018/Save Game 28.sav` and `Save Game 29.sav`, 2026-10-20; `lab/work/slot5-ohko-fresh.sav` (a copy of slot 5), 2026-10-21. |
