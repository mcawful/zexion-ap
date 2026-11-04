# The native helper

`zexion_ap.dll`, **the helper**, runs inside Zexion and does what the client cannot from
outside: work on the game's own thread, between frames. It gets in two ways: **the proxy**
`version.dll` in the game's folder loads it as the game starts, or the client loads it on
attaching (`client/native.py`; `tools/helper.py` too, for tests). The client talks to it over a
named pipe. With no client it changes only the title screen and the look of AP saves, and keeps
Feats and Steam achievements out of AP saves.

An **AP save** is a randomizer save whose item table and start match a seed a client has
played (and its location list's size: 154, or 155 with Secret Shuffle, `src/game_layout.h`,
`rando::locations`). A **wrapper** is a helper function the game's menus are pointed at in place of their
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
- A helper whose `hello` the client can't read counts as another version. Without a build it is
  refused, not unloaded: where it came from is not known, and the game folder's copy is likely
  the same file.
- A helper of the client's version that disagrees with it is another build of that version:
  it answers `error unknown request` or `error unknown line …`, refuses `config` with
  `error missing NAME` (it ignores names it does not know), reads `build=unknown` for a game
  build the client knows, or has no `lang` in its `hello` (1.0.0's release candidate, from
  before the translations: it would take the translation keys and ignore them) or no `case` (a
  build from before the mixed case: it would refuse the client's scanner line, and with it every
  `discs` request and the offline file), with a build of the client's own loaded in its place,
  as for another version (a build of its own that loads as such an earlier one is refused and
  not loaded again until the file changes, as a stale one is; `tests/test_native.py` only, not
  seen in game). Found while attaching, it is refused like
  another version; found in a later request (a side task's, or giving items), the client stops
  using it until the game restarts.
- Each of these, in the client's window: "The Zexion mod and this client don't match", with
  both versions (or "different builds") and what to do (`state.helper_failed`).
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
- It carries no version (no version resource, and it never talks to the client), so the client
  can't tell one release's proxy from another's. Its code is unchanged since it was added
  (0.20.0): every proxy so far does the same.
- Assumed, not tested: Steam's verify and updates check only the game's own files, so both
  DLLs stay. An antivirus may flag the unsigned proxy.
- Seen 2026-10-29 (develop d6bbdc9, a local build of the proxy, not a release): Windows Defender
  detected it as `Program:Win32/Contebrew.A!ml` and blocked loading it (error 225); an earlier
  build of the same sources was not flagged. Not checked against a release build.

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
| `hello` | `zexion-ap <version> case=1 build=<B1\|unknown> hooked= configured= feats=<off\|on\|on:why> folder= offline=<-\|N[:FILE]> active= title=<0\|1\|why> lang=<-\|NAME\|?> reads=<R/S>` (flags 0 or 1), then ` cache_missing=<keys>` when configured from a cache lacking client-only, translation or later keys, ` read_error=<why>` after a failed read | below |
| `ping` | `pong frames=<calls since load>` | |
| `config name=rva …` (frame) | `ok`, or `error <why>` | below |
| `offline_put config` or `offline_put seed NAME`, a newline, the file | `stored`, `unchanged`, `error <why>` | AP saves without the client |
| `apply` and lines `cas ITEM EXPECTED VALUE` (inventory bytes 0 to 49), `refresh`, `popup ITEM SUFFIX`, `notice ITEM TEXT` (frame) | `applied`, or `refused <why>` with nothing written | Grants; Popups |
| `ap_newgame` and lines `identify`, `expect`, `write` or `menu OFFSET HEX`, `start_table HEX`, `choice` (frame; `menu` lines need `identify` lines, `choice` needs `menu` lines) | `armed`, `refused <why>`, `error <why>` | New games |
| `newgame_status`, `disarm` (frame) | `armed`, `idle`, `applied slot N`, `refused <why>`; `disarmed` | New games |
| `ap_labels` and lines `label X Y HEX` (frame) | `labeled N`, `refused <why>` | Other players' items |
| `shield`, `die` (frame) | `spawned`, `died`, `refused <why>` | Received Energy Shields; DeathLink |
| `assists lock`, `assists free` (frame) | `locked`, `free`, `refused <why>` | The assist menu |
| `intel on`, `intel off` (frame) | the same, or `refused <why>` | Intel Chips |
| `discs COUNT REQUIRED HEX TOLD`, `discs`; `disk_slots COUNT REQUIRED SLOTS`, `disk_slots` (frame) | `discs on`, `discs off`, `disk_slots on`, `disk_slots off`, `refused <why>` | The OS Install Disks goal |
| `discs_status`, `mission_prompt COLUMN ROW`, `mission_status` (frame) | `shown N` or `not shown…`; `queued`, `refused not configured` or `error <why>`; `not asked`, `asked`, `opening after the fade`, `opening without the fade: …`, `shown`, `lapsed: the player left the landing pod`, `yes: ending when idle` (`, the screen kept black` or another note after it), `ending started` (until another save is loaded; the client reads it), … | The OS Install Disks goal |
| `mission_black` (frame) | how the last black after RETURN HOME's YES ended: `none`, `kept black` or `ended: …` | The OS Install Disks goal |
| `client_back`, `other_save` (frame) | `shown`, `none`; `told`, `already told`, `unknown` (no known seed: a plain randomizer game, left alone); or `refused <why>` | AP saves without the client |
| `unload` | `bye`, or `error <why>` | Unloading |

`hello`'s fields (spaces inside a field written as underscores):

- `feats`: Feats and achievements kept out of the AP save loaded (off, on, or on with why a
  change could not be made).
- `folder`: loaded from the game's own folder (by the proxy, or by a client with no build).
- `offline`: the seeds known to the offline look (read from the folder, and any a client put or
  that were read while it was connected), and the file of the one dressed.
- `case`: always 1: the helper takes the client's text in mixed case (English(HD), "Translations").
- `active`: a client has configured the helper and not gone away.
- `title`: the title screen's lines are up (1), not (0), or why not.
- `lang`: the language the game shows: `-` its default English, the loaded translation's folder
  name while one is on (`English(HD)`, `French`, `Brazilian_Portuguese`, …), or `?` when not known
  (not configured, the translation keys missing, or nothing readable; and with no frame). The
  client warns in any translation but English(HD) (`client/ticks.py`, `language_tick`).
- `reads`: the reads a frame has taken, and the seeds the last one found.
- `cache_missing`: the client-only, translation and later keys (below) the offline cache lacked,
  comma-separated. With a client-only one among them, `configured=1` means the offline look runs
  while the client's parts still refuse; with translation keys, the translation parts stand down
  ("Translations").

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
  that each way a client's state comes in waits for every key. The translation keys, marked
  `translation_key` (the translation object, its tables' pointers and counts, the HD message
  objects and slots, and the game's text functions), may be missing from the cache too: the
  helper then configures with them unset, every other part as usual (the client's parts ready if
  the client-only keys are there), and the translation parts stand down ("Translations"), so a
  cache written before them keeps the default English as it was; but for `large_count`, added
  after: without it only the Feats screen's title keeps its pixel look in a translation (stand-in tests:
  `tests/offline_tests.cpp`; seen in game 2026-11-02, B1, `zexion_ap.dll` `1c286c57…` loaded by
  the proxy over a cache from before them: `configured=1 lang=?` and the 13 keys in
  `cache_missing`, none missing after the client's first run, `lab/ap/live50`). Only `src/translation.cpp` reads them (`tests/test_cross_checks.py`);
  the later keys, marked `later_key` (`options_back_fn` and `pickup_list`, newer than caches
  already written), may be missing too: the helper configures without them, every part as usual
  but for what one serves (without `options_back_fn` the pause row is turned on OPTION MENU's way
  back a frame late, as before it: below; without `pickup_list` an AP save's major-item list is
  not filled: "Grants"), until a client's `config` gives them (stand-in tests:
  `tests/offline_tests.cpp`; working in game 2026-11-03, B1, `zexion_ap.dll` `fce9e73b…`, the
  1.0.0 RC at develop 76df074, over a cache from before `options_back_fn`, no client:
  `cache_missing=options_back_fn` in `tools/helper.py status` and Keys for a frame on the way back
  from Option, as above; after a client's run the cache rewritten, no `cache_missing`,
  `lab/ap/live50/client-r5.log`);
- each lies inside the game's image: functions in its code, data outside it and writable;
- the exception being import slots, which lie outside code: the C runtime's `malloc` and
  `free`, only read, and steam_api64's `SteamInternal_ContextInit`, written while an AP save is
  loaded or a save slot holds one ("Feats and achievements in AP saves").

The other arguments are described in `src/requests.h` and in the parts below. Anything else gets
`error unknown request`; errors always start with `error `. A "frame" request while the helper
is not hooked gets `error not hooked, so nothing runs on the game's thread`.

### On the game's thread

A "frame" request (and `hello`'s `configured`, `feats`, `active`, `offline`, `title`,
`lang` and `cache_missing`) goes to the game's thread through a one-slot mailbox
(`src/mailbox.cpp`) and runs at the next frame call. If no frame takes it within a second (the game paused in the
background, say), it is withdrawn, the reply is `refused no frame`, and it never runs
afterwards.

The frame call never waits on anything the pipe thread can hold: it takes the mailbox's work
with one atomic exchange, and only one thread at a time does the work.

### A client going away

When the pipe's client disconnects (the client exited or crashed, let the helper go while it
had no server connection, or `tools/helper.py` was done), the next frame call, before it serves
any request, puts back everything the parts below set (`Game::put_back_all` in
`src/game_calls.cpp`). A change held by a menu no longer open is gone with it. It goes inactive
until a client sends `config` again (the client resends every request after it attaches). The
client's popups not shown yet stay queued and still show ("Popups"): their items are already
given. The offline look then applies again (AP saves without the client). The helper stays
loaded and hooked.

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
That is an open menu's panel or entry, the pause menu's way back from OPTION MENU, the open map or
the game's transition routine holding one of its wrappers, or the scanner's text, the pause screen's branch, an item record, the
ending's patch or the map's on-screen marker write still the helper's; or, in a translation, a
popup still showing the text the helper wrote into a borrowed entry (until that message ends: 3
seconds in play), or a translated name or the popups' moved entry 70 still the helper's when no
frame came ("Translations"). It refuses (`error the ending RETURN HOME WITH THE DISKS?
started is on its way; not unloading`), before putting anything back, from RETURN HOME WITH
THE DISKS?'s YES until the helper has started the ending (and while the screen is still kept
black after it, until the helper's wrapper has run the ending routine), and while the ending
routine or that wrapper is the game's transition with the ending's patch in place: an unload
would drop the ending, or put
the patch back before the ending runs ("Feats and achievements in AP saves"). That window is
a few frames, longer while received items' popups (the helper's own) hold the ending after YES.
Working in game 2026-10-27 (575edbf, 0.30.0, `lab/ap/live35/client1.log` and
`reload_spam.txt`): `/reload_mod` sent every half second from the open prompt through YES,
refused as a menu open while the prompt showed, then twice as the ending on its way while five
received items' popups held it; the helper stayed, and the ending ran and sent the goal. That
run was before the ending's wrapper: its refusal and its put back on unload are stand-in tests
only (`native/tests/mission_tests.cpp`, `game_tests.cpp`).
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

