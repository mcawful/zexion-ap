# The native helper

`zexion_ap.dll`, **the helper**, runs inside Zexion and does what the client cannot from
outside: work on the game's own thread, between frames. It gets in two ways: **the proxy**
`version.dll` in the game's folder loads it as the game starts, or the client loads it on
attaching (`client/native.py`; `tools/helper.py` too, for tests). The client talks to it over a
named pipe. With no client it changes only the title screen and the look of AP saves, and keeps
Feats and Steam achievements out of AP saves.

An **AP save** is a randomizer save whose item table and start match a seed a client has
played. A **wrapper** is a helper function the game's menus are pointed at in place of their
own; it calls the game's and adds the helper's part.

**Status:** B1 only. "Working in game" below gives the date of the run on B1 and the
`docs/re/memory.md` heading that records it. The native tests ("Building") run most parts
against a stand-in game.

## Lifecycle

### Getting in

If a helper is already loaded (the proxy's, or an earlier client's), the client uses it.
Otherwise it loads one with `LoadLibraryW` from a remote thread, with a timeout, under the
mutex `Local\zexion-ap-load-<pid>`, which the proxy takes too.

- Which file: the client's build (`native/build/Release/zexion_ap.dll`, or `ZEXION_DLL`), else
  the game folder's copy, read only. The client never writes the game's files.
- A loaded helper of another version is unloaded and the build loaded instead. A game-folder
  copy with no build to replace it is refused: the player updates it there.
- A file that loads as another version is not loaded again until it changes; the client says to
  rebuild `native/` or copy this release's DLLs.
- While `zexion_ap.disabled` sits in the game's folder, neither the proxy nor the client loads
  the game folder's helper (a build of the client's own, `native/build` or `ZEXION_DLL`, still
  loads).

### The proxy

`proxy/`, built as `build/proxy/Release/version.dll`. The player copies it into the game's
folder with the helper (the setup guide has the steps; our tools never write there). Zexion's
`SDL2.dll` imports `VERSION.dll`, which is not a KnownDLL, so Windows takes the one in the
game's folder first (`docs/re/memory.md`, "Loading with the game").

- It exports all 17 of Windows' `version.dll` functions at the same ordinals, each passed on to
  the real one in the system folder (`VerLanguageNameA/W` forwarded to KERNEL32, as Windows'
  own does).
- Its DllMain only starts a thread. That thread pins the proxy (the game holds pointers into its
  exports), loads the real `version.dll`, then loads `zexion_ap.dll` from its own folder if the
  host is `Zexion.exe`, the file is there, no `zexion_ap.disabled` is beside it and none is
  loaded. It takes the client's mutex for this, waiting at most 5 seconds (else it loads
  nothing), and never frees the helper: the helper's own unload is its one reference.
- Assumed, not tested: Steam's verify and updates check only the game's own files, so both
  DLLs stay. An antivirus may flag the unsigned proxy.

Working in game: 2026-10-25, helper 0.20.0 ("Loading with the game").

### Which game

The helper reads the game's PE header and hooks nothing unless the build is one it knows
(`src/build.cpp`, kept in step with `KNOWN_BUILDS` in `client/patterns.py`;
`tests/test_cross_checks.py` checks them).

### The frame call

The game calls `SDL_GL_SwapWindow` only through its import-table slot, once per frame, on its
main thread (`docs/re/memory.md`, "The per-frame call"). The helper finds that slot by name
(`src/iat.cpp`) and, only if it still points at SDL, points it at a trampoline that is never
freed. The trampoline counts the call, runs the helper's frame function, calls SDL and
uncounts. It is registered with unwind information, so an exception or a stack walk passes
through it as through any compiled function.

Working in game: 2026-10-22 ("The per-frame call").

### The pipe

`\\.\pipe\zexion-ap-<pid>`, message mode: one UTF-8 request, one reply, one client at a time (a
second waits; the client reports the pipe busy).

- The helper creates it as the first and only pipe of that name, and refuses to start if
  another process holds it. Only the current user can open it, and only locally.
- The client opens it at the identification level only, so the server cannot act as the user,
  and checks that the process serving it is the game.
- A request over 64 KB drops the connection, which counts as the client going away.

