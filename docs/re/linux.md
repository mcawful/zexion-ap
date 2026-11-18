# The Linux build

Zexion's Linux binary (`builds.md`, "B1 on Linux") against what `memory.md` records for the
Windows exe. The Linux binary keeps its symbol table and loads at fixed addresses, so a global
is found by its name and a field by its offset from that name; the Windows exe has neither, so
its targets were found by byte pattern. Every address on this page is a link address of the
Linux binary, which is where it is expected at run time (it is `ET_EXEC`; not yet read from a
run).

The Linux targets are being mapped in stages. Nothing here has been read from the running Linux
game yet: every fact is from the binary on disk, except "Reading the running game", which is
from the test PC and a stand-in game. The disassembly is GNU `objdump -d -C
-M intel` of a copy in `lab/linux-b1/` (`all.asm`), which names each rip-relative operand as
`symbol+offset`.

## Lookup: targets

Each Windows target that has a Linux counterpart so far, where it is on Linux, and how sure.
"Shift" means the Windows address less `0x3CF6A0`, plus `gGameState`'s address ("`gGameState`
has the Windows layout"); "refs" counts the instructions in `all.asm` whose operand is exactly
that `gGameState+offset`.

| Target | Windows (B1) | Linux (B1) | How | Status |
|---|---|---|---|---|
| `pickup_counter` | `+0x3D0168` | `0x9E8308` (`gGameState+0xAC8`) | `LoadGame` stores the save's `0x935A` there | measured |
| `pickup_list` | `+0x3D0180` | `0x9E8320` (`gGameState+0xAE0`) | `LoadGame` copies the save's `0x9333` list there | measured |
| `rando_marker` | `+0x3E2DD8` | `0x9FAF78` (`gGameState+0x13738`) | `LoadGame` stores the save's `0x9402` there | measured |
| `map_grid` | `+0x3E469D` | `0x9FC83D` (`gGameState+0x14FFD`) | `LoadGame` copies the save's `0x877F` grid there | measured |
| `save_loaded` | `+0x3D016A` | `0x9E830A` (`gGameState+0xACA`) | shift; 28 refs | assumed |
| `flags` | `+0x3CF78D` | `0x9E792D` (`gGameState+0xED`) | shift; 76 refs, `LoadGame` among them | assumed |
| `frame_clock` | `+0x3CF750` | `0x9E78F0` (`gGameState+0xB0`) | shift; 55 refs | assumed |
| `cell_state` | `+0x3E5201` | `0x9FD3A1` (`gGameState+0x15B61`) | shift; 79 refs | assumed |
| `last_message` | `+0x3E29F0` | `0x9FAB90` (`gGameState+0x13350`) | shift; 21 refs | assumed |
| `fade` | `+0x3E2AE8` | `0x9FAC88` (`gGameState+0x13448`) | shift; 60 refs | assumed |
| `fade_level` | `+0x3E2F43` | `0x9FB0E3` (`gGameState+0x138A3`) | shift; 56 refs | assumed |
| `ailments` | `+0x3E2DC4` | `0x9FAF64` (`gGameState+0x13724`) | shift; 10 refs | assumed |
| `inventory` | `+0x3ED47E` | `*gMainCharacterSpriteP + 0x108E` | `LoadGame` copies the save's `0x92F3` bytes there | measured (the offset); assumed (where the pointer points) |
| `current_slot` | `+0xCDD080` | `gCurrentSaveGame` (`0x14945A0`, 4 bytes) | by name; `LoadGame` indexes `gSavedRandos` by it | assumed |
| `slot_array` | `+0x230600` | `gSaveGames` (`0x14945C0`, 30 × `0x9408`) | by name and size | assumed |
| `rando_slots` | `+0x347830` | `gSavedRandos` (`0x16BF7C0`, 30 × `0x908`) | by name and size; `LoadGame` copies one into `gCurrentRando` | assumed |
| `rando_blob` | `+0xCDD6D0` | `gCurrentRando` (`0x12F6E00`, `0x908` bytes) | by name; filled from `gSavedRandos` by `LoadGame` | assumed |

