# Memory

Where the running game keeps the state the client needs. Addresses are module-relative
(`Zexion.exe+offset`) and good for build B1 only; they become byte patterns before any client
code relies on them (see `/find-address`). Format per `docs/re/README.md`.

## Save data lives in Zexion.exe's static data

| Field | |
|---|---|
| Value | The save state is in the module image (writable static data), not the heap, so its offsets from the module base are fixed for a build:<br>• One block per save slot, in an array: slot N's first save block is at `Zexion.exe+0x230600 + (N−1) × 0x9408`. Each matches the first block of `Save Game N.sav` (file offsets `0x0002` to `0x9409`) byte for byte; checked for slots 1, 2, 3, 28, 29 and 30.<br>• The loaded randomizer save's appended section (file `0x12812` onward, without its `E8 03` header) is mirrored from `Zexion.exe+0x357E18` (location cells), with the item table at `+0x358084` and the seed at `+0x358120`.<br>• The seed also appears at `+0x356F10` and `+0xCDD9D8`. Other near-copies of slot 30's block are at `+0xDDAC58` (37,845 of 37,896 bytes equal; file offset `0x129` = 1) and `+0xDED468` (37,881 equal; `0x129` = 0). One of these may be the live play state; not yet shown. |
| Status | measured |
| Method | memory scan with `tools/memscan.py` (search for the seed and the all-Scrap table, then whole-block comparison against backup copies of the saves) |
| Build | B1 |
| Evidence | 2026-10-20, slot 30 loaded. Saves compared from `lab/backups/20261020-213828/` and `lab/backups/20261020-213055/`. None of the copies changed over a 3-second window, with the game possibly paused. |

## A pickup counter updates the instant an item is collected

| Field | |
|---|---|
| Value | `Zexion.exe+0x3D0168` counts pickups in the current session: it went 0 → 1 at the first pickup in slot 30 and 1 → 2 at the second, at the moment of collection (within 0.1 s, the sampling interval), with no pause or save involved. Its neighbors read as a record of 32-bit fields. It is the pause screen's `ITEMS: x/150` count (it read 1 with one pickup made, and a granted item did not change it); it is restored from the save on load. It says *that* something was collected, not *which* location. |
| Status | measured |
| Method | `tools/memscan.py snap --module-only` twice while standing still, then a 10 Hz recorder over the module logging bytes that had been stable; the first pickup by snapshots, the second by the recorder |
| Build | B1 |
| Evidence | Snapshots `lab/mem/s30-*` (first pickup; deleted after analysis, 1.1 GB) and `lab/mem/p2-before*`, log `lab/mem/p2-record.log` (second pickup at about 9.1 s), 2026-10-20. |

## Saving copies the live state into the slot array

| Field | |
|---|---|
| Value | When the game saved slot 30, its block in the slot array (`+0x33CAE8`) changed in 46 bytes at once, about 12 s after the pickup, and the slot 30 file changed at the same time. Read as file offsets, those 46 bytes are map exploration (`0x80xx`, `0x8Cxx`), flags near the start (`0x0004` to `0x0006`, `0x001C`, `0x00E3`, `0x43C7`, `0x4589`) that the earlier slot 28 and slot 30 saves also set, and values around `0x9321` to `0x935F` that behave like counters and stats. `0x9321` was 2 after two Scrap pickups, 3 after the first test's pickups, and 0 after slot 28's one normal-item pickup; it may count Scrap or filler pickups. Where the live state itself sits in memory was not found; matching the array's changes against earlier ones gave several equally likely positions. |
| Status | measured (array updated on save, the 46 offsets); assumed (what each offset means) |
| Method | the 10 Hz recorder above; byte comparison of slot 30 saves |
| Build | B1 |
| Evidence | `lab/mem/p2-record.log`; saves `lab/work/slot28-all-scrap.sav` (as loaded), `lab/backups/20261020-215709/` (after two pickups), `lab/backups/20261020-213055/` and `lab/backups/20261020-213828/`, 2026-10-20. |

## The live map grid flips at the moment of pickup