The requests (`src/requests.cpp` parses them; "frame" ones run on the game's thread, below):

| Request | Reply | Part |
|---|---|---|
| `hello` | `zexion-ap <version> build=<B1\|unknown> hooked= configured= feats=<off\|on\|on:why> folder= offline=<-\|N[:FILE]> active= title=<0\|1\|why> reads=<R/S>` (flags 0 or 1), then ` cache_missing=<keys>` when configured from a cache lacking client-only keys, ` read_error=<why>` after a failed read | below |
| `ping` | `pong frames=<calls since load>` | |
| `config name=rva …` (frame) | `ok`, or `error <why>` | below |
| `offline_put config` or `offline_put seed NAME`, a newline, the file | `stored`, `unchanged`, `error <why>` | AP saves without the client |
| `apply` and lines `cas ITEM EXPECTED VALUE` (inventory bytes 0 to 49), `refresh`, `popup ITEM SUFFIX`, `notice ITEM TEXT` (frame) | `applied`, or `refused <why>` with nothing written | Grants; Popups |
| `ap_newgame` and lines `expect`, `write` or `menu OFFSET HEX`, `start_table HEX` (frame) | `armed`, `refused <why>`, `error <why>` | New games |
| `newgame_status`, `disarm` (frame) | `armed`, `idle`, `applied slot N`, `refused <why>`; `disarmed` | New games |
| `ap_labels` and lines `label X Y HEX` (frame) | `labeled N`, `refused <why>` | Other players' items |
| `shield`, `die` (frame) | `spawned`, `died`, `refused <why>` | Received Energy Shields; DeathLink |
| `assists lock`, `assists free` (frame) | `locked`, `free`, `refused <why>` | The assist menu |
| `intel on`, `intel off` (frame) | the same, or `refused <why>` | Intel Chips |
| `discs COUNT REQUIRED HEX TOLD`, `discs`; `disk_slots COUNT REQUIRED SLOTS`, `disk_slots` (frame) | `discs on`, `discs off`, `disk_slots on`, `disk_slots off`, `refused <why>` | The OS Install Disks goal |
| `discs_status`, `mission_prompt`, `mission_status` (frame) | `shown N` or `not shown…`; `queued` or `refused not configured`; `not asked`, `asked`, `opening after the fade`, `opening without the fade: …`, `shown`, `lapsed: the player left the landing pod`, `yes: ending when idle`, `ending started`, … | The OS Install Disks goal |
| `client_back`, `other_save` (frame) | `shown`, `none`; `told`, `already told`, `unknown` (no known seed: a plain randomizer game, left alone); or `refused <why>` | AP saves without the client |
| `unload` | `bye`, or `error <why>` | Unloading |

`hello`'s fields (spaces inside a field written as underscores):

- `feats`: Feats and achievements kept out of the AP save loaded (off, on, or on with why a
  change could not be made).
- `folder`: loaded from the game's own folder (by the proxy, or by a client with no build).
- `offline`: the seeds read for AP saves without the client, and the file of the one dressed.
- `active`: a client has configured the helper and not gone away.
- `title`: the title screen's lines are up (1), not (0), or why not.
- `reads`: the reads a frame has taken, and the seeds the last one found.
- `cache_missing`: the client-only keys (below) the offline cache lacked, comma-separated. With
  it, `configured=1` means the offline look runs while the client's parts still refuse.

`config` carries the addresses the client resolved by byte pattern (`CONFIG_KEYS` in
`client/native.py`). The whole configuration is refused unless:

- every name is there. An offline cache ("AP saves without the client") may lack the client-only
  ones, or hold one that does not fit as below: those marked `client_only` in `kConfigKeys`
  (`src/game_calls.cpp`), which only the grants, new games, DeathLink, Energy Shields, RETURN
  HOME WITH THE DISKS? and the Feats guard's load test (`current_slot`, with no test when it is
  unset) read. The helper then configures with those unset (`hello` lists them as
  `cache_missing`), and `apply`, `shield`, `die`, `ap_newgame` and `mission_prompt` get `refused
  not configured` until a client's `config` sets every key. A cache lacking any other key is
  refused (`read_error=cache:_missing_NAME`) until a client rewrites it. So a release that adds
  a key only the client's parts read keeps working offline with the older cache (working in game
  2026-10-27, B1, the helper built at f838028: a cache without `fade` and `fade_level`, no
  client, `cache_missing=fade,fade_level`; the title line, the file select's AP logos and a
  dressed AP save played as usual, where #11's helper refused the same cache with
  `read_error=cache:_missing_fade`; a client-only key that does not fit: stand-in tests only);
  one that adds a key the offline look reads does not, until the client has run once.
  `tests/test_cross_checks.py` checks that only the client's parts read a client-only key, and
  that each way a client's state comes in waits for every key;
- each lies inside the game's image: functions in its code, data outside it and writable;
- the exception being import slots, which lie outside code: the C runtime's `malloc` and
  `free`, only read, and steam_api64's `SteamInternal_ContextInit`, written while an AP save is
  loaded or a save slot holds one ("Feats and achievements in AP saves").

The other arguments are described in `src/requests.h` and in the parts below. Anything else gets
`error unknown request`; errors always start with `error `. A "frame" request while the helper
is not hooked gets `error not hooked, so nothing runs on the game's thread`.

### On the game's thread

A "frame" request (and `hello`'s `configured`, `feats`, `active`, `offline`, `title` and
`cache_missing`) goes to the game's thread through a one-slot mailbox (`src/mailbox.cpp`) and
runs at the next frame call. If no frame takes it within a second (the game paused in the
background, say), it is withdrawn, the reply is `refused no frame`, and it never runs
afterwards.

The frame call never waits on anything the pipe thread can hold: it takes the mailbox's work
with one atomic exchange, and only one thread at a time does the work.

### A client going away

When the pipe's client disconnects (the client exited or crashed, or `tools/helper.py` was
done), the next frame call, before it serves any request, puts back everything the parts below
set (`Game::put_back_all` in `src/game_calls.cpp`). A change held by a menu no longer open is
gone with it. It goes inactive until a client sends `config` again (the client resends every
request after it attaches). The client's popups not shown yet stay queued and still show
("Popups"): their items are already given. The offline look then applies
again (AP saves without the client). The helper stays loaded and hooked.

The client's popups staying queued, and showing: working in game 2026-10-26 (0.30.0).

Working in game: 2026-10-25, the client stopped with helper 0.20.0 loaded by the proxy
("The escape skip", "Other players' items"). Not looked at: ARCHIPELAGO and the OS Install
Disks goal's screens.

### Unloading

`unload` first stops frame calls from offering ARCHIPELAGO or locking ASSIST again, then, in a
frame call, puts back what a client going away puts back and hides the title screen's lines.

It refuses (`error a menu is open; not unloading`) while a save is loaded with a menu open
(the pause, map or equipment screen, the quit menu), before putting anything back: an open
screen keeps what it was built from (the pause row's disc, an item record's name), which it
would show wrong until reopened (seen in game 2026-10-26, before this check; the refusal with
the pause screen open: working in game 2026-10-26, the Launcher's client log
`ZexionClient_2026_10_26_19_56_35.txt` at 20:10:47, with the client of that run; the client
after it, from 5681ed6, took any refusal for a failed request and dropped the helper, which by
the code then put things back under the open screen as for a client going away, not seen in
game; fixed since, `native.Helper.unload`: working in game 2026-10-27, 0.30.0, the refusal with
the pause screen open, the client keeping the helper and the screen unchanged, then a reload in
play, `lab/ap/live34/client1.log` at 11:35:53 and 11:36:02). It also refuses (`error
<why>; not unloading`) while the game may still call into the helper, or hold something of the
helper's.
That is an open menu's panel or entry, the open map or the game's transition routine holding one
of its wrappers, or the scanner's text, the pause screen's branch, an item record or the
ending's patch still the helper's. It refuses (`error the ending RETURN HOME WITH THE DISKS?
started is on its way; not unloading`), before putting anything back, from RETURN HOME WITH
THE DISKS?'s YES until the helper has started the ending, and while the ending routine is the
game's transition with the ending's patch in place: an unload would drop the ending, or put
the patch back before the ending runs ("Feats and achievements in AP saves"). That window is
a few frames, longer while received items' popups or a message hold the ending after YES.
Working in game 2026-10-27 (575edbf, 0.30.0, `lab/ap/live35/client1.log` and
`reload_spam.txt`): `/reload_mod` sent every half second from the open prompt through YES,
refused as a menu open while the prompt showed, then twice as the ending on its way while five
received items' popups held it; the helper stayed, and the ending ran and sent the goal.
If no frame came, it goes by what the last frame call found: the reason then ends in "(no frame
came)", and with nothing found it unloads, leaving the rest as each part's notes below say. A
refused unload sets again, in the next frame call and before any
request, what clients had set (`zap::ClientState`), since a client would not know to resend it.

