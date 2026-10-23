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

In a second session (2026-10-22): a pickup while a batch arrived, every count exact; the
helper unloaded mid-session, items going in by direct write and the client loading it again
24 s later; and keys received at a gate, which opened (`docs/re/memory.md`, "The client in
a live game"). Helper phase 1 is also **working in game** (build B1,
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

Helper phase 3, a new game from the AP seed, is **working in game** (build B1, 2026-10-22):
from RANDOMIZER with the helper writing the seed in (Standard, Easy and OHKO), and from
ARCHIPELAGO (Standard and Easy), which also showed again after backing out of two slots and gave way to
RANDOMIZER when the client disconnected ("New games", below).

The native tests run against a stand-in game: the test program's own data and functions,
laid out as the helper expects the game's (`tests/stand_in.h`), configured by RVA like the real
game. `tests/game_tests.cpp` covers the request parsers, configuring, applying, the popup
queue, the new-game watch, a whole ARCHIPELAGO press and the escape skip's patch; `tests/menu_tests.cpp` the
game-mode panel (`tests/stand_in_menu.h`): relabeling, putting it back, closed menus and
released widgets; `tests/apply_test.cpp` goes through the real pipe and hook in a busy host.

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
    - `hello` → `zexion-ap <version> build=<B1|unknown> hooked=<0|1> configured=<0|1> skip=<0|1>`
    - `ping` → `pong frames=<calls since load>`
    - `config name=rva …` → `ok`: the addresses the client resolved by byte pattern
      (`client/patterns.py`; the names are `CONFIG_KEYS` in `client/native.py`). Each must lie
      inside the game's image, functions in its code and data outside it, or the whole
      configuration is refused.
    - `apply` and then lines, one batch → `applied`, or `refused <why>` with nothing written:
      - `cas ITEM EXPECTED VALUE`, an inventory byte from 0 to 49;
      - `refresh`;
      - `popup ITEM SUFFIX`.
    - `ap_newgame` and then lines, the AP seed for the next new game (below) → `armed`, or
      `refused not configured` or `refused no frame`:
      - `expect OFFSET HEX`, bytes the randomizer section must hold (the seed's mode);
      - `write OFFSET HEX`, the AP seed's edits to the section;
      - `menu OFFSET HEX`, the randomizer menu's state as the seed's mode, for ARCHIPELAGO.
      - `start_table HEX`, the menu's start table, 5 bytes per room (x, y, flag), for a mode
        with Double Teleport.
    - `ap_labels` and then lines `label X Y HEX` → `labeled N`, or `refused not configured`
      or `refused no frame`: names for other players' items, by map cell (below).
    - `disarm` → `disarmed`: drops the plan and puts RANDOMIZER back.
    - `newgame_status` → `armed`, `idle`, `applied slot N` or `refused <why>`.
    - `unload` → `bye` (see below).
    - Anything else → `error unknown request`. Errors always start with `error `.
- **Grants, in the frame call.** `config`, `apply`, `ap_newgame`, `disarm`, `newgame_status`
  and `hello`'s `configured` and `skip` are handed to the game's thread through a one-slot mailbox
  (`src/mailbox.cpp`) and run at its next frame call. If no frame takes one
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
  hides the key's map marker; the gates read the key bytes, and a gate opened in the game for
  keys granted by byte alone: `docs/re/memory.md`, "Keys, the gates and the key markers").
- **Popups.** One at a time, in a frame call, once the game's last popup has ended:
  - The text is the item's icon and name from the game's own item record, then the client's
    suffix (`FROM ALICE`), with any character the game's font does not draw left out.
  - Each popup plays the item's own pickup sound at the player.
  - A popup the game cannot take yet (all four message slots busy) stays queued.
  - Nothing is shown outside play: popups wait. At least 20 frames separate two popups. A
    popup whose item has no record (the records not loaded yet) is dropped.
- **New games (helper phase 3).** While a plan is armed, each frame call:
  - Watches for a randomizer new game the game has queued (its new-game routine set to run, a
    timer above one frame, the randomizer marker set). If the section holds the `expect`
    bytes, it writes the `write` edits once, before the game builds the new game from them;
    otherwise it writes nothing and drops the plan. This is the path for RANDOMIZER, the mode
    and START GAME (working in game for Standard, Easy and OHKO seeds, 2026-10-22).
  - With `menu` lines, offers **ARCHIPELAGO** in place of RANDOMIZER on the game-mode panel
    the game shows after an empty slot is picked (`src/menu.cpp`). It reads the panel with
    fault-safe reads and checks every handler, size and pointer on the way before it
    relabels the choice and points it at the helper's own callback. RANDOMIZER is put back as
    soon as its menu is not the top one, on `disarm`, and before an unload, but only while
    that menu is still open: the game frees a closed menu's widgets without clearing them, so
    the helper then forgets the choice without writing. The callback, called by the game's
    widget code on its thread, sets the menu's state to the seed's mode, writes the edits (for
    the AP start), runs the game's placement, checks the `expect` bytes, writes the edits
    over the placement, puts the player's menu state back, and queues the new game in the
    chosen slot; the watch above reports it in the next frame. Once the game has queued the new
    game, only RANDOMIZER's callback goes back, so the label reads ARCHIPELAGO through the fade
    (seen in the game, 2026-10-23); a refused start puts the whole label back.
    For a mode with Double Teleport (OHKO) the request carries the randomizer menu's start
    table (`start_table`), which the placement reads: before the placement the callback fills
    it in if the menu never built it, accepts it if it matches, and refuses the start if it
    differs (run in the game for an OHKO seed, 2026-10-23). A section of another mode, or a slot the
    game will not start a game in, drops the plan with nothing queued, and the client arms it
    again. Working in game for Standard and Easy seeds (2026-10-22): ARCHIPELAGO started the AP
    seed in an empty slot, showed again after backing out of two slots, and RANDOMIZER
    came back when the client disconnected (`docs/re/memory.md`, "The game-mode panel").

  The layouts are in `docs/re/memory.md`, "Starting a new game" and "The game-mode panel".
- **Other players' items (helper phase 4).** The client sends `ap_labels`, then lines
  `label X Y HEX`: a name per map cell for the stand-in item, Scrap Energy (47), which the
  world gives every location holding another player's item. While it has labels, every
  frame call (`src/apitem.cpp`) checks Scrap Energy's record against the game's own values
  and restyles it (its in-world frame and popup icon pointed at a frame and a glyph that
  are empty in the game's art, the Navcore's steady palette in place of Scrap Energy's
  flashing one, no refill drop at pickup), names it after the label for the
  player's map cell (or ARCHIPELAGO ITEM), so the game's own pickup popup shows "MASTER
  SWORD FOR ALICE", and now and then paints an Archipelago logo into that frame and
  glyph of the sprite texture through OpenGL (the game's context is current in the swap
  hook), after checking the texture is the 2048 x 2048 sprite sheet and, once per texture,
  reading it back to find the game's art where `Sprites.png` has it and the two places
  blank; it leaves the game's texture binding, pixel-store state and GL errors as it found
  them, and paints nothing while a pixel buffer is bound. `ap_labels` with no lines takes
  the labels back. Unload restores the record (and one an earlier helper left restyled)
  and clears the pixels; it refuses (`error the stand-in item is still restyled (no frame
  came); not unloading`) if no frame came to do it. Working in the game since 2026-10-23
  (logo, named popup, restore on disconnect and on unload; `docs/re/memory.md`, "Other
  players' items", which also lists its limits).
- **The escape skip.** Once configured, every 30th frame call checks area event 3665 in the
  game's loaded event table (`src/skip.cpp`): "if flag 250, offer SKIP ESCAPE AND FINAL
  BATTLE?". Flag 250 is set only by the ending, so a first playthrough never gets the offer.
  If every field of the record is as traced, the helper points its other branch at the offer
  too, so grabbing Zexion offers the skip in any game while the helper is loaded; a table the
  game loads again is patched again. Nothing is saved, and unloading puts the record back (if
  no frame comes to do it, the record stays pointed at the offer, a game event, until Zexion
  restarts). Once an unload begins, no frame patches it again.
  `hello` reports it as `skip=1`. Working in game (2026-10-23: the prompt in a first
  playthrough, YES to the ship, and the goal at the ending; `docs/re/memory.md`, "The escape
  skip").
- **Unloading.** `unload` first stops frame calls from offering ARCHIPELAGO again, puts
  RANDOMIZER and event 3665 back in a frame call, and refuses (`error ARCHIPELAGO is still on the game's
  menu`) if no frame came to do it, since the menu would point into the unloaded DLL. Then it
  points the slot back at SDL, and only if it still holds the
  trampoline; otherwise it replies `error the frame hook was replaced by something else; not
  unloading` and stays loaded. It then points the trampoline at SDL too, waits until no call is
  counted inside (and for a call still inside ARCHIPELAGO's callback), and unloads the DLL. A call can then be in the trampoline (which is never
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
tests (`parts`, `game`, `unload_under_load` with 300 rounds, `apply_in_frames`, `menu`,
the ARCHIPELAGO choice on a stand-in panel, and `apitem`, the stand-in item's restyle and
painting against a fake GL), and runs
`tests/test_native.py` against the build on Windows.

The frame call never waits on anything the pipe thread can hold. It takes the mailbox's work
with one atomic exchange, and only one thread at a time does the work.
