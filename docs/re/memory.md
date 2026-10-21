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
| Value | `Zexion.exe+0x3D0168` counts pickups in the current session: it went 0 → 1 at the first pickup in slot 30 and 1 → 2 at the second, at the moment of collection (within 0.1 s, the sampling interval), with no pause or save involved. Its neighbors read as a record of 32-bit fields. Whether it is the `ITEMS: x/150` counter, and whether it is loaded from the save, is not known. It says *that* something was collected, not *which* location. |
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

## Which location was collected: known in the save, not yet in live memory

| Field | |
|---|---|
| Value | In the save, the map grid at file `0x877F` (54 × 54, row-major) holds `0x1B` at a location's cell once it is collected (`save-format.md`, "The map grid marks collected locations"). The live copy of that grid has not been located in memory yet. Searching memory for a distinctive run of the grid while playing should find it, since save data has the same layout in memory (above). `Zexion.exe+0x3D0168` is only a pickup counter. |
| Status | measured (in the save); not yet located (in memory) |
| Method | fork test, see `save-format.md` |
| Build | B1 |
| Evidence | 2026-10-20. |
