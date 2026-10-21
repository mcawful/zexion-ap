# Game builds

Every fact in `docs/re/` names one of these. Fingerprints come from
`.venv/Scripts/python tools/build_id.py`, which only reads the install.

## B1

Measured 2026-10-20 with `tools/build_id.py`, on the Steam install that was current that day.

- Zexion.exe SHA-256: `6d8a94bd4d641ee2125b4f966b099e2d58530ddfb20531d1d619fc54edde469b`
- Size: 2,474,496 bytes
- PE timestamp: 2026-06-27 07:59:26 UTC
- Machine: x64; DllCharacteristics: HighEntropyVA, DynamicBase (ASLR), NX
- DLLs beside the exe: SDL2.dll, SDL2_image.dll, fmod.dll, freetype.dll, glew32.dll, steam_api64.dll, zlib.dll
- Steam build id: 23943567
- Data files (SHA-256, first 16 hex):
  - `Files/RandoCells`: `c7c841fbd8378ed4` (247,860 bytes)
  - `Files/MapData`: `79a0510f4dbbee0f` (4,590,805 bytes)
  - `Files/Objects`: `72ea406ad4ff9b37` (6,128 bytes)
  - `Files/AreaEvents`: `bb8946fd16dd10ad` (152,802 bytes)
  - `Files/Flags`: `e9882b596fc69a59` (8 bytes)
  - `Files/Text`: `3bf8cd5e07ede18f` (114,050 bytes)

## Engine and mod support (B1)

| Field | |
|---|---|
| Value | A native x64 exe with ASLR (DynamicBase and HighEntropyVA), so addresses change on every run. It ships SDL2, SDL2_image, FMOD, FreeType, GLEW, zlib and the Steam API as DLLs beside it, and no Unity, GameMaker or Godot runtime. The only mod folders are `Mods/{Local,Workshop}/{Sprites,Translations}`. That the engine is custom C++ built with MSVC, and that it has no scripting language, is assumed from the owner's inspection of the exe and not recorded here. |
| Status | measured (machine, PE flags, DLLs, mod folder names); assumed (custom MSVC C++, no scripting) |
| Method | `tools/build_id.py` (PE header, DLL list); directory listing of the save folder's `Mods/` |
| Build | B1 |
| Evidence | `build_id.py` and `ls` of the install and save folders, 2026-10-20. |

## Older builds

Save files on the test machine were written before B1: slots 3 to 30 on 2025-12-19, slots 1 and
2 on 2026-06-13. Which builds wrote them is not known.
