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
  - `Files/Enemies`: `8e41d2ef5665ceae` (2,686,584 bytes)\*
  - `Files/Flags`: `e9882b596fc69a59` (8 bytes)
  - `Files/Text`: `3bf8cd5e07ede18f` (114,050 bytes)
  - `Files/Bosses`: `265feab61b4aa2cf` (1,176,558 bytes)\*
  - `Files/Missiles`: `4ea5d6ccb2830d21` (473,364 bytes)\*\*

\* Fingerprinted on 2026-10-22, from the same install.
\*\* Fingerprinted on 2026-11-02, from the copy `tools/game_data.py` made of the same install that day.

## Engine and mod support (B1)

| Field | |
|---|---|
| Value | A native x64 exe with ASLR (DynamicBase and HighEntropyVA), so addresses change on every run. It ships SDL2, SDL2_image, FMOD, FreeType, GLEW, zlib and the Steam API as DLLs beside it, and no Unity, GameMaker or Godot runtime. The only mod folders are `Mods/{Local,Workshop}/{Sprites,Translations}`, and B1 does not read the sprite ones (`memory.md`, "Other players' items"); whether it reads translation mods is not measured. That the engine is custom C++ built with MSVC, with no scripting language, is assumed (the owner's inspection of the exe; no evidence recorded). |
| Status | measured (machine, PE flags, DLLs, mod folder names; sprite mods not read, from disassembly); assumed (custom MSVC C++, no scripting) |
| Method | `tools/build_id.py` (PE header, DLL list); directory listing of the save folder's `Mods/`; disassembly for the sprite paths (`memory.md`) |
| Build | B1 |
| Evidence | `build_id.py` and `ls` of the install and save folders, 2026-10-20. |

## B1 on Linux

Measured 2026-11-18 with `tools/build_id.py`, on a Linux PC's Steam install (Flatpak Steam,
`~/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/Zexion`), the same
Steam build as B1. Zexion on Steam ships a Linux build of its own, which Steam installs on Linux
unless Proton is forced (Steam's default mapping read on the test PC, assumed elsewhere: the
Status below; `../DEVELOPING.md`, "Platforms").

- Zexion (Linux) SHA-256: `cd64cd9ec469b4f1c006ba381b5b1223fd9b47b51b8c8308adc5520d112670e7`
- Size: 3,156,984 bytes
- GNU build id: `4c4416eed9e8a1fa1b373ae3b1badec33510fc90`
- Machine: x86-64; type: EXEC (fixed addresses); symbol table: kept
- Libraries beside the binary: libGLEW.so.2.1, libSDL2-2.0.so.0, libSDL2_image-2.0.so.0,
  libfmod.so.13, libfreetype.so.6.20.2, libsteam_api.so, libz.so.1.3.1
- Steam build id: 23943567
- Steam depots installed: 3392511 (the data files) and 3392513 (the binary and its libraries),
  from the app manifest. The Windows exe's depot was not installed.
- Data files: all nine (`RandoCells`, `MapData`, `Objects`, `AreaEvents`, `Enemies`, `Flags`,
  `Text`, `Bosses`, `Missiles`) have B1's fingerprints above (the first 16 hex digits of their
  SHA-256).

| Field | |
|---|---|
| Value | **The same game as B1, built for Linux.** The data files are B1's (their fingerprints above), from depot 3392511 (that the Windows install takes the same depot is assumed: its depots were not read), and Steam's build id is B1's. The binary is a position-dependent executable (ELF `ET_EXEC`): its four `LOAD` segments go at their link addresses from `0x400000`, so the game's globals are at the same address on every run, with no ASLR for them (the libraries and the heap still move): in the game, its `.rodata` read back at its link address (`linux.md`, "Read from the running game"). It keeps its symbol table (5,480 `.symtab` symbols, 450 dynamic): the game's globals and functions have their source names (`gGameState`, `gSaveGames`, `gCurrentRando`). Built by GCC 9.4.0 (Ubuntu 20.04's, from `.comment`). It finds its libraries beside itself (`RUNPATH` `$ORIGIN`) and imports SDL2's `SDL_GL_SwapWindow` and `SDL_GetPrefPath` by name. Its save folder holds `Save Game N.sav` files of the Windows build's sizes, read by the Windows build's layout (`tools/save_lab.py slots`). |
| | **Sizes that agree with what was measured on Windows** (by symbol size; each correspondence since shown by disassembly, `linux.md`, "Lookup: targets"): `gSaveGames` is 30 × 37,896 bytes, and 37,896 (`0x9408`) is the Windows slot block (`memory.md`, "Save data lives in Zexion.exe's static data"); `gRandoScreenCells` is 247,860 bytes, `Files/RandoCells`' size; `gSavedRandos` is 30 × 2,312 bytes, `gCurrentRando`'s size; `gCurrentSaveGame` is 4 bytes. |
| | **The runtime Steam runs it in.** On the test PC (Flatpak Steam), Steam's `logs/compat_log.txt` maps app 3392510 to `SteamLinuxRuntime_sniper` (Steam Linux Runtime 3.0) at priority 90, as a "non-user mapping": Steam's own choice, not one made in the game's Properties, so there Steam runs this build in sniper by default, not under Proton. Sniper's glibc is 2.31, which a newer host glibc may stand in for (assumed for native Steam and the Steam Deck; under Flatpak's Steam the game ran on the Flatpak runtime's 2.44): `native/README.md`, "Its own runtime", where a release's Linux helper is built for 2.31: CI's build of it loaded and ran in the game on the test PC's Flatpak Steam, on the Flatpak runtime's glibc (2026-11-20). |
| Status | measured (fingerprint, ELF type and segments, symbols, compiler string, imports, data files equal to B1's, the save folder's slots, read with `tools/save_lab.py slots`, 2026-11-18; the default runtime mapping, on the test PC's Flatpak Steam, 2026-11-20); measured in the game (that it runs at its link addresses, 2026-11-18); assumed (that the Linux build's state matches the Windows build's layout in the fields `linux.md` does not name: those it names were checked one by one; that Steam runs this build by default with native Steam and on a Steam Deck too, from the store's platform list and a Deck's Properties showing "Steam Linux Runtime 3.0", 2026-11-16, not recorded here) |
| Method | `tools/build_id.py` on the install; `readelf -h -l -d -p .comment` and `nm` on a copy in `lab/linux-b1/`; `sha256sum` of the data files; the app manifest's `InstalledDepots`; `tools/save_lab.py slots`; Steam's `logs/compat_log.txt` (the runtime mapping) |
| Build | B1 (Linux) |
| Evidence | Output read on screen, 2026-11-18. The save folder: `~/.var/app/com.valvesoftware.Steam/.local/share/Gallant Leaf/Zexion` (Flatpak Steam keeps its home there), with slots 1 to 30 synced by Steam Cloud from the Windows PC's saves. The runtime mapping: read from Steam's `logs/compat_log.txt` on the test PC by the research agent, 2026-11-20 (not kept). No evidence is recorded for the native Steam and Steam Deck claim in the Status: the store's platform list and the reading of a Deck's Properties (2026-11-16) were not saved, so it stays assumed. |

## Older builds

Before the experiments of 2026-10-20, the test machine's save files were written by older
builds: slots 3 to 30 on 2025-12-19, slots 1 and 2 on 2026-06-13. Which builds is not known.
Slots 27 to 30 have since held lab saves written by B1.