The other Windows targets (`client/patterns.py`, and the helper's in `native/src/`) are not
mapped yet. The functions the helper calls have names in the Linux binary (`LoadGame`,
`createGameMessage`, `AssistMenu`, `EquipmentMenu`, ...), but which one each Windows target is
must be shown, not guessed from the name.

## The save record is the file less its first two bytes

| Field | |
|---|---|
| Value | `LoadGame(SaveGameRec*)` (`0x560EE5`) reads the record at the file's offsets less 2, as the Windows build's memory holds a slot's block (`memory.md`, "Save data lives in Zexion.exe's static data"): it tests the word at record `+0x9400` for 1 (the file's randomizer marker, `0x9402`), reads the ITEMS counter at record `+0x9358` (file `0x935A`), the continue point at `+0x935A` and `+0x935C` (file `0x935C`, `0x935E`), 26 major-item bytes from `+0x9331` (file `0x9333`), 64 inventory bytes from `+0x92F1` (file `0x92F3`) and the map grid from `+0x877D` (file `0x877F`), each a field `save-format.md` records at that file offset. A `SaveGameRec` is `0x9408` bytes: `gSaveGames` (30 of them), `gCheckpointSaves` (30) and `gTeleportSave` (one) are its multiples. |
| Status | measured (by disassembly, 2026-11-18) |
| Method | `LoadGame`'s listing in `all.asm`, its loads from the record (`rdi`, kept at `[rbp-0x38]`) read on screen against `save-format.md`'s lookup table; the arrays' sizes from `nm -S` |
| Build | B1 (Linux) |
| Evidence | `lab/linux-b1/all.asm`, `LoadGame(SaveGameRec*)` from `0x560EE5`, 2026-11-18. |

## `gGameState` has the Windows layout

| Field | |
|---|---|
| Value | `gGameState` (`0x9E7840`, `0x8CCBF8` bytes, in `.bss`) holds the state the Windows build keeps from `+0x3CF6A0` of its image, offset for offset, at least for four fields. `LoadGame` stores the save's ITEMS counter at `gGameState+0xAC8`, its major-item list at `+0xAE0`, its randomizer marker at `+0x13738` and its map grid at `+0x14FFD`; the Windows build keeps the same four at `+0x3D0168` (`pickup_counter`), `+0x3D0180` (`pickup_list`), `+0x3E2DD8` (`rando_marker`) and `+0x3E469D` (`map_grid`), and each Windows address less its Linux offset is `0x3CF6A0`. So for any Windows target inside that range, the Linux address is assumed to be the Windows address less `0x3CF6A0` plus `0x9E7840`, until each is checked (the Lookup's "shift"). |
| | **Where it may not hold.** A field whose type is another size on Linux would move everything after it: `wchar_t` is 4 bytes on Linux and 2 on Windows, and `long` is 8 and 4. The four agreeing fields span `+0xAC8` to `+0x14FFD`; past that, and in other structs, the layout is unchecked. The translation code takes `wchar_t` strings (`createTranslationGameMessage(wchar_t const*, ...)`), so its structs, and `memory.md`'s translation tables, are expected to differ. |
| | **The player state.** `LoadGame` copies the 64 inventory bytes to `*gMainCharacterSpriteP + 0x108E + ID`, the offset `memory.md` records for the Windows player state ("Live inventory: writing an item's byte grants it", the game reading `0x108E + ID`). The Windows player state is at `+0x3EC3F0`, which would be `gGameState+0x1CD50` (`0xA04590`) here; that the pointer holds that address is not checked: it is set at run time. |
| Status | measured (the four offsets, by disassembly, 2026-11-18); assumed (the rest of the range, by the shift) |
| Method | `LoadGame`'s stores in `all.asm` read on screen, against `memory.md`'s Lookup table; the reference counts by `grep -c '<gGameState+0x...>'` over `all.asm` |
| Build | B1 (Linux), against B1 (Windows) as `memory.md` records it |
| Evidence | `lab/linux-b1/all.asm`, 2026-11-18: `mov WORD PTR [rip+...],ax # 9e8308 <gGameState+0xac8>` after `movzx eax,WORD PTR [rax+0x9358]`; the list loop to `<gGameState+0xae0>` from `[rdx+rax*1+0x9331]`; `# 9faf78 <gGameState+0x13738>` after `[rax+0x9400]`; the grid loop to `<gGameState+0x14ffd>` from `+0x877d`. |

