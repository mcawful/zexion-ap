# The native helper

`zexion_ap.dll` runs inside Zexion and gives the client what can't be done from outside: work
on the game's own thread, between frames. The AP client loads it when it attaches to the game
(`client/native.py`, unless started with `--no-helper`) and sends received items through it;
`tools/helper.py` loads and unloads it for tests.

**Status:** helper phase 2, grants with the game's popup, sound and HUD refresh, is **working in
game** (build B1, 2026-10-22). The AP client loaded it into the running game (slot 7) and
sent items from a local server:
- the popup, the item's sound and an immediate HUD update for a Missile Box;
- the bomb icon for a Mega Bomb;
- the suit's color for Magnus Armor;
- three popups in turn for three items at once;
- after a death, every item given back with no popup repeated (`docs/re/memory.md`, "Game
  functions the helper calls").

Not run in the game yet: the client falling back to direct writes when the helper fails, and
a pickup while a batch arrives. Helper phase 1 is also **working in game** (build B1,
2026-10-22): loaded
into the running game with slot 7 in play, it reported `build=B1 hooked=1`, counted 166 frame
calls a second on a 165 Hz monitor, unloaded with the game still in play, loaded again and
unloaded again; the owner noticed no stutter or hitch. Before that, in test processes:

- A stand-in Python process (`tests/test_native.py`): it loads, answers on its pipe and
  unloads, and hooks nothing there, since that process is no known build.
- A test build, `zexion_ap_hooktest.dll`, that hooks the loading program's own `GetTickCount`
  import (`tests/unload_test.cpp`; it refuses to hook anything in a known game build): it
  hooks, counts calls, and survives 300 loads and unloads in CI (3,000 locally) while two
  threads call the hooked function nonstop.

Phase 2's native tests run against a stand-in game: the test program's own data and functions,
laid out as the helper expects the game's (`tests/stand_in.h`), configured by RVA like the real
game. `tests/game_tests.cpp` covers the request parsers, configuring, applying and the popup
queue; `tests/apply_test.cpp` goes through the real pipe and hook in a busy host.

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
    - `hello` → `zexion-ap <version> build=<B1|unknown> hooked=<0|1> configured=<0|1>`
    - `ping` → `pong frames=<calls since load>`
    - `config name=rva …` → `ok`: the addresses the client resolved by byte pattern
      (`client/patterns.py`; the names are `CONFIG_KEYS` in `client/native.py`). Each must lie
      inside the game's image, functions in its code and data outside it, or the whole
      configuration is refused.
    - `apply` and then lines, one batch → `applied`, or `refused <why>` with nothing written:
      - `cas ITEM EXPECTED VALUE`, an inventory byte from 0 to 49;
      - `refresh`;
      - `popup ITEM SUFFIX`.
    - `unload` → `bye` (see below).
    - Anything else → `error unknown request`. Errors always start with `error `.
- **Grants, in the frame call.** `config` and `apply` are handed to the game's thread through
  a one-slot mailbox (`src/mailbox.cpp`) and run at its next frame call. If no frame takes one
  within a second (the game paused in the background, say), it is withdrawn, the reply is
  `refused no frame`, and it is never run afterwards. An apply (`src/game_calls.cpp`):
  1. Refuses unless the game is in play: a save loaded, no menu, energy above 0.
  2. Checks every `cas`, and writes all of them or, if any byte differs, none.
  3. Sets max and current missiles and bombs from the counts, and on a new Energy Tank sets max
     and current energy, as the pickup routine does.
  4. Refreshes the HUD the way the game does after loading a save. This updates the missile
     number, shows the Mega Bomb icon and redraws the energy tanks, and redraws the weapon
     strip only while it shows.
  5. Sets the suit's color if Magnus Armor was just gained.
  6. Queues the popups.

  It leaves out what only a real pickup should do: the map cell, the pickup counter, the
  major-item list, achievements, switching to a new weapon, and a key's area event (which only
  hides the key's map marker; the gates read the key bytes: `docs/re/memory.md`, "Keys, the
  gates and the key markers").
- **Popups.** One at a time, in a frame call, once the game's last popup has ended:
  - The text is the item's icon and name from the game's own item record, then the client's
    suffix (`FROM ALICE`), with any character the game's font does not draw left out.
  - Each popup plays the item's own pickup sound at the player.
  - A popup the game cannot take yet (all four message slots busy) stays queued.
  - Nothing is shown outside play: popups wait. At least 20 frames separate two popups. A
    popup whose item has no record (the records not loaded yet) is dropped.
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
tests (`parts`, `game`, `unload_under_load` with 300 rounds, and `apply_in_frames`), and runs
`tests/test_native.py` against the build on Windows.

## Planned

1. **Helper phase 2, to finish:** seeing a gate open for a received key in the game (from the
   code it should; `docs/re/memory.md`, "Keys, the gates and the key markers").
2. **Helper phase 3:** a new game from the AP seed, written in after the game's generator runs at
   START GAME, replacing `/prepare`.
3. **Helper phase 4:** icons for other games' items.

The frame call never waits on anything the pipe thread can hold. It takes the mailbox's work
with one atomic exchange, and only one thread at a time does the work.
