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

## Pickup flag: candidate only

| Field | |
|---|---|
| Value | Across one pickup (slot 30, first reachable item, Scrap), comparing two snapshots taken while standing still with one taken after the pickup, about 5,300 bytes in the module changed that had been stable. Pausing right after the pickup opened a menu, so many are UI. In the near-copies of the save block, only one byte moved: `Zexion.exe+0x3D0168`, 0 → 1. Its neighborhood reads as a record of 32-bit fields (`83 31 00 00`, `01`, `03`, `07`, then `01` at `+0x10`), not a per-location flag array, and the matching save-file offset (`0x9DD`, if that copy were aligned like a save block) is 0 in every save, including ones made after this pickup. So it may be transient pickup state (the "item acquired" popup, say), not the collected flag. `+0x3CF78D` is therefore not shown to be laid out like a save block: a mostly-zero block matches another on most bytes by chance. |
| Status | assumed (candidate only) |
| Method | `tools/memscan.py snap` before, before again, and after the pickup; module bytes compared by address |
| Build | B1 |
| Evidence | Snapshots `lab/mem/s30-before`, `s30-before2`, `s30-after`, 2026-10-20. |