## The per-frame call: SDL_GL_SwapWindow through the PLT

| Field | |
|---|---|
| Value | The binary imports SDL2's `SDL_GL_SwapWindow` by name (undefined in `.dynsym`) and calls it only through its PLT entry (`0x40C120`, a jump through the GOT slot `0x6BB700`), from two places: `SWHardwareEnd(SpriteWorldRec*)` (`0x4CAF9A`) and `UpdatePalette()` (`0x652552`). So a preloaded library that defines `SDL_GL_SwapWindow` is called in SDL's place from both (the dynamic linker binds the slot to the first definition, the preloaded one's): the Linux helper's frame call (`native/README.md`, "The Linux helper"). Which of the two runs once a frame, as the Windows build's one call does (`memory.md`, "The per-frame call"), is not checked. |
| Status | measured (the import and the two call sites, by disassembly, 2026-11-18); assumed (once a frame; the binding to a preloaded definition, by the dynamic linker's rules, not run in the game) |
| Method | `nm -D` on the copy; `grep 'SDL_GL_SwapWindow' all.asm`, each call's function by the nearest symbol above it |
| Build | B1 (Linux) |
| Evidence | `lab/linux-b1/all.asm`, 2026-11-18: `call 40c120 <SDL_GL_SwapWindow@plt>` at lines for `0x4caf9a` and `0x652552`, and no other reference. |

## Reading the running game

| Field | |
|---|---|
| Value | **The client can't read the game itself.** Reading another process's memory on Linux (`/proc/<pid>/mem`, `process_vm_readv`) takes the kernel's ptrace permission. With Yama's `kernel.yama.ptrace_scope` at 1, the default of many distributions, a process may read only its own descendants, and the game, started by Steam, is not the client's. The test PC has the scope at 1: there a process that was not a stand-in game's ancestor was refused its `/proc/<pid>/mem` (`EACCES` on opening it). So the Linux helper reads the game's memory for the client, from inside it (`read`: `native/README.md`, "The Linux helper"), and opens it to no other process, whatever the scope. |
| | **Superseded: opening ptrace while a client is connected.** The helper's first design called `prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY)` while a client was connected and `PR_SET_PTRACER` 0 when it left. With the stand-in game, a process not its ancestor was refused its memory before a client connected, read it while one was, and was refused again after. It was replaced the same day, after review: the window let every process of the user in, not just the client, and by the kernel's rules (assumed: `/proc/<pid>/mem`'s permission is checked when it is opened, and an attach outlives the window) what one opened in the window stayed open after it. |
| | **Pids.** Flatpak's Steam is assumed to run the game in its own process-id namespace, so that its pid inside differs from the one in the host's `/proc` (both are in `/proc/<pid>/status`, `NSpid`; not seen with the game). |
| Status | measured (the test PC's ptrace scope, and the refusal, with the stand-in game, 2026-11-18); superseded (the first design's window, measured with the stand-in game the same day); assumed (the kernel's rules behind the window's flaw; the pid namespace) |
| Method | `cat /proc/sys/kernel/yama/ptrace_scope`; `tests/test_native_linux.py`, `test_the_games_memory_is_never_opened_to_other_processes` (and, for the first design, the test it replaced, `test_memory_is_readable_only_while_a_client_is_connected`); a by-hand read of `/proc/<pid>/mem` from a new session (`setsid`), which printed `errno 13 Permission denied` |
| Build | any (the stand-in game, not Zexion) |
| Evidence | The scope read `1`, 2026-11-18; the tests passed on the test PC the same day. |