Then it points the slot back at SDL, only if the slot still holds the trampoline (otherwise
`error the frame hook was replaced by something else; not unloading`, and it stays loaded),
points the trampoline at SDL too, waits until no call is inside the helper or one of its
wrappers, and unloads the DLL. A call can then be in the trampoline (never freed) or in SDL, but
not in the helper, so a rebuilt helper can be loaded without restarting the game.

If the pipe cannot be created, or breaks, the helper puts the game back as `unload` does
(retrying each second until a frame comes) and unloads itself the same way; if its hook was
replaced, it stays.

### Errors

An exception in a frame call is dropped and the game's call goes on, as if the helper had done
nothing. One on the helper's own thread stops the pipe; the helper stays loaded, its hook still
working.

## Parts

Each part: what it does, when it runs, how it is put back, and when it was working in game.
Every part is put back when a client goes away and on unload, unless it says otherwise. "Every
16th frame call" means a countdown reset to 15.

### Grants

An `apply` (`src/game_calls.cpp`):

1. Refuses unless the game is in play: a save loaded, no menu, energy above 0.
2. Checks every `cas`, and writes all of them or, if any byte differs, none.
3. Sets max and current missiles and bombs from the counts, and on a new Energy Tank max and
   current energy, as the pickup routine does.
4. Refreshes the HUD as the game does after loading a save: the missile number, the Mega Bomb
   icon, the energy tanks, and the weapon strip only while it shows.
5. Sets the suit's color if Magnus Armor was just gained.
6. Queues the popups.

It leaves out what only a real pickup should do: the map cell, the pickup counter, the
major-item list, achievements, switching to a new weapon, and a key's area event (which only
hides the key's map marker: `docs/re/memory.md`, "Keys, the gates and the key markers").

Working in game: 2026-10-22 ("Game functions the helper calls", "The client in a live game").

### Popups

- **When:** one at a time, in a frame call, in play only, once the game's last popup has ended
  and at least 20 frames after the helper's last, and not while RETURN HOME WITH THE DISKS? is
  on its way (its fade, before the game opens it). Each plays the item's own pickup sound.
- **Queue:** a popup the game cannot take yet (all four message slots busy) stays queued; one
  whose item has no record (records not loaded yet) is dropped. `applied` means queued, not
  shown; the client counts a received item's popup as done once `apply` took it. The queue
  outlives the client ("A client going away"), but not the helper: an unload, or the game
  closing, loses the popups still queued (their items are given).
- **Their save:** a client's popup is for the seed of the randomizer save loaded when `apply`
  took it (with none loaded, it shows in any save). While a save of another seed, or a normal
  save, is in play it waits (not keeping RETURN HOME WITH THE DISKS? waiting), and the next one
  for this save, or a helper notice, shows instead. A client of another seed does not drop them:
  they wait for their own save.
  Working in game 2026-10-26: popups waiting through a normal save, shown in their own save.
  A client of another seed leaving them queued: stand-in tests only (`tests/game_tests.cpp`).
- **Text:** a `popup` is the item's icon and name, then the client's suffix (≤ 40 characters);
  a `notice` is its text alone (≤ 200). Both are wrapped at the game's 24-character width
  (`src/text.cpp`; `docs/re/memory.md`, "Game functions the helper calls").
- **Follow-ups:** an `apply` may hold popups alone, such as the client's notice for an Area Map
  or Intel Chip found in the world, after the game's own popup.

Working in game: 2026-10-24, notices up to five lines ("Game functions the helper calls");
2026-10-25, a notice after a found Intel Chip's popup ("Intel Chips").

### Received Energy Shields

- **What:** `shield` runs the Energy Shield's pickup event as its pickup does, from the player's
  cell (`spawn_shield`, which checks the record, the event and its handler first).
- **When:** once the player has been in play in one cell for a second. The client asks once per
  received shield not yet given, and counts it once it sees the shield up (`client/shields.py`).
- **Put back:** nothing to put back.

Working in game: 2026-10-23 ("Received Energy Shields").

### DeathLink

- **What:** `die` deals a received death as the game's damage code does: energy to 0, then the
  game's death routine (`kill`), through an Energy Shield too.
- **When:** once the player has been in play in one cell for a second, the game's halt byte is
  clear, no message is showing (the routine posts one without checking it was made) and
  RETURN HOME WITH THE DISKS? is not between its fade and its prompt; otherwise `refused` (`not
  configured`, `not settled`, `halted`, `message showing`, `RETURN HOME WITH THE DISKS?
  opening`, `no frame`). The client queues received deaths and asks once a second, one at a time
  (`client/deathlink.py`).
- **Put back:** nothing to put back.

Working in game: 2026-10-23 ("DeathLink").

### The assist menu

- **What:** while locked (`assists lock`), the options menu's ASSIST entry is drawn in STYLE:
  AUTO's style with a padlock after it (font glyph `0xEF`, painted while armed), its callback is
  cleared so pressing it does nothing, and it is moved past the panel's selectable entries so the
  cursor skips it (only when each entry it moves is live and numbered by its place).
- **When:** each frame call while the options menu is on top (`src/menu.cpp`). `locked` means
  armed: whether an entry was found shows only in the game. The client locks it while a seed
  that disallows assists has its save loaded, until the slot receives Assist Unlock.
- **Put back:** `assists free`, the menu closing, a client going away or an unload, into a menu
  still open only, and the order only if the panel is as the helper left it. An unload no frame
  comes for leaves it locked until the options menu closes, harmlessly.

Working in game: 2026-10-23, the lock and Assist Unlock; 2026-10-25, unlocked in another save
("The assist menu").

### New games

While a plan is armed (`ap_newgame`), each frame call:

