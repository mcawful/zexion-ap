# The native helper

`zexion_ap.dll` runs inside Zexion and gives the client what can't be done from outside: work
on the game's own thread, between frames. Either the player puts it in the game's folder with
the proxy `version.dll`, which loads it as the game starts ("The proxy", below), or the AP
client loads it when it attaches to the game (`client/native.py`); the client sends received
items through it, and `tools/helper.py` loads and unloads it for tests. Without a client it
changes nothing in play except the offline look of an AP save (a save of a seed a client has
played: "AP saves without the client", below), and it adds its lines to the title screen; the
game is otherwise the base game.

**Status:** each part below ends with when it was working in game, every run on build B1 and
recorded in [`docs/re/memory.md`](../docs/re/memory.md); the native tests ("Building") run
each part against a stand-in game.

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
  imports three functions from `VERSION.dll`, which is not a KnownDLL, so Windows takes a
  `version.dll` in the game's folder first (`docs/re/memory.md`, "Loading with the game"). The
  proxy exports all 17 of Windows' `version.dll` functions at the same ordinals, each passing
  the call to the real one in the system folder (loaded once, by its full path, by the thread
  below or the first call, whichever comes first; `VerLanguageNameA/W` forwarded to KERNEL32 as
  Windows' own does). Its DllMain only starts a thread; once the process has started, that
  thread pins the proxy (the game holds pointers into its exports), loads the real one, then
  `zexion_ap.dll` from its own folder if it is there, the host is `Zexion.exe`, no
  `zexion_ap.disabled` file sits beside it and none is loaded, under the client's mutex
  (waiting at most 5 seconds for it, else loading nothing), and never frees it (the helper's
  own unload is its one reference). **Installing** is the player's step, never our tools':
  with Zexion closed, copy `version.dll` and `zexion_ap.dll` of one release next to
  `Zexion.exe` (Steam, Zexion, Manage, Browse local files); replace both to update, create
  `zexion_ap.disabled` to turn it off, delete both to remove it. Steam's verify and its updates
  check only the game's own files (third-party guides read 2026-10-25, not tested here), so
  both should stay; an antivirus may flag an unsigned proxy. Working in game (2026-10-25,
  helper 0.20.0): the game loaded the proxy, Windows' own `version.dll` beside it and the
  helper, hooked (`hello`: `folder=1`) and inert with no client; the client used it without
  loading another.
- **Which game.** It reads the game's PE header and hooks nothing unless the build is one it
  knows (`src/build.cpp`, kept in step with `KNOWN_BUILDS` in `client/patterns.py`;
  `tests/test_cross_checks.py` checks it).
- **The frame call.** The game calls `SDL_GL_SwapWindow` only through its import-table slot,
  once per frame, on its main thread (`docs/re/memory.md`, "The per-frame call"). The helper
  finds that slot by name (`src/iat.cpp`) and, only if it still points at SDL, points it at a
  trampoline in memory that is never freed: it counts the call, runs the helper's frame
  function, which then calls SDL as before, and uncounts it. The trampoline is registered with
  unwind information, so an exception or a stack walk passes through it as through any
  compiled function. Working in game (2026-10-22, slot 7 in play): `build=B1 hooked=1`, 166
  frame calls a second on a 165 Hz monitor, unloaded with the game in play, loaded and
  unloaded again; the owner noticed no stutter or hitch.
- **The pipe.** `\\.\pipe\zexion-ap-<pid>`, message mode: one UTF-8 request, one reply, one
  client at a time (a second waits, and `client/native.py` reports the pipe busy). The helper
  creates it as the first and only pipe by that name, refusing to start if another process
  holds it; only the current user can open it, and only locally. The client opens it at the
  identification level only, so the server can't act as the user, and checks that the process
  serving it is the game. A request longer than 64 KB drops the connection, which counts as
  the client going away. The requests (`src/requests.cpp` parses them); those marked "frame"
  run on the game's thread, below:

  | Request | Reply | Part |
  |---|---|---|
  | `hello` | `zexion-ap <version> build=<B1\|unknown> hooked= configured= skip= folder= offline=<-\|N[:FILE]> active= title=<0\|1\|why> reads=<R/S>` (flags 0 or 1), then ` read_error=<why>` after a failed read | below |
  | `ping` | `pong frames=<calls since load>` | |
  | `config name=rva …` (frame) | `ok`, or `error <why>` | below |
  | `offline_put config` or `offline_put seed NAME`, a newline, the file | `stored`, `unchanged`, `error <why>` | AP saves without the client |
  | `apply` and lines `cas ITEM EXPECTED VALUE` (inventory bytes 0 to 49), `refresh`, `popup ITEM SUFFIX`, `notice ITEM TEXT` (frame) | `applied`, or `refused <why>` with nothing written | Grants; Popups |
  | `popups` (frame) | `pending N` | Popups |
  | `ap_newgame` and lines `expect`, `write` or `menu OFFSET HEX`, `start_table HEX` (frame) | `armed`, `refused <why>`, `error <why>` | New games |
  | `newgame_status`, `disarm` (frame) | `armed`, `idle`, `applied slot N`, `refused <why>`; `disarmed` | New games |
  | `ap_labels` and lines `label X Y HEX` (frame) | `labeled N`, `refused <why>` | Other players' items |
  | `shield`, `die` (frame) | `spawned`, `died`, `refused <why>` | Received Energy Shields; DeathLink |
  | `assists lock`, `assists free` (frame) | `locked`, `free`, `refused <why>` | The assist menu |
  | `intel on`, `intel off` (frame) | the same, or `refused <why>` | Intel Chips |
  | `discs COUNT REQUIRED HEX TOLD`, `discs`; `disk_slots COUNT REQUIRED SLOTS`, `disk_slots` (frame) | `discs on`, `discs off`, `disk_slots on`, `disk_slots off`, `refused <why>` | The OS Install Disks goal |
  | `discs_status`, `mission_prompt`, `mission_status` (frame) | `shown N` or `not shown…`; `queued`; `not asked`, `asked`, `opening`, `shown`, `yes: ending when idle`, `ending started`, … | The OS Install Disks goal |
  | `client_back`, `other_save` (frame) | `shown`, `none`; `told`, `already told`; or `refused <why>` | AP saves without the client |
  | `unload` | `bye`, or `error <why>` | Unloading |

  `hello`: `folder`, loaded from the game's own folder (by the proxy, or by a client with no
  build of its own); `offline`, the seeds read for AP saves without the client and the file of
  the one dressed; `active`, a client is there; `title`, the title screen's lines up, or why
  not; `reads`, the reads a frame has taken and the seeds the last one found; spaces in a
  field are written as underscores. `config` carries the addresses the client resolved by
  byte pattern (`CONFIG_KEYS` in `client/native.py`); each must lie inside the game's image,
  functions in its code and data outside it (writable, except the C runtime's `malloc` and
  `free` import slots, which are only read), or the whole configuration is refused, and every
  name is required, in an offline cache too (an older helper's cache lacking one is refused,
  `read_error=cache:_missing_NAME`, until a client rewrites it). The other arguments are
  described in `src/requests.h` and in the parts below. Anything else → `error unknown
  request`; errors always start with `error `. A "frame" request while the helper is not
  hooked → `error not hooked, so nothing runs on the game's thread`.