| Field | |
|---|---|
| Value | The live map grid (54 × 54, row-major, same values as the save's grid at file `0x877F`) is at `Zexion.exe+0x3E469D`. With slot 27 loaded it matched that save's grid in all 2,916 cells and showed location #52's cell (25, 25) as `0x19`. At the pickup, that cell became `0x1B` in the same 50 ms sample as the pickup counter at `+0x3D0168` went 0 → 1, and no other cell changed. So a client can detect a collected location by polling this grid at the location cells from the randomizer list. The memory around the grid is not a copy of the rest of the save block (31,215 of 37,896 bytes equal, mostly zeros; the file-`0x4589` flag reads 0 there after the pickup), so the live inventory and flag array are elsewhere. |
| Status | measured (one location, one pickup) |
| Method | search of writable memory for each explored row of slot 27's saved grid, outside the slot array, voting on a grid start; then a 20 Hz watcher on the cell and the counter during the pickup |
| Build | B1 |
| Evidence | Slot 27 loaded from `lab/backups/20261020-221347/`; pickup at 12.45 s into the watcher, 2026-10-20. |

## The live inventory is not in the module's static data

| Field | |
|---|---|
| Value | While holding the Gravity Harness (slot 28, after its first pickup), no copy of the save's owned-items array including entry 24 exists in writable memory, and neither does a bitfield of the owned IDs or an array of 32-bit values per ID. `Zexion.exe+0x3CF78D` holds the save-block layout with the starting items (7, 15, 16, 22) but not item 24, so it looks like a copy taken at load time. The live inventory is probably in a heap object. At this pickup the map cell went `0x18` → `0x1A` about 0.5 s before the pickup and `0x1A` → `0x1B` at it, together with the pickup counter; a client should treat only `0x1B` as collected. |
| Status | measured (the searches and the cell sequence); assumed (heap) |
| Method | 10 Hz module recorder during the pickup; `memscan.py find` for the array, bitfield and 32-bit layouts; reading known near-copies of the save block |
| Build | B1 |
| Evidence | `lab/mem/p3-before2`, `p3-before3`, `p3-record.log`, 2026-10-20. |

## A full-memory diff of one pickup leaves thousands of candidates

| Field | |
|---|---|
| Value | Five full snapshots of writable memory around the Gravity Harness pickup in slot 28 (two before, three after, standing still) left 5,908 bytes that went 0 → 1 at the pickup and stayed: 3,322 and 2,254 in two large heap regions (graphics- or level-sized), 123 in the module, 209 in small heap regions. None of them sits in an array indexed by item ID with the starting items (7, 15, 16, 22) set at the same stride, and no list of the owned IDs exists in 8-, 16- or 32-bit form. Setting 267 of the small-region and module candidates back to 0 at once crashed the game, so narrowing by zeroing must use small groups, or be replaced by a debugger. Saves were not affected. |
| Status | measured |
| Method | `memscan.py snap` (full) five times; candidate filter and stride search by address; a batch write of zeros with read-back |
| Build | B1 |
| Evidence | 2026-10-20. Snapshots `lab/mem/f-*` deleted after the crash made their addresses meaningless. |

## The pickup record holds the major-item list

| Field | |
|---|---|
| Value | The save's major-item list and its length are at a fixed distance from live memory: live = file + `0x3C6E4D`. The fields around them are not (play time, file `0x9356`, is live at `+0x3D0164`), so the save writes this region field by field. At the Gravity Harness pickup, live `+0x3D0180` (file `0x9333`, the first list entry) went 0 → `0xC7` and `+0x3D019A` (file `0x934D`, the list length) went 0 → 1, in the same 0.1 s as the pickup counter at `+0x3D0168` (file `0x931B`). A hardware write breakpoint on slot 28's array entry at file `0x0129` in the slot array never fired during a save, because that byte isn't what records the item (`save-format.md`). Appending a code (`0xC0`) to the live list and raising the length to 2 changed nothing in the inventory, so the list does not grant items. |
| Status | measured |
| Method | 10 Hz module recorder; x64dbg hardware write breakpoint at `Zexion.exe+0x32A3FF` during a save; save comparison |
| Build | B1 |
| Evidence | `lab/mem/p3-record.log`; saves `lab/backups/20261020-223111/` and `20261020-224429/`, 2026-10-20. |

## Live ability flags: writing one grants the item

| Field | |
|---|---|
| Value | The live ability flags (the save's 32 bytes at file `0x9300`) are at `Zexion.exe+0x3ED48B`. With slot 28 loaded and the Gravity Harness owned, they read flags `0x00`, `0x0B` and `0x13`, exactly as saved. Writing 1 to flag `0x04` (`+0x3ED48F`) made the S-Blade appear in the inventory at once, with no pickup, reload or save. The recorder had caught flag `0x0B` (`+0x3ED496`) going 0 → 1 at the Gravity Harness pickup. Whether the granted S-Blade also works in play has not been checked yet. The pause screen's item count stayed at 1, matching the pickup counter at `+0x3D0168`: a granted item does not count as a pickup, and the counter is the `ITEMS: x/150` number, so it counts locations collected. |
| Status | working in game (the item appears in the inventory); not yet checked (the item works when used) |
| Method | search of writable memory for the save's 32 flag bytes (two hits: slot 28 in the slot array, and this one); `memscan.py write` of one byte with read-back; the owner compared the inventory against a baseline |
| Build | B1 |
| Evidence | 2026-10-20, slot 28 loaded from `lab/backups/20261020-224429/`; write logged by `memscan.py` (`0x7ff717cdd48f: 00 -> 01`). |
