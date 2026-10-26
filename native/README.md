# The native helper

`zexion_ap.dll` runs inside Zexion and gives the client what can't be done from outside: work
on the game's own thread, between frames. Either the player puts it in the game's folder with
the proxy `version.dll`, which loads it as the game starts (below, "The proxy"), or the AP
client loads it when it attaches to the game (`client/native.py`); the client sends received
items through it. `tools/helper.py` loads and unloads
it for tests. Without a client it changes nothing in play, except the offline look for a save
of a seed a client has played ("AP saves without the client", below), and it adds its lines to
the title screen ("The title screen's lines"); the game is otherwise the base game.

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
game. `tests/tests.cpp` (the `parts` test) covers what needs no stand-in game: the
import-table hook and the trampoline, the build check and the pipe (`src/pipe.cpp`);
`tests/game_tests.cpp` the request parsers (the `discs` and `@N NAME` ones too), configuring,
applying, the popup queue, the new-game watch, a whole ARCHIPELAGO press, the escape skip's
patch, what a refused unload sets again, the received shield and death, the ASSIST watch and
the scanner's text; `tests/menu_tests.cpp` the
game-mode panel and the options menu (`tests/stand_in_menu.h`): relabeling, putting it back,
closed menus and released widgets, and the ASSIST lock (its look, the cursor order, putting it
back); `tests/apitem_tests.cpp` other players' items (the stand-in records restyled and named,
the logo painted through a fake OpenGL, the label parser); `tests/equip_tests.cpp` the equipment
screen's disk, `tests/pause_tests.cpp` the pause screen's disk row and its branch,
`tests/files_tests.cpp` the file select's disk count, `tests/intel_tests.cpp` the Intel Chip,
`tests/title_tests.cpp` the title screen's lines, `tests/mapmarks_tests.cpp` the map's markers and names, `tests/proxy_test.cpp` the proxy (its exports give what Windows' own give, and it loads no
helper outside Zexion), `tests/offline_tests.cpp` AP saves without the client (the seed file,
its match and disk count, the folder, the cache check, `offline_put`, and a stand-in game dressed
and undressed),
`tests/mission_tests.cpp` the RETURN HOME WITH THE DISKS? prompt; `tests/apply_test.cpp` goes
through the real pipe and hook in a busy host (a batch and its popup, `popups`, a batch no frame
took, and an unload refused while something else is hooked over the helper, with what the
client had set armed again). `tests/pipe_client.h` is the pipe client the
`parts`, `apply_in_frames` and `unload_under_load` tests share.

## How it works

- **Getting in.** `client/native.py` loads it with `LoadLibraryW` from a remote thread, with a
  timeout and under a named mutex (`Local\zexion-ap-load-<pid>`), so two loaders can't race,
  unless one is loaded already: the proxy's (below), or an earlier client's, which it uses. One
  of another version is unloaded and the built one loaded; a copy in the game's folder with no
  build to replace it is refused (the player updates it there), and with no build the client
  loads the game folder's copy (reading it only). A file that loads as another version (an
  out-of-date build) is not loaded again until it changes: the client says to rebuild
  `native/` or copy this release's DLLs. The client never writes the game's files.
- **The proxy** (`proxy/`, built as `build/proxy/Release/version.dll`). Zexion's `SDL2.dll`
  imports `GetFileVersionInfoA`, `GetFileVersionInfoSizeA` and `VerQueryValueA` from
  `VERSION.dll`, which is not a KnownDLL, so Windows takes a `version.dll` in the game's folder
  first (measured on this machine, 2026-10-25). The proxy exports all 17 of Windows'
  `version.dll` functions at the same ordinals, each passing the call to the real one in the
  system folder (loaded once, by its full path, by the thread below or the first call,
  whichever comes first; `VerLanguageNameA/W` forwarded to KERNEL32 as Windows' own does). Its
  DllMain only starts a thread; once the process has started, that thread pins the proxy (the
  game holds pointers into its exports), loads the real one, then `zexion_ap.dll` from its own
  folder, if it is there, the host is `Zexion.exe`, no `zexion_ap.disabled` file sits beside it,
  and none is loaded, under the client's mutex (waiting at most 5 seconds for it, and loading
  nothing if it does not get it), and never frees it (the helper's own unload is its one
  reference).
  **Installing** is the player's step, never our tools': with Zexion closed, copy
  `version.dll` and `zexion_ap.dll` of one release next to `Zexion.exe` (Steam, Zexion, Manage,
  Browse local files). Updating: replace both with the game closed. Turning it off: create
  `zexion_ap.disabled` there; removing: delete both. Steam's verify and its updates check only
  the game's own files (external, third-party guides read 2026-10-25, not tested here), so
  both should stay; an antivirus may flag an unsigned proxy (external). Working in game
  (2026-10-25, B1, helper 0.20.0): the game loaded the proxy and Windows' own `version.dll`
  beside it and the helper from its folder, hooked (`hello`: `folder=1`), with no client
  inert (RANDOMIZER on an empty slot; from 0.21, see "AP saves without the client"); the
  client used it ("loaded with the game") without
  loading another; stopping the client put the AP save back to the base game in play (the
  stand-ins and the Area Map's records the game's, read back live; the disk's stand-in a plain
  Scrap Chunk and ASSIST normal, the owner's reading; event 3665 the game's, `else -1`, read
  back live: the escape skip off).
