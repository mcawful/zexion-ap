# The native helper

`zexion_ap.dll` runs inside Zexion. The AP client (or `tools/helper.py`, for tests) loads it
into the running game, and it gives the client what can't be done from outside: work on the
game's own thread, between frames.

**Status:** phase 1, the skeleton. It loads, hooks the frame call, counts frames, answers on
its pipe and unloads. It has been loaded into a stand-in process (`tests/test_native.py`), not
yet into the game. It changes nothing in the game's state.

## How it works

- **Getting in.** The client loads it with `LoadLibraryW` from a remote thread
  (`client/native.py`, through pymem). Nothing is written to the game's files.
- **Which game.** It reads the game's PE header and hooks nothing unless the build is one it
  knows (`src/build.cpp`, kept in step with `KNOWN_BUILDS` in `client/patterns.py`).
- **The frame call.** The game calls `SDL_GL_SwapWindow` through its import table once per
  frame (from disassembly; `private/` research, to be measured live). The helper replaces that
  one import-table slot (`src/iat.cpp`), so its code runs on the game's thread each frame, then
  calls SDL as before. It hooks only a slot that still points at SDL, and puts it back only if
  it still points at the helper.
- **The pipe.** `\\.\pipe\zexion-ap-<pid>`, local only, message mode: one UTF-8 request, one
  reply. Requests:
  - `hello` → `zexion-ap <version> build=<B1|unknown> hooked=<0|1>`
  - `ping` → `pong frames=<frames since load>`
  - `unload` → `bye`, after putting the import slot back; the DLL then waits until no frame is
    inside the hook and unloads itself, so a rebuilt helper can be loaded without restarting
    the game.
  - Anything else → `error unknown request`. Errors always start with `error `.

## Building

Visual Studio 2022 or later with the C++ workload, and CMake 3.20 or later. From the repo root:

```sh
cmake -S native -B native/build -A x64
cmake --build native/build --config Release
ctest --test-dir native/build -C Release --output-on-failure
```

The DLL lands in `native/build/Release/zexion_ap.dll` (gitignored). The C runtime is linked
statically, so it needs nothing the game doesn't already load. CI builds it and runs the native
tests and the stand-in load test on Windows.

## Planned

1. **Phase 2:** grants applied in the frame call, and the game's own popup, sound and HUD
   refresh for received items.
2. **Phase 3:** a new game from the AP seed, written in after the game's generator runs at
   START GAME, replacing `/prepare`.
3. **Phase 4:** icons for other games' items.
