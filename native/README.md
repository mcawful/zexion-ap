# The native helper

`zexion_ap.dll` runs inside Zexion and gives the client what can't be done from outside: work
on the game's own thread, between frames. `tools/helper.py` loads it through
`client/native.py`; the AP client does not use it yet.

**Status:** helper phase 1, the skeleton. **Working in game** (build B1, 2026-10-22): loaded
into the running game with slot 7 in play, it reported `build=B1 hooked=1`, counted 166 frame
calls a second on a 165 Hz monitor, unloaded with the game still in play, loaded again and
unloaded again; the owner noticed no stutter or hitch. Before that, in test processes:

- A stand-in Python process (`tests/test_native.py`): it loads, answers on its pipe and
  unloads, and hooks nothing there, since that process is no known build.
- A test build, `zexion_ap_hooktest.dll`, that hooks the loading program's own `GetTickCount`
  import (`tests/unload_test.cpp`; it refuses to hook anything in a known game build): it
  hooks, counts calls, and survives 300 loads and unloads in CI (3,000 locally) while two
  threads call the hooked function nonstop.

It changes nothing in the game's state yet: it only counts frames.

## How it works

- **Getting in.** `client/native.py` loads it with `LoadLibraryW` from a remote thread, with a
  timeout and under a named mutex, so two loaders can't race. Nothing is written to the
  game's files.
- **Which game.** It reads the game's PE header and hooks nothing unless the build is one it
  knows (`src/build.cpp`, kept in step with `KNOWN_BUILDS` in `client/patterns.py`).
- **The frame call.** The game imports `SDL_GL_SwapWindow` by name and calls it only through
  its import-table slot, from code its main loop runs, once per frame (166 calls a second on
  a 165 Hz monitor), on the game's main thread (inferred: the game starts no threads of its
  own); `docs/re/memory.md`, "The per-frame call".
  - The helper finds that slot by name (`src/iat.cpp`).
  - It points the slot at a small trampoline: a few bytes of code in memory that is never
    freed. The trampoline counts the call, runs the helper's frame function, which then calls
    SDL as before, and uncounts it. It is registered with unwind information, so an exception
    or a stack walk passes through it as through any compiled function (a native test raises
    one behind it and catches it in front).
  - It hooks only a slot that still points at SDL.
- **The pipe.** `\\.\pipe\zexion-ap-<pid>`, message mode: one UTF-8 request, one reply, one
  client at a time (a second waits, and `client/native.py` reports the pipe busy).
  - The helper creates it as the first and only pipe by that name, refusing to start if
    another process holds it. Only the current user can open it, and only locally.
  - The client opens it at the identification level only, so the server can't act as the
    user, and checks that the process serving it is the game.
  - Requests longer than 64 KB drop the connection.
  - Requests:
    - `hello` → `zexion-ap <version> build=<B1|unknown> hooked=<0|1>`
    - `ping` → `pong frames=<calls since load>`
    - `unload` → `bye` (see below).
    - Anything else → `error unknown request`. Errors always start with `error `.
- **Unloading.** `unload` points the slot back at SDL, and only if it still holds the
  trampoline; otherwise it replies `error the frame hook was replaced by something else; not
  unloading` and stays loaded. It then points the trampoline at SDL too, waits until no call is
  counted inside, and unloads the DLL. A call can then be in the trampoline (which is never
  freed) or in SDL, but not in the helper, so a rebuilt helper can be loaded without
  restarting the game. If the pipe cannot be created, or breaks, the helper unhooks and
  unloads itself the same way.
- **Errors.** No exception leaves the helper's thread; on one, the helper stays loaded, its
  hook still working, and stops answering.

## Building

Visual Studio 2022 or later with the C++ workload, and CMake 3.20 or later. From the repo root:

```sh
cmake -S native -B native/build -A x64
cmake --build native/build --config Release
ctest --test-dir native/build -C Release --output-on-failure
```

The DLL lands in `native/build/Release/zexion_ap.dll` (gitignored). The C runtime is linked
statically, so it needs nothing the game doesn't already load. CI builds it, runs the ctest
tests (`parts`, and `unload_under_load` with 300 rounds), and runs `tests/test_native.py`
against the build on Windows.

## Planned

1. **Helper phase 2:** grants applied in the frame function, and the game's own popup, sound
   and HUD refresh for received items. The frame function must never wait on anything the pipe
   thread can hold while that thread waits for a client.
2. **Helper phase 3:** a new game from the AP seed, written in after the game's generator runs at
   START GAME, replacing `/prepare`.
3. **Helper phase 4:** icons for other games' items.