- **Which game.** It reads the game's PE header and hooks nothing unless the build is one it
  knows (`src/build.cpp`, kept in step with `KNOWN_BUILDS` in `client/patterns.py`;
  `tests/test_cross_checks.py` checks it).
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
  - Requests longer than 64 KB drop the connection, which counts as the client going away
    ("A client going away", below).
  - Requests:
    - `hello` → `zexion-ap <version> build=<B1|unknown> hooked=<0|1> configured=<0|1> skip=<0|1> folder=<0|1> offline=<-|N[:FILE]> active=<0|1> title=<0|1|why> reads=<R/S>`
      and ` read_error=<why>` after a failed read (`folder`: loaded from the game's own
      folder, by the proxy or by a client with no build of its own; `offline`: the seeds read for AP saves without the client, and the
      file of the one whose save is dressed; `active`: a client is there; `title`: the title
      screen's lines up (1), not (0), or why they could not be made; `reads`: the reads a
      frame has taken, and the seeds the last one found; each field one word, the spaces in
      `offline`, `title` and `read_error` written as underscores)
    - `offline_put config` or `offline_put seed NAME`, then a newline and the file's text →
      `stored`, `unchanged` or `error <why>`: one of the files for AP saves without the client
      (below), checked as the reader checks it (the config with at least 8 function checks,
      each on one of its keys; NAME in the client's `<seed>-<team>-<slot>` form, at most 48 KB)
      and written by the helper, on the pipe's thread.
    - `client_back` → `shown`, `none`, or `refused <why>`: from a client whose session sees its
      seed's save in play; if that save (the same seed's section) was dressed with no client,
      ARCHIPELAGO CLIENT CONNECTED. pops up, once.
    - `other_save` → `told`, `already told`, or `refused <why>`: from a client whose
      session sees a randomizer save of another seed in play. The helper pops up
      THE ARCHIPELAGO CLIENT IS FOR ANOTHER SEED. LOAD THAT SEED'S SAVE OR START A NEW GAME.
      (silent) once a load and again after a client's `config` (a reconnect), and, if it
      has that save's seed file, gives the save that seed's offline look until it goes
      ("AP saves without the client", below).
    - `ping` → `pong frames=<calls since load>`
    - `config name=rva …` → `ok`, or `error <why>` (`missing NAME`, say): the addresses the client resolved by byte pattern
      (`client/patterns.py`; the names are `CONFIG_KEYS` in `client/native.py`). Each must lie
      inside the game's image, functions in its code and data outside it (writable, except the
      C runtime's `malloc` and `free` import slots, which are only read), or the whole
      configuration is refused. Every name is required, in an offline cache too: one written
      for an older helper that lacks a name is refused (`read_error=cache:_missing_NAME`), and
      the helper stays without the offline look until a client's `config` and `offline_put`
      rewrite it.
    - `apply` and then lines, one batch → `applied`, or `refused <why>` with nothing written:
      - `cas ITEM EXPECTED VALUE`, an inventory byte from 0 to 49;
      - `refresh`;
      - `popup ITEM SUFFIX`;
      - `notice ITEM TEXT`, a popup of the text alone (an Area Map's hint).
    - `popups` → `pending N`, configured or not: how many of the popups `apply` queued are not
      shown yet (below). A popup leaves the count once shown, dropped for want of an item record,
      or dropped with the client that sent it; the helper's own notices are not counted.
    - `ap_newgame` and then lines, the AP seed for the next new game (below) → `armed`,
      `refused not configured` or `refused no frame`, or `error <why>` for a bad line or for
      none to write (`error nothing to write`):
      - `expect OFFSET HEX`, bytes the randomizer section must hold (the seed's mode);
      - `write OFFSET HEX`, the AP seed's edits to the section;
      - `menu OFFSET HEX`, the randomizer menu's state as the seed's mode, for ARCHIPELAGO.
      - `start_table HEX`, the menu's start table, 5 bytes per room (x, y, flag), for a mode
        with Double Teleport.
    - `ap_labels` and then lines `label X Y HEX` (X, Y this repo's cell (x, y), which the game shows as X = y, Y = x) → `labeled N`, or `refused not configured`
      or `refused no frame`: names for other players' items, by map cell (below); HEX is the
      item and whom it is for, joined by a newline, or `@N` and whom it is for, for another
      Zexion player's item N, which the helper names from that item's record (its icon and
      the game's name) once the records are loaded; `@N NAME` gives item N that name instead
      (this player's own OS Install Disk, which the game's record calls DATA DISC, with the
      disk glyph the helper paints, the record having no icon; and, for another Zexion
      player, an OS Install Disk, an Area Map and an Intel Chip, drawn as item 49:
      `client/aplabels.py`, `zexion_look`).
    - `shield` → `spawned`, or `refused <why>`: a received Energy Shield (below).
    - `die` → `died`, or `refused <why>`: a received DeathLink (below).
    - `assists lock` → `locked`, `assists free` → `free`, or `refused <why>`: the options
      menu's ASSIST entry (below).
    - `discs COUNT REQUIRED HEX TOLD` → `discs on`, `discs` → `discs off`, or `refused
      <why>`: the OS Install Disks goal (below): the count, the number needed, the scanner's
      line in hex (one line of the game's font, under 32 characters), and whether the player
      has been told the number (1, from the scanner's flag 39) or not (0, "OF ?"); TOLD may be
      left out, meaning 1.
    - `disk_slots COUNT REQUIRED SLOTS` → `disk_slots on`, `disk_slots` (or no slot) →
      `disk_slots off`, or `refused <why>`: the file select's disk count (below) for the save
      slots holding the seed, SLOTS a comma list (1 to 30) or `-`. REQUIRED is checked but
      not shown.
    - `intel on` → `intel on`, `intel off` → `intel off`, or `refused <why>`: the Area Map
      named INTEL CHIP with its map reveal off (below).
    - `discs_status` → what the equipment screen last did: `shown N`, `not shown: <why>` or
      `not shown yet`.
    - `mission_prompt` → `queued`: offer RETURN HOME WITH THE DISKS? when the game is next idle
      in play; `mission_status` → `not asked` (none yet, or the prompt called off), `asked`,
      `opening`, `asked again: another transition came
      first`, `shown`, `yes: ending when idle`, `ending started` or what was not as traced.
    - `disarm` → `disarmed`: drops the plan and puts RANDOMIZER back.
    - `newgame_status` → `armed`, `idle`, `applied slot N` or `refused <why>`.
    - `unload` → `bye` (see below).
    - Anything else → `error unknown request`. Errors always start with `error `. A request
      run on the game's thread (the list below) while the helper is not hooked →
      `error not hooked, so nothing runs on the game's thread`.
- **Grants, in the frame call.** `config`, `apply`, `ap_newgame`, `ap_labels`, `shield`, `die`, `assists`,
  `intel`, `discs`, `disk_slots`, `discs_status`, `mission_prompt`, `mission_status`,
  `client_back`, `other_save`, `disarm`, `newgame_status`, `popups` and `hello`'s `configured`, `skip`,
  `active`, `offline` and `title` are handed to the game's thread through a one-slot mailbox
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
  - A `popup`'s text is the item's icon and name from the game's own item record, then the
    client's suffix (`FROM ALICE`), up to 40 characters, of which only letters (upper-cased),
    digits, spaces and `.,'!?-` are kept. The game lays a popup out a given number of
    characters a line and cuts wherever a line ends, so the suffix starts a line of its own,
    its words wrapped at 24, each line is centered in the longest with spaces before it and
    padded with spaces after (`popup_text` and `center_lines` in `src/text.cpp`), and
    the line width passed is the longest line's, which is how wide the game draws the box.
  - A `notice` is its text alone, up to 200 characters, keeping `:` too (an Area Map's hint,
    "HINT: SLIDE MOD IS AT GUARDIA 22,7", `client/hints.py`): left-aligned, words wrapped
    whole at 24 (only a word longer than a whole line is split, from where it starts, a hyphen
    ending each line it fills: seen in game 2026-10-25, B1, helper 0.19.0, with a made-up hint
    sent through the pipe by a one-off script, "HINT: SLIDE MOD IS AT
    SUPERCALIFRAGILISTICEXPIALIDOCIOUS 22,7", shown as the three lines expected; the owner's
    reading, not saved), each line but
    the last padded with spaces to the widest, which is the box's width (`notice_text`). The
    game shows a popup's text past two or three lines only in part, so a notice of three lines
    or more ends with a blank line, which makes the game show every real line, and once the
    game has made the message the helper fits its box back to the real lines (`fit_notice_box`:
    its half-height and centre, only if they read what the game wrote for that text). The game
    copies a message into a 256-byte slot with no check (`docs/re/memory.md`, "Game functions
    the helper calls"), so lines past what it holds are left out. Working in the game since
    2026-10-24, up to five lines.
  - An `apply` may hold popups alone, with no `cas` lines: the client sends a notice that way
    for an Area Map (or Intel Chip) found in the world, after the game's own pickup popup
    (its sound plays again; working in game 2026-10-25), and a found disk's count as a popup;
    for a find made with no client, or one whose popup was not shown, later, once
    (`followups_tick` in `client/zexion_client.py`; "AP saves without the client").
  - Each popup plays the item's own pickup sound at the player.
  - A popup the game cannot take yet (all four message slots busy) stays queued.
  - Nothing is shown outside play: popups wait. At least 20 frames separate two popups. A
    popup whose item has no record (the records not loaded yet) is dropped.
  - `applied` means queued, not shown: `popups` says how many of a client's are still queued,
    so the client can count a popup as shown only once it has left the queue.
- **Received Energy Shields.** `shield` → `spawned` or `refused <why>`: runs the Energy
  Shield's pickup event the way its pickup does, from the player's cell, once the player has
  been in play in one cell for a second (`spawn_shield`; checked against B1 first). The client
  asks for one per received shield not yet given, and counts it in the server's data storage
  once it reads the shield up in the game, or asks again if it does not see it within 3 seconds
  of play (`docs/re/memory.md`, "Received Energy Shields"). The spawn and the client's check work in game (2026-10-23, B1).
- **DeathLink.** `die` → `died` or `refused <why>`: deals a death received from another player
  as the game's damage code does, once the player has been in play in one cell for a second,
  the game's halt byte is clear and no message is showing (the routine posts one and does not
  check it was made): energy to 0, then the game's death routine (`kill`; the routine and the
  byte found by pattern, checked by `config`), through an Energy Shield too. The client queues received deaths and asks once a second
  while the player is in play, one death at a time. Works in game (2026-10-23, B1; `docs/re/memory.md`, "DeathLink").
- **The assist menu.** While locked (`assists lock`), each frame call looks for the options
  menu's ASSIST entry in the top menu (a live widget with the game's handler, ASSIST's
  callback, found by pattern, and a 12-glyph label reading ASSIST and six blanks), draws it in
  the style of the controls screen's STYLE: AUTO (5), puts a padlock after it (font glyph
  `0xEF`, empty in the game's art, which the helper paints into the sprite texture while the
  lock is armed and clears after) and clears its callback, so pressing it does nothing (the
  game's widget handler calls a callback only if one is set) and nothing in the menu points
  into the helper. It stays locked while the options menu is open, under another menu too, and
  draws the locked look again each frame the options menu is on top: style 5 under palette 0
  with STYLE: AUTO's grey tint (the options menu takes another palette after some screens, the
  map's 26 among them). It also moves ASSIST past the panel's selectable entries (the panel's
  `+0x1EC` counts the children the cursor reaches, from the first), the ones after it up one
  and renumbered, so the cursor skips it; only when each one it moves is as expected.
  `assists free`, an unload, or the
  menu closing puts it back (into a menu still open only; the widget it locked is found again by its pointer, so a label changed
  meanwhile still gets the game's callback back). The lock is dropped when any pipe client disconnects
  (and set again by the offline look for a save of a seed the client left a file for: "AP
  saves without the client")
  other than through `unload` (the AP client exited or crashed, or `tools/helper.py` was done:
  "A client going away"), and armed again if an unload is refused. An unload no frame comes for leaves it locked (and
  moved) until the options menu closes, harmlessly: nothing in it points into the helper. `locked` means armed:
  whether an entry was found shows only in the game. The client locks it while a seed that
  disallows assists has its save loaded, until the slot receives Assist Unlock. The lock works in game (2026-10-23, B1; `docs/re/memory.md`,
  "The assist menu"), unlocking too (by Assist Unlock, and on loading another save,
  2026-10-25).
- **New games (helper phase 3).** While a plan is armed, each frame call:
  - Watches for a randomizer new game the game has queued (its new-game routine set to run, a
    timer above one frame, the randomizer marker set). If the section holds the `expect`
    bytes, it writes the `write` edits once, before the game builds the new game from them;
    otherwise it writes nothing and drops the plan. This is the path for RANDOMIZER, the mode
    and START GAME (working in game for Standard, Easy and OHKO seeds, 2026-10-22).
  - With `menu` lines, offers **ARCHIPELAGO** in place of RANDOMIZER on the game-mode panel
    the game shows after an empty slot is picked (`src/menu.cpp`). It reads the panel with
    fault-safe reads and checks every handler, size and pointer on the way before it
    relabels the choice and points it at the helper's own callback. The border the panel draws
    round the button under the mouse is sized for RANDOMIZER's ten letters and a blank, so its
    right-hand side would sit on ARCHIPELAGO's last letter: while ARCHIPELAGO is offered the
    panel's handler is the helper's, which calls the game's and then moves that side a
    character out and stretches the top and bottom edges to meet it, in the frame the game
    lays the border out (`fit_border`; the frame call does the same, for a border laid out
    without the handler; working in the game since 2026-10-24, fitted from the first frame).
    RANDOMIZER, and the game's handler, are put back as
    soon as its menu is not the top one, on `disarm`, and before an unload, but only while
    that menu is still open: the game frees a closed menu's widgets without clearing them, so
    the helper then forgets the choice without writing. The game's handler goes back even if
    the choice no longer looks like the one relabeled. The callback, called by the game's
    widget code on its thread, sets the menu's state to the seed's mode, writes the edits (for
    the AP start), runs the game's placement, checks the `expect` bytes, writes the edits
    over the placement, puts the player's menu state back, and queues the new game in the
    chosen slot; the watch above reports it in the next frame. Once the game has queued the new
    game, only RANDOMIZER's callback (and the panel's handler) goes back, so the label reads
    ARCHIPELAGO through the fade
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
  `label X Y HEX`: a name per map cell (this repo's (x, y), not the pause screen's X and Y) for the stand-in items, Scrap Energy (47) and Scrap
  Chunk (48), which the world gives every location holding another player's item (48 for a
  progression item). While it has labels, every frame call (`src/apitem.cpp`) checks both
  records against the game's own values and, while a save is loaded and no menu is open
  (the equipment screen draws item frame 95, blank in the game's art, and the map cursor shows a
  record's name; in a menu the records are the game's own and the logos are cleared),
  restyles them (their in-world frames pointed at
  frames that are empty in the game's art, no refill drop at pickup), names each after the
  label for the cell of a live stand-in of its kind within reach of the player (the cell the
  item object carries: an enemy's drop lands wherever the enemy dies), else the player's
  cell's if it has one, else the nearest such stand-in's (or AP ITEM): the logo glyph, then the label laid
  out as the popups below are, so the game's own pickup popup shows "PROGRESSION ITEM" (or "AP
  ITEM") over "FOR ALICE", both centered (the records keep no icon of their own, so the name is shown as
  written). The game's pickup code makes that popup with a line width of 24, as an immediate
  in its code; while a stand-in is named after a label (the one within reach, or the
  player's cell's) the helper changes it (from the frame call, on the game's thread, after
  checking the instruction's bytes) to that label's longest line, so the box is no wider than the text, and puts 24 back elsewhere, and on
  unload, which waits for a frame to do it. It also now and then paints
  Archipelago's logo (with its up arrow, flashing, for progression) into those frames and
  its ring of dots into the glyph of the sprite texture through OpenGL (the game's context
  is current in the swap hook), after checking the texture is the 2048 x 2048 sprite sheet
  and, once per texture for everything the helper paints there (the font glyphs below too),
  reading it back to find the game's art where `Sprites.png` has it and the places to paint
  blank (or already the helper's); a texture not as expected is read again at most twice,
  five seconds apart, then left alone until the game makes another, and a place not blank
  stops only what paints there. It leaves the game's texture binding, pixel-store state and
  GL errors as it found them, and paints nothing while a pixel buffer is bound.
  `ap_labels` with no lines takes the labels back. Unload restores the records (and any an
  earlier helper left restyled) and clears the pixels; it refuses (`error an item record
  is still restyled (no frame came); not unloading`) if no frame came to do it. Working in
  the game since 2026-10-23 (both logos, the flashing arrow, named popup, restore on
  disconnect and on unload; `docs/re/memory.md`, "Other players' items", which also lists its
  limits).
- **Other Zexion players' items, as themselves.** For a cell whose label is another Zexion
  player's item (`@N`), every frame call (`src/looks.cpp`) finds the stand-in objects there
  in the game's pool of live item objects and dresses each as item N: its palette set to
  N's, and its frame-list base moved so the frame the game computes from the stand-in's
  record lands on N's, animated with the game's clock. The record, and so the pickup, stay
  the stand-in's. It writes only objects that are stand-ins with the template's base and
  palette 8, or that it dressed itself (the same spawn), and puts them back without the
  label and on unload. Some rooms show an item through an object in the game's general
  object pool instead (type 2026 in the reversed-gravity rooms, 715 elsewhere; their cell at
  `+0x210`): a scan of that pool every 31st frame call finds any live object drawing a stand-in's
  logo (the item sprites' frame list at frame 63 or 95), whatever its type, and it is dressed
  by its frame shown (`+0x1B0`, which those objects' updates do not recompute) and palette,
  its frame list left as the game's. Working in the game since 2026-10-23, both kinds too
  (`docs/re/memory.md`, "Live item objects").
- **Intel Chips.** While the client has sent `intel on` (this seed's save loaded, its Area
  Maps Intel Chips: the world's Area Map Hints), each frame call checks the Area Map's item
  record (49) and, if it reads as the game loads it, names it INTEL CHIP and sets its pickup's
  area event (3551, which reveals the map around it) to none (`src/intel.cpp`); `intel off`,
  the client going away and an unload put the name and event back (the offline look names it
  again for a save of a seed the client left a file for), and an unload refuses
  while the record is still renamed (`error an item record is still restyled`). Working in game (2026-10-25,
  B1; `docs/re/memory.md`, "Intel Chips").
- **The OS Install Disks goal.** While the client has sent `discs` (this seed's save loaded,
  the seed's goal the OS Install Disks), the helper:
  - rewrites the scanner's message, Text entry 12 in the loaded table, ENERGY KEYS DETECTED!,
    to the client's line, COLLECT 15 OS INSTALL DISKS! (`src/scan.cpp`, every 31st frame
    call; only over the game's text or its own; put back when off and before an unload);
  - paints the disk glyph, a ring round a hub, into font glyph 0xFB (empty in the game's art),
    and clears it when off;
  - with one disk or more, adds the disk to the equipment screen (`src/equip.cpp`): it points
    the pause screen's EQUIPMENT entry at its own callback, which calls the game's and then,
    before the screen's first frame, moves the Energy Tank left and makes a second widget for
    the disk (the game's widget constructor, the Data Disc's record, the count as its label),
    reachable by cursor and mouse, and wraps the panel's handler to show OS INSTALL DISK, its
    icon and description when it is selected; the game frees the widget with the menu;
  - turns the pause screen's KEYS row into the disk row (`src/pause.cpp`): the `jne` that
    builds the row only once flag 39 is set made a `jmp`, then on the frame after the pause
    screen opens, the small Data Disc icon in the middle and the count (" 2 OF ? ",
    " 2 OF 15 " once told) on the label's line, the key slots and icons hidden;
  - on `mission_prompt` (`src/mission.cpp`; the client sends it after each save at the
    landing pod with enough disks), once idle in play with no popup showing, sets the
    game's transition routine to its own opener, which runs the game's YES/NO prompt builder,
    relabels the question RETURN HOME WITH THE DISKS? and points YES at the helper; YES calls
    the game's NO to close the prompt, then, once idle again, the helper starts the game's
    ending (routine 4). An opener another transition replaced is set again once idle.
  (Without a client, the offline look sets each save's own count: "AP saves without the
  client".) While the client has sent `disk_slots` with slots (the client reads which save slots hold
  the seed, a save loaded or not), and for the saves of the other seeds it has files for
  (each its own count, from the offline look; the client's count wins for its seed's
  slots), it also shows the count on the file select, COPY FILE and
  DELETE FILE in place of the keys (`src/files.cpp`): it points each entry's handler at its
  own, which calls the game's and rewrites the entry after it fills it, and checks the
  entries each frame; for those slots, DISK over KEYS in the KEYS row's size (font glyphs
  0xFC and 0xFD, painted while on: a 3-wide D and I, then the game's own S and 4-wide K
  from KEYS, one blank column apart) and the count below it, moved 5 pixels down, centered on the items
  bracket; nothing with no disk. When off and before an unload, the game's frames for the
  keys, the count's glyphs' place and the handlers go back, in menus still open; a refused
  unload turns the count on again.
  Working in game: the scanner's line, the equipment screen, the disk glyph, the disk row
  (as a whole), the prompt, NO and YES to the ending, and the file select's count, its
  put-back on a disconnect and nothing for a save with no disk (2026-10-24 and 2026-10-25,
  B1; `docs/re/memory.md`, "The scanner", "The equipment screen", "The mission prompt", "The
  pause screen's KEYS row", "The file select").
- **AP saves without the client** (`src/offline.cpp`, `src/game_offline.cpp`). While
  connected, the client sends the
  helper (`offline_put`, every few seconds when they change; `client/offline.py`) the
  addresses it resolved for this build, with the first 16 bytes of each game function among
  them, and a small file per seed: its item table and start, the Intel Chip and ASSIST
  settings, the OS Install Disks (the count, the number needed, the scanner's line, and the map
  cells of this player's own disks not counted yet) and the item labels. The helper writes them
  into `%LOCALAPPDATA%\ZexionAP` (`config-<build>.txt`, `seeds\<seed>-<team>-<slot>.txt`),
  never the game's folder, itself: a client run from a packaged Windows app (MSIX), as this
  repo's tests run it from the Claude desktop app, has its own writes under AppData redirected
  to a folder only it sees (inferred: the game, started by Steam, got error 3, path not found, opening a folder the client had just written and could read back; the redirect itself was not traced). It
  reads them when it starts and each time a client goes away (on the pipe's thread, handed to
  a frame), at most 256 seed files, the most recently written first. With no client yet, the cached addresses configure it if they are for its own
  build and the game holds every checked function's bytes (at least 8), inactive. Then, with
  no client, or with a client that said the loaded save is another seed's (`other_save`;
  its own notice then stands for the not-connected one), every 16th frame call: a save of one of the seeds (a save loaded, the randomizer
  marker 1, the loaded section's item table and start the seed's) is dressed once it is in
  play, as the client would: the item labels, INTEL CHIP, the ASSIST lock, the escape skip,
  the disk count (the file's, plus each of this player's own disks the map shows collected)
  and the scanner's line, told by flag 39; and one silent notice, ARCHIPELAGO CLIENT NOT
  CONNECTED. CHECKS ARE SENT WHEN IT CONNECTS., once a load. Leaving the save undresses it. The
  file select shows each save of a seed with the disks its own count, from its own map in the
  game's copy of the slot. A client's `config` takes the offline look off (with the addresses
  it was set with) before its own and withdraws the warning if it has not shown yet; the
  client then asks `client_back`, and if that save was dressed it pops up ARCHIPELAGO CLIENT
  CONNECTED. (silent), before the client's catch-up popups (a disk's count, an Intel Chip's
  hint) for what was found offline. The folder is `LOCALAPPDATA`'s, and only an absolute one. What needs the server (received items, hints, the goal and
  RETURN HOME WITH THE DISKS?) waits for the client; what was picked up meanwhile is on the
  save's map, and the client sends those checks when it next connects with the save loaded.
  Working in game (2026-10-25, B1, helpers 0.21.4 to 0.21.8, the `live25` seed: the look, a
  disk counted offline, a fresh start with no client, and the two disks picked up offline
  sent once the client connected; `docs/re/memory.md`, "AP saves without the client"). The
  CLIENT CONNECTED. notice: live-tested 2026-10-25 (0.21.8), asked for by the client.
- **The map.** In an Archipelago save (labels known), every 16th frame call raises the
  map squares of the progression stand-in (item 48: this player's OS Install Disks and
  other players' progression items) from the game's basic marker to its important one
  (`src/mapmarks.cpp`; the marker is the square's value in the saved grid, the game's
  important range being item IDs 3 to 35), and while the map is open the helper wraps the
  map's per-frame handler (both of its menu child's slots, `+0x168` and `+0x178`): after
  the game's handler writes the cursor's name, it writes the square's label's name over a
  SCRAP ENERGY or SCRAP CHUNK, in the same frame. The handler goes back when off and before
  an unload, which refuses while an open map holds the stand-in. Working in game
  (2026-10-25, helpers 0.24.0 and 0.25.1; `docs/re/memory.md`, "Map markers").
- **The title screen's lines.** While configured (by a client, or with none from the
  cached addresses) and no save is loaded, every 9th frame call looks through the
  game's draw list for the
  title's copyright line; when it is up without them, the helper adds ARCHIPELAGO MOD
  and its version, and ©2026 MCAWFUL, centered under the copyright line, and above them,
  only while no client is connected, START THE AP CLIENT TO PLAY A SEED (`src/title.cpp`). It
  makes them with the game's own text functions, as the title makes its copyright
  line, in that line's palette and style, and the game frees them when it leaves the
  title; nothing is kept between looks. A client going away leaves them; an unload
  hides them (stand-in tests only, not seen in game). Working in game (2026-10-25, helpers 0.22.0 and 0.22.1: with the client,
  from the cache with none, and again on a later title; `docs/re/memory.md`, "The
  title screen").
- **A client going away.** When the pipe's client disconnects (the AP client exited or crashed,
  or `tools/helper.py` was done), the next frame call, before it serves any request, puts back
  everything the helper changed, as unload does (`Game::put_back_all` in `src/game_calls.cpp`: the
  new-game plan, the escape skip, the OS Install Disks goal's changes, the Area Map's name, the
  stand-in items and their looks, the map's handler, ARCHIPELAGO, the ASSIST lock and the
  offline look; a change held by a menu no
  longer open is gone with it), drops the popups not shown yet, and goes inactive: nothing comes
  back until a client sends `config` again (the client resends every request after it
  attaches), except the offline look for a save of a seed the client left a file for
  (above). The helper stays loaded and hooked. Working in game (2026-10-25, above: what was
  seen is the stand-in and Area Map records, ASSIST and the escape skip put back; ARCHIPELAGO
  and the OS Install Disks goal's screens going back on a disconnect not looked at).
- **The escape skip.** While a client is there (configured, not gone), or with none (or a client of another seed) while a save is dressed offline ("AP saves without the client"), every 31st frame call checks area event 3665 in the
  game's loaded event table (`src/skip.cpp`): "if flag 250, offer SKIP ESCAPE AND FINAL
  BATTLE?". Flag 250 is set only by the ending, so a first playthrough never gets the offer.
  If every field of the record is as traced, the helper points its other branch at the offer
  too, so grabbing Zexion offers the skip in any game while a client is there (and in a save
  dressed offline); a table the
  game loads again is patched again. Nothing is saved, and unloading puts the record back (if
  no frame comes to do it, the record stays pointed at the offer, a game event, until Zexion
  restarts). Once an unload begins, no frame patches it again.
  `hello` reports it as `skip=1`. Working in game (2026-10-23: the prompt in a first
  playthrough, YES to the ship, and the goal at the ending; `docs/re/memory.md`, "The escape
  skip").
- **Unloading.** `unload` first stops frame calls from offering ARCHIPELAGO or locking ASSIST
  again, puts RANDOMIZER (with the panel's handler), ASSIST and event 3665 back in a frame call,
  with the OS Install Disks goal's changes (the scanner's text, the pause screen's branch,
  EQUIPMENT's callback and the Energy Tank panel's handler, a prompt opener not yet run and a
  prompt's YES, pointed at NO, and the file screens' entry handlers) and the Area Map's
  record (INTEL CHIP), the offline look and the map's handler, hides the title screen's lines, and refuses (`error a menu
  still holds the helper's callback (no frame came); not unloading`) if no frame came to take
  ARCHIPELAGO off, or while an open menu's panel or entry, or the game's transition routine,
  may still hold one of the helper's functions, since the game would call into the unloaded
  DLL; likewise while the scanner's text or the pause screen's branch is still the helper's
  (the locked ASSIST holds
  no callback of the helper's, so it cannot). A refused unload sets again, in the next frame call
  however long it takes to come and before any request that frame serves, everything clients
  had set, which a client would not know to resend: the new-game plan
  (and so ARCHIPELAGO), the item labels (and so the stand-ins' looks and the map's names),
  ASSIST's lock, INTEL CHIP, the disk counts and scanner line, the file select's counts and
  a RETURN HOME WITH THE DISKS? prompt on its way (the offline look dresses its save again by
  itself). Then it
  points the slot back at SDL, and only if it still holds the
  trampoline; otherwise it replies `error the frame hook was replaced by something else; not
  unloading` and stays loaded. It then points the trampoline at SDL too, waits until no call is
  counted inside (and for a call still inside ARCHIPELAGO's callback, a panel's handler, a file
  entry's handler, the map's handler, EQUIPMENT's callback, the prompt's opener or its YES),
  and unloads the DLL. A call can then be in the trampoline (which is never
  freed) or in SDL, but not in the helper, so a rebuilt helper can be loaded without
  restarting the game. If the pipe cannot be created, or breaks, the helper puts the game
  back as `unload` does (retrying each second until a frame comes to do it) and then unhooks
  and unloads itself the same way; if its hook was replaced, it stays.
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
the ARCHIPELAGO choice and the ASSIST lock on stand-in menus, `apitem`, the stand-in item's
restyle and painting against a fake GL, and `equip`, `mission`, `pause`, `files`, `intel`,
`title`, `mapmarks`, `proxy` and `offline`; the test programs share `tests/check.h`), and
runs `tests/test_native.py` against the build on Windows.

The sources: `src/dllmain.cpp` (the hook, the pipe's requests, the stand-ins the game calls),
`src/game_calls.cpp` (Game: configure, grants, popups and each part's watch),
`src/game_offline.cpp` (Game's offline look), `src/requests.cpp` (the requests' parsers),
`src/text.cpp` (the popups' text layout), `src/pipe.cpp` (the pipe's server),
`src/game_mem.cpp` and `src/game_layout.h` (fault-safe
memory access and the layouts several parts share), and one file per part, as named above.

The frame call never waits on anything the pipe thread can hold. It takes the mailbox's work
with one atomic exchange, and only one thread at a time does the work.
