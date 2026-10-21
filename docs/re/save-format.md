# Save format

The save folder is `%APPDATA%\Gallant Leaf\Zexion`. Never write there except through
`tools/save_lab.py` (see `/save-lab`).

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
| Value | 75,794 bytes for slot 1 and for every unused slot; 78,108 bytes (2,314 more) for slot 2. That slot 2 is a randomizer save, and that the extra bytes hold its item placement, is an assumption from the owner's account and has not been checked. |
| Status | measured (sizes); assumed (what slot 2 is) |
| Method | file inspection |
| Build | B1 installed; files written by older builds |
| Evidence | `ls -la` and `save_lab.py slots`, 2026-10-20. |

## Unused slots hold a fixed template

| Field | |
|---|---|
| Value | Slots 3 to 30, all unused, have identical content (SHA-256 prefix `8c9868d64c45`), which is not all zeros: `E8 03`, zeros to `0x7BAD`, `0xFF` from `0x7BAE` to `0x877E` inclusive (3,025 bytes), zeros, one `0xFF` at `0x9360`, then 8 records of 14 bytes from `0x9364` to `0x93D3`, each `FF FF FF FF 00 00 00 00 30 00 02 00 00 00`, then zeros to the end. What the regions mean is not known. `save_lab.py` treats a slot as empty when its content is the one at least 3 slots share. |
| Status | measured |
| Method | file inspection: `xxd -a` and a byte-value histogram of slot 3; `save_lab.py slots` for the matching hashes of slots 3 to 30 |
| Build | B1 installed; files written 2025-12-19 by an older build |
| Evidence | 2026-10-20. Histogram of slot 3: 72,718 × `00`, 3,058 × `FF`, 8 × `30`, 8 × `02`, one each of `E8` and `03`. |

## A save is a header and two copies of one block

| Field | |
|---|---|
| Value | A 75,794-byte save is `E8 03` followed by two 37,896-byte blocks: `0x0002` to `0x9409` and `0x940A` to `0x12811`. In slots 1 and 2 the same structures appear in both blocks, `0x9408` bytes apart (FF runs at `0x7BBA` and `0x10FC2`, the 14-byte records at `0x9388` and `0x12790`). In the unused-slot template the second block is all zeros. Why there are two copies (a backup, or two states) is not known. |
| Status | measured (layout); assumed nothing about purpose |
| Method | file inspection of backup copies: positions of FF runs of 16+ bytes and of the bytes `30 00 02 00`, in slots 1, 2 and 3 |
| Build | B1 installed; files written by older builds |
| Evidence | Script over `lab/backups/20261020-211239/`, 2026-10-20. |

## Randomizer data is appended after the two blocks

| Field | |
|---|---|
| Value | Slot 2's extra 2,314 bytes are appended at `0x12812`, after both blocks, which sit at the same offsets as in slot 1. The appended part starts with its own `E8 03`, then:<br>• `0x12814`: 154 little-endian u16 pairs, all distinct, sorted by the second value then the first, both in 0 to 53; then 4 zero bytes.<br>• `0x12A80`: 154 bytes, each 0 to 50; then 2 zero bytes.<br>• `0x12B1C`: 8 bytes, `da 76 2e 8f ab 2b 1d e3`.<br>• `0x12B24` onward: more u16 data, not yet examined, up to `0x130F8`, then zeros to the end.<br>Assumed meanings: the pairs are the (x, y) map cells of item locations, the bytes are the item placed at each (IDs 0 to 50 from `ItemNames.txt`; the most common are 0, 1, 2, 36 and 46), and the 8 bytes are the seed. None of these meanings has been tested. |
| Status | measured (layout, counts, value ranges); assumed (meanings) |
| Method | file inspection and a parsing script over the backup copy of slot 2 |
| Build | B1 installed; slot 2 written 2026-06-13 by an older build |
| Evidence | `lab/backups/20261020-211239/Save Game 2.sav`, 2026-10-20. Byte-value counts over the 154 bytes: 35 × 0, 29 × 1, 18 × 2, 17 × 36, 17 × 46, 6 × 49. |
