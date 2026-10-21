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