- **On the game's thread.** A "frame" request (with `hello`'s `configured`, `skip`, `active`,
  `offline` and `title`) is handed to the game's thread through a one-slot mailbox
  (`src/mailbox.cpp`) and run at its next frame call. If no frame takes one within a second
  (the game paused in the background, say), it is withdrawn, the reply is `refused no frame`,
  and it is never run afterwards.
- **Grants.** An apply (`src/game_calls.cpp`):
  1. Refuses unless the game is in play: a save loaded, no menu, energy above 0.
  2. Checks every `cas`, and writes all of them or, if any byte differs, none.
  3. Sets max and current missiles and bombs from the counts, and on a new Energy Tank sets max
     and current energy, as the pickup routine does.
  4. Refreshes the HUD the way the game does after loading a save: the missile number, the Mega
     Bomb icon, the energy tanks, and the weapon strip only while it shows.
  5. Sets the suit's color if Magnus Armor was just gained.
  6. Queues the popups.

  It leaves out what only a real pickup should do: the map cell, the pickup counter, the
  major-item list, achievements, switching to a new weapon, and a key's area event (which only
  hides the key's map marker; the gates read the key bytes: `docs/re/memory.md`, "Keys, the
  gates and the key markers"). Working in game (2026-10-22: a Missile Box's popup, sound and
  HUD number, the bomb icon, Magnus Armor's color, three popups in turn, and every item given
  back after a death with no popup repeated; `docs/re/memory.md`, "Game functions the helper
  calls" and "The client in a live game").