- **RANDOMIZER:** watches for a randomizer new game the game has queued. If the section holds
  the `expect` bytes (the seed's amounts and checked settings), it writes the `write` edits (the AP seed) once, before
  the game builds the new game from them; otherwise it writes nothing and drops the plan.
- **ARCHIPELAGO:** with `menu` lines, offers ARCHIPELAGO in place of RANDOMIZER on the
  game-mode panel shown after an empty slot is picked (`src/menu.cpp`), after checking every
  handler, size and pointer on the way with fault-safe reads. The panel's handler is then a
  wrapper that fits the border drawn around the button under the mouse to ARCHIPELAGO's eleven
  letters. Choosing it sets the menu's
  state to the seed's settings, writes the edits, runs the game's placement, checks the `expect`
  bytes, writes the edits over the placement, puts the player's menu state back and queues the
  new game (a Custom seed's settings too: working in game, 2026-10-27). For
  Double Teleport (OHKO) it first fills in the menu's start table from
  `start_table` if the menu never built it, and refuses the start if it differs.
- A section of another mode, or a slot the game will not start a game in, drops the plan with
  nothing queued, and the client arms it again.
- **Put back:** RANDOMIZER and the game's handler go back as soon as the menu is not on top, on
  `disarm` and before an unload, but only while that menu is still open (the game frees a closed
  menu's widgets without clearing them). While the panel may still hold the wrapper, the helper
  refuses to unload. Once the new game is queued only the callback and handler go back, so the
  label reads ARCHIPELAGO through the fade.

Working in game: 2026-10-22, Standard and Easy; 2026-10-23, OHKO and the label through the fade
("Starting a new game", "The game-mode panel").

### Other players' items

- **Labels:** `ap_labels` names map cells (this repo's (x, y); the game shows X = y, Y = x) for
  the **stand-ins**, Scrap Energy (47) and Scrap Chunk (48), which the world puts at every
  location holding another player's item (48 for progression, but not for another Zexion
  player's supplies). HEX is the item and whom it is for, joined by a newline; or `@N` and whom
  it is for (another Zexion player's item N, named from its record); or `@N NAME` (item N under
  that name: this player's own OS Install Disk, or another Zexion player's disk, Area Map or
  Intel Chip). See `client/aplabels.py`.
- **Records:** every frame call (`src/apitem.cpp`) checks both records against the game's values
  and, while a save is loaded and no menu is open, restyles them: in-world frames empty in the
  game's art, no refill drop, and each named after the label for the cell of a live stand-in of
  its kind within 96 pixels of the player (chosen, not measured), else the player's cell's, else
  the nearest loaded one's (or AP ITEM). The game's pickup popup then shows the logo glyph, then
  PROGRESSION ITEM (or AP ITEM) over FOR ALICE. The map cursor shows the label's first line
  only: what the item is, not whom it is for.
- **Popup width:** while a stand-in is named, the helper sets the width the pickup code passes
  (an immediate 24) to the label's longest line, after checking the instruction is `BA` with a
  width from 1 to 24.
- **Logo:** it paints Archipelago's logo (with a flashing arrow for progression) into those
  frames and a glyph, through OpenGL in the swap hook, after checking the texture is the 2048 x
  2048 sprite sheet. It leaves the game's texture binding, pixel-store state and GL errors as it
  found them, and paints nothing while a pixel buffer is bound.
- **Put back:** the records (and any an earlier helper left restyled), the width (24) and the
  pixels. Unload refuses (`error an item record is still restyled (no frame came)`) if no frame
  came to do it.
- **Limits:** two stand-ins of one kind for different locations within reach share the nearer
  one's name (not seen); with a translation active the popup shows the translation's text; and
  Scrap Energy in another seed's save would look the same, so the client sends labels only while
  this seed's save is loaded.

Working in game: 2026-10-23 ("Other players' items"); 2026-10-26, a three-game seed: no owner
on the map cursor, other Zexion players' supplies minor ("Map markers").

### Other Zexion players' items, as themselves

- **What:** for a cell labeled `@N`, every frame call (`src/looks.cpp`) dresses the stand-in
  objects there, in the game's pool of live item objects, as item N: N's palette, and the
  frame-list base moved so the frame the game computes lands on N's. The record, and so the
  pickup, stay the stand-in's.
- **Safety:** it writes only stand-ins with the template's base and palette 8, or ones it
  dressed (the same spawn).
- **Other pool:** some rooms show an item through the general object pool; a scan of that pool
  every 31st frame call finds any live object drawing a logo frame and dresses it the same way.
- **Put back:** when the label goes, and with the rest.

Working in game: 2026-10-23 ("Live item objects").

### Intel Chips

- **What:** names the Area Map's record (49) INTEL CHIP and turns off its pickup's area event
  (3551, the map reveal) (`src/intel.cpp`), only if the record reads as the game loads it or as
  the helper left it.
- **When:** each frame call while the client has sent `intel on` (this seed's save loaded, its
  Area Maps Intel Chips).
- **Put back:** `intel off`, with the rest; an unload refuses while it cannot.

Working in game: 2026-10-25 ("Intel Chips").

### The OS Install Disks goal

While the client has sent `discs` (the count, the number needed, the scanner's line in hex, and
TOLD: 1 once flag 39 says the player has been told the number, else 0 and "OF ?"; left out, 1),
the helper:

- writes the scanner's message (Text entry 12) as the client's line (`src/scan.cpp`), only over
  the game's text or its own;
- paints the disk glyph into font glyph `0xFB`;
- with a disk or more, wraps the pause screen's EQUIPMENT callback (`src/equip.cpp`): after the
  game builds the screen, it moves the Energy Tank left and adds a widget for the disk (the
  count as its label), showing OS INSTALL DISK, its icon and description when selected; the game
  frees it with the menu;
- makes the pause screen's KEYS row the disk row (`src/pause.cpp`): the `jne` that builds the
  row only once flag 39 is set made a `jmp`, then a disc and the count, the key slots and icons
  hidden. As soon as a save of a known disk seed (one the client left a file for) is loaded, in
  play or not, the row shows the count from that file until the client's arrives, so a pause
  screen opened at once shows the disks, not KEYS;
- while that row is on, paints its disc, a 24 x 24 white image of the helper's own, at (128,
  1008) in the sprite texture (blank there in the game's art, checked as the glyphs are), and
  points the row's icon sprite at one frame-list entry the helper owns: a copy of the small Data
  Disc's (frame 829) with the image's place, drawn 24 x 24. The entry is allocated once and
  kept for good, never freed, so a pause screen still holding it after an unload points at live
  memory; whenever the image is not painted (the row off, before an unload, a texture it could
  not paint into) the entry shows frame 829's place instead, the game's disc enlarged until the
  screen is closed. A pause screen opened before the image is painted shows frame 829 as is;
- on `mission_prompt` (sent after each save at the landing pod with enough disks), once idle in play
  with no popup of its own to show and the game's last message (GAME SAVED, 180 frames) down to
  its last 150 frames, half a second in (`kFadeMessageLeft`, the owner's pick in game, 2026-10-27;
  until then 70, as far into it as the game's escape skip fade is into Zexion's popup:
  `docs/re/memory.md`, "The mission prompt"; a popup of the helper's own is never cut short),
  opens the game's YES/NO prompt as RETURN HOME WITH THE DISKS? (`src/mission.cpp`) the way the
  game opens it as SKIP ESCAPE AND FINAL BATTLE?: it runs the game's fade to black (area event
  3667, checked byte for byte, through the game's handler for its type) and sets its opener as the transition 60 frames away (the
  pause is blocked while that counts; popups and received deaths wait). Its YES closes the prompt
  through the game's NO and then starts the ending once no message shows; NO fades the screen back
  in, as the game's NO does there. If event 3667 or its handler is not as traced, the prompt opens
  at once with no fade, as before. A request not yet opened lapses (`lapsed: the player left the
  landing pod`) once the player is in play outside the seed's start cell (the loaded section's start
  record's row / 20 and column / 14 against the player state's cell, as the client tests a save at
  the pod: `docs/re/memory.md`, "The mission prompt"; a cell off the map, as the raw cell reads
  for a moment, counts as not known), as when popups held it while the player
  walked on; the client offers it again after the next save there. Once its opener is set, it opens
  where the player is when its 60 frames are up. An opener the helper takes back before it runs (the
  save left, `discs` off, an unload) turns the fade back to the normal level; so does one the game
  runs with the player not in play (a death during the fade: asked again), but only while the fade
  is still event 3667's, as below, not a fade the death set. One another transition took the place
  of (a save at the save point again, which sets the pause opener back and brings no fade; a door; a
  death) is asked again once idle (and lapses if the player is then outside the start cell), and its
  fade is turned back only while the game's fade (the `fade` and `fade_level` targets) is still
  event 3667's: target −1, a step every 8 frames from level 4, or done at −1, and no further back
  than the helper last saw it with its opener in place, nor the transition timer higher (a save
  leaves it counting). Anything else, another transition's fade even with 3667's own values (it
  starts again from level 4, its timer at 70), is left alone. The same when `discs` turns off after
  such a save. Working in game: the fade turned back after saves at the pod during it, 2026-10-27
  (the owner's report). A request lapsing outside the start cell, 2026-10-27 (0.30.0, the owner's
  report, `lab/ap/live34/client1.log`): saved at the pod with twelve received items' popups
  still to show, then walked a few rooms away during them, and left during GAME SAVED with no
  popup queued; no prompt either time, and offered again after the next save there. The ending
  after YES waiting for the popups of five items received while the prompt was open, then
  starting, 2026-10-27 (575edbf, the owner's report, `lab/ap/live35/`). Stand-in tests only: the
  fade turned back when the helper takes its opener back, another transition's fade left alone, a
  death's own fade left alone, and `discs` turned off after such a save.

With `disk_slots` (the save slots holding the seed), and for other seeds it has files for, it
shows each save's count on the file select, COPY FILE and DELETE FILE in place of the keys
(`src/files.cpp`, wrapping each entry's handler): DISK over KEYS, and the count. Nothing shows
for a save with no disk; the client's count wins for its seed's slots.

Put back: `discs` and `disk_slots` with no arguments, and with the rest. An unload refuses while
the scanner's text or the pause screen's branch is still the helper's.

Working in game: 2026-10-24 and 2026-10-25 ("The scanner", "The equipment screen", "The mission
prompt", "The pause screen's KEYS row", "The file select"); 2026-10-26, the reminder after the
received disks' popups (`docs/DEVELOPING.md`), and the pause row with the file's count on a
pause right after loading and the 24 x 24 disc ("The pause screen's KEYS row"); 2026-10-27,
the fade before RETURN HOME WITH THE DISKS?, the offer again after each save at the pod and
YES to the goal ("The mission prompt"; NO from the owner's report); later that day, the fade
turned back after a save at the pod during it (the owner's report).

### AP saves on the file select

- **What:** on the file select, COPY FILE and DELETE FILE, each AP save's entry shows
  Archipelago's logo, the world's AP item logo in its colors, right of FILE NN
  (`src/files.cpp`, with the disk count). The logo is one 16 x 16 image painted over four font
  glyphs empty in the game's art (`0x8C`, `0x8D`, `0x9C`, `0x9D`, two by two in the font), set
  in the entry's blank cells at columns 16 and 17 of rows 0 and 1 and raised 4 px.
- **Which saves:** every 16th frame call, each slot in use that is a randomizer save and whose
  own copy of the section is a known seed's: one the helper has a seed file for, or one a client
  has put (the Feats guard's test, `Game::known_section`, on the slot's copy). So it works with
  no client, and normal and plain randomizer saves are never marked.
- **Colors:** written only over blank cells in palette 26 at style `0x15` (white), which draw
  a glyph in its own colors; anything else (another palette, style or frame there) is left
  alone.
- **No flash:** the same handler wrapper as the disk count rewrites the entry after the game's
  fill, and the frame-call watch catches fills it does not see (a copy, a delete).
- **A new game's fade to white:** it leaves the logo's own colors alone, so while a new game's
  transition runs (`newgame_fn` queued, its 40-frame timer counting down) the helper repaints
  the image whiter each frame, all white on the last, and in its colors again after. A load's
  fade to black darkens the logo with the rest.
- **Put back:** the cells' frames and rows when a save stops being marked, when nothing is
  marked any more, and before an unload; the image is cleared from the texture.

Working in game: 2026-10-26, helper 0.30.0 with this change, with no client and with one: the
file select through paging, COPY FILE, DELETE FILE, a copy of an AP save and its delete, and the
fade to white of a new game started beside an AP save ("The file select"). A new ARCHIPELAGO
game's own save getting the logo, and a plain randomizer save beside it none: working in game
2026-10-27 (0.30.0, the owner's report).

### AP saves without the client

`src/offline.cpp`, `src/game_offline.cpp`; the client side is `client/offline.py`.

- **What the client leaves:** while connected, `offline_put` (when they change) sends the
  addresses it resolved for this build, with the first 16 bytes of each game function, and a
  small file per seed: its item table and start, the Intel Chip and ASSIST settings, the OS
  Install Disks (count, number needed, scanner's line, and this player's own disks not counted
  yet) and the item labels.
- **Where:** `offline_put` checks each file as the reader will (the config with at least 8
  function checks; NAME in `<seed>-<team>-<slot>` form; each file at most 48 KB). The helper
  writes it on the pipe's thread into `%LOCALAPPDATA%\ZexionAP` (`config-<build>.txt`,
  `seeds\<NAME>.txt`; an absolute `LOCALAPPDATA` only), never the game's folder. The helper
  writes them, not the client: a client run from a packaged app has its AppData writes
  redirected (inferred: `docs/re/memory.md`, "AP saves without the client").
- **Reading:** when it starts and each time a client goes away, at most 256 seed files, newest
  first. With no client yet, the cached addresses configure it if they are for its build and the
  game holds every checked function's bytes, even if the cache lacks the client-only keys (a
  cache from before a release that added one: "The pipe"). So the offline look, the map markers
  and the title lines all run with no client once the cache checks out.
- **The look:** with no client, or a client that said the loaded save is another seed's
  (`other_save`), every 16th frame call a save of one of the seeds is dressed once in play as the
  client would: the item labels, INTEL CHIP, the ASSIST lock, the disk count
  (the file's, plus each own disk the map shows collected) and the scanner's line, with one
  notice a load that the client is not connected (or, for `other_save`, is for another seed;
  only for a save of a seed it has a file for, so a plain randomizer game gets no notice).
  While the save stays dressed, each look sets that look again where it was changed: a client of
  another seed, attaching with the save in play, takes its own look back, which is not this
  save's (stand-in tests, `tests/offline_tests.cpp`; working in game 2026-10-26).
  The file select shows each such save's count, from its map in the game's copy of the slot,
  and the AP logo ("AP saves on the file select").
- **Put back:** leaving the save undresses it. A client's `config` takes the offline look off
  first and withdraws a notice not yet shown; the client then asks `client_back`, which pops up
  ARCHIPELAGO CLIENT CONNECTED. once if that save was dressed.
- **Waits for the client:** received items, hints, the goal and RETURN HOME WITH THE DISKS?.
  What was picked up is on the save's map, and the client sends those checks when it next
  connects with the save loaded.

Working in game: 2026-10-25 ("AP saves without the client").

### The map

- **What:** in an AP save holding the seed the labels are for, in play or in a menu (a plain
  randomizer save is never touched), raises the map squares of item 48 (this player's OS
  Install Disks and other players' progression items, but not a Zexion player's supplies:
  `worlds/zexion/world.py`, `important_elsewhere`) from the game's basic marker to its
  important one (`src/mapmarks.cpp`). While the map is open, a wrapper on the map's per-frame
  handler writes the square's label's name over SCRAP ENERGY or SCRAP CHUNK after the game
  writes the cursor's name, in the same frame.
- **When:** the markers every 16th frame call while labels are set, once two of those looks in a
  row have found the seed's save loaded (during a load the section may still be the last
  save's while the grid is the new one's); the names while the map is open and this seed's save
  is loaded.
- **Put back:** the markers are not: the game saves the map grid, and they stay as the game would
  have set them. The handler goes back when the names stop and before an unload, which refuses
  while an open map holds the wrapper.

Working in game: 2026-10-25 ("Map markers"); 2026-10-26, a three-game seed, the markers raised
in a pause right after loading; 2026-10-26, a second three-game seed (`live30`) with the
two-look check, the unseen squares named as the game names its own and the markers still raised
after a save, a return to the title and a load.

### The title screen's lines

- **What:** adds ARCHIPELAGO MOD and its version, and ©2026 MCAWFUL, centered under the title's
  copyright line, and above them, only while no client is active, START THE AP CLIENT TO PLAY A
  SEED (`src/title.cpp`). It makes them with the game's own text functions in that line's
  palette and style, and the game frees them with the title.
- **When:** while configured (by a client or from the cache) and no save is loaded, every 9th
  frame call it looks through the game's draw list for the copyright line without the helper's
  lines. One that failed is tried again on the next title.
- **Put back:** a client going away leaves them; an unload hides them (stand-in tests only).

Working in game: 2026-10-25, the two lines under the copyright, with a client and from the cache
("The title screen"). The client line: working in game 2026-10-27 (0.30.0, the owner's
screenshots), START THE AP CLIENT TO PLAY A SEED centered above the two lines with the client
closed, gone with it open.

### Feats and achievements in AP saves

`src/feats.cpp`; the game's side is `docs/re/memory.md`, "Feats and achievements".

- **What:** while an AP save is loaded, the game earns no Feats and sets no Steam achievement.
  Two changes:
  - **The ending.** Its first Steam call (`ending_unlocks`, `call [SteamInternal_ContextInit]`)
    begins a stretch that sets 14Victory, the ending's database entries, this run's Feats and
    flag 251. The helper writes 12 bytes over the call: `mov byte [flag 251], 0` and `jmp
    ending_join`, which is where the ending goes on, just before it sets flag 250. It writes
    them only if the call goes through `context_init_slot` and the join comes after it, with
    flag 251's store just before it and flag 250's 0x11 bytes after it. Flag 250, the client's
    goal and the save after it are unchanged. With flag 251 clear, the way back to the title
    records no Feat. That holds for a save that already held flag 251 from an earlier ending,
    too.
  - **Steam.** Every achievement call fetches its interface through `context_init_slot`, the
    import slot of steam_api64's `SteamInternal_ContextInit`. The helper points that slot at a
    stub. For `stats_context` the stub returns a stand-in interface whose every method is
    `xor eax, eax; ret`, while it blocks; every other call, the SteamUtils context's included,
    goes on to steam_api64. It decides at each call, with no helper code: it blocks while the
    guard is on, and while an AP save is being loaded. The game's load routine reads
    `current_slot`, sets the randomizer marker from that slot and the save loaded, and ends in
    the map's 15Explorer check, all in one game call before any frame call: a fully explored AP
    save would otherwise set 15Explorer each time it is loaded. So the stub also blocks when a
    save is loaded (`save_loaded` not 0xFF: the load sets it before its tail jump, and the title
    keeps the last save's marker and `current_slot`), the marker word (`rando_marker`) is 1 and
    the slot in `current_slot` holds an AP save, by a table
    of 30 bytes the frame call fills, with no save loaded, from the slots' own copies of their
    sections (`Game::ap_file_slots`, as for the file select's logo, every 16th frame call). It
    keeps the table while a save loads, and clears it once two frame calls in a row find a save
    loaded that is not an AP save. The slot is hooked whenever the stub can block: the guard on,
    or the table not empty. Without `current_slot` (a client-only key, so an offline cache may
    lack it), `rando_marker` or `save_loaded` there is no table; and since the table is filled
    only with no save loaded, a helper loaded in the middle of a save has none until the title
    comes back. The helper hooks the slot only if:
    - the import table names it for that function;
    - it still holds steam_api64's own function (so another hook is never replaced);
    - the context's init function asks for `STEAMUSERSTATS_INTERFACE_VERSION`.
  - **The Feats of Natu screen.** It still follows an AP ending, by a path not traced, with
    nothing flashing. The helper writes ARCHIPELAGO SAVE: NO FEATS EARNED, centered, into the
    blank last line of its title label, under FEATS OF NATU! (`feats_screen_note`). It writes
    only on that screen as it follows an ending: the top menu with 4 panels, two rows of 7
    Feats, a live title widget whose label is three equal lines (the middle one naming NATU),
    and MISSION COMPLETE's statistics under it: a panel of 10 lines and, seen after an ending in a
    seed with Allow Assists on, ASSISTS USED's 8 as a second (read live on B1, 2026-10-27; what
    makes the game add it is not traced). It writes the whole line or nothing, only over a line
    that is blank or already its note, and only if every glyph of it is writable and has the
    font's frames.
    - **When:** after an ending the patch jumped over. The guard went off as the save left with
      the patch in place, flag 250 set and flag 251 clear; a refused patch gives no note. It
      ends when a save is loaded, or when the screen has come and gone. It looks every 8th
      frame call while no save is loaded.
- **An AP save** (`Game::ap_save_loaded`): a randomizer save is loaded (a save, and the marker
  set) whose item table and start are a seed's the helper knows. It knows a seed from:
  - a seed file read from `%LOCALAPPDATA%\ZexionAP` (at start and each time a client goes
    away);
  - one a client has put since (`offline_put seed`), known from the next frame call;
  - the seed the labels were set for.

  That covers the client's own seed, a save played with no client, and another seed's save
  with a client connected (`other_save`). It reuses what the offline look matches saves by,
  rather than the client's word, so it needs no client: the ending can run offline, or while a
  client of another seed is connected. A plain randomizer save matches no seed and is left
  alone. Not covered: an AP save played without the helper, or one whose seed the helper has
  no file for (every new game from ARCHIPELAGO is made with a client, which leaves the file).
- **When:** every frame call while configured. It goes on once two frame calls in a row have
  found an AP save loaded, since during a load the section may still be the last save's. It
  goes off at once: no save loaded (MISSION COMPLETE onward, the title), a normal save, a
  randomizer save of no known seed. The load test (above) is set with no save loaded, while the
  guard is off. `hello` reports `feats=off`, `on`, or `on:` and why a change could not be made:
  the guard's state, not the load test's (with the guard off it reads `off`, the test set or
  not).
- **Put back:** both as soon as no AP save is loaded, and before an unload; Steam's slot stays
  hooked with the guard off while the load test is set (the title and the file select, with an AP
  save in a slot), the stub then passing every call but a load's through (at the title, a
  menu's 12GetBonus or 13SecretCode reaches Steam). It stays on when a client
  goes away, since it applies with no client too. The ending is put back only where it still reads
  as the helper wrote it; while it does, an unload refuses (`error the ending's patch is still the
  helper's; not unloading`). Steam's slot is put back even if no frame comes, but only if it still
  holds the stub, with the load test turned off. The stub, the stand-in and its vtable are in two
  pages allocated once and never freed. A call that read the slot just before, or still holds the
  stand-in, so lands in live memory that runs no helper code, and an unload need not wait for it. If
  something hooked over the stub since, the stub stays in its chain and passes everything through.
  The screen's note is blanked again before an unload; the game frees it with the screen otherwise.
- **An unload leaves a gap:** from the unload to the next helper's second frame call (the
  client's `/reload_mod` waits 3 s before loading it again), an AP save in play has no guard, so
  a pickup's achievement (a weapon, 10Burst, 11Armor and the like) then reaches Steam, and so
  would an ending the game itself starts then (the escape's). RETURN HOME WITH THE DISKS?'s
  ending is not exposed: an unload refuses from its YES until the ending has run
  ("Unloading"). Closing the gap would mean keeping the slot and the patch across an unload for
  the next helper to take over, which is more than a few seconds' gap is worth, so it stays,
  documented here (by code reading; not measured in game).
- **Left as it was:** database entries an AP game unlocks (lore, game-wide) stay in
  `GameUnlocks.sav`. They can complete the database and give 16CompleteReport later, in a
  normal game.

Working in game: 2026-10-26 ("Feats and achievements"), two AP endings with the guard (a third,
earlier, is inconclusive: which helper ran is not known), both of the OS Install Disks goal
through RETURN HOME WITH THE DISKS? with the client connected, and a normal ending. In the AP
endings the ending was jumped over, the goal was sent, no Feat was earned and `GameUnlocks.sav`
was unchanged; the second showed the note. In the normal ending the Feats were earned as usual.
The guard also went on in an AP save with no client connected, before the client connected.
Those runs had the Steam stub as it was before the load test (feats.h).

Working in game 2026-10-27 (0.30.0, the stub as it is now; `lab/ap/live34`, "Feats and
achievements" in `docs/re/memory.md`):
- an AP ending through RETURN HOME WITH THE DISKS?, the client disconnected and connected again
  during its cutscenes: the note shown, `GameUnlocks.sav` rewritten and unchanged;
- after an AP save was quit to the title, with AP saves in their slots, an achievement reaching
  Steam (13SecretCode, typed at the title; Steam's record), Steam's slot read on the stub at that
  title just after;
- a plain randomizer save in play with a client connected: Steam's slot back on steam_api64 and
  the ending's call the game's own (read from memory), no notice and no look.

Working in game 2026-10-27 (575edbf's 0.30.0, then the note's fix in d451e6b; `lab/ap/live35` and
`lab/ap/live36`, "Feats and achievements" in `docs/re/memory.md`):
- with 575edbf, an AP ending through RETURN HOME WITH THE DISKS? with the client connected: the
  goal sent, nothing lit on the screen, the note shown, `GameUnlocks.sav` rewritten and
  unchanged;
- with 575edbf, an AP ending through the escape skip with no client (the helper's offer, retired
  since; a core-goal seed with Allow Assists on): flag 251 left clear and `GameUnlocks.sav` unchanged, but no note.
  MISSION COMPLETE had ASSISTS USED under it as a second panel, which the screen check refused;
  fixed in d451e6b;
- with d451e6b, the same save's ending through the escape skip with a client of another seed
  connected: flag 251 clear, `GameUnlocks.sav` unchanged, nothing lit, and the note shown over
  ASSISTS USED.

Not seen in game:
- Steam's stand-in stopping an achievement. No achievement the owner has left is cheap to earn in
  play (13SecretCode is taken at the title only), so this is tested only with the stand-in;
- the load test: a fully explored AP save loaded from the file select earning no 15Explorer
  (by disassembly and the stand-in only; the owner's 15Explorer is earned already).

## Building

Windows only: Visual Studio 2022 or later with the C++ workload, and CMake 3.20 or later, with a
multi-config generator (the default Visual Studio one; `CMakeLists.txt` refuses single-config
ones, and another compiler than MSVC). From the repo root:

```sh
cmake -S native -B native/build -A x64
cmake --build native/build --config Release
ctest --test-dir native/build -C Release --output-on-failure
```

The helper lands in `native/build/Release/zexion_ap.dll` (gitignored), the proxy in
`native/build/proxy/Release/version.dll`. The C runtime is linked statically, so the helper needs
nothing the game doesn't already load.

CI builds both, runs the ctest tests, and runs `tests/test_native.py` on Windows, which loads
the build into a sleeping Python process (no known build, so nothing is hooked) and checks the
pipe and unloading. That test skips without a build.

Most ctest tests run against **the stand-in game**: the test program's own data and functions,
laid out as the helper expects the game's (`tests/stand_in.h`, `tests/stand_in_menu.h`) and
configured by RVA like the real game. A test's source is `tests/<name>_tests.cpp`
(`apply_test.cpp`, `unload_test.cpp` and `proxy_test.cpp` for `apply_in_frames`,
`unload_under_load` and `proxy`). They share `tests/check.h`;
`tests/fake_gl.h` is the fake OpenGL of `apitem` and `offline`;
`tests/pipe_client.h` is the pipe client of `parts`, `apply_in_frames` and
`unload_under_load`, and the last two's busy threads and wait for an unload.

- `parts`: the import-table hook and the trampoline (an exception raised behind it caught in
  front), `game_mem.h`'s menu, panel and label readers and its handler-slot swap, the build check
  and the pipe.
- `requests`: the request parsers, the popup text's layout and the mailbox.
- `game`: the `discs` and `@N NAME` parsers, configuring, applying, the popup queue, the
  new-game watch, a whole ARCHIPELAGO press, what a refused unload sets
  again, the received shield and death, the ASSIST watch and the scanner's text, and RETURN HOME
  WITH THE DISKS? through the frame call (its fade and popups, a save or a door in its place, a
  request lapsing outside the start cell, a death's own fade left alone, the ending after YES
  waiting for the last message, and the ending's wait that an unload refuses for).
- `menu`: the game-mode panel and the options menu (relabeling, putting back, closed menus and
  released widgets, the ASSIST lock's look and cursor order).
- `apitem`: the stand-in records restyled and named, the logo, the font glyphs, the pause
  row's disc and the file select's logo in its colors painted through a fake OpenGL, the label
  parser.
- `equip`, `pause`, `files`, `intel`, `title`, `mapmarks`, `mission`, `offline`: the part of
  that name (`files`: the disk count and the AP logo; `offline`: the seed file, its match and
  disk count, the folder, the cache check, `offline_put`, a stand-in game dressed and
  undressed, the AP saves' slots, and the file select's logo through the frame call: painted,
  whitened through a new game's fade, and cleared by `restore_files`).
- `feats`: Feats and achievements in AP saves. It covers the AP-save test in each case (no config, a
  plain randomizer save, a normal save, a put seed, a seed file with no client and with a client of
  another seed, the labels' seed), the guard on and off, and a stand-in ending run with and without
  the patch. It also covers the Steam import hooked and passed through, the stub's test while an AP
  save loads (by slot, marker and context, kept and dropped, off with `release_steam`, none without
  `current_slot`), the refusals, putting back with and without a frame, and the Feats of Natu
  screen's note (on a stand-in screen, after an AP ending only). The program imports
  `SteamInternal_ContextInit` by name from a stand-in `steam_api64.dll` (`tests/steam_stub.cpp`), as
  the game does. Both are built into `build/feats/`, apart from the helper.
- `proxy`: its exports give what Windows' own give, and it loads no helper outside Zexion.
- `apply_in_frames`: through the real pipe and hook in a busy host (a batch and its popup,
  a batch no frame took, an unload refused while something else is hooked over the
  helper, with what the client had set armed again).
- `unload_under_load`: a test build, `zexion_ap_hooktest.dll`, hooks the loading program's own
  `GetTickCount` import (it refuses to hook a known game build), counts calls, and survives 300
  loads and unloads while two threads call it nonstop.

## Source map

`src/`:

| File | What |
|---|---|
| `dllmain.cpp` | the hook, the pipe's requests, the wrappers the game's menus call |
| `game_calls.cpp` | `Game`: configure, grants, popups, shields, deaths, each part's watch |
| `addresses.h` | `GameAddresses`: the configured addresses, one per `config` key, which every part reads (the client-only ones null after an offline cache lacking them) |
| `game_offline.cpp` | `Game`'s offline look |
| `requests.cpp` | the requests' parsers |
| `text.cpp` | the popups' text layout |
| `pipe.cpp`, `mailbox.cpp` | the pipe; the hand-off to the game's thread |
| `iat.cpp` | the import-table hook and trampoline |
| `build.cpp` | the known builds |
| `game_mem.cpp`, `game_layout.h` | fault-safe memory access; the layouts several parts share |
| `apitem`, `looks`, `menu`, `intel`, `scan`, `equip`, `pause`, `files`, `mission`, `offline`, `mapmarks`, `title`, `feats` | one part each, as named above (`menu`: ARCHIPELAGO and the ASSIST lock; `feats`: Feats and achievements in AP saves) |

`proxy/` is the proxy `version.dll`.