- **Limits:** "working in game" below is in the game's default English. With a translation on
  the game draws the translation's text, in its own font, in place of the pixel-font glyphs the
  helper rewrites; how each part's text shows there is under "Translations" (the helper's fix,
  run in game 2026-11-02 with the results and bugs listed there). Seen in English(HD), French and Japanese on 2026-11-01 (B1, helper 1.0.0, the release
  candidate), before that: what a part changes in what an entry does worked, but what it changed
  in the text did not show (the ASSIST padlock, OS INSTALL DISK, the disk row's count,
  ARCHIPELAGO, RETURN HOME WITH THE DISKS?, a stand-in's name in its pickup popup), and many
  received items' popups showed nothing; the title lines, the file select's logo and disk count,
  and the stand-ins' logo worked (`docs/re/memory.md`, "Translations").

### Grants

An `apply` (`src/game_calls.cpp`):

1. Refuses unless the game is in play: a save loaded, no menu, energy above 0; and in an ending
   RETURN HOME WITH THE DISKS? started, whose cutscenes read as play (`refused RETURN HOME WITH
   THE DISKS? ending`, "The OS Install Disks goal").
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

**The major-item list** (`pickup_list`; the file select's ITEMS row draws each save's): every
frame call in play in an AP save (`Game::watch_pickups`), the helper appends to it each owned
item the game's pickup would list (`pickups::listed`, `src/game_layout.h`) whose icon is not in
it yet, by item ID, stopping at 26 as the game does (it reads the records' icons with
`safe_read`). The game's pickup lists this world's own items found in it; the received items
are what the helper adds. It leaves out what the new game started with (the section's
start-with array, `rando::started_with`), as the game does (Standard's Machine Gun); and an
older save's items, owned before this helper, go in by item ID, not in the order they came. So an AP save's row fills as items arrive, and an
older one's the first time it is played with this helper; it shows on the file select once the
game saves. It runs without a client too, once the client has written `pickup_list` into the
offline cache (`helper.py status` shows `cache_missing=pickup_list` until then). The list is
the game's own data, so a client going away or an unload leaves it (`docs/re/memory.md`, "The
pickup record holds the major-item list").

Working in game: 2026-10-22, everything above but the major-item list ("Game functions the helper
calls", "The client in a live game"); the major-item list, 2026-11-04 (B1, `zexion_ap.dll`
`65878411…`, the build of develop 1acaaaf, the owner's report: an AP save's ITEMS row on the file
select filled; the Machine Gun's row in a Standard save not singled out).

### Popups

- **When:** one at a time, in a frame call, in play only, once the game's last popup has ended
  and at least 20 frames after the helper's last, and not while RETURN HOME WITH THE DISKS? is
  on its way (its fade, before the game opens it), nor in the black after its YES (a popup
  waiting ends the black) or the ending it started (until the save's next load). Each plays
  the item's own pickup sound.
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
- **Text:** a `popup` is the item's icon and name from its record, then the client's suffix (≤ 40
  characters); a `notice` is its text alone (≤ 200). Both are wrapped at the game's 24-character
  width (`src/text.cpp`; `docs/re/memory.md`, "Game functions the helper calls"), whatever
  width the helper has set for the game's own pickup popup ("Other players' items").
- **Stand-ins received:** a received Scrap Energy or Scrap Chunk (this player's own filler, found
  in another world) is named SCRAP ENERGY or SCRAP CHUNK with no icon, as the game loads them,
  not from its record, which the helper may have restyled as another player's item ("Other
  players' items"). Stand-in tests (`tests/game_tests.cpp`); working in game 2026-11-02 (B1,
  `zexion_ap.dll` `1c286c57…`, `lab/ap/live50`), each sent from the server's console: Scrap
  Energy away from any stand-in and Scrap Chunk in a labeled stand-in's room, in English(HD),
  read Scrap Energy and Scrap Chunk over FROM ARCHIPELAGO; in the default English, SCRAP ENERGY
  FROM ARCHIPELAGO.
- **Follow-ups:** an `apply` may hold popups alone, such as the client's notice for an Area Map
  or Intel Chip found in the world, after the game's own popup.
- **In a translation:** popups and notices show in the translation's font, from a borrowed
  table entry; one may wait a frame or two for it ("Translations"; working in game 2026-11-02
  in English(HD), French and Japanese).

Working in game: 2026-10-24, notices up to five lines ("Game functions the helper calls");
2026-10-25, a notice after a found Intel Chip's popup ("Intel Chips").

### Received Energy Shields

- **What:** `shield` runs the Energy Shield's pickup event as its pickup does, from the player's
  cell (`spawn_shield`, which checks the record, the event and its handler first).
- **When:** once the player has been in play in one cell for a second, and not in an ending
  RETURN HOME WITH THE DISKS? started (`refused RETURN HOME WITH THE DISKS? ending`). The client
  asks for each received shield not yet given, again for one not seen up within a few seconds,
  and counts it once it sees the shield up; after three requests in a row not seen up it asks
  for none until the save is left or the game closes, or another slot connects
  (`client/shields.py`).
- **Put back:** nothing to put back.

Working in game: 2026-10-23 ("Received Energy Shields"). A bug, 2026-11-02 (B1, `zexion_ap.dll`
`946b08a1…`, `lab/ap/live50/client-r3.log`): with a client attached to a save already in play
(slot 8, past its ending), a received shield came up (seen around the player in the owner's
screenshot) but the client never read it as up, so it spawned it again three times a round,
a round about every minute, until the save was left; it still missed after the next load, and
read a shield up at once on the load after the ending replayed. Fixed since in the client
(its read of the shield, and the stop above): working in game 2026-11-03 (B1, `zexion_ap.dll`
`fce9e73b…`, the 1.0.0 RC at develop 76df074, `lab/ap/live50` slot 4, `server-r6.log`,
`client-r6.log`, `client-r7.log`): the client attached to the save in play, a shield sent, one
spawn, read as up at the first look; nothing more over three minutes and a leave and reload; three
more shields, the last refreshing the one up (the owner's report), each spawned once and read as
up, none spawned again. The stop was not reached (`docs/re/memory.md`, "Received Energy Shields").

### DeathLink

- **What:** `die` deals a received death as the game's damage code does: energy to 0, then the
  game's death routine (`kill`), through an Energy Shield too.
- **When:** once the player has been in play in one cell for a second, the game's halt byte is
  clear, no message is showing (the routine posts one without checking it was made) and
  RETURN HOME WITH THE DISKS? is not between its fade and its prompt, nor in the black after its
  YES or the ending it started; otherwise `refused` (`not configured`, `not settled`, `halted`,
  `message showing`, `RETURN HOME WITH THE DISKS? opening`, `RETURN HOME WITH THE DISKS?
  ending`, `no frame`). The client queues received deaths and asks once a second, one at a time
  (`client/deathlink.py`); it drops the queue while an ending that began in this load runs
  (`Session.holding`, also an ending played again through RETURN HOME); a save that had already
  ended deals them as any save does until its ending is played again (stand-in tests only).
- **Put back:** nothing to put back.

Working in game: 2026-10-23 ("DeathLink"); 2026-11-02 (B1, `zexion_ap.dll` `946b08a1…`,
`lab/ap/live52/carol.log`), a death received during an ending that began in that load (RETURN
HOME's YES on a disks seed) dropped, and none dealt on the next load.

### The assist menu

- **What:** while locked (`assists lock`), the options menu's ASSIST entry is drawn in STYLE:
  AUTO's style with a padlock after it (font glyph `0xEF`, painted while armed), its callback is
  cleared so pressing it does nothing, and it is moved past the panel's selectable entries so the
  cursor skips it (only when each entry it moves is live and numbered by its place).
- **When:** each frame call while the options menu is on top (`src/menu.cpp`). `locked` means
  armed: whether an entry was found shows only in the game. The client locks it while a seed
  that disallows assists has its save loaded, until the slot receives Assist Unlock.
- **Found again each frame:** never taken on the pointer kept from the frame it was locked.
  Leaving Video, GENERAL (which holds the language) or the assist menu closes the options menu
  and builds another (`0x124D30`), whose widgets may take
  the freed ones' places: its VERSION 1.20, 12 glyphs with no callback like the locked ASSIST,
  in ASSIST's (`docs/re/memory.md`, "The assist menu"). Each frame call the helper finds the
  entry again in the menu open now: a live widget with the generic handler, its callback
  cleared, its glyphs reading ASSIST and the padlock, in the panel it was locked in. It writes
  only into one that passes; any other is forgotten unwritten, and the new menu's ASSIST is
  locked. One the game relabeled stays shut until its menu is built again. (Stand-in tests,
  and run in game 2026-11-03, B1, `zexion_ap.dll` `7a72e4de…` from PR #89, in English(HD):
  Option, Video and back several times, the map and back, then the default English; the
  owner: "seems to work". The bug it fixes showed only sometimes, so a clean run is weaker
  evidence than a repro. GENERAL and the assist menu: stand-in tests only, not run in game.)
- **Put back:** `assists free`, the menu closing, a client going away or an unload, only into
  the entry found again as above, and the order only if the panel is as the helper left it. An
  unload no frame comes for leaves it locked until the options menu closes, harmlessly.
- **In a translation:** while locked, the entry's translated text (SmallText 137) is built
  again with the HD padlock after it ("Translations", the helper's icons; in English(HD) with
  no space before it, the padlock's cell holding its own gap), the padlock left out where it
  would pass the options box's right edge (its border's top right piece), since a translation
  sizes the box from its own texts; the entry's text again when unlocked. Built again whenever
  the game builds the label again. Each frame call with the menu on top, every translated
  glyph gets the style the options menu gives its entries (`0x15`) and alpha 0.45, a gray on
  the black screen: a translated glyph's color is its style's through a table the screens
  rewrite, so the locked style 5, gray on a first open, took the cursor's colors after the
  video options or the map (`docs/re/memory.md`, "The assist menu"); building it again gives
  alpha 1.0 back. (Seen in game 2026-11-02 in English(HD) as the pixel look it replaced, its
  padlock past the box. Then run in game later that day, `zexion_ap.dll` `946b08a1…`: the entry
  in the HD font, gray, its padlock inside the box but low and against its border; after coming
  back from another menu (Video, the map) it took the selection's colors (a bug); once it showed
  unlocked after going through menus, not reproduced (a bug): "Translations", "Run in game
  again". Fixed since, stand-in tests only, not run in game yet: the look above, the padlock
  on the text's line with no space before it, and the offline look held through menus ("AP
  saves without the client"). On the 1.0.0 RC (develop `78320a6`) the owner then saw, sometimes,
  after Video and back in English(HD): the version line gray with the padlock in place of its
  blank and ASSIST open, white with no padlock; that is the reused place above, fixed since by
  finding the entry again each frame, run in game 2026-11-03 as above. On the RC itself, B1,
  `zexion_ap.dll` `fce9e73b…` (develop 76df074), the owner's own check the same day: the assist
  lock in English(HD) working, by the owner's report, which did not single out the look's parts
  above, nor GENERAL or the assist menu.)

Working in game: 2026-10-23, the lock and Assist Unlock; 2026-10-25, unlocked in another save
("The assist menu").

### New games

While a plan is armed (`ap_newgame`), each frame call:

- **RANDOMIZER:** watches for a randomizer new game the game has queued (by the player's own
  START GAME). If the section holds the `identify` bytes (the seed's text, typed into the
  menu), it places the seed over the player's placement as ARCHIPELAGO does (below: the menu
  set to the seed's settings, the placement run, the menu put back, `expect` checked), so the
  game is the seed's down to the Hints' map rectangles, which only the placement draws
  (`docs/re/randocells.md`, "What each setting does in the game"); then it writes the `write`
  edits (the AP seed) once, before the game builds the new game from them. Otherwise it
  writes nothing and drops the plan; if the seed's placement is refused (or throws), the
  player's own placement is put back first, so the queued game is the one they made. Over a
  RANDOMIZER game the game's own start table is used, not the plan's (the client's fallback
  when the two differ), and the seed's second start must be one of its rooms. A plan without `menu` lines is only checked against
  `expect` as the player's placement made it (the client always sends them). Working in
  game: 2026-10-27 (`docs/re/memory.md`, "Starting a new game").
- **ARCHIPELAGO:** with `menu` lines and `choice`, offers ARCHIPELAGO in place of RANDOMIZER on the
  game-mode panel shown after an empty slot is picked (`src/menu.cpp`), after checking every
  handler, size and pointer on the way with fault-safe reads. The panel's handler is then a
  wrapper that fits the border drawn around the button under the mouse to ARCHIPELAGO's eleven
  letters. In a translation the label's translated text is built again as Archipelago (the game
  sizes that border itself), and from SmallText 354 again when RANDOMIZER goes back, where the
  game's attach would build it now ("Translations"; run in game 2026-11-02 in English(HD): the
  label built where the game first built it, at the window's corner, a bug listed there, since
  fixed and working in game later that day, `946b08a1…`: "Translations", "Run in game
  again"). Choosing it sets the menu's
  state to the seed's settings, writes the edits, runs the game's placement, checks the `expect`
  bytes, writes the edits over the placement, puts the player's menu state back and queues the
  new game (a Custom seed's settings too: working in game, 2026-10-27). For
  Double Teleport (OHKO, or a Custom seed with it on) it first fills in the menu's start table
  from `start_table` if the menu never built it, and refuses the start if it differs.
- A section of another seed (its text) or mode, or a slot the game will not start a game in,
  drops the plan with nothing of the seed written, and the client arms it again.
- **Put back:** RANDOMIZER and the game's handler go back as soon as the menu is not on top, on
  `disarm` and before an unload, but only while that menu is still open (the game frees a closed
  menu's widgets without clearing them). While the panel may still hold the wrapper, the helper
  refuses to unload. Once the new game is queued only the callback and handler go back, so the
  label reads ARCHIPELAGO through the fade. Every write goes only into a choice still holding
  the helper's callback (and the handler only over the helper's own): the hover border too,
  which the panel's wrapper fits inside the game's frame, where a menu closed and built again
  since the frame call could have put another choice in its place.

Working in game: 2026-10-22, Standard and Easy; 2026-10-23, OHKO and the label through the fade
("Starting a new game", "The game-mode panel"); 2026-10-29, a Custom seed with Secret Shuffle
by ARCHIPELAGO and by RANDOMIZER with its text, each passing the client's `expect` on the
section's 155 cells, the two sections equal byte for byte (develop e088773, the helper loaded by
the proxy, `zexion_ap.dll` SHA-256 `9b609b92…`; `lab/ap/live46`).

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
  PROGRESSION ITEM (or AP ITEM) over FOR ALICE (in English(HD), Progression Item or AP Item
  over for Alice, as the client writes them: "Translations", mixed case). The map cursor shows
  the label's first line only: what the item is, not whom it is for.
- **Popup width:** while a stand-in is named, the helper sets the width the pickup code passes
  (an immediate 24) to the label's longest line, after checking the instruction is `BA` with a
  width from 1 to 24.
- **Colors:** every live stand-in not dressed as a Zexion item draws in palette 26 at style 34
  while they are restyled (`src/looks.cpp`, `ItemLooks::own_colors`): palette 26 draws the
  texture's own colors, under white (cream with the fourth PALETTE display option), where the
  game's palette 8 follows the suit, the weapon and the room, so the logo kept changing colors
  (`docs/re/memory.md`, "Other players' items"). An
  object is colored only with the template's frame list and palette 8 or 26; its style is kept,
  and palette 8 and that style are put back when the stand-ins are, and on an unload. An
  item-pool stand-in draws in palette 8, its colors wrong, for its first frame (the game's spawn)
  and for one frame after its Zexion item look comes off; a general-pool one, until the next scan
  finds it (up to 30 frame calls, as for its Zexion item look). Palette 26 on a stand-in, written by hand, worked in game (2026-11-03);
  the helper's own colors: working in game 2026-11-04 (B1, `zexion_ap.dll` `65878411…`, the build
  of develop 1acaaaf, the owner's report: the logo's colors the same across areas).
- **Logo:** it paints Archipelago's logo (with a flashing arrow for progression) into those
  frames and a glyph, through OpenGL in the swap hook, after checking the texture is the 2048 x
  2048 sprite sheet. The logo is outlined side-on (the owner's pick): every pixel touching a
  circle (or the arrow) left, right, above or below is black, as is the one pixel left of the
  arrow's tip that black shuts in, the rest transparent, so the plain logo's middle stays open
  and a circle on the 16 x 16 frame's edge is not outlined past it. Palette 26 draws what is
  painted over the room as it is, so the gaps the logo had left transparent showed the sky
  through its black borders in a sky room (`docs/re/memory.md`, "Other players' items"). The
  painting leaves the game's texture binding, pixel-store state and GL errors as it found them,
  and paints nothing while a pixel buffer is bound. The side-on outline: working in game
  2026-11-04 (B1, `zexion_ap.dll` `65878411…`, develop 1acaaaf, the owner's report: in a sky
  room, on the plain and the arrow logos).
- **Put back:** the records (and any an earlier helper left restyled), the width (24) and the
  pixels. Unload refuses (`error an item record is still restyled (no frame came)`) if no frame
  came to do it.
- **In a translation:** the pickup popup and the map cursor read the translation's names
  (ItemNames 47 and 48), not the records: the label is written there too ("Translations";
  working in game 2026-11-02 in English(HD): the pickup popup AP ITEM over FOR BOB, with no logo
  glyph, and the map cursor AP ITEM). The label's logo glyph is now the helper's HD logo
  (U+E03C, "Translations"), so both showed it (working in game later that day, `zexion_ap.dll`
  `946b08a1…`: the logo before AP ITEM over FOR BOB, and before AP ITEM on the map cursor; bugs:
  the popup's two lines start at the same x, not centered, and the map cursor's logo, where the
  game's own names carry no icon: "Translations", "Run in game again"). Fixed since: the pickup
  popup's lines centered with leading spaces (where they fit the name's room), and the map cursor
  showing the first line without the logo (working in game 2026-11-03, B1, `zexion_ap.dll`
  `12f090c6…`: the owner's screenshot of the pickup popup, not kept in `lab/`, the logo, then
  Progression Item over for AneziaDS2, both lines centered; the map cursor's AP Item read back,
  below).
- **Limits:** two stand-ins of one kind for different locations within reach share the nearer
  one's name (not seen); and Scrap Energy in another seed's save would look the same, so the
  client sends labels only while this seed's save is loaded.

Working in game: 2026-10-23 ("Other players' items"); 2026-10-26, a three-game seed: no owner
on the map cursor, other Zexion players' supplies minor ("Map markers").

### Other Zexion players' items, as themselves

- **What:** for a cell labeled `@N`, every frame call (`src/looks.cpp`) dresses the stand-in
  objects there, in the game's pool of live item objects, as item N: N's palette, and the
  frame-list base moved so the frame the game computes lands on N's. The record, and so the
  pickup, stay the stand-in's.
- **Safety:** it writes only stand-ins with the template's base and palette 8 or 26 (the
  helper's own colors, "Other players' items"), or ones it dressed (the same spawn); and, for
  R'Talva X34 Y51's marker (below), only its shown byte (`+0x120`), cleared and set again, only
  while it is the spawn it hid.
- **Other pool:** some rooms show an item through the general object pool; a scan of that pool
  every 31st frame call finds any live object drawing a logo frame and dresses it the same way
  (R'Talva X34 Y51's marker, below, is hidden instead).
- **The marker:** R'Talva X34 Y51's marker (type 1146, known by its first script state, 8210, at
  `+0x32E`) draws logo frame 63 in its own palette while it waits for the boss (flag 154 clear),
  empty in the game's art, so it showed the helper's logo swaying in the gate's center; after the
  boss it is location 81's reward display (`docs/re/memory.md`, "Other players' items"). While
  the stand-ins are restyled (with any labels, other Zexion players' items among them or not) and
  flag 154 is clear, every frame call hides it (`ItemLooks::hide_marker`, apart from the dressing) (clears its shown
  byte, `+0x120`, which the draw loop skips and its own update leaves; its frame, frame list and
  palette stay the game's), but only if the game shows it. It is not counted as a stand-in for
  naming them. It is shown again, on whatever frame, the frame call after flag 154 is set or it
  leaves the logo frame (its reward), when the stand-ins are put back and on an unload, if it is
  still that spawn and still hidden. Other objects on a logo frame in their own palette (a boss's
  drop showing another game's item, type 1998) are left as they are, drawing the logo frame
  through their own palette. The marker can show
  the logo when it comes into view, until the next scan (up to 30 frame calls), and for a frame
  in a hit's blink. Hiding it by that byte worked in game
  (written by hand, 2026-11-03, `docs/re/memory.md`); the helper's own hiding: working in game
  2026-11-04 (B1, `zexion_ap.dll` `65878411…`, the build of develop 1acaaaf, the owner's report:
  the gate's crystal without the AP logo; the reward after the boss not singled out).
- **Put back:** when the label goes, and with the rest.

Working in game: 2026-10-23 ("Live item objects").

### Intel Chips

- **What:** names the Area Map's record (49) INTEL CHIP and turns off its pickup's area event
  (3551, the map reveal) (`src/intel.cpp`), only if the record reads as the game loads it or as
  the helper left it.
- **When:** each frame call while the client has sent `intel on` (this seed's save loaded, its
  Area Maps Intel Chips).
- **Put back:** `intel off`, with the rest; an unload refuses while it cannot.
- **In a translation:** the pickup popup reads ItemNames 49: Intel Chip is written there while
  the record is INTEL CHIP ("Translations"; not run in game yet).

Working in game: 2026-10-25 ("Intel Chips").

### The OS Install Disks goal

While the client has sent `discs` (the count, the number needed, the scanner's line in hex, and
TOLD: 1 once flag 39 says the player has been told the number, else 0 and "OF ?"; left out, 1),
the helper:

- writes the scanner's message (Text entry 12) as the client's line (`src/scan.cpp`), in
  capitals (the client sends it in mixed case for English(HD): "Translations"), only over the
  game's text or its own;
- paints the disk glyph into font glyph `0xFB`;
- with a disk or more, wraps the pause screen's EQUIPMENT callback (`src/equip.cpp`): after the
  game builds the screen, it moves the Energy Tank left and adds a widget for the disk (the
  count as its label), showing OS INSTALL DISK, its icon and description when selected; the game
  frees it with the menu;
- makes the pause screen's KEYS row the disk row (`src/pause.cpp`): the `jne` that builds the
  row only once flag 39 is set made a `jmp`, then a disc and the count, the key slots and icons
  hidden. As soon as a save of a known disk seed (one the client left a file for) is loaded, in
  play or not, the row shows the count from that file until the client's arrives, so a pause
  screen opened at once shows the disks, not KEYS. OPTION MENU opens over the pause screen, which
  stays open under it, and the game calls the pause menu's way back (`options_back_fn`, which
  OPTION MENU's opener puts at the menu's `+0x68`) as it closes, before it draws that frame;
  while the row is on, that pointer is the helper's wrapper, which calls the game's and then
  turns the row in the same call (in a translation the game builds KEYS's translated text there
  again, which the 1.0.0 release candidate showed for one frame before the count: the owner's
  report, 2026-11-03; fixed since, working in game 2026-11-03, B1, `zexion_ap.dll` `fce9e73b…`,
  the 1.0.0 RC at develop 76df074, English(HD), the client connected, `lab/ap/live50/client-r5.log`:
  no Keys frame after Option, the owner's report; `options_back_fn` matched once live,
  `tools/check_patterns.py --live`). The game's way back sets the
  pointer to its own again; the helper puts it back when the row turns off and before an unload,
  while its menu is open and still holds the wrapper;
- while that row is on, paints its disc, a 24 x 24 white image of the helper's own, at (128,
  1008) in the sprite texture (blank there in the game's art, checked as the glyphs are), and
  points the row's icon sprite at one frame-list entry the helper owns: a copy of the small Data
  Disc's (frame 829) with the image's place, drawn 24 x 24 at (160, 184) (`kIconY`): 4 above
  the middle of the game's four key icons, (160, 188) (`docs/re/memory.md`, "The pause screen's
  KEYS row"). What 184 itself was read from is not recorded beyond the code's "read live on B1,
  2026-10-24"; the disc there looked right in game (2026-10-26). The entry is allocated once and
  kept for good, never freed, so a pause screen still holding it after an unload points at live
  memory; whenever the image is not painted (the row off, before an unload, a texture it could
  not paint into) the entry shows frame 829's place instead, the game's disc enlarged until the
  screen is closed. A pause screen opened before the image is painted shows frame 829 as is;
- on `mission_prompt COLUMN ROW` (sent after each save at the Landing Pod with enough disks,
  in a save past its ending too, with home: the Landing Pod's save point in tiles, 191 329,
  whichever room the seed starts in; the column below 54 × 14 and the row below 54 × 20, or `error bad mission_prompt request`), once idle in play
  with no popup of its own to show and the game's last message (GAME SAVED, 180 frames) down to
  its last 150 frames, half a second in (`kFadeMessageLeft`, the owner's pick in game, 2026-10-27;
  until then 70, as far into it as the game's escape skip fade is into Zexion's popup:
  `docs/re/memory.md`, "The mission prompt"; a popup of the helper's own is never cut short:
  while the game's last message is still the helper's, the prompt waits for its end, read from
  that message's frames left, so in game frames),
  opens the game's YES/NO prompt as RETURN HOME WITH THE DISKS? (`src/mission.cpp`) the way the
  game opens it as SKIP ESCAPE AND FINAL BATTLE?: it runs the game's fade to black (area event
  3667, checked byte for byte, through the game's handler for its type) and sets its opener as the transition 60 frames away (the
  pause is blocked while that counts; popups and received deaths wait). Its YES closes the prompt
  through the game's NO and then starts the ending once no popup of the helper's shows (one up
  is never cut short; a message of the game's, GAME SAVED, does not hold it: the ending routine
  ends every message itself as it starts, `docs/re/memory.md`, "The mission prompt"; until then
  the ending also waited for GAME SAVED, up to about 1.5 s of black in which the player could
  still move and pause, the owner's report, 2026-11-02); NO fades the screen back
  in, as the game's NO does there. NO's close puts back, inside its call, the fade the prompt's
  builder saved (the level −1, then toward 4 a step every 6 frames: `docs/re/memory.md`, "The
  mission prompt"), so right after it YES turns that fade off with the screen still black (the
  fade's step, countdown and target set as event 3667's fade ends): no fade back into play
  between YES and the ending. Only a fade that reads as NO's close leaves it is turned off
  (`mission_status` then says `yes: ending when idle, the screen kept black`); anything else
  fades back in as before. While it is
  black, popups and received deaths wait (`refused RETURN HOME WITH THE DISKS? ending`); a popup
  of the helper's waiting brings the screen back in for it (as NO's close would have), and the
  ending follows the popups as before; so does the ending not starting within 240 game frames in
  play (`kDarkFrames`: a safety that errs long, as after YES the ending waits only for a menu
  to close and the gap after the helper's last popup, and no popup of the helper's shows in the
  black, a waiting one ending it first; counted from the game's frame clock, `frame_clock`, which stands still while a menu has the game paused, so
  frames with a menu open, the pause, which saves the fade and puts it back, don't count; until the release
  candidate it counted 180 of the helper's frame calls, which a 165 Hz monitor would run out in
  about 1.1 s, before GAME SAVED ends, by disassembly: below), and the prompt cancelled (the save
  left, `discs` off, the client gone); asked for with a menu open, the fade back waits until it has closed and put its saved
  black back. Another fade since (a death's) is left alone, and an unload is refused while the
  screen is kept black (below, "Unloading"); leaving the save (the title, which owns the fade)
  drops it. `mission_black` says how the last black ended (`kept black` while it lasts; `ended:
  the ending, faded in from black`, `ended: kDarkFrames in play with no ending, faded back in`,
  `ended: a popup waiting…`, `ended: cancelled…`, `ended: the save left`, or another `ended: …`
  note: another fade, the ending not faded in or with the fade no longer the helper's), for a
  live check. In the release candidate the owner saw the screen fade back into play at the pod
  for a moment before the ending (its retest, 2026-11-02): the limit above running out first is
  the cause the numbers fit (by disassembly; the run with this fix, 2026-11-02, measured 90 game
  frames, about 230 frame calls, and no fade back; the release candidate was not run again:
  `docs/re/memory.md`, "The mission prompt"); the other one found is the ending routine itself,
  which sets the level to 4 as it warps the player to the ending's first scene, in a frame the
  game draws before the helper's frame call (not seen in game: the wrapper hides it).
  For that one the helper starts the ending through a wrapper of its own, set as
  the transition with timer 1 in the routine's place: the game runs it, it runs the game's
  routine, and, while the black is still the helper's, it sets the level back to black at once
  (event 3667 aimed from −1 to 4, a step every 8 frames, `kEndingFadeStep`) before that frame is
  drawn: the ending fades in from black, at the normal level 40 frames in. The black ends there.
  A wrapper still the transition after the routine (not as traced) becomes the idle routine; one
  an unload finds not yet run becomes the game's routine (the ending then comes without the fade
  in). Anything else in the wrapper's place before it ran (a save at the save point, which sets
  the pause opener back and leaves the fade alone; a door, timer 70; a death): the ending is
  asked for again, the black kept while it is still the helper's (with its 240 game frames), else the
  other transition's fade left alone. From the frame the helper sets the ending's wrapper until
  another save is loaded (`save_loaded` changes, as at MISSION COMPLETE) or another transition takes its
  place as above, `mission_status` says `ending started`, a `mission_prompt` is ignored (the
  ending's cutscenes read as play and its own save as a save at the pod), `apply`, `shield` and
  `die` are refused (`RETURN HOME WITH THE DISKS? ending`: the client gives them in the save's
  next load), and popups already queued wait for the seed's next load. Stand-in tests
  (`native/tests/mission_tests.cpp`, `game_tests.cpp`); run in game 2026-11-02 (B1,
  `zexion_ap.dll` `946b08a1…`, English(HD), `lab/ap/live50` slot 8, past its ending, and
  `lab/ap/live52` slot 9, before it): after YES the screen went black, then faded back into play
  at the pod for a moment before the ending began, on both saves (a bug, the causes above; fixed
  since, working in game 2026-11-02, B1, `zexion_ap.dll` `d179d32d…`, `lab/ap/live52`, a new
  game in lab slot 11, its first ending and a replay: black until the ending, then straight into
  it, the numbers fitting the limit's running out as the cause; the ending starting with GAME
  SAVED up, since, working in game the same day, `zexion_ap.dll` `28ab9954…`, slot 11, a replay:
  black for a frame or two after YES, then the ending fading in from it; on a first ending,
  working in game 2026-11-03, B1, `zexion_ap.dll` `fce9e73b…`, the 1.0.0 RC at develop 76df074,
  English(HD), the owner's report: YES, black, then the ending at once). Working: the ending played again on the save past it, with nothing new sent and the
  Feats note shown; the goal sent on the save before it; items received during either (a
  Missile Container sent during the replay; the items collected at the goal) held, their popups
  shown on the next load; a death received during the first ending dropped; the pause opened
  in the black after YES and EXIT GAME going to the title with no black left, and
  `/reload_mod` not refused after that. Not run in game: a door or a death right after YES, and
  the prompt asked for again. If event 3667 or its
  handler is not as traced, the prompt opens at once with no fade, as before. A request not yet
  opened lapses (`lapsed: the player left the landing pod`) once the player is in play outside home's cell (the request's row / 20 and column
  / 14 against the player state's cell, as the client tests a save at the pod: `docs/re/memory.md`,
  "The mission prompt"; a cell off the map, as the raw cell reads for a moment, counts as not
  known), as when popups held it while the player walked on; the client offers it again after the
  next save there. Until 1.0.0 home was the seed's start cell, read from the loaded section's start
  record; the in-game runs from 2026-10-27 and earlier all started at the Landing Pod, where the
  two are the same. Home
  given by the request, from a seed started elsewhere: working in game 2026-11-02 (an OHKO seed
  started at Open Cave, B1, `zexion_ap.dll` `1c286c57…`, `lab/ap/live50`): no prompt after a save
  in the start room; after a save at the pod, the prompt after the fade, NO, a request lapsing
  while received items' popups held it and the player left the pod's room, the offer again after
  the next save there, and YES to the goal. Once its opener is set, it opens
  where the player is when its 60 frames are up. An opener the helper takes back before it runs (the
  save left, `discs` off, an unload) turns the fade back to the normal level; so does one the game
  runs with the player not in play (a death during the fade: asked again), but only while the fade
  is still event 3667's, as below, not a fade the death set. One another transition took the place
  of (a save at the save point again, which sets the pause opener back and brings no fade; a door; a
  death) is asked again once idle (and lapses if the player is then outside home's cell), and its
  fade is turned back only while the game's fade (the `fade` and `fade_level` targets) is still
  event 3667's: target −1, a step every 8 frames from level 4, or done at −1, and no further back
  than the helper last saw it with its opener in place, nor the transition timer higher (a save
  leaves it counting). Anything else, another transition's fade even with 3667's own values (it
  starts again from level 4, its timer at 70), is left alone. The same when `discs` turns off after
  such a save. Working in game: the fade turned back after saves at the pod during it, 2026-10-27
  (the owner's report). A request lapsing outside the pod's cell (a seed started there), 2026-10-27 (0.30.0, the owner's
  report, `lab/ap/live34/client1.log`): saved at the pod with twelve received items' popups
  still to show, then walked a few rooms away during them, and left during GAME SAVED with no
  popup queued; no prompt either time, and offered again after the next save there. The ending
  after YES waiting for the popups of five items received while the prompt was open, then
  starting, 2026-10-27 (575edbf, the owner's report, `lab/ap/live35/`). Stand-in tests only: the
  fade turned back when the helper takes its opener back, another transition's fade left alone, a
  death's own fade left alone, and `discs` turned off after such a save.

With `disk_slots` (the save slots holding the seed), and for other seeds it has files for, it shows
each save's count on the file select, COPY FILE and DELETE FILE in place of the keys
(`src/files.cpp`, wrapping each entry's handler): DISK over KEYS, and the count. The count's two
digits sit 5 px below their row (`kCountDrop`), centered on the items bracket beside them (`0xD9`
over `0xDA`, rows 3 and 4: its middle 7.5 px below row 3's top, a digit's 3, so 4.5; 5 chosen in
game). Nothing shows for a save with no disk; the client's count wins for its seed's slots.

Put back: `discs` and `disk_slots` with no arguments, and with the rest. An unload refuses while
the scanner's text or the pause screen's branch is still the helper's, or the pause menu's way
back from OPTION MENU is still its wrapper.

In a translation ("Translations"): the scanner's line goes into SmallText 12 too; the equipment
screen's disk (its name after the HD disk icon) and RETURN HOME WITH THE DISKS?'s question are
built again in the translation's font; the pause row's count is KEYS's translated text built
again ("2 of 15", centered on the row as the game centers a prompt's line, built again
whenever the game builds KEYS again: in the same call on OPTION MENU's way back, above, and in the
next frame call otherwise), the key slots' characters hidden; the disk count
popups carry the HD disk icon. Run in game 2026-11-02 in
English(HD) (B1, `zexion_ap.dll` `1c286c57…`, `lab/ap/live50`): the received disks' count popups
and the reminder in the HD font; the pause row's count in the pixel font with no Keys over it;
the equipment screen's OS Install Disk and its description in the HD font, the description's
first line running past the box's right edge (a bug); the question in the HD font, left of
center (a bug); NO, and YES sending the goal. The scanner's line: not run in game yet. Since
then the description is wrapped at the box's columns and the question centered, as the game
does its own, and the pause row's count and the icons are in the translation's font: working in
game later that day (`zexion_ap.dll` `946b08a1…`, `lab/ap/live50`), the description wrapped
inside its box, the question centered, the count "4 of ?" in the HD font, and the equipment
screen's name after the HD disk icon (whose look the owner wants drawn again). The scanner's
line: still not run in game.

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
  in the entry's blank cells at columns 16 and 17 of rows 0 and 1 and raised 4 px. Its side-on
  black outline (painted since the stand-ins showed the sky through transparent gaps: "Other
  players' items") falls on the file select's black background, so it looks as it did (assumed
  from that background; not run in game).
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
  marked any more, and before an unload; the image is cleared from the texture. Only into an
  entry still holding the helper's handler: one the game built again in a wrapped entry's
  place (its menu closed and built again at the same address) is left alone, the old one
  forgotten.

Working in game: 2026-10-26, helper 0.30.0 with this change, with no client and with one: the
file select through paging, COPY FILE, DELETE FILE, a copy of an AP save and its delete, and the
fade to white of a new game started beside an AP save ("The file select"). A new ARCHIPELAGO
game's own save getting the logo, and a plain randomizer save beside it none: working in game
2026-10-27 (0.30.0, the owner's report).

### AP saves without the client

`src/offline.cpp`, `src/game_offline.cpp`; the client side is `client/offline.py`.

- **What the client leaves:** while connected, `offline_put` (when they change) sends the
  addresses it resolved for this build, with the first 16 bytes of each game function, and a
  small file per seed: its item table (154 bytes, or 155 with Secret Shuffle) and start, the Intel Chip and ASSIST settings, the OS
  Install Disks (count, number needed, scanner's line, and this player's own disks not counted
  yet) and the item labels.
- **Where:** `offline_put` checks each file as the reader will (the config with at least 8
  function checks; NAME in `<seed>-<team>-<slot>` form; each file at most 48 KB). The helper
  writes it on the pipe's thread into `%LOCALAPPDATA%\ZexionAP` (`config-<build>.txt`,
  `seeds\<NAME>.txt`; an absolute `LOCALAPPDATA` only), never the game's folder. The helper
  writes them, not the client: a client run from a packaged app has its AppData writes
  redirected (inferred: `docs/re/memory.md`, "AP saves without the client").
- **Reading:** when it starts and each time a client goes away (anything that used the pipe,
  `tools/helper.py status` too), at most 256 seed files, newest first. Seed files read with no
  client replace the last ones, a dressed save's seed kept dressed if they still hold it (its
  record as it was); those read while a client is
  connected, and any seed file a client puts, are added to them from the next frame call
  (`Game::merge_offline`: a seed already there takes its old record's place, unless a save of it
  is dressed), so another seed's save started earlier in the same client run is known as one
  (`other_save`). With no client yet, the cached addresses configure it if they are for its
  build and the game holds every checked function's bytes, even if the cache lacks the
  client-only or translation keys (a cache from before a release that added one: "The
  pipe"). So the offline
  look, the map markers and the title lines all run with no client once the cache checks out.
- **The look:** with no client, or a client that said the loaded save is another seed's
  (`other_save`), every 16th frame call a save of one of the seeds is dressed as the client
  would, once it has been in play since it loaded (a menu or a death later keeps the same save,
  so it is dressed at once when a client goes away with a menu up): the item labels, INTEL CHIP, the ASSIST lock (the file free once the slot has
  received Assist Unlock: `client/ticks.py`, `offline_seed`; stand-in tests only), the disk count
  (the file's, plus each own disk the map shows collected) and the scanner's line, with a
  notice that the client is not connected (or, for `other_save`, is for another seed; only for a
  save of a seed it has a file for, so a plain randomizer game gets no notice): once a load, and
  again each time a client that configured during the load goes away (working in game
  2026-10-29, 1.0.0 (b790be9), `lab/ap/live47`).
  While the save stays dressed, each look sets that look again where it was changed: a client of
  another seed, attaching with the save in play, takes its own look back, which is not this
  save's (stand-in tests, `tests/offline_tests.cpp`; working in game 2026-10-26).
  A client with no server connection (the server down, say) lets the helper go, as if it had
  closed, while a save it has not dressed is loaded: one loaded after a drop's save was left, or
  the one in play when the player disconnects. Such a save then gets this look and its notice
  too. The client keeps the helper for a save it dressed before a drop (its look stays), in the
  ending (its hold), and at the title, where after a drop an armed new game stays armed until a
  save is loaded. It attaches again on connecting, before it gives any item; an attach that
  fails holds the items back for up to 30 s, then they are given as with no helper
  (`client/zexion_client.py`, `leave_helper_tick` and `poll`; stand-in tests,
  `tests/test_zexion_client.py`). Working in game 2026-10-29 (`lab/ap/live47`, the Launcher's
  client): a save loaded during a drop and a Disconnect in play, each given this look and its
  notice, then the client's again on connecting, with CLIENT CONNECTED and received items'
  popups. Not seen in game: a failed attach on connecting, an ending during a drop or after a
  Disconnect, and an armed new game kept at the title during a drop.
  The file select shows each such save's count, from its map in the game's copy of the slot,
  and the AP logo ("AP saves on the file select").
- **Put back:** leaving the save undresses it: no save loaded, or in play reading as no known
  seed. With a menu up, a save in play since its load that reads as no known seed is held as it
  is until play, so the lock never lifts in the options menu for one read (none such is known;
  stand-in tests, `tests/offline_tests.cpp`). A client's `config` takes the offline look off
  first and withdraws the notices not yet shown (the not-connected warning, and the last
  client's other-seed notice and CLIENT CONNECTED.); the client then asks `client_back`, which
  pops up ARCHIPELAGO CLIENT CONNECTED. once if that save was dressed. Each notice is for the
  load it was queued in: one not shown when the save is left (no save loaded) is withdrawn, not
  shown in the next save, and the next load is told again. The other-seed notice and CLIENT
  CONNECTED. are also for their client: one not shown when it goes away is withdrawn. A
  not-connected warning withdrawn before it showed is given again when the save is next dressed
  with no client.
- **Waits for the client:** received items, hints, the goal and RETURN HOME WITH THE DISKS?.
  What was picked up is on the save's map, and the client sends those checks when it next
  connects with the save loaded.

Working in game: 2026-10-25 ("AP saves without the client"); 2026-10-29, a Secret Shuffle
seed's file (a 155-byte table, one `table` line): the save dressed with the client closed, its
notice shown, the map's unseen squares named as the game names its own (develop e088773,
`9b609b92…`; `lab/ap/live46`).

### The map

- **What:** in an AP save holding the seed the labels are for, in play or in a menu (a plain
  randomizer save is never touched), raises the map squares of item 48 at the section's
  locations (154, or 155 with Secret Shuffle) (this player's OS
  Install Disks and other players' progression items, but not a Zexion player's supplies:
  `worlds/zexion/world.py`, `important_elsewhere`) from the game's basic marker to its
  important one (`src/mapmarks.cpp`), obscured or shown as the game's own majors are under
  the seed's Obscure Majors, Obscure Minors and Obscure Class. Under Majors on with Minors and
  Class off a stand-in shown as a minor in a square a hint, a map room or Hints Revealed
  revealed becomes an obscured major and one seen directly a shown major; the value cannot tell
  an Area Map's reveal (shown) from a hint's ("Map markers"). While an item is on screen the
  game writes its square every frame as a shown item of its ID's class, which took a stand-in
  back to a minor (measured live), and its minimap dot's border was seen to flash (assumed to be
  this); so the helper also patches that
  write (`seen_marker_code`, 50 bytes) to give item 48 a major's class, as an item on screen
  of IDs 3 to 35 gets (`0x1A`, or `0x0E` in a square a hint revealed). The patch is the game's
  code with one more compare: it calls and reads nothing of the helper's. While the map is open,
  a wrapper on the map's per-frame handler writes the square's label's name over SCRAP ENERGY
  or SCRAP CHUNK after the game writes the cursor's name, in the same frame; in a translation it
  builds the cursor's translated text again as the label's first line ("Translations";
  working in game 2026-11-02 in English(HD): AP ITEM); since, without the logo, and in
  English(HD) in the case the client sent it (AP Item, Progression Item, OS Install Disk) or,
  for another Zexion player's item, the translation's own name for it (working in game
  2026-11-03, B1, `zexion_ap.dll` `12f090c6…`, `lab/ap/live50`: AP Item on a DLCQuest
  player's square, its translated glyphs read back with a read-only probe as A, P, I, t, e, m,
  shown, no logo among them, the pixel ones hidden; another Zexion player's items by the game's
  names, the owner's look, `lab/ap/live52`; Progression Item and OS Install Disk not looked at
  on the map). Labels in capitals, as a client from before the mixed case (31d9203) writes them
  and a seed's offline file may still hold them, show in capitals: the owner's report of AP ITEM
  and PROGRESSION ITEM on the map cursor with `12f090c6…` (develop 97a6f25's build; a report,
  no run recorded) came, in the owner's word, from such a client (the client's version was not
  read; the capitals are that client's `client/aplabels.py`).
- **When:** the markers every 16th frame call while labels are set, once two of those looks in a
  row have found the seed's save loaded (a precaution: the load routine writes the section, the
  marker and the grid in one game call, `docs/re/memory.md`, "The loaded save's randomizer section
  is at +0xCDD6D0"; the file select's own grid merge around it is not traced; a player who pauses at
  once and opens the map sees the squares raised within about 32 frame calls: two looks, one every
  16th); the on-screen write's patch from that same look, until the first frame call that finds no
  labels or another save loaded (bytes there other than the game's write as traced are left alone,
  and nothing reports it: the stand-in then flips as
  before); the names while the map is open and this seed's save is loaded.
- **Put back:** the markers are not: the game saves the map grid, and they stay as the game would
  have set them. The on-screen write goes back as above, when a client goes away and before an
  unload, only while it still reads as the helper wrote it; an unload refuses while it could not
  be put back (a plain randomizer game's Scrap Chunks would show as majors on screen). The
  handler goes back when the names stop and before an unload, which refuses while an open map
  holds the wrapper.

Working in game: 2026-10-25 ("Map markers"); 2026-10-26, a three-game seed, the markers raised
in a pause right after loading; 2026-10-26, a second three-game seed (`live30`) with the
two-look check, the unseen squares named as the game names its own and the markers still raised
after a save, a return to the title and a load; 2026-10-27, two Custom seeds under Obscure
Majors on and Minors off, Class off and on (`live40`, `live41`), the stand-ins marked as the
game's own majors, a stand-in's minimap dot flashing while on screen; 2026-10-28, with the
on-screen write's patch (`tests/mapmarks_tests.cpp` runs the game's bytes and the patch side by
side), no flash ("Map markers").

### The title screen's lines

- **What:** adds ARCHIPELAGO MOD and its version, and ©2026 MCAWFUL, centered under the title's
  copyright line, and above them, only while no client is active, START THE AP CLIENT TO PLAY A
  SEED (`src/title.cpp`). It makes them with the game's own text functions in that line's
  palette and style, and the game frees them with the title.
- **When:** while configured (by a client or from the cache) and no save is loaded, every 9th
  frame call (every one in English(HD), and at once when the language changes) it looks through
  the game's draw list for the copyright line without the helper's lines, and makes them only
  while no menu is open (`menu_state` 0): made under one (a client configuring the helper with
  the file select up), they would join the list after the menu's objects, over them. One that
  failed is tried again on the next title. In English(HD) a menu's builder hides every glyph of the HD
  list, the lines' too (`docs/re/memory.md`, "Where a rebuilt label goes"), so they are shown
  again at the next frame call the title is up with no menu open (stand-in tests only).
- **Shown:** while the game's © is shown, the hint only while no client is active. In
  English(HD) the two translated lines also only while no menu is open (`menu_state` 0): the
  file select (the title's key press opens it, and its OPTION MENU button opens Option over
  it) opens over the title, which stays in the draw list,
  and the HD list is drawn over them, so the lines showed over those menus in the 1.0.0 RC
  (`docs/re/memory.md`, "The title screen"). They are hidden there with their count kept and
  not built again; any of the helper's lines found without the game's line are hidden too. The
  pixel lines (©2026 MCAWFUL, and every line in another language) follow the game's © as before.
  With `menu_state` unreadable the two translated lines stay hidden. `menu_state` reads 3 with
  the file select up; with Option up over it, and back on the file select after it, it is not
  measured (the options builder stores 2 or has the widget set 3, never 0). Whether a frame call between a menu closing and a load (or a new
  game) starting sees `menu_state` 0 with the © still shown, and so shows the lines for a moment,
  is not known. Stand-in tests, and working in game 2026-11-03 (B1, `zexion_ap.dll` `fce9e73b…`,
  the 1.0.0 RC at develop 76df074, English(HD), the owner's report: the lines only on the title).
- **Put back:** a client going away leaves them; an unload hides them (stand-in tests only).
- **In English(HD):** the version's line and the hint in the translation's font and in mixed
  case (Archipelago Mod 1.0.0, Start the AP client to play a seed): each line's translated text
  built by the game's builder on the helper's text object (the room `0x92300` gives every
  label), centered on the screen, its pixel glyphs hidden, and shown and hidden in their place;
  ©2026 MCAWFUL stays in pixels, as the game's own copyright line does. In another language the
  pixel lines show again. The game's text destructor frees the translated glyphs with the
  title (`docs/re/memory.md`, "The title screen"). Seen in game 2026-11-02 (B1, the 1.0.0 RC,
  develop d484024, `zexion_ap.dll` `6e5dcb1b…`, the owner's screenshots): the two lines stayed
  on screen after the title, over the Option menu, below the Option box and over the file
  select's bottom buttons (a bug, fixed since as under "Shown"; working in game 2026-11-03, as
  there).

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
  - **The Feats of Natu screen.** It still follows an AP ending, with nothing flashing:
    MISSION COMPLETE's builder always leads on to it (by disassembly). The helper writes
    ARCHIPELAGO SAVE: NO FEATS EARNED, centered, into the blank last line of its title label,
    under FEATS OF NATU! (`feats_screen_note`). It writes only on that screen as it follows an
    ending: the top menu with 4 panels (the title's 3 widgets, EXIT, and two rows of Feats: 7,
    or 6 on top and 5 or 6 below when the game hides some: 6 and 5 seen), a live title widget
    whose label is three equal lines (the middle one naming NATU), and MISSION COMPLETE's
    statistics under it: a panel of 10 lines and, seen after an ending in a seed with Allow
    Assists on, ASSISTS USED's 8 as a second (read live on B1, 2026-10-27; what makes the game
    add it is not traced). With the secret row (a secret Feat earned, or the title screen's code
    typed since the game started) the top menu has a fifth panel of 5 Feats and the game moves
    the title up 8 and the rows up 16 (read live, 2026-10-28): the last line is still blank, and
    the note goes there (working in game, 2026-10-28; 0.30.0 showed none there). It writes the
    whole line or nothing, only over a line that is blank or already its note, and only if every
    glyph of it is writable and has the font's frames. In a translation, while the note (or the
    title written over the hint) is there, the title's translated text is built again as
    LargeText 220's (the game's Feats of Natu!) with "Archipelago save: no Feats earned" under
    it, wrapped, the shorter line centered on the longer with leading spaces, the two centered
    and kept at the title's middle as the game lays out its title ("Translations"); the game's
    own entry (220, or 219 for the hint) again when the note goes.
    Without LargeText's count (a cache from before the `large_count` key), or with too little
    room, the title's pixel glyphs are shown in its place instead, the whole title in the pixel
    font. (Seen in game 2026-11-02 in English(HD), `lab/ap/live50`, as that pixel title and
    note; the HD title: working in game later that day, `zexion_ap.dll` `946b08a1…`, after RETURN
    HOME's YES on a save past its ending: Feats of Natu! over "Archipelago save: no Feats
    earned", both in the HD font, the two lines starting at the same x, as again on
    `12f090c6…` on 2026-11-03: a bug, the title not centered over the note. Since, the title's
    line centered with leading spaces: working in game 2026-11-03, B1, `zexion_ap.dll`
    `fce9e73b…`, the 1.0.0 RC at develop 76df074, English(HD), `lab/ap/live50/client-r5.log`
    (slot 4, RETURN HOME's YES on a save past its ending): both lines centered, the owner's
    screenshot, not kept in `lab/`.)
    - **The hint at the title.** In a game that has never reached Ending B (and an AP ending
      does not reach it, so a player whose only endings are AP ones), the title is the game's
      hint COMPLETE ANY ONE OF THESE / TO SEE THE EXPANDED ENDING! on the upper two lines of a
      label 8 lower. On it the helper writes the whole label: FEATS OF NATU! and the note under
      it, on the lower two lines (y 40 and 56) when the note's line clears the top row of Feats
      by 16, else the upper two (y 24 and 40, with the secret row: the game places the hint
      there either way, by disassembly). It keeps the hint's letters and puts them back when it
      unloads, if the label still reads as it wrote it. It writes only if every glyph takes
      text and the hint names EXPANDED ENDING; the game's other hints (`docs/re/memory.md`;
      when each shows is not decoded) are left alone, with no note. Working in game,
      2026-10-28 (a fresh save folder: on the lower two lines over rows of 6 and 5, and with the
      code typed, on the upper two over the secret row; and the hint back, as the game wrote
      it, on an unload with the screen up).
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
  found an AP save loaded, a precaution: a load writes the section and the marker in one game
  call (`docs/re/memory.md`, "The loaded save's randomizer section is at +0xCDD6D0"). It
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
  ASSISTS USED;
- 2026-10-28, with the secret row's change (an Abridged Finale AP save, no client): the note
  shown over the secret row, and, after a restart with no code typed, in the usual layout;
  flag 251 clear and `GameUnlocks.sav` unchanged both times;
- 2026-10-28, with the hint's change, in a fresh save folder (no Ending B, no Feats on record)
  and the same save: FEATS OF NATU! and the note in place of the hint, over rows of 6 and 5,
  and again with the code typed, on the upper two lines over the secret row; flag 251 clear,
  and `GameUnlocks.sav` given no Feat, no Ending A or B entry and no `+0xCE00A8`; an unload with
  that screen up put the game's hint back.

Not seen in game:
- Steam's stand-in stopping an achievement. No achievement the owner has left is cheap to earn in
  play (13SecretCode is taken at the title only), so this is tested only with the stand-in;
- the load test: a fully explored AP save loaded from the file select earning no 15Explorer
  (by disassembly and the stand-in only; the owner's 15Explorer is earned already);
- the note over a bottom row of 6 (by disassembly and stand-in tests only).

### Translations

`src/translation.cpp`; the game's side is `docs/re/memory.md`, "Translations" and the sections
after it. Run in game 2026-11-02, below ("Run in game"). Stand-in tests (`tests/translation_tests.cpp`, and the
translation cases in `offline_tests.cpp`, `mission_tests.cpp`, `equip_tests.cpp`,
`feats_tests.cpp`, `pause_tests.cpp` and `apitem_tests.cpp`) cover all but the map cursor's
name (by disassembly only: "The helper's calls in a translation"). Their builder lays out a stand-in font's glyphs
from the place it is given, wrapping as the game's does, so they check where a rebuilt text
lands, not only the place passed.

- **What:** while the game shows a translation (its flag, `tr_flag`), the helper's text shows in
  the translation's font:
  - **Popups and notices** go into SmallText 66 to 72, entries empty in every translation that
    the game never reads, split in two by pointing entry 70 at the run's middle (116 and 117
    characters in English(HD), 96 and 97 in Japanese), and the message is given its text as the
    game gives GAME SAVED its own (`hd_text_fn`): the game reads the entry again every frame
    while the message shows. The areas take turns (the popup before may still be closing); a
    notice longer than one takes the whole run while both are free (234 characters; 194); one
    whose area is taken waits a frame, at most 240. Its text: the item's icon as the
    private-use character the translations' fonts draw (as the pickup does), the translation's
    own name for the item (a stand-in's own, not the label over it), then the client's suffix
    on the next line: in English(HD) in the case the client sent it, its FROM as "from" ("from
    Bob"), in any other translation in capitals as in the default English ("FROM BOB"; the
    owner's choice, 2026-11-02); a notice's text likewise, the client's and the helper's own
    (NOT CONNECTED, CLIENT CONNECTED, the other seed's) in mixed case in English(HD) only. Laid out as the font draws it (`FontMetrics`, `layout_popup`: the
    builder's advances, read from the font as loaded): no line wider than a pixel popup's 192
    pixels, words whole; a popup's lines centered on the widest with leading spaces (the game
    starts every line at the same x); a notice's left as they wrap; without the font's metrics,
    wrapped at 30 characters. A popup that cannot go there (longer than the
    run, its area still taken after the 240 frames, the run not as traced, the tables
    unreadable with the flag set, or from an unload's put-back on) shows the helper's pixel
    text, with the mark the game's last translated message left in its slot cleared (told by
    the HD object's serial), so it shows, in its box.
  - **Mixed case** (English(HD) only, the owner's choice, 2026-11-02; `Game::mixed_case`, by
    the language's name): the client writes its text in mixed case (hints, the disks' popups and
    reminder, the scanner's line, the labels: `client/hints.py`, `client/discs.py`,
    `client/aplabels.py`), and the helper keeps each in its own case beside the capitals the
    8 x 8 font draws (`Popup::as_sent`, `Label::own_first` and `own_who`; the scanner's line
    written into the game's pixel Text table in capitals, `scan_line_upper`). English(HD) shows
    the case as sent, with the helper's own notices in mixed case; every other translation and
    the default English show capitals, as before (their fonts may draw no lower case). Labels
    sent in capitals (a client from before this, or a seed's offline file it wrote) show in
    capitals. The labels working in game 2026-11-03 (B1, `zexion_ap.dll` `12f090c6…`): the
    pickup popup's Progression Item over for AneziaDS2, the map cursor's AP Item ("Other
    players' items", "Map markers"); the hints, the disks' popups and reminder, the scanner's
    line and the notices: stand-in tests only, not run in game yet.
  - **Names the game reads while they show,** written into the tables in place and cut to each
    entry's room: the stand-ins' labels into ItemNames 47 and 48 (both lines, centered on the
    wider with leading spaces as a received item's popup's, since the pickup popup starts each
    line at one x, where those spaces fit the entry's room, else as they are so no name is cut)
    while their records are restyled in play, for their pickup popup, each held as it is while
    a pickup message bound to it shows in the AP save (the HD message reads it again every frame:
    `docs/re/memory.md`, "Translated messages"), so walking into another cell or pausing during
    the popup leaves its name, unless another stand-in of that record is within reach, whose
    name the next pickup needs (stand-in test only, not run in game); Intel Chip into 49 while the Area
    Map's record is INTEL CHIP; the scanner's line into SmallText 12 (its message event's entry)
    while the Text table's entry is the helper's.
  - **Menu labels built again** with the game's builder (`hd_build_fn`, the string from
    `wstring_new_fn`), laid out as the game lays out that label's own (`Rebuild`;
    `docs/re/memory.md`, "Where a rebuilt label goes"): ARCHIPELAGO (Archipelago; SmallText
    354's text again when RANDOMIZER goes back, not through the new game's fade) and Return home
    with the disks? where the game's attach would build them now (the label's first pixel glyph
    less 1: the game moves a label without updating the place its build was given), the question
    then centered as the game centers its own; the equipment screen's OS Install Disk and its
    description (no icon) where the game built them for the selection, the description wrapped
    at the box's columns; the map cursor on a labeled stand-in's square (the label's first
    line, without the logo: the game's map names carry no icon); ASSIST while locked, with the
    padlock, in the locked look; the pause row's count, centered on the row; and the Feats of
    Natu screen's title with the note under it (LargeText 220, read through the translation
    object and bounded by `large_count`), the shorter line centered on the longer, its middle
    kept as the game keeps its title's. One built again whenever the game builds its label
    again (told by the glyph count and the place the build was given). A text with more glyphs
    than the label holds is not built; one whose text was hidden (a hidden widget's, as the game-mode
    panel's on the file select before an empty slot is picked) is built hidden, its count kept
    for the game to show when the widget shows.
  - **The helper's icons** (`IconSheets`, `src/apitem.cpp`): its disk, padlock and AP logo,
    painted into the translations' icon sheets (BigFontIcons.png's 64 x 64 cells for
    English(HD), PixelFontIcons.png's 10 x 9 for the rest) at frames 0x96 to 0x98, blank in
    both and reached by no record, which the builder draws for U+E03A to U+E03C like the game's
    item icons (`docs/re/memory.md`, "The translations' icons"); only where those cells read
    blank (or already the helper's) and the game's art is where the PNG has it (a sheet not so
    read three times at most, each read being the whole sheet), now and then while a translation
    shows, and cleared when none does or before an unload. White, as the
    game's icons are: the text's color tints them. The HD padlock is drawn from the text's cap
    height to its baseline (y 6 to 46 of its cell), its left 16 units in as its gap: it follows
    ASSIST with no space (measured on the 2026-11-02 retest's shot; not run in game yet). The
    disk and the logo, which lead their text, are drawn in x 10 to 54, y 6 to 50 of the cell, a
    square chosen to match the game's own icons (their boxes' medians x 9 to 53, y 7 to 50:
    `docs/re/memory.md`, "The assist menu"): the owner's picks D2 and L1 of the 2026-11-02
    mockups: a solid disc with a hub hole, a thin groove and two shine cuts, and the pixel
    glyph's dots, smaller (the disk D2 working in game 2026-11-03, B1, `zexion_ap.dll`
    `fce9e73b…`, the 1.0.0 RC at develop 76df074, the owner's report; the logo L1 not singled
    out). `hd_icon` names the helper's glyphs (0xFB,
    0xEF, 0xEE) as these characters, so popups, names and labels carry them.
  - **Pixel glyphs shown** in place of a label's translated ones, as the game does with the flag
    clear (`0x168AE9`), only for the Feats screen's title when its HD text cannot be built
    (no `large_count`, or too little room).
- **Which translation:** all the same way, the helper's text in English in that translation's
  font. Whether a font has the Latin letters and the icons is the translation's (Japanese's
  draws both, seen 2026-11-02; Simplified Chinese's not looked at). The client warns in any but
  English(HD) (`lang`).
- **When:** each frame call (the names, the areas' emptying: `Game::watch_translation`), each
  popup (`Game::pump_popups`) and each label built. None of it with the flag clear (the default
  English, even with a translation's tables still loaded), without the translation keys (an
  offline cache from before them: "The pipe"), or with the object's fields not where the other
  keys say (`translation_keys`).
- **Safety:** the tables are heap memory, freed and loaded again when the player picks another
  translation (`docs/re/memory.md`, "Translation tables"). Each write goes into the tables as
  they are now, checked against the pointers and count read when its state was made, so after a
  language change nothing goes into the old ones, the split is made again in the new SmallText
  at the next popup and the names go into the new ItemNames; each entry only within its room
  (to the next entry's offset, less the line break before it), and put back only while it still
  reads as the helper wrote it.
- **Put back:** a name when no longer wanted, with the flag clear, a client going away and
  before an unload. An area is emptied (zeros over what was written) once its message has gone
  (its serial cleared, at most 180 frames, or its HD object taken by another message), so a
  save-state load that binds a message to it finds it empty; entry 70 is put back when a client
  goes away and before an unload, once no area is in use. An unload refuses while a popup still
  reads an area (`error a popup still shows the helper's translated text; not unloading`, until
  that message ends: 3 seconds in play; whether a message counts down off play, at the title,
  was not traced), and, with no frame, while a name is still the helper's or entry 70 still
  moved. Menu labels need nothing: the game frees them with their menu.
- **Run in game:** 2026-11-02, B1, `zexion_ap.dll` `1c286c57…` and `version.dll` `1b4e35e9…`
  (develop 2e3b94e) loaded by the proxy, the client from source, an OHKO seed with the disks goal
  and a DLC Quest player (`lab/ap/live50`: logs and screenshots).
  - Working in English(HD): the title lines; received popups in the HD font with their box, icon,
    the HD name and the suffix on the next line, singly after a save's GAME SAVED, in batches of
    5 and 8 (every one shown), and after a language change; notices (an
    Intel Chip's hint, long ones too; NOT CONNECTED; CLIENT CONNECTED; the disk reminder); a
    stand-in's pickup popup and the map cursor on its square ("Other players' items"); the pause
    row's count and the locked ASSIST in the pixel font (in the translation's font since: the run
    after, below); the equipment screen's name; the question's text, NO and YES; the Feats
    of Natu screen's note (likewise); `/reload_mod` (the unload and
    the load again; its refusal while a popup shows was not tried).
  - A language change mid-play, with popups showing: English(HD) to French and back, then
    Japanese, then the default English, with no crash; the popups after each change shown, in
    French and Japanese with the translation's name and FROM … in English. The client's warning
    once, for French, and none in English(HD) or the default English; `lang=-` in the default
    English and `English(HD)` in English(HD) (`tools/helper.py status`).
  - Bugs (English(HD)): ARCHIPELAGO's label does not show on the game-mode panel: the entry is
    blank (choosing it starts the seed), its hover border is drawn further left, and the label
    shows instead at the window's top-left corner on the file select until an empty slot is
    picked; RANDOMIZER's label built again when the client went away showed at that corner too,
    and on an empty slot's panel after that the game's own Randomizer entry showed at the
    panel's left edge, away from Normal, while the helper stayed loaded. The question sits left of center (about 90 pixels left of where the game's own line
    starts). The equipment screen's description is one line with no break and runs past the
    box's right edge. ASSIST's padlock is drawn past the options box's right edge. Fixed
    since, working in the run after (below): the panel's label and the question built where the
    game's attach would build them now, the question centered and the description wrapped as the
    game does its own, and the locked ASSIST built again in the translation's font with its
    padlock only where it stays in the box ("The assist menu"; `docs/re/memory.md`, "Where a
    rebuilt label goes").
  - The default English, the same run: received popups singly and in a batch of 5, NOT
    CONNECTED and CLIENT CONNECTED, ARCHIPELAGO on the panel, the locked ASSIST, the pause row,
    the equipment screen's disk, RETURN HOME's question (centered) and NO, the map cursor and a
    stand-in's pickup popup (with its logo glyph), all as before.
  - Not run in game: the scanner's line, an Intel Chip picked up in the world (ItemNames 49),
    the unload's refusal while a popup shows, Assist Unlock in a translation, a save-state load.
- **Run in game again:** 2026-11-02, later that day, B1, `zexion_ap.dll` `946b08a1…` and
  `version.dll` `7a1b2b40…` (develop ea1a801) loaded by the proxy, the client from source,
  `lab/ap/live50` again and a disks seed `lab/ap/live52` (logs; the owner's screenshots in
  `lab/ap/live50/shots-r2/` and `lab/ap/live52/shots/`).
  - Working in English(HD): ARCHIPELAGO on the game-mode panel in its place, in the HD font,
    with its hover border around it; the file select with nothing stray, before an empty slot is
    picked and after the client went away; Randomizer back in its place after the client went
    away; RETURN HOME's question centered (within about 10 pixels of the YES/NO column's
    middle); the equipment screen's description wrapped inside its box; the locked ASSIST in the
    HD font with its padlock inside the box; the pause row's count in the HD font ("4 of ?");
    the HD disk icon on the equipment screen, and the HD AP logo on a stand-in's pickup popup
    and on the map cursor; long notices wrapped (NOT CONNECTED on three lines, an Intel Chip's
    hint on two, its lines left as they wrap); a received popup's name centered over "from
    Archipelago", and "from Bob" (the owner's reading); the Feats of Natu screen's title and
    note in the HD font. No stutter noticed (the owner, through the run's play in HD).
  - Bugs (English(HD)): after coming back from another menu (Video, the map) the locked ASSIST
    takes the selection's colors, as the default English once did; once, after going through
    menus, ASSIST showed unlocked (white, selectable, no padlock) in a seed that locks it (not
    reproduced; whether it opened was not tried). ASSIST's padlock sits below the text's
    baseline, against the box's right border. The map cursor shows the AP logo before AP ITEM,
    where the game's own names carry no icon. A stand-in's pickup popup (AP ITEM over FOR BOB)
    starts both lines at the same x, not centered. All five fixed since. Stand-in tests only,
    not run in game yet: the locked look in the options entries' style at alpha 0.45, and the
    offline look kept through menus (a re-read of the offline files, done whenever anything
    left the pipe, had undressed the save until play: a path that unlocks ASSIST with a menu
    up); the padlock on the text's line with no space before it ("The assist menu", "AP saves
    without the client"; the ASSIST lock in English(HD) working on the 1.0.0 RC since, run
    below, no part of it singled out). Working in game 2026-11-03 (`zexion_ap.dll` `12f090c6…`): the map
    cursor without the logo and the pickup popup's lines centered ("Other players' items"). A
    second path to ASSIST unlocked, seen on the 1.0.0 RC (the version line locked in its place
    after Video), fixed since by finding the entry again each frame, run in game 2026-11-03 (the
    owner: "seems to work"; "The assist menu").
  - The owner's requests: the helper's text in mixed case in English(HD) (the notices, the
    stand-ins' labels, the title lines); the title lines in the HD font but ©2026 MCAWFUL, which
    matches the game's pixel ©2025 GALLANT LEAF; the HD disk icon (and the padlock) drawn again.
  - The default English: the title, ARCHIPELAGO on the panel with its hover border, a received
    popup (FROM ARCHIPELAGO in capitals), RETURN HOME's question centered and NO, as before.
  - Not run in game: as in the run before, and French and Japanese since the HD icons (their
    FROM … in capitals).
- **Run on the 1.0.0 RC:** 2026-11-03, B1, `zexion_ap.dll` `fce9e73b…` and `version.dll`
  `2a640209…` (develop 76df074) loaded by the proxy, English(HD): the client from source on
  `lab/ap/live50` slot 4 (`client-r5.log`, `server-r5.log`), then the owner's own checks.
  - Working in English(HD): the Feats of Natu screen's title and note both centered
    (the owner's screenshot); no Keys frame on the pause row after Option with the client
    connected, and the documented frame of Keys before it, over a cache without
    `options_back_fn` ("The pipe"; "The OS Install Disks goal"); by the owner's report, the
    assist lock ("The assist menu"), the D2 disk icon (the helper's icons, above), the title
    lines only on the title ("The title screen's lines") and YES on a first ending ("The OS
    Install Disks goal").
  - Not run in game: as before, and French and Japanese since the HD icons.

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
pipe and unloading. That test skips without a build. In CI, where `ZEXION_REQUIRE=helper`
is set, a missing build fails the job instead (`test_the_helper_is_built_where_ci_requires_it`).

Most ctest tests run against **the stand-in game**: the test program's own data and functions,
laid out as the helper expects the game's (`tests/stand_in.h`, `tests/stand_in_menu.h`) and
configured by RVA like the real game. A test's source is `tests/<name>_tests.cpp`
(`apply_test.cpp`, `unload_test.cpp` and `proxy_test.cpp` for `apply_in_frames`,
`unload_under_load` and `proxy`). They share `tests/check.h`; `tests/fake_gl.h` is the fake
OpenGL of `apitem` and `offline`; `tests/pipe_client.h` is the pipe client of `parts`,
`apply_in_frames` and `unload_under_load`, and the last two's busy threads and wait for an
unload.

- `parts`: the import-table hook and the trampoline (an exception raised behind it caught in
  front), `game_mem.h`'s menu, panel and label readers, its handler-slot swap, its memory check
  (page by page, a guard page left armed), a sprite's frame bounds and the code patches' put-back
  (one that fails held), the build check and the pipe.
- `requests`: the request parsers, the popup text's layout and the mailbox.
- `game`: the `discs` and `@N NAME` parsers, configuring, applying, the major-item list's fill
  (an AP save's only, each icon once, by item ID, up to 26), R'Talva X34 Y51's marker hidden
  (every frame while flag 154 is clear, by its shown byte; shown again on the flag, off the logo
  frame and on restore; no stand-in for names; another type left alone), the stand-ins drawn
  in palette 26 at style 34 (their first style kept; a dressed one, one not as expected and this
  world's items left alone; put back when not restyled and on restore),
  the popup queue, the
  new-game watch, a whole ARCHIPELAGO press, what a refused unload sets
  again, the received shield and death, the ASSIST watch and the scanner's text, and RETURN HOME
  WITH THE DISKS? through the frame call (its fade and popups, a save or a door in its place, a
  request lapsing outside home's cell, at a start that is not home too, the `mission_prompt`
  parser, a death's own fade left alone, the helper's own popup holding the fade while it is the
  game's last message, however many frame calls pass, the ending after YES
  starting with a message of the game's still up but waiting for the helper's own popup, and the ending's wait that an unload refuses for; the screen
  kept black after YES, a popup waiting bringing it back in, received deaths refused (`RETURN
  HOME WITH THE DISKS? ending`), and no prompt, popup or death while the ending started runs,
  until another save is loaded). `mission` covers the black after YES on its own: kept, not
  kept for a fade not as NO leaves it, ended by kDarkFrames game frames in play (from a stand-in
  frame clock, with frame calls coming faster than game frames, as at 165 Hz), another fade or
  cancel, and what `mission_black` then says; and
  the ending out of it through the helper's wrapper, in a stand-in main loop that draws each frame
  after the transition and the stepper: the routine's frame drawn black, then the fade in, and the
  wrapper never left as the transition (unload, a routine not as traced, none in the table).
- `menu`: the game-mode panel and the options menu (relabeling, putting back, closed menus and
  released widgets, the ASSIST lock's look and cursor order, the options menu built again after
  Video, GENERAL or the assist menu with another widget in the locked ASSIST's place).
- `apitem`: the stand-in records restyled and named, the logo (in its own colors, the arrow
  yellow then white), the font glyphs, the pause
  row's disc and the file select's logo in its colors painted through a fake OpenGL, the
  translations' icon sheets painted and cleared (and left alone where not as traced; each
  sheet's icons made once), the label parser.
- `equip`, `pause`, `files`, `intel`, `title`, `mapmarks`, `mission`, `offline`: the part of
  that name (`files`: the disk count and the AP logo; `title`: also its two lines built in
  English(HD)'s font and back to pixels, hidden there under a menu (Option, the file select,
  a client connecting meanwhile, English(HD) picked there), none made under a menu, with
  `menu_state` unreadable and
  with the game's line gone, and the default English's
  lines following the © whatever the menu; `offline`: the seed file, its match and
  disk count, the folder, the cache check, `offline_put`, a stand-in game dressed and
  undressed, the look held through a menu (the files read again, a read of no known seed, a
  client going away, ASSIST locked again in the same frame call), a notice withdrawn with its save, the not-connected warning again after a client
  came and went, a seed put with a client, the AP saves' slots, and the file select's logo
  through the frame call: painted, whitened through a new game's fade, and cleared by
  `restore_files`, and a cache without the translation keys or the later keys).
- `translation`: the translations, on a stand-in English(HD) (its tables as the game's parser
  leaves them): the text helpers, the tables' entries and their room, the popups in the borrowed
  areas (taking turns, waiting, emptying, the whole run, too long for it: the pixel text and the
  slot's mark cleared; the same with the tables unreadable, while unloading and with text in
  the run's entry 68: no split), a language
  change mid-popup and between a name's write and its put-back (the old tables left alone), the scanner's line, ARCHIPELAGO built again and back on the
  panel (not where the game first built it), a label with no first pixel glyph or too little
  room left alone, the locked ASSIST's translated text with its padlock (no space before it in
  English(HD), one in a pixel font; left out at a narrow box, built again over the game's, its
  style 0x15 and alpha 0.45 pinned, back when unlocked at full alpha), the map cursor's name
  without the logo, the helper's icons as characters and
  through the game (painted while a translation shows, cleared when the default English is
  picked and before an unload, a sheet the game uses left alone), the popups' layout (measured
  widths, centering, the cap, the fallback, the suffix's case, a stand-in's name's lines
  centered, and through the game into ItemNames, not where the spaces would pass its room, and
  held while its pickup message shows (another cell, paused; not with no save loaded; another
  of its record within reach takes it, even with one of the other nearer; a language change
  names the new table);
  the mixed case in English(HD) and capitals in French and the default English for a client's
  notice, the helper's own, a label and the scanner's line; a popup through the game measured
  with its icon's width, a long notice wrapped), and
  the default English (the flag clear, or nothing loaded) left as it was (`mission`, `equip`,
  `pause` and `feats` cover RETURN HOME WITH THE DISKS?'s question, centered, the equipment
  screen's disk, its description wrapped, the pause row's count and the Feats title with the
  note in a translation; `apitem` the icon sheets' painting and the HD padlock's place in its
  cell).
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
  helper, with what the client had set armed again, and a seed file put with a client
  connected known to the offline look from the next frame call), with `LOCALAPPDATA` in a
  folder of its own under `%TEMP%`.
- `unload_under_load`: a test build, `zexion_ap_hooktest.dll`, hooks the loading program's own
  `GetTickCount` import (it refuses to hook a known game build), counts calls, and survives 300
  loads and unloads while two threads call it nonstop.

## Source map

`src/`:

| File | What |
|---|---|
| `dllmain.cpp` | the hook, the pipe's requests, the wrappers the game's menus call |
| `game_calls.cpp` | `Game`: configure, grants, popups, shields, deaths, each part's watch |
| `addresses.h` | `GameAddresses`: the configured addresses, one per `config` key, which every part reads (the client-only, translation and later ones null after an offline cache lacking them) |
| `game_offline.cpp` | `Game`'s offline look |
| `requests.cpp` | the requests' parsers |
| `text.cpp` | the popups' text layout |
| `pipe.cpp`, `mailbox.cpp` | the pipe; the hand-off to the game's thread |
| `iat.cpp` | the import-table hook and trampoline |
| `build.cpp` | the known builds |
| `game_mem.cpp`, `game_layout.h` | fault-safe memory access; the layouts several parts share |
| `apitem`, `looks`, `menu`, `intel`, `scan`, `equip`, `pause`, `files`, `mission`, `offline`, `mapmarks`, `title`, `feats`, `translation` | one part each, as named above (`menu`: ARCHIPELAGO and the ASSIST lock; `feats`: Feats and achievements in AP saves; `translation`: Translations) |

`proxy/` is the proxy `version.dll`.