- **Popups.** One at a time, in a frame call, once the game's last popup has ended, at least 20
  frames apart, and only in play (they wait otherwise); each plays the item's own pickup sound
  at the player. A popup the game cannot take yet (all four message slots busy) stays queued;
  one whose item has no record (the records not loaded yet) is dropped. `applied` means
  queued, not shown: `popups` counts a client's popups still queued (not the helper's own
  notices), so the client counts one as shown only once it has left the queue.
  - A `popup`'s text is the item's icon and name from its record, then the client's suffix
    (`FROM ALICE`), up to 40 characters, of which only letters (upper-cased), digits, spaces
    and `.,'!?-` are kept. The game cuts a line wherever its width ends, so the suffix starts a
    line of its own, its words wrapped at 24, each line centered in the longest and padded
    with spaces (`popup_text`, `center_lines` in `src/text.cpp`); the width passed is the
    longest line's, which is how wide the game draws the box.
  - A `notice` is its text alone, up to 200 characters, `:` kept (an Area Map's hint, "HINT:
    SLIDE MOD IS AT GUARDIA 22,7", `client/hints.py`): left-aligned, words wrapped whole at 24,
    a word longer than a line split with a hyphen ending each line it fills (working in game
    2026-10-25, helper 0.19.0, a test hint with a long made-up word sent through the pipe;
    the owner's reading), each line but the last padded to the widest (`notice_text`). The
    game shows a text past two or three lines only in part, so a notice of three lines or more
    ends with a blank line, and once the game has made the message the helper fits its box
    back to the real lines (`fit_notice_box`, only if the box reads what the game wrote).
    Lines past the message's 256-byte slot are left out. Working in game up to five lines
    (2026-10-24; `docs/re/memory.md`, "Game functions the helper calls").
  - An `apply` may hold popups alone: the client's notice for an Area Map or Intel Chip found
    in the world, after the game's own popup (its sound plays again; working in game
    2026-10-25), and a found disk's count; for a find made with no client, or whose popup was
    not shown, later, once (`followups_tick` in `client/ticks.py`).
- **Received Energy Shields.** `shield` runs the Energy Shield's pickup event as its pickup
  does, from the player's cell, once the player has been in play in one cell for a second
  (`spawn_shield`, which checks the record, the event and its handler first). The client asks
  for one per received shield not yet given and counts it once it sees the shield up
  (`client/shields.py`). Working in game (2026-10-23; `docs/re/memory.md`, "Received Energy
  Shields").
- **DeathLink.** `die` deals a received death as the game's damage code does, energy to 0 and
  then the game's death routine (`kill`), through an Energy Shield too, once the player has
  been in play in one cell for a second, the game's halt byte is clear and no message is
  showing (the routine posts one without checking it was made); otherwise `refused` (`not
  configured`, `not settled`, `halted`, `message showing`, `no frame`). The client queues
  received deaths and asks once a second, one at a time (`client/deathlink.py`). Working in
  game (2026-10-23; `docs/re/memory.md`, "DeathLink").
- **The assist menu.** While locked (`assists lock`), each frame call looks in the top menu for
  the options menu's ASSIST entry (a live widget with the game's handler, ASSIST's callback
  and a 12-glyph label reading ASSIST and six blanks), draws it in STYLE: AUTO's style (5,
  palette 0, its grey tint: the options menu takes another palette after some screens),
  puts a padlock after it (font glyph `0xEF`, painted into the sprite texture while the lock is
  armed) and clears its callback, so pressing it does nothing and nothing in the menu points
  into the helper; it draws that again each frame the options menu is on top. It also moves
  ASSIST past the panel's selectable entries (those after it up one and renumbered), so the
  cursor skips it, only when each entry it moves is live and numbered by its place. `assists
  free`, an unload or the menu closing puts it back, into a menu still open only, and the order
  only if the panel is as it left it (the widget is found again by its pointer, so a changed
  label still gets the game's callback back). An unload no frame comes for leaves it locked
  until the options menu closes, harmlessly. `locked` means armed: whether an entry was found
  shows only in the game. The client locks it while a seed that disallows assists has its save
  loaded, until the slot receives Assist Unlock. Working in game (2026-10-23, the lock and
  Assist Unlock; 2026-10-25, unlocked in another save; `docs/re/memory.md`, "The assist
  menu").
- **New games.** While a plan is armed, each frame call:
  - Watches for a randomizer new game the game has queued (its new-game routine set to run, a
    timer above one frame, the randomizer marker set). If the section holds the `expect`
    bytes (the seed's mode), it writes the `write` edits (the AP seed) once, before the game
    builds the new game from them; otherwise it writes nothing and drops the plan. This is the
    path for RANDOMIZER, the mode and START GAME (working in game for Standard, Easy and OHKO
    seeds, 2026-10-22).
  - With `menu` lines (the randomizer menu's state as the seed's mode), offers **ARCHIPELAGO**
    in place of RANDOMIZER on the game-mode panel shown after an empty slot is picked
    (`src/menu.cpp`), after checking every handler, size and pointer on the way with
    fault-safe reads. While it is offered the panel's handler is the helper's, which calls the
    game's and then fits the border the panel draws round the button under the mouse to
    ARCHIPELAGO's eleven letters (`fit_border`; working in game 2026-10-24, from the first
    frame). Its callback, run by the game's widget code, sets the menu's state to the seed's
    mode, writes the edits, runs the game's placement, checks the `expect` bytes, writes the
    edits over the placement, puts the player's menu state back and queues the new game; for
    Double Teleport (OHKO) it first fills in the menu's start table from `start_table` (5 bytes
    a room: x, y, flag) if the menu never built it, and refuses the start if it differs. A
    section of another mode, or a slot the game will not start a game in, drops the plan with
    nothing queued, and the client arms it again. RANDOMIZER and the game's handler go back as
    soon as the menu is not the top one, on `disarm` and before an unload, but only while that
    menu is still open (the game frees a closed menu's widgets without clearing them); the
    handler goes back even if the choice no longer looks relabeled, and while the panel may
    still hold it the helper refuses to unload. Once the new game is queued only the callback
    and handler go back, so the label reads ARCHIPELAGO through the fade; a refused start puts
    the label back too. Working in game (Standard and Easy, 2026-10-22: the seed started,
    ARCHIPELAGO again after backing out of two slots, RANDOMIZER back on disconnect; OHKO and
    the label through the fade, 2026-10-23; `docs/re/memory.md`, "Starting a new game" and
    "The game-mode panel").
- **Other players' items.** The client's `ap_labels` name map cells (this repo's (x, y), which
  the game shows as X = y, Y = x) for the stand-in items, Scrap Energy (47) and Scrap Chunk
  (48), which the world gives every location holding another player's item (48 for
  progression): HEX is the item and whom it is for, joined by a newline, `@N` and whom it is
  for (another Zexion player's item N, named from its own record), or `@N NAME` (item N with
  that name: this player's own OS Install Disk, or another Zexion player's disk, Area Map or
  Intel Chip; `client/aplabels.py`, `zexion_look`); no lines take them back. Every frame call
  (`src/apitem.cpp`) checks both records against the game's values and, while a save is loaded
  and no menu is open (in menus the records are the game's and the logos cleared: the
  equipment screen draws item frame 95, and the map cursor shows a record's name), restyles
  them: in-world frames empty in the game's art, no refill drop, and each named after the
  label for the cell of a live stand-in of its kind within 96 pixels of the player (chosen,
  not measured; an enemy's drop carries its location's cell wherever it lands), else the
  player's cell's, else the nearest loaded one's (or AP ITEM). The game's own pickup popup
  then shows the logo glyph, then "PROGRESSION ITEM" (or "AP ITEM") over "FOR ALICE", laid out
  as the popups above, and while a stand-in is named the helper sets the popup width the
  pickup code passes (an immediate 24) to the label's longest line, putting 24 back elsewhere
  and on unload. It paints Archipelago's logo (with its flashing up arrow for progression) into
  those frames and its ring of dots into a glyph, through OpenGL in the swap hook, after
  checking the texture is the 2048 x 2048 sprite sheet and reading it back once per texture to
  find the game's art and the places to paint blank (a texture not as expected is read again
  at most twice, five seconds apart, then not until the game makes another); it leaves the
  game's texture binding, pixel-store state and GL errors as it found them, and paints nothing
  while a pixel buffer is bound. Unload restores the records (and any an earlier helper left
  restyled) and clears the pixels, refusing (`error an item record is still restyled (no frame
  came); not unloading`) if no frame came. **Limits:** two stand-ins of one kind for different
  locations within reach share the nearer one's name (not seen); with a translation active the
  popup shows the translation's text for Scrap Energy; and Scrap Energy in another seed's save
  would look the same, so the client sends the labels only while this seed's save is loaded.
  Working in game (2026-10-23: both logos, the flashing arrow, the named popup, the restore on
  disconnect and on unload; `docs/re/memory.md`, "Other players' items").
- **Other Zexion players' items, as themselves.** For a cell labeled `@N`, every frame call
  (`src/looks.cpp`) dresses the stand-in objects there, in the game's pool of live item
  objects, as item N: N's palette, and the frame-list base moved so the frame the game computes
  from the stand-in's record lands on N's, animated with the game's clock; the record, and so
  the pickup, stay the stand-in's. It writes only stand-ins with the template's base and
  palette 8, or ones it dressed (the same spawn), and puts them back without the label and on
  unload. Some rooms show an item through an object in the general object pool instead; a
  scan of that pool every 31st frame call finds any live object drawing a logo frame (63 or 95
  of the item sprites) and dresses it by its frame shown (`+0x1B0`) and palette. Working in
  game (2026-10-23, both kinds; `docs/re/memory.md`, "Live item objects").
- **Intel Chips.** While the client has sent `intel on` (this seed's save loaded, its Area Maps
  Intel Chips), each frame call checks the Area Map's record (49) and, if it reads as the game
  loads it (or as the helper left it), names it INTEL CHIP and turns off its pickup's area event
  (3551, the map reveal) (`src/intel.cpp`); `intel off`, the client going away and an unload
  put both back, and an unload refuses while it cannot. Working in game (2026-10-25;
  `docs/re/memory.md`, "Intel Chips").
- **The OS Install Disks goal.** While the client has sent `discs` (the count, the number
  needed, the scanner's line in hex, and TOLD: 1 once flag 39 says the player has been told the
  number, else 0 and "OF ?"; left out, 1), the helper:
  - writes the scanner's message (Text entry 12, ENERGY KEYS DETECTED!) as COLLECT 15 OS
    INSTALL DISKS! (`src/scan.cpp`, only over the game's text or its own);
  - paints the disk glyph, a ring round a hub, into font glyph 0xFB;
  - with a disk or more, wraps the pause screen's EQUIPMENT callback (`src/equip.cpp`): after
    the game builds the screen, it moves the Energy Tank left and adds a widget for the disk
    (the Data Disc's record, the count as its label), reachable by cursor and mouse, showing
    OS INSTALL DISK, its icon and description when selected; the game frees it with the menu;
  - makes the pause screen's KEYS row the disk row (`src/pause.cpp`): the `jne` that builds the
    row only once flag 39 is set made a `jmp`, then the small Data Disc icon and the count
    (" 2 OF ? ", " 2 OF 15 " once told), the key slots and icons hidden;
  - on `mission_prompt` (`src/mission.cpp`; the client sends it after each save at the landing
    pod with enough disks), once idle in play with no popup showing, opens the game's YES/NO
    prompt as RETURN HOME WITH THE DISKS?, whose YES closes the prompt through the game's NO
    and then starts the game's ending (routine 4); an opener another transition replaced is
    set again once idle.

  With `disk_slots` (SLOTS, the save slots holding the seed, a comma list or `-`; REQUIRED is
  checked, not shown), and for the saves of other seeds it has files for, it shows each save's
  count on the file select, COPY FILE and DELETE FILE in place of the keys (`src/files.cpp`,
  wrapping each entry's handler and checking the entries each frame): DISK over KEYS in the
  KEYS row's size (font glyphs 0xFC and 0xFD, painted while on) and the count moved 5 pixels
  down onto the items bracket; nothing with no disk; the client's count wins for its seed's
  slots. Working in game (2026-10-24 and 2026-10-25: the scanner's line, the equipment screen,
  the disk glyph, the disk row as a whole, the prompt, NO and YES to the ending, the file
  select's count, its put-back on a disconnect, and nothing for a save with no disk;
  `docs/re/memory.md`, "The scanner", "The equipment screen", "The mission prompt", "The pause
  screen's KEYS row", "The file select").
- **AP saves without the client** (`src/offline.cpp`, `src/game_offline.cpp`). While
  connected, the client sends the helper (`offline_put`, when they change; `client/offline.py`)
  the addresses it resolved for this build, with the first 16 bytes of each game function, and
  a small file per seed: its item table and start, the Intel Chip and ASSIST settings, the OS
  Install Disks (the count, the number needed, the scanner's line, and the cells of this
  player's own disks not counted yet) and the item labels. `offline_put` checks each as the
  reader will (the config with at least 8 function checks; NAME in the client's
  `<seed>-<team>-<slot>` form, at most 48 KB), and the helper writes it, on the pipe's thread,
  into `%LOCALAPPDATA%\ZexionAP` (`config-<build>.txt`, `seeds\<seed>-<team>-<slot>.txt`; an
  absolute `LOCALAPPDATA` only), never the game's folder: a client run from a packaged Windows
  app (MSIX), as this repo's tests run it from the Claude desktop app, has its own writes under
  AppData redirected where the game does not see them (inferred: `docs/re/memory.md`, "AP saves
  without the client"). It reads them when it starts and each time a client goes away, at most
  256 seed files, newest first; with no client yet, the cached addresses configure it if they
  are for its build and the game holds every checked function's bytes. Then, with no client,
  or a client that said the loaded save is another seed's (`other_save`), every 16th frame
  call, a save of one of the seeds (a randomizer save whose section's item table and start are
  the seed's) is dressed once in play as the client would: the item labels, INTEL CHIP, the
  ASSIST lock, the escape skip, the disk count (the file's, plus each own disk the map shows
  collected) and the scanner's line, with one silent notice once a load, ARCHIPELAGO CLIENT
  NOT CONNECTED. CHECKS ARE SENT WHEN IT CONNECTS. (for `other_save`, THE ARCHIPELAGO CLIENT
  IS FOR ANOTHER SEED. LOAD THAT SEED'S SAVE OR START A NEW GAME., once a load and again after
  a reconnect). Leaving the save undresses it; the file select shows each such save's own
  count, from its map in the game's copy of the slot. A client's `config` takes the offline
  look off first and withdraws a notice not yet shown; the client then asks `client_back`,
  which pops up ARCHIPELAGO CLIENT CONNECTED. once if that save was dressed, before the
  client's catch-up popups for what was found offline. Received items, hints, the goal and
  RETURN HOME WITH THE DISKS? wait for the client; what was picked up is on the save's map, and
  the client sends those checks when it next connects with the save loaded. Working in game
  (2026-10-25: the look, a disk counted offline, a fresh start with no client, the offline
  finds sent on connecting, the CLIENT CONNECTED. notice, and another seed's save with a
  client; `docs/re/memory.md`, "AP saves without the client").
- **The map.** In an AP save (labels known), every 16th frame call raises the map squares of
  item 48 (this player's OS Install Disks and other players' progression items) from the
  game's basic marker to its important one (`src/mapmarks.cpp`), and while the map is open the
  helper wraps the map's per-frame handler (both of its menu child's slots, `+0x168` and
  `+0x178`, again where the game put its own back): after the game writes the cursor's name, it
  writes the square's label's name (OS INSTALL DISK for this player's disk; the item and FOR
  ALICE when they fit in 17, else FOR ALICE) over SCRAP ENERGY or SCRAP CHUNK, in the same
  frame. The handler goes back when off and before an unload, which refuses while an open map
  holds the helper's. Working in game (2026-10-25; `docs/re/memory.md`, "Map markers").
- **The title screen's lines.** While configured (by a client, or from the cached addresses)
  and no save is loaded, every 9th frame call looks through the game's draw list for the
  title's copyright line; when it is up without them, the helper adds ARCHIPELAGO MOD and its
  version, and ©2026 MCAWFUL, centered under it, and above them, only while no client is
  connected, START THE AP CLIENT TO PLAY A SEED (`src/title.cpp`). It makes them with the
  game's own text functions in that line's palette and style, and the game frees them with the
  title; nothing is kept between looks, and one that failed is tried again on the next title. A
  client going away leaves them; an unload hides them (stand-in tests only). Working in game
  (2026-10-25: with the client, from the cache with none, and again on a later title;
  `docs/re/memory.md`, "The title screen").
- **The escape skip.** While a client is there, or while a save is dressed offline, every 31st
  frame call checks area event 3665 in the game's loaded event table (`src/skip.cpp`): "if
  flag 250, offer SKIP ESCAPE AND FINAL BATTLE?". Flag 250 is set only by the ending, so a
  first playthrough never gets the offer; if every field of the record is as traced, the helper
  points its other branch at the offer too, so grabbing Zexion offers the skip in any game. A
  table the game loads again is patched again; nothing is saved. Unloading puts the record
  back (if no frame comes to do it, it stays pointed at the offer, a game event, until Zexion
  restarts), and once an unload begins no frame patches it again. `hello` reports it as
  `skip=1`. Working in game (2026-10-23: the prompt in a first playthrough, YES to the ship,
  and the goal at the ending; `docs/re/memory.md`, "The escape skip").
- **A client going away.** When the pipe's client disconnects (the AP client exited or
  crashed, or `tools/helper.py` was done), the next frame call, before it serves any request,
  puts back everything the helper changed (`Game::put_back_all` in `src/game_calls.cpp`): the
  new-game plan and ARCHIPELAGO, the escape skip, the OS Install Disks goal's changes (the
  scanner's text, the pause screen's branch, EQUIPMENT's callback and the Energy Tank panel's
  handler, a prompt opener not yet run and a prompt's YES, pointed at NO, and the file screens'
  entries), the Area Map's record, the stand-in items and their looks, the map's handler, the
  ASSIST lock and the offline look; a change held by a menu no longer open is gone with it. It
  drops the popups not shown yet and goes inactive until a client sends `config` again (the
  client resends every request after it attaches), except the offline look for a save of a
  seed the client left a file for. The helper stays loaded and hooked. Working in game
  (2026-10-25, helper 0.20.0 loaded with the game, the client stopped: the stand-in and Area
  Map records read back live as the game's, the disk's stand-in a plain Scrap Chunk and ASSIST
  normal, the owner's reading, and event 3665 the game's, `else -1`, read back live; not looked
  at: ARCHIPELAGO and the OS Install Disks goal's screens).
- **Unloading.** `unload` first stops frame calls from offering ARCHIPELAGO or locking ASSIST
  again, then puts back in a frame call what a client going away puts back, and hides the title
  screen's lines. It refuses (`error a menu still holds the helper's callback (no frame came);
  not unloading`) if no frame came to take ARCHIPELAGO off, or while an open menu's panel or
  entry, the open map or the game's transition routine may still hold one of the helper's
  functions (the game would call into the unloaded DLL), or the scanner's text, the pause
  screen's branch or an item record is still the helper's. A refused unload sets again, in the
  next frame call and before any request it serves, everything clients had set, which a client
  would not know to resend: the new-game plan, the item labels, ASSIST's lock, INTEL CHIP, the
  disk counts and scanner line, the file select's counts and a RETURN HOME WITH THE DISKS?
  prompt on its way. Then it points the slot back at SDL, only if it still holds the
  trampoline (otherwise `error the frame hook was replaced by something else; not unloading`,
  and it stays loaded), points the trampoline at SDL too, waits until no call is counted
  inside (nor inside one of its callbacks or handlers the game may be running), and unloads
  the DLL. A call can then be in the trampoline (never freed) or in SDL, but not in the helper,
  so a rebuilt helper can be loaded without restarting the game. If the pipe cannot be
  created, or breaks, the helper puts the game back as `unload` does (retrying each second
  until a frame comes) and unloads itself the same way; if its hook was replaced, it stays.
- **Errors.** No exception leaves the helper's thread; on one, the helper stays loaded, its
  hook still working, and stops answering.

## Building

Visual Studio 2022 or later with the C++ workload, and CMake 3.20 or later. From the repo root:

```sh
cmake -S native -B native/build -A x64
cmake --build native/build --config Release
ctest --test-dir native/build -C Release --output-on-failure
```

The DLL lands in `native/build/Release/zexion_ap.dll` (gitignored), the proxy in
`native/build/proxy/Release/version.dll`. The C runtime is linked statically, so it needs
nothing the game doesn't already load. CI builds both, runs the ctest tests, and runs
`tests/test_native.py` on Windows, which loads the build into a stand-in Python process: it
answers on its pipe and unloads, and hooks nothing, that process being no known build (the
test skips without a build).

The ctest tests (`native/CMakeLists.txt` maps each name to its `tests/` source) run against a
stand-in game: the test program's own data and functions, laid out as the helper expects the
game's (`tests/stand_in.h`, `tests/stand_in_menu.h`) and configured by RVA like the real game.
They share `tests/check.h`, and `tests/pipe_client.h` is the pipe client of `parts`,
`apply_in_frames` and `unload_under_load`.

- `parts`: the import-table hook and the trampoline (an exception raised behind it caught in
  front), the build check and the pipe.
- `requests`: the request parsers, the popup text's layout and the mailbox.
- `game`: the `discs` and `@N NAME` parsers, configuring, applying, the popup queue, the
  new-game watch, a whole ARCHIPELAGO press, the escape skip's patch, what a refused unload
  sets again, the received shield and death, the ASSIST watch and the scanner's text.
- `menu`: the game-mode panel and the options menu (relabeling, putting back, closed menus
  and released widgets, the ASSIST lock's look and cursor order).
- `apitem`: the stand-in records restyled and named, the logo painted through a fake OpenGL,
  the label parser.
- `equip`, `pause`, `files`, `intel`, `title`, `mapmarks`, `mission`, `offline`: the part of
  that name (`offline`: the seed file, its match and disk count, the folder, the cache check,
  `offline_put`, a stand-in game dressed and undressed).
- `proxy`: its exports give what Windows' own give, and it loads no helper outside Zexion.
- `apply_in_frames`: through the real pipe and hook in a busy host (a batch and its popup,
  `popups`, a batch no frame took, an unload refused while something else is hooked over the
  helper, with what the client had set armed again).
- `unload_under_load`: a test build, `zexion_ap_hooktest.dll`, hooks the loading program's own
  `GetTickCount` import (it refuses to hook anything in a known game build), counts calls, and
  survives 300 loads and unloads in CI (3,000 locally) while two threads call it nonstop.

The sources (`src/`): `dllmain.cpp` (the hook, the pipe's requests, the stand-ins the game
calls), `game_calls.cpp` (`Game`: configure, grants, popups, shields, deaths and each part's
watch), `game_offline.cpp` (`Game`'s offline look), `requests.cpp` (the requests' parsers),
`text.cpp` (the popups' text layout), `pipe.cpp`, `mailbox.cpp`, `iat.cpp` (the import-table
hook), `build.cpp` (the known builds), `game_mem.cpp` and `game_layout.h` (fault-safe memory
access and the layouts several parts share), and one file per part, as named above:
`apitem`, `looks`, `menu` (ARCHIPELAGO and the ASSIST lock), `skip`, `intel`, `scan`,
`equip`, `pause`, `files`, `mission`, `offline`, `mapmarks` and `title`. `proxy/` is the
proxy `version.dll`.

The frame call never waits on anything the pipe thread can hold. It takes the mailbox's work
with one atomic exchange, and only one thread at a time does the work.
