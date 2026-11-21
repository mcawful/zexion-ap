# The native helper

`zexion_ap.dll`, **the helper**, runs inside Zexion (its Windows build; the Linux build's helper
runs every part of it but unloading: "The Linux helper", below) and does what the client cannot from
outside: work on the game's own thread, between frames. It gets in two ways: **the proxy**
`version.dll` in the game's folder loads it as the game starts, or the client loads it on
attaching (`client/native.py`; `tools/helper.py` too, for tests). The client talks to it over a
named pipe. It finds the game's addresses itself ("Finding its own addresses"), so with no client,
from the first start on a PC no client has ever run on (stand-in tests; not yet run in the game from
a first start), it is meant to change the title screen, the look of AP saves (the file select's
disk count too) and, in every save, how the game's text window cuts tall text ("Popups"); to set
this world's own traps off at their pickup ("Traps") and show an own OS Install Disk's count popup
at its pickup ("The OS Install Disks goal"); and to keep Feats and Steam achievements out of AP
saves.

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
  copy with no build to replace it is refused: the player updates it there. One that refuses to
  unload ("Unloading": a menu open, a trap waiting or running) is asked again every 5 s, and the
  client's line says why (`native.NotUnloaded`); meanwhile items are held back as after a failed
  attach (up to 30 s, then given with no popup: "AP saves without the client"; the traps
  received then are kept for the helper that replaces it, whatever it refuses for next, until
  one attaches, the old one is gone and the new fails to load, or the game closes: "Traps",
  "Once"), also when the client attaches first with the game already running. That hold is only for
  a helper that may still come (no reply, no frame yet, a failed load, one refusing to unload): with
  the mod missing, not matching, or turned off by `zexion_ap.disabled`, items are given at once with
  no popup (`client/ticks.py`, `attach_helper`; stand-in tests, `tests/test_zexion_client.py`).
- A file that loads as another version is not loaded again until it changes; the client says to
  rebuild `native/` or copy this release's DLLs.
- A helper whose `hello` the client can't read counts as another version. Without a build it is
  refused, not unloaded: where it came from is not known, and the game folder's copy is likely
  the same file.
- A helper of the client's version that disagrees with it is another build of that version:
  it answers `error unknown request` or `error unknown line …`, refuses `config` with
  `error missing NAME` (it ignores names it does not know), or reads `build=unknown` for a game
  build the client knows. Found while attaching, it is refused like another version; found in a
  later request (a side task's, or giving items), the client stops using it until the game
  restarts.
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

### Finding its own addresses

`src/findaddr.cpp`; the patterns are `client/patterns.py`'s, written into `src/findaddr_patterns.h`
by `tools/gen_native_patterns.py` (never typed by hand; `tests/test_gen_native_patterns.py` fails
while the header is out of date: run the tool after any change to `client/patterns.py`, and for a
new build, `docs/re/new-build.md`). So the helper needs no client, and no file a client left, to
know where the game's state and functions are.

- **When:** on the helper's own thread, right after it hooks the game and before it reads the
  offline files and serves the pipe, so before any frame call takes its result and before a
  client can ask. Never in `DllMain`: that only starts the thread, which runs after the loader
  lock is released. Only in a build the helper knows (`identify_build`): in another it scans
  nothing and hooks nothing.
- **How:** the client's rules (`client/game.py`, `locate_all`), on a copy of the image's `.text`
  read fault-safe: each pattern of a target must match exactly once (overlapping matches count),
  every pattern must lead to the same address (`kAt`: the match plus the addend; `kRip`: the end
  of the referring instruction plus its 32-bit field plus the addend), and a target one pattern
  of which fails is not found. All of `client/patterns.py`'s targets the helper takes
  (`CONFIG_KEYS`: all but `cell_state` and `pickup_counter`, which only the client reads) are
  found this way: every one is a pattern's match, a followed `rip` field or a function start with
  an addend, none needs more.
- **What then:** with no client yet, the first frame call after the files are read configures
  the helper from the scan, inactive as the cache did (only what runs with no client uses it).
  `Game::configure` gives the same checks to these addresses as to a client's (`check_key`): each
  lies in the image with 16 bytes after it, in the kind of section its key needs (a function in
  code, data in writable data, an import slot outside code), with the room `kConfigKeys` gives
  it. They compare no bytes: the cache's comparison with the bytes a client read
  (`check_offline_cache`) is not applied to the scan, which has no such record, its patterns
  having matched the code itself. A scan that missed a required key,
  or whose addresses fail those checks, configures nothing: the helper then tries the offline
  cache ("AP saves without the client", "The pipe"), and then waits for a client. Missing only
  an optional key (`text_cut_code`, the AILMENTS keys) it configures without it, as the cache
  did.
- **A client's `config` still takes over,** with its own addresses, as before. If they differ
  from the scan's for any key (or the scan lacks one the client sent, or the reverse), the reply
  is `ok differs KEY,KEY…` instead of `ok`: the helper took them all the same, and the client
  logs a warning naming the keys. Both find them by the same patterns, so a difference is a bug
  in one of the two scans (the client keeps its own: it reads and writes the game itself, and runs
  with no helper). When the scan did not run (the hook-test build, a build the helper does not
  know), the reply is always `ok`.
- **Why it did not work, in `hello`:** `scan=` is `-` before it ran; `ok`; `ok_without_NAME,NAME`
  (only optional keys not found: configured without them); `refused_<why>` (it did not configure:
  `refused_NAME:_pattern_2_matched_0_times`, `refused_NAME:_patterns_disagree_(...)`, or the
  check of an address `configure` made); `failed_NAME:_<why>` (a target was not found, and
  nothing has tried to configure from the scan yet, or a client's `config` came first); or why
  it did not run (`not_a_build_the_helper_knows`). `addresses=` is `-` while not configured,
  then where the configuration came from: `scan`, `cache` or `client`. The client logs a
  warning when it attaches to a helper whose scan did not find everything (`ok_without_…`
  too), saying by `addresses=` whether the mod then uses a cache or only the client's addresses
  (`client/native.py`, `scan_warning`).
- **An exception in the scan** (an allocation of the copy of `.text` failing, say) is caught: `scan`
  then reads `scan_failed`, and the helper goes on to read the files and serve the pipe, as it
  would after a scan that found nothing (stand-in tests do not reach it: nothing makes the
  scan throw there). The reads of the files are guarded the same way.
- **The Linux helper does not scan:** it already finds each address by name in the game's symbol
  table at start, after the binary's GNU build id matched a known build (`configure_self`,
  `read_own_symbols` in `linux/src/helper.cpp`; "The Linux helper"), and refuses a client's
  `config` that gives any key another address (`parse_config`), so a mismatch there is an
  error, not a warning. Like the Windows helper's, its configuring needs no seed file and no
  client: a first start on a machine no client has run on (no data folder, no seed file, no synced
  copy in the save folder: a fresh Steam Deck) configures from the first frames, once the game's
  player pointer reads the player state, so the title lines show with no client having connected
  (stand-in tests only; the Steam Deck run of 2026-11-20 loaded the mod, but whether the title
  lines showed with no client was not reported). It used to wait for a seed
  file to have been read; that rule dated from before the Windows scan, when a config came only
  from files a client had written.
- **Cost, and the pipe's start:** the pipe comes up after the scan, so a helper loaded late (the
  client's injection, `/reload_mod`) answers about that much later. On a copy of B1's exe
  (2026-11-20, the development PC, `tests/findaddr_tests.cpp`, 248 patterns): `find_all` 205 ms and
  `find_in_image` on the exe mapped as an image 197 ms in a Release build, 382 ms and 407 ms in a
  Debug build, whose checked containers slow `count_matches`. The client's wait for the pipe is
  `CONNECT_SECONDS` (`client/native.py`), 5 s since this change (3 s before the helper scanned at
  start): about 24 times the Release scan, 13 the Debug one. Not measured: the scan in the game under load.

Evidence, on build B1 only: the helper's scanner, run in `tests/findaddr_tests.cpp` on a copy of
the exe's `.text`, and through `find_in_image` on that copy mapped as an image (`SEC_IMAGE`: the
known-build check, the section walk over the in-memory headers and the fault-safe copy), finds all
95 addresses, at the RVAs the client's `locate_all` finds
(`tests/test_native_scan.py`, with `ZEXION_EXE` naming the copy: skipped where there is none, so
never in CI), and the rules' cases run on synthetic code in CI: exactly once, none, overlapping
matches, wildcards, both kinds, the addend, disagreeing patterns, the ends of the code, texts the
helper cannot read, an unknown build. **Not run in the game**: the helper configuring itself from
its scan on a PC with no client's files, and the title lines, the text window's repair, the
Feats guard and the map markers running from it, are stand-in tests only (the hook-test build
checks `hello`'s fields).

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
- A request over 64 KB drops the connection, which counts as the client going away. The client
  splits the popups of a big release into `apply` batches under it, the writes in the first
  (`Session.give_through_helper`; stand-in tests only, not run in game).

The requests (`src/requests.cpp` parses them; "frame" ones run on the game's thread, below):

| Request | Reply | Part |
|---|---|---|
| `hello` | `zexion-ap <version> build=<B1\|unknown> hooked= configured= feats=<off\|on\|on:why> folder= offline=<-\|N[:FILE]> active= title=<0\|1\|why> lang=<-\|NAME\|?> reads=<R/S> scan=<-\|ok\|why> addresses=<-\|scan\|cache\|client>` (flags 0 or 1), then ` read_error=<why>` after a failed read | below |
| `ping` | `pong frames=<calls since load>` | |
| `config name=rva …` (frame) | `ok`, `ok differs KEY,KEY…` (taken; the helper's own scan found other addresses for those keys), or `error <why>` | below; "Finding its own addresses" |
| `offline_put config` or `offline_put seed NAME`, a newline, the file | `stored`, `unchanged`, `error <why>` | AP saves without the client |
| `apply` and lines `cas ITEM EXPECTED VALUE` (inventory bytes 0 to 49), `refresh`, `intel`, `popup ITEM SUFFIX` (ITEM a record, 0 to 50, or a trap, 102 or 103), `notice ITEM TEXT` (frame) | `applied`, or `refused <why>` with nothing written | Grants; Popups; Traps |
| `ap_newgame` and lines `identify`, `expect`, `write` or `menu OFFSET HEX`, `start_table HEX`, `choice` (frame; a plan needs `write`, `menu` and `identify` lines) | `armed`, `refused <why>`, `error <why>` | New games |
| `newgame_status`, `disarm` (frame) | `armed`, `idle`, `applied slot N`, `refused <why>`; `disarmed` | New games |
| `ap_labels` and lines `label X Y HEX` (frame) | `labeled N`, `refused <why>` | Other players' items |
| `shield`, `die` (frame) | `spawned`, `died`, `refused <why>` | Received Energy Shields; DeathLink |
| `drop_status` (frame) | `queued N; last <outcome>`, the outcome `none`, `spawned ITEM` or `refused <why> ITEM`: the received scraps' drops, for live checks | Received scrap |
| `trap_status` (frame) | `queued N; last <outcome>`, the outcome `none`, `set ITEM`, or `nothing ITEM`: the traps set off, for live checks | Traps |
| `assists lock`, `assists free` (frame) | `locked`, `free`, `refused <why>` | The assist menu |
| `intel on`, `intel off` (frame) | the same, or `refused <why>` | Intel Chips |
| `discs COUNT REQUIRED HEX TOLD`, `discs` (frame) | `discs on`, `discs off`, `refused <why>` | The OS Install Disks goal |
| `discs_status`, `mission_prompt COLUMN ROW`, `mission_status` (frame) | `shown N` or `not shown…`; `queued`, `refused not configured` or `error <why>`; `not asked`, `asked`, `opening after the fade`, `opening without the fade: …`, `shown`, `put back: the prompt was up, YES is NO` (an unload or a client gone left it open with YES put back to NO's callback), `put back: YES was pressed, the ending had not started` (a cancel dropped the YES), `lapsed: the player left the landing pod`, `yes: ending when idle` (`, the screen kept black` or another note after it), `ending started` (until another save is loaded; the client reads it), … | The OS Install Disks goal |
| `mission_black` (frame) | how the last black after RETURN HOME's YES ended: `none`, `kept black` or `ended: …` | The OS Install Disks goal |
| `client_back`, `other_save` (frame) | `shown`, `none`; `told`, `already told`, `unknown` (no known seed: a plain randomizer game, left alone); or `refused <why>` | AP saves without the client |
| `unload` | `bye`, or `error <why>` | Unloading |

`hello`'s fields (spaces inside a field written as underscores):

- `feats`: Feats and achievements kept out of the AP save loaded (off, on, or on with why a
  change could not be made).
- `folder`: loaded from the game's own folder (by the proxy, or by a client with no build).
- `offline`: the seeds known to the offline look (read from the folder, and any a client put or
  that were read while it was connected), and the file of the one dressed.
- `active`: a client has configured the helper and not gone away.
- `title`: the title screen's lines are up (1), not (0), or why not.
- `lang`: the language the game shows: `-` its default English, the loaded translation's folder
  name while one is on (`English(HD)`, `French`, `Brazilian_Portuguese`, …), or `?` when not known
  (not configured, or nothing readable; and with no frame). The
  client warns in any translation but English(HD) (`client/ticks.py`, `language_tick`).
- `reads`: the reads a frame has taken, and the seeds the last one found.
- `scan`, `addresses`: the helper's own scan of the game, and where the addresses it is
  configured with came from ("Finding its own addresses").

`config` carries the addresses the client resolved by byte pattern (`CONFIG_KEYS` in
`client/native.py`). The whole configuration is refused unless:

- every name is there, from a client's `config`, from the helper's own scan ("Finding its own
  addresses"), or from the offline cache a client wrote ("AP saves without the client"), which
  holds them all, but `text_cut_code` and the three AILMENTS keys (below). A cache lacking one is
  refused (`read_error=cache:_missing_NAME`) until a client has run once and rewritten it; that
  matters only when the scan found nothing, since the scan holds every key of this release. A
  release that adds a key adds its patterns to `client/patterns.py` and regenerates the header, and
  decides for that key alone whether a cache written before it is refused (with everything
  the helper does without a client off until the player's first client run after updating, were
  the scan to fail there too) or lets the helper configure without that key, with a null check
  where it is read. 1.1.0's `text_cut_code` is the second kind (`optional` in
  `kConfigKeys`, `src/game_calls.cpp`): without it the rest configures (`hello` `configured=1`, no
  `read_error`) and the text window's cut stays the game's ("Popups", "Tall text") until a
  client's config, which holds it (stand-in tests, `tests/offline_tests.cpp`). `ailments`,
  `ailments_hit_code` and `ailments_wave_code`, added after 1.1.0 for the traps, are too: without
  them a trap runs with AILMENTS as the game applies it ("Traps");
- each lies inside the game's image: functions in its code, data outside it and writable;
- the exception being import slots, which lie outside code: the C runtime's `malloc` and
  `free`, only read, and steam_api64's `SteamInternal_ContextInit`, written while an AP save is
  loaded or a save slot holds one ("Feats and achievements in AP saves").

The other arguments are described in `src/requests.h` and in the parts below. Anything else gets
`error unknown request`; errors always start with `error `. A "frame" request while the helper
is not hooked gets `error not hooked, so nothing runs on the game's thread`.

### On the game's thread

A "frame" request (and `hello`'s `configured`, `feats`, `active`, `offline`, `title`,
and `lang`) goes to the game's thread through a one-slot mailbox
(`src/mailbox.cpp`) and runs at the next frame call. If no frame takes it within a second (the game paused in the
background, say), it is withdrawn, the reply is `refused no frame`, and it never runs
afterwards.

The frame call never waits on anything the pipe thread can hold: it takes the mailbox's work
with one atomic exchange, and only one thread at a time does the work.

### A client going away

When the pipe's client disconnects (the client exited or crashed, let the helper go while it
had no server connection, or `tools/helper.py` was done), the next frame call, before it serves
any request, puts back everything the parts below set in the game's memory and drops what the
client asked for (`Game::client_gone`: `put_back_game`, then `forget_client`, and
`forget_client_seed`, which closes the look's gate until the next client has put its
seed, in `src/game_calls.cpp`). A change held by a menu no longer open is gone with it. A running trap's
AILMENTS patch stays, as the trap does ("Traps"). It goes inactive
until a client sends `config` again (the client resends every request after it attaches). The
client's popups not shown yet stay queued and still show ("Popups"): their items are already
given. The offline look then applies again (AP saves without the client). The helper stays
loaded and hooked.

Working in game: 2026-10-25, the client stopped with helper 0.20.0 loaded by the proxy
("The escape skip", "Other players' items"); the client's popups staying queued, and showing,
2026-10-26 (0.30.0, "The client in a live game"). Not looked at: ARCHIPELAGO and the OS Install
Disks goal's screens.

### Unloading

`unload` first stops frame calls from running any part's watch (so nothing offers ARCHIPELAGO,
locks ASSIST or applies anything again), then, in a frame call, puts back in the game's memory
what a client going away puts back (`Game::put_back_game`) and hides the title screen's lines.
What the client asked for stays set.

It refuses (`error a menu is open; not unloading`) while a save is loaded with a menu open
(the pause, map or equipment screen, the quit menu), before putting anything back: an open
screen keeps what it was built from (the pause row's disc, an item record's name), which it
would show wrong until reopened (seen in game 2026-10-26, before this check). The refusal with
the pause screen open, the client keeping the helper and the screen unchanged, then a reload in
play: working in game 2026-10-27 (0.30.0, `native.Helper.unload`; `docs/re/memory.md`, "The
client in a live game").

It first stops the copy thread ("Seed files in the save folder") with a wait of at most five
seconds: stuck in a file call that does not return (an antivirus filter, a cloud placeholder on the
save folder) it would keep the DLL from being freed, so the unload is refused (`error the copy thread is
busy; not unloading`) and the helper works on (a refused unload restarts the thread). After the unload
has put everything back, the pipe thread stops and joins the copy thread again (already gone: no
wait) before it frees the DLL; if that wait times out the DLL stays loaded, as when the hook was
replaced, and is never freed under the thread.

It also refuses (`error <why>; not unloading`) while the game may still call into the helper, or
hold something of the helper's. That is an open menu's panel or entry, the pause menu's way back
from OPTION MENU, the open map or the game's transition routine holding one of its wrappers, or
the scanner's text, the pause screen's branch, an item record, the ending's patch, the map's
on-screen marker write, the text window's cut or the AILMENTS tests (a trap's: "Traps") still the
helper's; or, in a translation, a popup still showing the text the helper wrote into a borrowed
entry (until that message ends: 3 seconds in play), or a translated name or the popups' moved
entry 70 still the helper's when no frame came ("Translations").

It refuses (`error the ending RETURN HOME WITH THE DISKS? started is on its way; not
unloading`), before putting anything back, from RETURN HOME WITH THE DISKS?'s YES until the
helper has started the ending (and while the screen is still kept black after it, until the
helper's wrapper has run the ending routine), and while the ending routine or that wrapper is
the game's transition with the ending's patch in place: an unload would drop the ending, or put
the patch back before the ending runs ("Feats and achievements in AP saves"). That window is a
few frames, longer while a popup of the helper's already up holds the ending after YES.
Working in game 2026-10-27 (575edbf, 0.30.0): `/reload_mod` sent every half second was refused as
a menu open while the prompt showed, then as the ending on its way while received items' popups
held it, and the ending ran and sent the goal (`docs/re/memory.md`, "The mission prompt"). That
run was before the ending's wrapper: its refusal and its put back on unload are stand-in tests
only (`native/tests/mission_tests.cpp`, `game_tests.cpp`).

It refuses (`error a trap is running; not unloading`), before putting anything back, while a trap's
AILMENTS patch is held ("Traps"): putting it back would let AILMENTS end or shorten the trap. That
is at most a trap's 12 s, longer with traps of one kind queued. It refuses too (`error a trap is
waiting to go off; not unloading`, ending in "(no frame came)" when none came) while a trap for the
save loaded, or for any save (one received with none loaded), waits to go off: the unload would drop
it, and the client counts it handed over. That is a quarter of a second of play once its popup's
turn has come, longer while the popups queued before it show first, the game is halted, a menu is
open, a transition counts down (a TELEPORT TO's 60 frames), or RETURN HOME or the ending holds it. A
trap waiting for a save that is not loaded (another
save, or none: the title) is lost with the helper, as its popup is (stand-in tests:
`tests/apply_test.cpp` over the pipe, `tests/game_tests.cpp`; not run in game). Working in game
2026-11-11, B1, `zexion_ap.dll` `9370c356…` built from 5f66f1b, the seed `lab/ap/live61`, slot 8,
the owner's reports; `lab/ingame-138-traps-check3.md`: `/reload_mod` during a Disruption Trap was
refused with the client's "can't be reloaded right now (a trap is running)", and the trap ran on.
Tried 2026-11-11 (`8bd8e712…`, `develop` e500ab9, `lab/ingame-138-develop-checks.md`) with a trap
received in the pause: refused for the menu ("can't be reloaded with a menu open"), as the menu's
refusal comes first and the client hands a trap over only in play, so the trap was still waiting in
the client, not the helper; it went off at full length after. The waiting trap's own refusal has not
been seen in game.

If no frame came, it goes by what the last frame call found: the reason then ends in "(no frame
came)", and with nothing found it unloads, leaving the rest as each part's notes below say. A
refused unload lets the watches run again from the next frame call, and they apply what the
client asked for again from what was kept, since a client would not know to resend it (as the
offline look, the Feats guard and the title lines are applied again by theirs). This kept state,
and the watches applying it again after a refusal: stand-in tests only, not run in game (the
refusal itself ran on 0.30.0, above).

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

- **Limits:** "working in game" below is in the game's default English. With a translation on the
  game draws the translation's text, in its own font, in place of the pixel-font glyphs the helper
  rewrites; how each part's text shows there is under "Translations" (run in game 2026-11-02).
  Before the helper's fix, on 2026-11-01 (B1, helper 1.0.0, the release candidate), what a part
  changes in what an entry does worked in a translation, but the text it wrote did not show (the
  title lines, the file select's logo and disk count and the stand-ins' logo did), nor did many
  received items' popups (`docs/re/memory.md`, "Translations").

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

An apply leaves out what only a real pickup should do: the map cell, the pickup counter, the
major-item list (the frame call fills that in: below), achievements, switching to a new weapon,
and a key's area event (which only hides the key's map marker: `docs/re/memory.md`, "Keys, the
gates and the key markers").

**During a TELEPORT TO's countdown,** which reads as play, an `apply` is still taken. The load at
its end loads the player from a copy of the play state (`docs/re/memory.md`, "The mission prompt",
"A save at the Landing Pod"). In a randomizer save that copy is made again at the countdown's end,
except from the escape skip's YES to the ending (flags 300 and 195) or with a byte not traced
reading 2 ("Received Energy Shields", "Limits"), so by the code those writes are kept; otherwise it
is the copy made when the entry was chosen, which undoes them, and the client's next look finds the
items missing and gives them again (`client/reconcile.py`), so none is lost. Not changed here, and
not seen in game; the popups wait out the countdown ("Popups").

**The major-item list** (`pickup_list`; the file select's ITEMS row draws each save's): every
frame call in play in an AP save (`Game::watch_pickups`), the helper appends to it each owned
item the game's pickup would list (`pickups::listed`, `src/game_layout.h`) whose icon is not in
it yet, by item ID, stopping at 26 as the game does (it reads the records' icons with
`safe_read`). The game's pickup lists this world's own items found in it; the received items
are what the helper adds. It leaves out what the new game started with (the section's
start-with array, `rando::started_with`), as the game does (Standard's Machine Gun); and an
older save's items, owned before this helper, go in by item ID, not in the order they came. So an AP save's row fills as items arrive, and an
older one's the first time it is played with this helper; it shows on the file select once the
game saves. It runs without a client too (the helper's own addresses, "Finding its own addresses"). The list is
the game's own data, so a client going away or an unload leaves it (`docs/re/memory.md`, "The
pickup record holds the major-item list").

Two known limits of the ITEMS row, found by review, not seen in game, and left as they are
(issues #166, closed, and #167, open). The first is cosmetic (no item is given, lost or doubled;
the list grants nothing); the second is by the code a Linux question only, and is not traced
there:
- **An own item can be listed twice, only by a second copy of it.** The helper appends an owned
  item's icon only if it is not in the list, and the game's pickup appends without that check
  (Linux B1 `0x6428FA`; Windows `0x1F761`: `docs/re/linux.md`, "Mapped by disassembly: the
  major-item list"; `docs/re/memory.md`, "The pickup record holds the major-item list"). So the
  icon is listed twice when a copy of an item is given by the client (the helper lists it) and
  this world's own object for that item is then picked up in its room. The client gets no item
  from its own world (`items_handling = 0b001`, `client/zexion_client.py`), so no checked own
  location, `!collect` included, gives it one: the second copy has to come from outside the
  seed's plan, by a console `/send`, `!getitem`, an item link or an old seed's plando. (The
  first reading of #166 had `!collect` granting by the inventory byte; that route does not
  exist, as the code shows: read on the code, not run in game.) Avoiding it means knowing, in
  the helper, the cell of each own location (the seed's files carry none but the disks' and
  traps') and holding the listing while that cell is uncollected, or telling a legitimate
  repeated icon from this one (not traced); more machinery than the row is worth. The duplicate
  also takes one of the row's 26 places (the game's append and the helper's both stop there, and
  B1 has 27 listable kinds), so a later item's icon can miss the row.
- **A save state taken in another save, loaded while an AP save is current.** Save states are a
  player-facing assist (SAVE STATES, `docs/re/memory.md`, "The assist menu"). `loadState` restores
  the whole of `gGameState`: the inventory, the major-item list, the map grid, the flags, the
  randomizer marker and `save_loaded`. The section the client and the helper match a save by,
  `gCurrentRando`, lies outside it.
  - **Windows B1, by the code (read in the disassembly, not run in game):** the load copies the
    state's own section into `gCurrentRando` when the restored marker is 1 (`0x2007D0`, `0x20112A`
    to `0x20114E`; `memory.md`, "The assist menu", "Save states"). So the seed test (marker and
    section, `client/session.py`, `Session.step`) sees the state's own seed: a normal save's
    state leaves no seed's save, another seed's state reads as another seed's save (no check, no
    grant), and the same seed's is that seed's own finds, which the client already accepts from
    any of its saves. The helper's start-with test then reads the state's own section too, so
    the ITEMS row lists what that save started with. No false check and no wrong grant comes
    from a state by this path. The load is reached from two callers (`0x7AB3A` and `0x7CADC`)
    behind one gate, which opens on the word `+0x3E2DBA` (SAVE STATES, inferred from its place)
    or on the byte `+0x345EF4`, which a Ctrl+D toggle may set (a lead, not traced, and not known
    to be live in release B1: `memory.md`, "The assist menu", "Save states"). A locked AP save has
    the word off. The states are 16 files, named with no slot, so one can come from another
    day's save.
  - **Linux B1: not traced.** `loadState` inflates `gGameState` at `0x4AF4EE` to `0x4AF4FD`
    (`docs/re/linux.md`, "Mapped by disassembly: the major-item list"); whether it then copies the
    state's section into `gCurrentRando` as Windows' does has not been read. If it does not,
    the client would take a state of another randomizer save for the current save's (the
    marker is 1, the section the current save's, the stamp unchanged) and could send the checks
    of the cells that state collected and give items into its inventory, which the game saves
    into the current slot, until the next death or load brings the state's section in; the
    ITEMS row would list the wrong starting items. That needs the load key to work (by the
    word: assists allowed, Allow Assists or Assist Unlock received, and SAVE STATES turned on in
    the AP save; or by the byte, if that lead is live) and a state made in another randomizer
    save: deliberate steps either way.
  - Not fixed; the issue stays open for the Linux question.

Working in game: 2026-10-22, everything above but the major-item list ("Game functions the helper
calls", "The client in a live game"); the major-item list, 2026-11-04 (B1, `zexion_ap.dll`
`65878411…`, the build of develop 1acaaaf, the owner's report: an AP save's ITEMS row on the file
select filled; the Machine Gun's row in a Standard save not singled out).

### Popups

- **When:** one at a time, in a frame call, in play only, once the game's last popup has ended and
  at least 20 frames after the helper's last, and not while RETURN HOME WITH THE DISKS? is on its
  way (its fade, before the game opens it), nor from its YES (the screen kept black or not) through
  the ending it started: the ending does not wait for them, so they show at the save's next load.
  Nor while a transition counts down (a TELEPORT TO's 60 frames, which read as play): the load at
  its end clears the message slots (`0x1F9CE0`) and would cut a popup short (stand-in tests only:
  `test_received_things_wait_out_a_teleport`). Each plays the item's own pickup sound.
- **Queue:** a popup the game cannot take yet (all four message slots busy) stays queued; one
  whose item has no record (records not loaded yet) is dropped. `applied` means queued, not
  shown; the client counts a received item's popup as done once `apply` took it. The queue
  outlives the client ("A client going away"), but not the helper: an unload, or the game
  closing, loses the popups still queued (their items are given). A received trap waits in the
  queue as its popup, in its turn, and goes off as it shows, which an unload waits for in the
  save loaded ("Traps", "Unloading"); while it cannot go off (one of its kind running, the
  game's ending) the popups behind it show first, and while it waits a moment (a quarter second
  into play, the game halted) they wait with it.
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
  width the helper has set for the game's own pickup popup ("Other players' items"). A popup
  for item 44, a received OS Install Disk, is the disk glyph and OS INSTALL DISK (in a
  translation, the HD disk icon and the name in its case), not the record's DATA DISC with no
  icon (in translations other than English(HD): not run in game); an Area Map's reads Intel
  Chip in an `apply` with `intel` ("Intel Chips").
- **Stand-ins received:** a received Scrap Energy or Scrap Chunk (this player's own filler, found
  in another world) is named SCRAP ENERGY or SCRAP CHUNK with no icon, as the game loads them,
  not from its record, which the helper may have restyled as another player's item ("Other
  players' items"); its drop follows ("Received scrap"). Stand-in tests
  (`tests/game_tests.cpp`); working in game 2026-11-02 (B1,
  `zexion_ap.dll` `1c286c57…`), each sent from the server's console: Scrap
  Energy away from any stand-in and Scrap Chunk in a labeled stand-in's room, in English(HD),
  read Scrap Energy and Scrap Chunk over FROM ARCHIPELAGO; in the default English, SCRAP ENERGY
  FROM ARCHIPELAGO.
- **Follow-ups:** an `apply` may hold popups alone, such as the client's notice for an Area Map
  or Intel Chip found in the world, after the game's own popup. A received OS Install Disk's or
  Intel Chip's `popup` is followed in the same `apply` by its count or hint as a `notice`, so
  the two show one after the other, each with the item's sound, and wait together (a chip's
  two waiting: not run in game).
- **Tall text:** the game's window over a popup's text cuts a line's top wrong past the first
  line, so from five rows of glyphs on (a four-line notice with its blank row, or a five-line
  popup) a line flashes back whole while the box opens and closes; the game's own popups never
  get there (`docs/re/memory.md`, "Game functions the helper calls"). While configured (with or
  without a client, in every save; not from a 1.0.0 cache, which lacks the key: "The pipe"), the
  helper repairs that cut in the game's code (`text_cut_code`, 24 bytes on Windows; `TextCut` in
  `src/text.h`, built by `src/text.cpp`): each frame call it patches it if it still reads as the game's, and puts it
  back with the rest of the game ("Unloading"). Every line then opens and closes with the box
  as the game's short popups do. A translation draws its own text, never through this window;
  the game's other message routine (`0x185540` on B1) draws its own through it too.
  Tests: `tests/game_tests.cpp`. Working in game 2026-11-08 (B1, `zexion_ap.dll` `b8ef0214…`):
  the pod reminder (four lines and its blank row) in the default English, every line opening
  and closing with the box, filmed; in English(HD), a two-line hint as before. Again with the
  patch in `CodePatch` (`src/game_mem.h`), 2026-11-08 (B1, `zexion_ap.dll` `0f51bb6f…`, develop
  5597753, the seed `lab/ap/live57`, slot 6, the owner's report): the pod reminder in the
  default English, no line flashing. Not run: a
  five-line popup, a tall popup in a translation, the other message routine's text (what it
  shows is not recorded).
- **In a translation:** popups and notices show in the translation's font, from a borrowed
  table entry; one may wait a frame or two for it ("Translations"; working in game 2026-11-02
  in English(HD), French and Japanese).

Working in game: 2026-10-24, notices up to five lines ("Game functions the helper calls");
2026-10-25, a notice after a found Intel Chip's popup ("Intel Chips"); 2026-11-08 (B1,
`zexion_ap.dll` `2c29d10e…`, PR #118's head `c0be163`, the seed `lab/ap/live55`, the owner's
report), each received OS Install Disk named as the disk with its icon over its sender, then
its count: one before the scanner and one after in the default English ("OS Install Disk 1",
then "2 of 4"); two at once in English(HD), with the HD disk icon, in mixed case (popup, "3 of
4", popup, "4 of 4", then the pod reminder); and one sent while RETURN HOME's question was up,
with no popup over it, both its popups shown at the next load after YES and the ending (their
language and the count's text not stated); and a received Intel Chip named as the chip over its
sender, then its hint, in the default English and in English(HD) ("Intel Chips"). The sounds
were not singled out.

### Received Energy Shields

- **What:** `shield` runs the Energy Shield's pickup event as its pickup does, from the player's
  cell (`spawn_shield`, which checks first that the record names event 3553, that the event's
  whole record is B1's, and that its handler is in the game's code: `run_item_event`, as for
  "Received scrap").
- **When:** once the player has been in play in one cell for a second with no transition counting
  down (a TELEPORT TO's 60 frames read as play, and the load at their end resets the player and the
  room, keeping a shield only as the teleport's last copy of the play state holds it: below; the
  second starts again after it: `test_received_things_wait_out_a_teleport`; working in game
  2026-11-14 (B1, `zexion_ap.dll` `1b3a3e9f…` built from 99dc486, `lab/ap/live62`, slot 5, the
  owner's report; `lab/ingame-checkpoint-checks.md`): one received in the pause, then TELEPORT TO
  START, came up 1.45 s after the teleport's load ("the shield arrived after the teleport"); a run
  that matches the hold without isolating it, that countdown reading as play for 1.000 s,
  `kSettledMs`, and the load likely moving the player to another cell (inferred, not recorded in
  that run: the 2026-11-13 trace's teleport did), so the one-second rule alone would likely have
  held it too), and not in an ending RETURN HOME WITH THE DISKS? started (`refused RETURN HOME WITH
  THE DISKS? ending`). The client asks for each received shield not yet given, again for one not
  seen up within a few seconds, and counts it once it sees the shield up; after three requests in a
  row not seen up it asks for none until the save is left or the game closes (another seed or slot
  has its own count of misses: `client/shields.py`, `client/state.py`'s `SeedState`). A shield
  already up through a TELEPORT TO comes back after its load: the load restores the strength of the
  teleport's last copy of the play state and makes a new shield for it (by disassembly, not seen in
  game: `docs/re/memory.md`, "Received Energy Shields", "Limits"). In a randomizer save that copy is
  made at the countdown's end (except from the escape skip's YES to the ending, flags 300 and 195,
  or with a byte not traced reading 2), so a shield spawned just before the pause whose strength
  comes only during the countdown comes back too. Where the copy is the one made as the entry was
  chosen, that shield would be lost to the load (when a spawned shield's strength is set is not
  traced): read up by `confirm_shield` if the client looks during the countdown, then not given
  again (the limit "counted, then lost to a death or reload"); or, at a first look after the load, a
  miss, which counts toward the three in a row that stop shields until the save is left or the game
  closes (`client/shields.py`, `Shields.looked`, STOP); the helper cannot hold it, the spawn coming
  before the pause.
- **Put back:** nothing to put back.

Working in game: 2026-10-23 ("Received Energy Shields"); the client's check as it is now, with the
client attached to a save already in play, each shield spawned once and read as up and none spawned
again, 2026-11-03 (B1, `zexion_ap.dll` `fce9e73b…`, the 1.0.0 RC at develop 76df074); one received
while disconnected, given once on reconnecting and not again after another reconnect, 2026-11-04
(B1, `zexion_ap.dll` `a7469024…`, develop 345fea7); one received in the pause, then TELEPORT TO
START, up after the teleport's load, 2026-11-14 (B1, `zexion_ap.dll` `1b3a3e9f…`, 99dc486, "When").
The stop after three not seen up: stand-in tests only, not reached in game (`docs/re/memory.md`,
"Received Energy Shields"). The check of the whole event (since 2026-11-08; before, its type,
subtype, object and own number): working in game 2026-11-09 (B1, `zexion_ap.dll` `5f761061…`,
develop 88bebfb, the seed `lab/ap/live57`, slot 6, the owner's report): a shield sent from the
server's console came up and was read as up.

### Received scrap

- **What:** a received Scrap Metal, Scrap Energy or Scrap Chunk (46 to 48) gets the drops its
  pickup spawns: once its popup has shown, the helper runs the item's pickup event (3548, 3549
  or 3550) as the pickup does, from the player's cell (`pump_drops`, `run_item_event`). Scrap
  Chunk's runs a fade of layer 7 after it, as the game's does. The drops start near the top-left
  corner of the player's cell (the corner plus (5, 8), as the pickup's start at its item's
  cell's), not at the player: where that puts them in a room of several cells, and whether
  they can be reached from anywhere in it, is not recorded (run once from the far side of a
  big room, below). Its inventory byte stays the
  client's grant, +1 Scrap Metal ("Grants"). The record must name the event (or −1, a
  stand-in's record while the helper restyles it, or Scrap Metal's 3548 while it is named
  after this player's own Scrap Metal: "Other players' items"), and the event, and
  the fade it runs next, must be B1's records byte for byte, their handlers in the game's code
  (`docs/re/memory.md`, "Received scrap"); otherwise it is dropped, and `drop_status` says
  why. Nothing is run for a received Area Map (its event, 3551, reveals the map around the
  cell it is run from, and a received one has no square of its own: `docs/re/memory.md`,
  "Intel Chips"), an Energy Shield (the client's `shield`) or a notice, nor for a
  stand-in found in the world (its record names no event while restyled: the item is another
  player's), but for one in a cell labeled this player's own Scrap Metal, whose record names
  Scrap Metal's event so that the game's own pickup spawns the drops ("Other players'
  items", "This player's own Scrap Metal").
- **When:** as `shield`: once the player has been in play in one cell for a second with no
  transition counting down (the load at a TELEPORT TO's end rebuilds the room: stand-in tests only,
  `test_received_things_wait_out_a_teleport`), and not from RETURN HOME WITH THE DISKS?'s fade
  through the ending it started; one a frame call, in the order of their popups. In another seed's
  save or a normal save it waits for its popup's save ("Popups"); off the map, for the player to be
  on it. Nor in the game's own ending, whose cutscenes read as play: the helper reads the client's
  goal (flag 250 set, flag 211 clear: `client/session.py`) every frame, and once a load has been in
  play with it false, every drop waits while it reads true, whenever its popup showed, until the
  next load (`docs/re/memory.md`, "The ending: flag 250", "The escape skip"), as the client holds
  received items then. A save past its ending reads it true from the start and gets its drops, until
  it plays the ending again (211 set at the ship, or by the escape skip's YES, and cleared as the
  ending begins; 211 read set counts even on a frame not in play). As for the client, an ending
  replayed under Abridged Finale (no escape, so no 211) is not told apart; that 211 behaves in a
  replay as measured in a first playthrough is assumed; and a death after 211 is set at the ship
  (the save point's flags back: 250 set, 211 clear) holds drops, as it does the client's items,
  until the next load. The popup itself still shows over the ending's cutscenes, as before.
- **Once:** a drop rides on its popup. The client hands each received item's popup over once
  (its count in the server's data storage: "Grants"); the helper queues the drop as it shows
  the popup and forgets it once run. So a scrap received while disconnected, or out of play,
  drops after its popup at the next load; one whose popup was handed over before 1.1.0, or
  that was written directly with no helper, or that the slot held at its first connection (the
  client counts those handed over with no popup: `client/state.py`, `announced_start`), never
  drops; one whose popup the client hands over
  again drops again (the client stopped after `apply` took the popups and before the server
  stored their count: the popup then shows twice too); and the queue outlives the client
  ("A client going away") but not the helper: an unload, or the game closing, loses the drops
  not spawned yet, as it does the popups. Drops lost to a death or a reload before they are
  collected are not given again, as with the game's own pickup.
- **Put back:** nothing to put back.

Stand-in tests (`tests/game_tests.cpp`, `test_received_scrap_drops`). Working in game
2026-11-09 (B1, `zexion_ap.dll` `5f761061…` with `version.dll` `02a5e7f4…`, develop 88bebfb,
the seed `lab/ap/live57`, slot 6, the owner's report), each sent from the server's console: a
Scrap Metal, a Scrap Energy and a Scrap Chunk dropped after their popups, the Scrap Chunk with
the player at the far side of a big room, its fade looking right; a Scrap Metal sent while the
client was disconnected dropped once after the reconnect, and nothing came again after a second
reconnect. The owner said they worked as expected: which drops came, how many, where they
appeared and what collecting them gave were not described (what a drop gives stays assumed:
`docs/re/memory.md`, "Received scrap"), and `drop_status` was not read. Not run in game: drops
held through the game's ending or RETURN HOME WITH THE DISKS?, or waiting for their save or for
the player to be on the map.

### DeathLink

- **What:** `die` deals a received death as the game's damage code does: energy to 0, then the
  game's death routine (`kill`), through an Energy Shield too.
- **When:** once the player has been in play in one cell for a second with no transition counting
  down (a death in a TELEPORT TO's countdown would race the load at its end: stand-in tests only,
  `test_received_things_wait_out_a_teleport`), the game's halt byte is clear, no message is showing
  (the routine posts one without checking it was made) and RETURN HOME WITH THE DISKS? is not
  between its fade and its prompt, nor in the black after its YES or the ending it started;
  otherwise `refused` (`not configured`, `not settled`, `halted`, `message showing`, `RETURN HOME
  WITH THE DISKS? ending`, `no frame`; `RETURN HOME WITH THE DISKS? opening` is kept as a defensive
  check but not expected, the fade's countdown refusing first as `not settled`). The client queues
  received deaths and asks once a second, one at a time (`client/deathlink.py`); it drops the queue
  while an ending that began in this load runs (`Session.holding`, also an ending played again
  through RETURN HOME); a save that had already ended deals them as any save does until its ending
  is played again (stand-in tests only).
- **Put back:** nothing to put back.

Working in game: 2026-10-23; 2026-11-02 (B1, `zexion_ap.dll` `946b08a1…`), a death received
during an ending that began in that load (RETURN HOME's YES on a disks seed) dropped, and none
dealt on the next load ("DeathLink").

### Traps

- **What:** a received Disruption Trap or Disorient Trap (the world's codes 102 and 103, with no
  record of the game's: `src/trap.h`) pops up as its name over its sender (DISRUPTION TRAP over
  FROM ALICE; in a translation, Disruption Trap in the translation's case), with no icon and
  Scrap Metal's pickup sound, and goes off as its popup shows, in the frame the game takes it
  (the owner's choices, 2026-11-11: at once with the popup, then the popup in its turn).
  The helper makes the writes the game's own touch makes
  (`run_trap`; `docs/re/memory.md`, "Ailments: the script step that sets them"), but longer and
  past a shield (the owner's choices), and the game's per-frame code does the rest:
  - **Disruption Trap**, the Phazo's (its contact state 2857, then op 4's timer): sound 26 as
    the state plays it; then `+0xDC2` becomes 660 (about 12 s at the 55 frames a second
    measured; the Phazo's 240 is about 4.3 s) unless it is already longer (a Phazo's touch during
    a trap's, at most 240, leaves it). An Energy Shield up
    does not stop it (op 4's shield branch, the shield taking it, is left out). Energy weapons
    don't fire while it runs.
  - **Disorient Trap**, the Balorb wave's (its contact state 2902, then op 5, then the state it
    chains to, 2904): unless `+0xDC4` is already 660 or more, `+0xDDB` turns by 4 (mod 8) if
    none is running, then `+0xD20` becomes at least 2 and `+0xDC4` 660. Then sound 82, 2904's,
    whatever op 5 did, as the game's chain plays it. The controls are reversed while it runs.
  - **AILMENTS left out** (the owner's choice): op 5's test of it is not made, and while a timer a
    trap set runs, the helper patches that timer's two tests of AILMENTS in the per-frame update
    (`ailments_hit_code`, `ailments_wave_code`: each test's `jne` made a `jmp`, `trap::HitPatch`
    and `trap::WavePatch`), so it counts down as at NORMAL: REDUCED (1) would make the disruption
    a third as long and cut the disorientation to 60 frames, IMMUNE (2) would end either at once.
    It patches only code that reads as B1's (but the loads' displacements) and whose loads lead to
    the configured `ailments` (`trap::ailments_reads`), as a trap starts (before the game's next
    frame) and each frame call while the timer runs, and puts it back once the timer has run out,
    with no save loaded, and on an unload, which is refused while it is held (`a trap is running`,
    "Unloading"); a client going away leaves it to the trap, which outlives the client. An enemy's
    touch of the same kind meanwhile also counts down as at NORMAL.
  - **Left out:** the game's random number the handler draws (the two states' random part is 0,
    so the duration is the same); and where the sounds play: each at the player's position,
    where the game plays 26 at the touching object's and 82 at a point on the screen.
- **This world's own**, picked up in it: the game hands out an important stand-in there, labeled
  as one of its owner's items, whose pickup popup names the trap (`@N/NAME`, `@N NAME/PICKUP` for
  a disk: "Other players' items"; `client/aplabels.py`, `trap_label`). The helper sets it off
  itself, at the pickup, with a client or none (`watch_own_traps`, `src/game_offline.cpp`): the
  seed's file lists each own trap's map cell and item (`traps`, "AP saves without the client";
  the client writes it from the slot data's `trap_locations`), and every frame call in play in
  that seed's save it reads those cells of the map grid; one that reads collected (`0x1B`) and
  did not the frame before, after two frames in play in a row, is a pickup (the frame that
  first shows it may be of a menu: a pause taken on the pickup's frame), which queues the trap
  with no popup of the helper's, to go off at once, whatever popups wait. Two frames is
  chosen: that a load fills the map grid within one frame call is assumed, and not measured
  for a load from the file select; a load that took longer while reading as play would set
  off every own trap the save holds. A read-only trace of 2026-11-15 did not settle it: at
  a RELOAD FROM SAVE the grid was the save's by the first sample that read in play (0.3 ms
  after the last that read menu), and two loads from the file select were over a map that
  held the save's cells already, which shows nothing (`lab/ingame-collected-checks.md`,
  "Check P"). It follows the
  save, as this world's other items do
  (the owner's decision, 2026-11-14): after a death or a reload without saving, the item lies in
  its room again, and picking it up again sets the trap off again. No load is taken for a
  pickup: a load comes from a menu, a death or no save, and the first two frames in play
  after any of those, or after another seed's save, only take the cells as they are; a
  TELEPORT TO's load, at the end of a countdown that reads as play, turns no cell collected
  (`game_calls.h`, `watch_own_traps`). A trap queued and not gone off yet (behind one of its
  kind still running, or through a death) is dropped when its cell no longer reads collected,
  its pickup undone by a load: the item is back, and its next pickup queues it again. One
  pickup queues it once: undone and made again before play has settled, so before the queued
  one was dropped, it finds that one still queued and adds none (`own_trap_found`). And a
  queued one is for the save slot it was picked up in: still waiting when the player quits to
  the title and loads another slot's save of the seed that holds the cell collected, it is
  dropped, not set off there (`pump_traps`; both the re-auditor's, 2026-11-15, stand-in
  tests). Dropped, not kept for its own save: loading that save again, or a COPY FILE of it
  in another slot, does not set it off, its cell reading collected already. The helper has the list from the client's `offline_put` even when the file cannot
  be kept: with no `LOCALAPPDATA` folder, a file over 48 KB, or a write that fails, the
  request is refused (the client then says so) after the seed has been handed to the
  running helper, and the client sends a file over 48 KB all the same while the pipe takes
  the request (64 KB). A seed the helper took but cannot keep (no folder, too large) is not
  put again until its text changes, and one put again before a frame took the last replaces
  it in the helper's queue (`note_put_seed`). Until the client's first `offline_put` of a seed, which waits for
  the received items' list and the scouted locations (asked for every 5 s), a helper with no
  file of that seed from an earlier run knows no own trap: one picked up in those seconds
  does nothing. A seed's file merged with another list of traps starts the two looks over,
  so a pickup in those two frames is missed (neither seen). With no helper attached an own
  trap does nothing, and nothing makes it up later: the client says so once.
  The client sends nothing for an own trap and keeps no record of one. A seed file written before
  the `traps` line lists none: its traps do nothing with no client until a client has connected once
  (only development builds made such files: 1.1.0 has no traps). Stand-in tests
  (`tests/game_tests.cpp`, `test_own_traps_go_off_at_each_pickup`; `tests/apply_test.cpp`, through
  the frame hook and the pipe: each fails with the watch taken out of `watch_all`). Working in game,
  2026-11-14 (B1, slot 6, the seed `lab/ap/live64`, `zexion_ap.dll` `5f19e4d4…` built from bbaf3c5;
  the owner's reports, `lab/ingame-collected-checks.md`). With no client: this world's own Disorient
  Trap at Kresezoa 26,26 set off at its pickup, back in its room after a RELOAD FROM SAVE, and set
  off again at the next pickup (the owner: "that trap also went as expected"; a read-only watcher
  timed the two pickups and the reload between, through the menu in 1.3 s). With the client running:
  its own Disruption Trap at Kresezoa 25,25 set off at its pickup (the client's log has the find),
  and its cell read not collected again 41 s later, after a reload made in game (nothing in the
  client's log of a return to the title; its kind was not logged); that it went off again at a
  second pickup is the owner's word alone ("that happened as expected"). Not run in game: a death, a
  trap queued behind one of its kind, a pause on the pickup's frame, a queued one through a TELEPORT
  TO or carried to another slot, a load from the file select over a different map (the two traced on
  2026-11-15 were over a map that already held the save's cells, which left the question open), and
  no helper; the second review round's fixes here (one pickup queued once, a queued one dropped in
  another slot's save, the seed handed over before the file's checks) are stand-in tested only.
- **When:** one a frame call (`pump_traps`, after the watches, so never while an unload runs), once
  the player has been in play for a quarter of a second in any cell with no transition counting down
  (the game's transition timer above 0, `transition_queued`: a TELEPORT TO's 60 frames before its
  load, which read as play, and whose load's reset would undo the trap) and with the game's halt
  byte clear, not from RETURN HOME WITH THE DISKS?'s fade through the ending it started, and not in
  the game's own ending, held as received scrap's drops are ("Received scrap", "When"), in the save
  it was queued for, its `apply`'s or its pickup's (another seed's save or a normal save: it
  waits). One of each kind at a time
  (the owner's choice): a trap waits while one of its kind that a trap started still runs, and
  starts as it runs out, so traps that arrive together (after a reconnect, say) each get their 12 s,
  one after another; an enemy's own effect does not hold one (the trap restarts it, as the enemy's
  touch would), but an enemy's touch in a trap's last 240 or 251 frames extends that timer, and the
  next trap of its kind waits for it too. A received trap's popup takes its turn in the popup queue
  as any received item's does, and the trap goes off as it shows (the owner's choice, 2026-11-11:
  "the traps should always go off as soon as the trap popup goes off"): after the popups queued
  before it for the save loaded, a disk's and its count or a chip's and its hint among them, and
  past popups waiting for another save. "Shows" is the frame the game's popup routine takes it
  (`message_fn` returns a message): one not taken (every message slot busy, a translation's entry
  not free yet) keeps its trap waiting with it. While its trap waits for a moment (a quarter second
  into play, a transition counting down, the halt byte) the popups behind it wait too, as behind any
  popup; while it waits for one of its kind, or through the game's ending, they go first, and its
  popup shows only as its trap starts (nor does it hold RETURN HOME's fade). A popup that goes ahead
  of such a trap's keeps its own follow-ups (a disk's count, a chip's hint: the notices of its own
  `apply`; another `apply`'s stay put) right behind it, so a trap whose kind runs out while that
  popup shows comes after them (since the review of 1ead60c, stand-in tests only:
  `test_a_popups_notices_stay_ahead_of_a_trap_it_passed` and
  `test_a_later_batchs_notice_stays_behind_a_trap_passed`). With no record for Scrap Metal (records
  not loaded) it waits, never dropped as other popups are. That the game draws every message its
  routine takes is not traced, and a popup a load cuts short has already set its trap off (the
  load's reset then ends it). Held through the pause and every menu (not in play), a death (energy
  0), a load, and a TELEPORT TO's countdown: after its menu closes the game plays the teleport in
  the player's room for 60 frames that read as play, then loads, and the load resets the player's
  timers, so a trap received in the pause waits until a quarter second after that load, with its
  popup (before, in check 8, a TELEPORT TO START, it went off during the teleport and the load ended
  it 0.39 s later: below; `docs/re/memory.md`, "Telling play from menus, the title screen and
  death"). RELOAD FROM CHECKPOINT and RELOAD FROM SAVE load as their menu closes, and a death's
  respawn behind its menu, so none of them has such a stretch; room changes leave the timer alone
  (the same heading). A cutscene other than the ending's is not told apart from play either: none
  was measured. Without the AILMENTS keys (an offline cache written before the traps, configuring a
  helper whose own scan failed, that a client has not configured since), a trap runs with AILMENTS as the game applies it.
- **Once:** a received trap rides on its popup: the client hands each received item's popup over
  once (its count in the server's data storage), and the helper queues the popup as the `apply`
  takes it and runs the trap once, in the frame it shows the popup and takes it off the queue. So
  one received while disconnected, or out of play, runs once at the next load; one written with no
  helper (no popup) never runs, unless an old helper of another version refused to unload (for
  whatever reason, however its later refusals change): those received then are kept as held
  traps for the helper that replaces it (`Session.keep_traps`; stand-in tests only, not run in
  game). At a slot's first connection (no count stored yet), every item
  already received counts as handed over with no popup, so as not to flood the player with old ones,
  but the traps among them still get their popup, and so run, once (`client/state.py`, `held_traps`;
  `Session.held_traps`): unlike received scrap's drops, which those first items do not get
  ("Received scrap"). Those still waiting are kept in their own record in the server's data storage
  (`held_key`, stored before the count and emptied once they are handed over), so a reconnect or a
  client restarted before the player is in play keeps them, while the count, stored at once, still
  pops up what comes meanwhile. The queue outlives the client ("A client going away") but not the
  helper (the game closing loses the traps not run yet, as it does the popups, and an unload those
  waiting for a save not loaded, refusing for the rest; a received one now waits there behind the
  popups queued before it, so longer than its quarter second: a reconnect's many popups can keep it
  a minute or more). This world's own is no client's to send or record: the helper sets it off
  at each pickup ("This world's own", above). As with a received scrap's drop, a received trap
  can run twice where the helper took it and the client closed before the server stored its
  record (the popup count or the held traps' record): at that moment, or after the connection dropped then and
  before a reconnect. That includes a held trap's first record lost with the connection: the client
  restarted then finds no count and no record, and counts that trap held again. A reconnect keeps
  the session's own list of held traps, not the server's record, so an emptying store the connection
  lost does not hand a held trap over again (it writes the record again instead), and a second
  client of the slot writes the record only when its own list changes. A connection's first record
  of them is made only where the server has none (the `default` operation; later ones `replace`,
  sent without waiting for the server's reply: `Connection.held_stored`), so a second client's
  first, arriving late, never lands over the other's emptied one. A null some other client of the
  room wrote there counts to the server as a record, so that first record keeps the null and a
  restart before the hand-over finds no held trap: lost, which needs another client to write the
  key. Junk in the popup count (not a count, from another client) reads as none stored, so a client
  restarted then counts every trap the slot ever received as held and sets them off again. A trap
  whose effect a reload or a death ends early is not given again, as with the enemies' own; nor one
  a save at a save point ends (its player reset, by disassembly, not seen in game:
  `docs/re/memory.md`, "Ailments: the script step that sets them", "What clears them").
- `trap_status` (for live checks): `queued N; last <outcome>`, N the traps not set off yet (this
  world's own and the received ones' popups waiting), the outcome `none`, `set ITEM`, or `nothing
  ITEM` (its timer already longer).
- **Put back:** the AILMENTS tests' patches, as above, but not when a client goes away: a running
  trap keeps its patch, as it outlives the client, and an unload is refused while one is held (`a
  trap is running`, before anything is put back), then puts it back (`put_back_traps`); the timers
  are the game's, and run out as the enemies' do.

Stand-in tests (`tests/game_tests.cpp`: `test_received_traps`, `test_own_traps`,
`test_traps_without_ailments`, `test_traps_held_for_the_ending`, `test_traps_wait_out_a_teleport`). **Run in game in its first
design** (2026-11-11, B1, `zexion_ap.dll` `8fb38a28…` built from PR #138's 57381bf, the seed
`lab/ap/live59`, slot 7, the owner's reports; `lab/ingame-138-traps-checks.md`), when a trap went
off after its popup closed, for 240 and 251 frames, an Energy Shield took a Disruption Trap and
AILMENTS applied: a received Disruption Trap and Disorient Trap ("that all worked as expected"); a
Disruption Trap with a shield up ("the shield kept the trap from working"), no shield spawned again;
one sent in the pause menu, set off after it ("popup then trap"); one sent while the client was
disconnected, once after the reconnect, not again after a second; this world's own Disruption Trap
after its pickup popup, then labeled by its name; another Zexion player's Disorient Trap in this
world, nothing to the player; a Disorient Trap with AILMENTS REDUCED ("about a second") and IMMUNE
("got the sound but no effect"). The owner then asked for the design above. **Run in game as it is
now** but for the pickup text, the popup waiting with its trap, and what sets this world's own
off (then the client's follow-up, once per location; since 2026-11-14 the helper's watch, at
each pickup, working in game that day: "This world's own") (2026-11-11, B1, `zexion_ap.dll`
`a5647085…` built from 81dd8cc, the seed `lab/ap/live60` with a Bumper Stickers player, slot 7, the
owner's reports; `lab/ingame-138-traps-recheck.md`): this world's own Disruption Trap, its important
marker and disguised look, going off at once for about 12 s; a received one with its popup, at once,
about 12 s; two Disorient Traps at once, one after the other; a Disruption Trap past a shield, whose
strength read 80 after (a full one); a Disorient Trap with AILMENTS IMMUNE and a Disruption Trap
with REDUCED, each its full length, the disruption's patch read in memory 2 s in (both `jne`s `EB`,
the other timer's untouched) and put back after; another game's trap as PROGRESSION ITEM FOR BUB and
another Zexion player's disguised, nothing to the player; and the three AILMENTS targets resolved
live. The owner then asked for the pickup popup to name the trap (it named the disguise) and for a
queued trap's popup to wait for it. **Working in game** with those and the drawn disguises
(2026-11-11, B1, `zexion_ap.dll` `9370c356…` built from 5f66f1b, the seed `lab/ap/live61`, slot 8,
the owner's reports; `lab/ingame-138-traps-check3.md`): a trap sent before the slot's first
connection, popping up and going off when the owner started the save; this world's own trap
disguised as Zenith Thrusters and another Zexion player's as the Jump Booster, each pickup popup
naming the trap (FOR BOB for Bob's); two Disruption Traps at once, each popup showing as its trap
went off; and the refused unload above. **Working in game as it is now** (2026-11-11, B1,
`zexion_ap.dll` `8bd8e712…` built from `develop` e500ab9, the seeds `lab/ap/live62`, slot 5, and
`lab/ap/live63`, slot 6, the owner's reports, the trap timers read in memory every 20 ms;
`lab/ingame-138-develop-checks.md`): a trap sent before the slot's first connection, the client
connected at the title and started again before play, going off once at the new game and not again
after another restart (the held traps' record); a trap shown as an OS Install Disk (`@44 OS Install
Disk/NAME`), its pickup popup naming the trap and the disk count unchanged; one shown as this
world's own Beta Key, with the map revealed in full, a major's symbol and no key marker on its
square while the real keys showed theirs, its pickup popup naming the trap; another Zexion player's
with the fallback look (its owner started with every upgrade), shown as the Rocket Slide, FOR BOB,
nothing to the player; a trap received while dead, going off 0.8 s after the respawn for its full
660 frames; and two Disruption Traps with the client closed 3.3 s into the first: the first ran its
660 frames, the second, waiting in the helper, went off with its popup as the first ran out, 660
frames, with no client, and neither again once the client was back. **Not working in game** on
e500ab9: a trap received in the pause went off during a teleport and was reset 0.39 s later, in
check 8, a TELEPORT TO START, though its record named RELOAD FROM CHECKPOINT (the owner, asked on
2026-11-14: "it was teleport because it did the teleport animation from the room you're in to the
room you go to, the reload from checkpoint (or reload from save) option just loads your character in
that spot"; its timing, the reset 1.0 s after the menu closed, and the 2026-11-13 trace, in which
RELOAD FROM CHECKPOINT has no such stretch, agree; "When"). Held through that countdown since
907abfd: **working in game** (2026-11-13, B1, `zexion_ap.dll` `55fd60e4…` built from 907abfd, the
seed `lab/ap/live62`, slot 5, a new save, the owner's reports, the trap timers read in memory every
2 ms; `lab/ingame-checkpoint-checks.md`): a Disruption Trap received in the pause, then TELEPORT TO
START: set 0.25 s after the teleport's load, 660 frames, with its popup as it went off ("the trap
arrived after loading into the area the teleport took me to"; asked on 2026-11-14 whether the popup
showed as the effect started: "yes, as soon as the popup came in, the effect happened"); with a trap
received in the pause, RELOAD FROM CHECKPOINT ("I got it after loading in from the checkpoint") and
RELOAD FROM SAVE ("got the trap when loading back in"), neither run in game before (the old record
of check 8 named RELOAD FROM CHECKPOINT, but that was a TELEPORT TO START: above); and, as before,
one received while dead, through the respawn ("I got the trap after getting back into the game"),
each its full 660 frames. The other two TELEPORT TO entries set the same timer (by disassembly) and
were not run. Also on e500ab9, the popup order, where a trap's popup and the trap came before a
disk's popup and count received with it.
That order is changed since 704bd25 (a received trap's popup in its turn, the trap going off as it
shows; stand-in tests `test_a_trap_popup_takes_its_turn` and
`test_a_parked_trap_popup_does_not_hold_the_mission`, `test_a_trap_popup_with_no_record_waits`):
**working in game** 2026-11-11 (B1, `zexion_ap.dll` `a8939f69…` built from 932411b, the seed
`lab/ap/live62`, slot 5, a new save, the owner's report; `lab/ingame-popup-order-checks.md`):
paused, an OS Install Disk then a Disorient Trap sent; on unpausing the disk's popup and its count
came first, then the trap's popup with the trap ("those came in the right order"). That run was on
932411b; keeping a popup's own follow-ups ahead of a trap it passed ("When") was added after it, in
the fixes from the review of 1ead60c, and has stand-in tests only. **Not run in game:** a trap's
popup in a translation (the name in the translation's case: by the code, `layout_popup`); received
traps kept through a refused version swap, and a big release's popups split into batches under the
pipe's 64 KB; an unload refused while a trap waits (the client hands a trap over only in play, and a
menu's refusal comes first: tried with one received in the pause, refused for the menu, the trap
then going off at full length); a trap through the ending or RETURN HOME; a key-look trap's missing
marker after the scanner (only a map revealed in full was run); a trap's popup passed by others
while it waits for one of its kind, one held through the game's ending, and RETURN HOME not held by
one; a connection's first held-traps record beside another client's (the `default` operation); and
the restart doubles above.

### The assist menu

- **What:** while locked (`assists lock`), the options menu's ASSIST entry is drawn in STYLE:
  AUTO's style with a padlock after it (font glyph `0xEF`, painted while armed), its callback is
  cleared so pressing it does nothing, and it is moved past the panel's selectable entries so the
  cursor skips it (only when each entry it moves is live and numbered by its place).
- **When:** each frame call while the options menu is on top (`src/menu.cpp`). `locked` means
  armed: whether an entry was found shows only in the game. The client locks it while a seed
  that disallows assists has its save loaded, until the slot receives Assist Unlock. The helper
  locks it only while the loaded save is the look's own (`Game::seed_look_loaded`, "Intel
  Chips"), and puts it back, the padlock too, in another save or none, whatever the client last
  said: its server dropping leaves the ask as it was. Stand-in tests only, not run in game.
- **Found again each frame:** never taken on the pointer kept from the frame it was locked. Leaving
  Video, GENERAL (which holds the language) or the assist menu closes the options menu and builds
  another (`0x124D30`), whose widgets may take the freed ones' places: its VERSION 1.20, 12 glyphs
  with no callback like the locked ASSIST, in ASSIST's (`docs/re/memory.md`, "The assist menu").
  Each frame call the helper finds the entry again in the menu open now: a live widget with the
  generic handler, its callback cleared, its glyphs reading ASSIST and the padlock, in the panel it
  was locked in. It writes only into one that passes; any other is forgotten unwritten, and the new
  menu's ASSIST is locked. One the game relabeled stays shut until its menu is built again.
  (Stand-in tests, and run in game 2026-11-03, B1, `zexion_ap.dll` `7a72e4de…`, in English(HD): the
  owner, "seems to work", a weaker check than a repro since the bug showed only sometimes:
  `docs/re/memory.md`, "The assist menu". After GENERAL and the assist menu: stand-in tests only,
  not run in game.)
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
  alpha 1.0 back. (Run in game 2026-11-02 in English(HD), `zexion_ap.dll` `946b08a1…`: the entry in
  the HD font, gray, its padlock inside the box; the bugs that run found are fixed since
  ("Translations", "Run in game again"). Stand-in tests only, not run in game yet: the look above
  after Video or the map, the padlock on the text's line with no space before it, and the offline
  look held through menus ("AP saves without the client"). The lock in English(HD): working in game
  on the 1.0.0 RC, 2026-11-03, B1, `zexion_ap.dll` `fce9e73b…`, develop 76df074, by the owner's
  report, which did not single out the look's parts above, nor GENERAL or the assist menu; and
  2026-11-04, B1, `zexion_ap.dll` `a7469024…`, develop 345fea7, the owner's report: gray, with its
  padlock, unselectable, no stray pixel letters.)

Working in game: 2026-10-23, the lock and Assist Unlock; 2026-10-25, unlocked in another save
("The assist menu").

### New games

The plan, the seed's placement and the RANDOMIZER route are `src/newgame.cpp`'s, and the
ARCHIPELAGO choice `src/choice.cpp`'s, both shared with the Linux helper ("The Linux helper").
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
  when the two differ), and the seed's second start must be one of its rooms. Working in
  game: 2026-10-27 (`docs/re/memory.md`, "Starting a new game").
- **ARCHIPELAGO:** with `choice`, offers ARCHIPELAGO in place of RANDOMIZER on the
  game-mode panel shown after an empty slot is picked (`src/choice.cpp`), after checking every
  handler, size and pointer on the way with fault-safe reads. The panel's handler is then a
  wrapper that fits the border drawn around the button under the mouse to ARCHIPELAGO's eleven
  letters. In a translation the label's translated text is built again as Archipelago (the game
  sizes that border itself), and from SmallText 354 again when RANDOMIZER goes back, where the
  game's attach would build it now ("Translations"; working in game 2026-11-02 in English(HD),
  `zexion_ap.dll` `946b08a1…`: "Run in game again"). Choosing it sets the menu's
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
the proxy, `zexion_ap.dll` SHA-256 `9b609b92…`; "Starting a new game").

### Other players' items

- **Labels:** `ap_labels` names map cells (this repo's (x, y); the game shows X = y, Y = x) for
  the **stand-ins**, Scrap Energy (47) and Scrap Chunk (48), which the world puts at every
  location holding another player's item (48 for progression and traps, but not for another Zexion
  player's supplies), this world's own OS Install Disks and traps (48). HEX is the item and whom
  it is for, joined by a newline; or `@N` and whom it is for (another Zexion player's item N,
  named from its record); or `@46` alone, for no one (this player's own Scrap Metal: a checked
  item left in a room that waits for its pickup, "This player's own Scrap Metal" below); or
  `@N NAME` (item N under that name: this player's own OS Install Disk,
  or another Zexion player's disk, Area Map or Intel Chip). A trap looks important: the world
  gives it the progression stand-in (48), another game's labeled as its progression items are, and
  a Zexion trap, this player's own or another Zexion player's, as `@N/PICKUP` (`@N NAME/PICKUP`
  for a disk, under its name): the Zexion item the world drew for it (slot data `trap_looks`: one
  of the trap's owner's upgrades, keys and OS Install Disks that lies in the seed, so none they
  start with or the seed leaves out, none twice in a world until they run out, `world.trap_looks`;
  `aplabels.trap_label`), so it looks like that item and the map's cursor names it so ("Other
  Zexion players' items, as themselves"), but its pickup popup reads PICKUP, the trap's name, with
  no icon, then whom it is for (`Label::pickup`; "Traps"). With none of those in the seed to draw
  (every upgrade started or left out, in Custom mode or by `start_inventory_from_pool`, and no key
  or disk placed), any of the 16 upgrades, which the owner may then hold, or which may not be in
  the seed. A trap shown as a disk is never counted as one: the client counts disks from those
  received and the slot's own disk locations collected in the save (`client/discs.py`), which
  never hold a trap. A trap
  shown as one of this player's own keys has no key marker on the map, which a real one in this
  world has once flag 39 is set, by the scanner or from the start by a map revealed in full (flag
  291, Hints Revealed; `docs/re/memory.md`, "Keys, the gates and the key markers" and "The
  scanner"): a tell the owner accepted. Working in game 2026-11-11 (B1, `zexion_ap.dll` `8bd8e712…`
  from `develop` e500ab9, `lab/ap/live63`, slot 6, Hints Revealed): the trap's square showed a
  major's symbol (ADVANCED TECH at the cursor) and the real keys their markers and names. See
  `client/aplabels.py`.
- **Records:** every frame call (`src/apitem.cpp`) checks both records against the game's values
  and, while a save is loaded and no menu is open, restyles them: in-world frames empty in the
  game's art, no refill drop, and each named after the label for the cell of a live stand-in of
  its kind within 96 pixels of the player (chosen, not measured), else the player's cell's, else
  the nearest loaded one's (or AP ITEM). The game's pickup popup then shows the logo glyph, then
  PROGRESSION ITEM (or AP ITEM) over FOR ALICE (in English(HD), Progression Item or AP Item
  over for Alice, as the client writes them: "Translations", mixed case). The map cursor shows
  the label's first line only: what the item is, not whom it is for. A label is kept as its
  parts (`Label`, `src/text.h`: the glyph, the two lines, the `@N` item and its name), from which
  the record's name is laid out in capitals and the translated texts are made.
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
  (ItemNames 47 and 48), not the records: the label is written there too ("Translations"; working in
  game 2026-11-02 in English(HD), `zexion_ap.dll` `946b08a1…`, with the helper's HD logo, U+E03C, as
  the label's logo glyph). The pickup popup's lines are centered with leading spaces (where they fit
  the name's room), and the map cursor shows the first line without the logo, as the game's own
  names carry no icon: working in game 2026-11-03 (B1, `zexion_ap.dll` `12f090c6…`: the owner's
  screenshot of the pickup popup, not kept in `lab/`, the logo, then Progression Item over for
  AneziaDS2, both lines centered; the map cursor's AP Item, "The map").
- **Kept collected** (the client; the helper's one part is to hide what is left of such an
  item in a reversed-gravity room: "The marker", below): another player's item (not
  this world's own disks and traps, which follow the save) stays collected once the server has
  its location as checked, whoever checked it: found in any save of the seed, or before a death
  or a reload without saving, collected by its owner (`!collect`), or released (`!release`).
  While this seed's save is in play the client writes the cell's item state to collected, so
  the game makes no item there when it loads the room, then its map cell to `0x1B`, as a pickup
  leaves both (`client/session.py`, `mark`; `docs/re/memory.md`, "What decides whether an item
  appears"): compare-and-set writes, only for a location two polls in a row (in the same save
  slot, seconds apart at most) found still to mark. **Never in a room that waits for its
  item's pickup** (`ids.PICKUP_ROOMS`: Exodia X20 Y0, R'Talva X34 Y51 and Kresezoa X5 Y23): marked
  from afar, Exodia's was a soft-lock (its item gone, the boss fight its pickup starts never began,
  and the room could not be left), R'Talva's reward, whose pickup turns the warp gate on, still
  showed after the fight and gave nothing while the gate waited for its touch, and Kresezoa X5
  Y23's boss, which holds the cell's item, still dropped the collected item's picture, whose
  pickup gave nothing (the runs below).
  There the item stays, and once the server has it as
  checked the client labels it as the game's Scrap Metal, for no one (`@46`: `client/ticks.py`,
  `foreign_labels`; the labels are sent again when one changes), so what is left looks like
  what it is, a pickup the room needs, and gives its Scrap Metal as any stand-in does (the
  owner's decision, 2026-11-15); with the helper it is picked up as the game's own Scrap
  Metal ("This player's own Scrap Metal" below). Kresezoa X5 Y23 was listed on 2026-11-16: left
  unmarked, the boss's drop shows as scrap and is an actual Scrap Metal (B1, a second run of the
  same game session, `zexion_ap.dll` `ca19c141…`, the client restarted with the room listed, the
  owner's words "it was an actual scrap this time with the pickups", no time recorded,
  `lab/ingame-event-rooms-checks.md`, "Kresezoa X5 Y23 re-check"; `docs/re/memory.md`,
  "What decides whether an item appears", "The type-28 events"). That run marked the 14 squares
  of the seed `lab/event-rooms/live66` from afar: Fijedar X5 Y39, Guardia X25 Y11, Vallahar X9
  Y14 and Machinus X27 Y24 played with no soft-lock; **not run:** nine (Guardia X41 Y10 and X22
  Y9, Pulsava X51 Y12 and X46 Y13, Phobyss X18 Y41, Kresezoa X22 Y26, Crysmoon X49 Y32, X52 Y38
  and X53 Y20), whether Fijedar X5 Y39's enemy drops anything, and the other squares where a
  boss holds the item (the scan for them missed this one). The three listed are the ones found
  so far: 49 other location squares have an after-pickup event in the game's per-cell table
  (`docs/re/memory.md`, "What decides whether an item appears") and are marked as any other;
  an arena, one more event square and four more of the type-28 squares (Fijedar X5 Y39, Guardia X25
  Y11, Vallahar X9 Y14, Machinus X27 Y24, above) were played marked with no harm, the rest not. **An
  item the game has made already** (a reload at a save point beside it, its owner collecting while
  the player stands by it) goes too: the item's own update ends its object once its cell's state
  reads collected, with the writes a pickup ends it with (`docs/re/memory.md`, "What decides whether
  an item appears"), so for a cell with an item object in the loaded rooms the client writes the
  state, waits a moment, and writes the map cell once no such object is left, never under one still
  there. After a reload beside it the item shows until the second poll and is gone (by the code: not
  run in game). The state written under a live item object also lets an object of the cell run the
  cell's after-pickup event then, with the player in the room (`docs/re/memory.md`, "A cell's event
  after its item is collected"): the one cell run this way in game on 2026-11-14, Kresezoa X22 Y13,
  has no such event; an event square's, Kresezoa X23 Y21's (event 879), was run so on 2026-11-15:
  the item went, and nothing else was reported; an arena's is not run. And a cell once seen with an
  item object is marked without the loaded and near tests for the rest of that load
  (`mark_collected`, `ended`), which is what lets its map cell follow; a room that shows its item as
  an object of another kind beside the item-pool object (type 715: Guardia X22 Y7) was not run
  marked. **Left out** is a cell whose item may be in the loaded rooms as something else: one an
  object holds or has dropped, and, with no item object of its own, one the game has loaded (its
  loaded byte, in the array right after the per-cell state) or within two map squares of the
  player's (a margin on top of the loaded cells, which are the squares next to the one the game
  tracks): an object of another kind may be the item there, as in a reversed-gravity room, which a
  written state does not end (marked from afar, such a room still shows the item, as a picture that
  gives nothing: the third run below; with the helper that picture is hidden, "The marker" below).
  That one stays to pick up as usual, and is marked once its room is unloaded and the player is
  three squares away. Right before the writes the client checks the save again (in play, a
  randomizer save, the slot's stamp as at the look) and reads the states, the loaded cells, the item
  objects and the player's cell afresh; the save, the loaded cells and the item objects once more
  before the map cell. No write starts while a transition counts down (the game's transition timer
  not 0): a TELEPORT TO reads as play, under the same stamp, for the second before its load, which
  takes back what was written since its entry was chosen. After the last write the client looks once
  more, and reports as marked only what still reads so: nothing if the game does not read as in play
  with no transition counting, and not a cell whose item state reads there again, which the next
  polls finish. That look sees less than happened both ways: a pause or a death right after the
  writes leaves them in place unreported, and a load that begins after it takes back marks it
  reported; the next polls read the bytes again either way. `cell_state` and `pickup_counter`, the
  two targets the marking alone needs, may be missing: on a game build where the patterns of either
  no longer match, the client still attaches and only the marking stops, said once
  (`patterns.CLIENT_ONLY_TARGETS`). **A marked cell gives nothing** (the owner's decision,
  2026-11-15: no Scrap Metal for marks). The one Scrap Metal a stand-in's pickup gives is the game's
  own, at the pickup, and the client's sum counts a stand-in's only for the cells picked up (so one
  a death took back is given again, and a Scrap Metal received is still owed on top). It tells them
  by the game's ITEMS count, which a pickup raises and a mark leaves alone: the collected cells that
  count does not account for are the marked ones (`client/reconcile.py`, `marked_count`;
  `docs/re/memory.md`, "The pickup counter and the collected cells": the count equals the collected
  cells in every lab save copy looked at but the marked one, short by its marks). Nothing is ever
  taken away. A stand-in picked up and then lost to a death or a reload is marked once the save is
  back, and gives none then either: its Scrap Metal went with the load; the same for one picked up
  in another save of the seed, marked in this one. A marked square reads explored on the map. A mark
  stays in the save once the game saves: such a save played against another room of the same seed
  sends its marked locations as checks. With no client connected nothing is marked, and a failing
  read or write of the marking is said once while items and checks go on. Stand-in tests
  (`tests/test_session.py`). Working in game, 2026-11-14 (B1, slot 6, the seed `lab/ap/live64` with
  a second Zexion player; the client of PR #144's branch at bbaf3c5, which holds this marking
  unchanged, with the helper `zexion_ap.dll` `5f19e4d4…`; the owner's reports and the client's log,
  `lab/ingame-collected-checks.md`): another player's item picked up, then RELOAD FROM SAVE without
  saving, was gone from its room, its square collected on the map (marked 3 s after the check was
  sent; marked twice more in the next two minutes, a load before each being the likely cause, not
  confirmed); its owner's `!collect` while the player stood beside one of the items marked 81 in one
  pass 4 s later, left the two within two squares until the player had walked away (then marked
  them, and they were gone on coming back), and the rest were gone from rooms not yet visited and
  found on the map; all 84 of the other player's items were marked by the end, and none was marked
  again while the owner walked and used the map; the marks were in the save file once the game saved
  (a lab copy: item state 1 and `0x1B`). By that day's rule each marked cell gave one Scrap Metal:
  82 after the collect, which that save keeps. Not run in game: a death, another save of the seed,
  `!release`, and a cell whose item an object holds while its room is loaded (the client connecting
  with locations already checked is the third run below); nor, in that run, what was added after it
  (c77879d and after): the loaded-cell test, the checks right before the writes, the attach without
  `cell_state` or `pickup_counter`, a marked cell giving no Scrap Metal, the transition timer's test
  and the look after the writes (a marked cell giving none and the loaded bytes read live are in the
  run after the review rounds, below; the rest is stand-in tested only). A second run (2026-11-14,
  B1, slot 5, the seed `lab/ap/live65`, the client and helper of PR #144's branch at 79bee7a; the
  owner's reports and read-only looks, same record): with the player beside another player's item at
  Kresezoa X22 Y13, its owner's `!collect` marked all 79 of that player's items in one pass a second
  later, the item object beside the player gone from the pool and its square collected (the owner:
  it disappeared, and the map marked every one of that player's squares); the rooms looked at held
  nothing where an item was. That build still marked squares with an after-pickup event: Exodia X20
  Y0 was the soft-lock above, and at Exodia X22 Y1, a reversed-gravity room, an Energy Tank still
  showed over a marked square. A third run (2026-11-15, B1, slot 5, a new game of the same seed with
  every one of that player's items checked on the server already; the client of PR #144's branch at
  a17904b, the same helper; the owner's reports and read-only looks, same record), with Exodia X20
  Y0 left unmarked and labeled Scrap Metal: the client marked the other 78 a second after the save
  loaded; at Exodia X20 Y0 the item was scrap, its pickup started the boss fight, and the fight
  could be won and the way on taken. What marking leaves behind, each harmless but the boss
  reward's, which is why that room is listed since: in a reversed-gravity room (Exodia X22 Y1) the
  item still shows over its marked square until touched, and touching it gives nothing (no popup or
  sound; the inventory and the pickup counter read the same after), while a marked item drawn the
  usual way in that area (Exodia X27 Y0) was gone; a boss's reward (R'Talva X34 Y51) still shows
  after the fight, gives nothing when touched, and that touch is what turns the warp gate on and
  brings the area's music back, so the room does wait for it; in the arena (Guardia X51 Y10 and Y11
  marked) the enemies holding the two marked items dropped nothing, those holding this world's own
  two dropped them, and the room was left as usual; Kresezoa X23 Y21, an event square of Exodia X20
  Y0's kind, marked, showed nothing unusual. Not run in game: a death, another save of the seed,
  `!release`, and a reload beside an item.
- **This player's own Scrap Metal** (`@46` alone, for no one; `Label::scrap`, `src/text.h`):
  the label the client gives another player's checked item in a room that waits for its
  pickup (above). The square still holds the stand-in, so the helper makes its pickup the
  game's Scrap Metal's. Its popup reads the name alone, with no icon, as the game's does (the
  record's own has none; in a translation too, by the code: not run in one). While a
  stand-in's record is named after such
  a cell it names Scrap Metal's pickup event, 3548, where it otherwise names none, so the
  game's own pickup spawns Scrap Metal's drops (`ApItems::watch`): only while that event, its
  record and its handler are as traced on B1 (`Game::item_event_ready`, the check a received
  scrap's drop goes through); back to none when the record is named after another cell, and
  to the game's own event with the rest. Its map square is a minor's: item 48's there is not
  raised, and one raised before the label came is lowered to what it was raised from
  (`minor_value`, `src/mapmarks.cpp`). Two values are left as they are: an obscured major
  under Obscure Class, which the game writes for a minor there too; and a shown major under
  Obscure Minors alone in an unfinished game, the raise there not being one to one. While
  the item is on screen the game's patched write still gives a square of item 48 a major's
  class each frame ("The map"), so the square is left as it is while an item object of its
  cell is live (`watch_map_marks`, the item pool read at each pass; with no read of the pool
  every such square is left): lowered between those writes, it flipped back and forth every
  pass, and the client, whose poll waits for two equal reads of the map, held its checks and
  grants while the player stood there (the third review's finding, 2026-11-15; stand-in
  tests, not run in game). So with the item in its loaded room and seen, the square is a
  major's, and a save made then holds that; it is lowered once no item object of the cell is
  left (the room left), and again after a load while the label holds. Under Obscure Minors
  alone it stays a shown major until the item is picked up.
  Another Zexion player's Scrap Metal (`@46` and whom it is for) stays their item: the logo,
  no event. A stand-in of another cell taken while its record is named after the scrap cell
  (two of one record within reach, or one taken in the frame call after the player's cell
  named it) gets Scrap Metal's name and drops, nothing else: its inventory byte, its map
  square and its check are the pickup's own. A record left naming 3548 by a helper that went
  without a frame call is restyled again by this helper, but is not known to one built
  before this change, which leaves it so until the game starts again. A fourth run (2026-11-15, B1, slot 6, a new game of
  `lab/ap/live65`, the client with R'Talva X34 Y51 unmarked, the helper of PR #145's 399f57e,
  before this change; the owner's report, `lab/ingame-collected-checks.md`, "Check J"):
  R'Talva X34 Y51's reward showed as scrap, its popup read Scrap Metal with the AP symbol, nothing
  dropped (whether the gate turned on at that pickup was not said), and the map showed the square as
  a major's (ADVANCED TECH); hence this. Stand-in tests (`tests/apitem_tests.cpp`,
  `test_own_scrap_metal_names_scrap_metals_event`; `tests/game_tests.cpp`,
  `test_own_scrap_metal_is_picked_up_as_the_games`; `tests/mapmarks_tests.cpp`,
  `test_own_scrap_metals_square_is_a_minors`, and `tests/game_tests.cpp`,
  `test_a_scrap_square_is_left_while_its_item_is_live` and
  `test_scrap_squares_are_held_while_the_item_pool_cannot_be_read`; each failing with `Label::scrap`
  false). The pickup reads the record's event when the item is taken, as it reads its name
  (`docs/re/memory.md`, "What a scrap's pickup runs"). Working in game, 2026-11-15 (B1,
  `zexion_ap.dll` `8c4202ad…`, PR #146's fe4fe1b, slot 6 of `lab/ap/live65`, the owner's report,
  `lab/ingame-collected-checks.md`, "Check K"): the scrap at R'Talva X34 Y51 and the one at Exodia
  X20 Y0 were each picked up as the game's regular Scrap Metal, with "the pickups from it" (the
  owner's words). The client was connected for the first minute after the save's load and then
  stopped (its session ended), the helper running on alone; which of the two each pickup ran under
  is not known. The map square, the same day and helper (a new game of that seed in slot 5, the
  client connected, the presets' Obscure Majors and Minors; "Check L"): R'Talva X34 Y51's square, of
  item 48 (another player's trap), read `0x13`, an obscured minor's, and the cursor named it UNKNOWN
  ITEM where it had said ADVANCED TECH before this change (the owner's report, and the grid read
  live); Exodia X20 Y0's, of item 47, the same. Run again 2026-11-15 with the helper of c7186e9
  (`zexion_ap.dll` `fdd42c43…`; slot 5, a new game against a server with nothing checked, then the
  items' owner's collect; the owner's reports and read-only looks, "Check O"): Exodia X20 Y0's scrap
  gave its popup "with the pickups", the fight started, was won and the room left normally; R'Talva
  X34 Y51's "same thing as before", and its pickup turned the warp gate on and brought the area's
  music back. R'Talva's square read `0x15` before its item was checked and `0x13` once it was
  labeled; the owner saw both rooms as minor items on the map. With each item on screen its square
  held one value in every sample of a read-only watch (Exodia's `0x0D` over 6 s, R'Talva's `0x1F`
  over 8 s, some 400,000 and 590,000 samples): no flip. Not run in game: the square under the other
  Obscure settings; and a scrap-labeled square of item 48 that the game's on-screen write does raise
  (R'Talva's was not raised with its reward on screen, and Exodia X20 Y0 held item 47 in this seed),
  so the hold under a live item object is stand-in tested only.
- **Run in game after the review rounds** (2026-11-15, B1, slot 5, a new game of the seed
  `lab/ap/live65` against a server with none of the other player's items checked; the client
  and helper of PR #146's branch at c7186e9, which holds this marking; the owner's reports,
  read-only looks and a read-only trace, `lab/ingame-collected-checks.md`, "Check M" to
  "Check P"): another player's item picked up gave its one Scrap Metal; one picked up and
  then lost to a RELOAD FROM SAVE was marked 0.76 s after the load, gone from its room and
  collected on the map, and gave none; a Scrap Metal received after that was given on top;
  its owner's `!collect`, with the player standing in an event square's room (Kresezoa X23
  Y21), marked 74 in one pass 1.3 s later, the item in that room among them (it
  disappeared; nothing else was reported there), and gave none. The Scrap Metal count read
  1, 1, 2, 4 and 4 at those steps, and the game's pickup count equaled the collected cells
  less the marks at every read. The loaded bytes, read live for the first time, were the
  3 x 3 cells around the player's. Not run in game: a death, another save of the seed,
  `!release`, a reload beside an item, an arena's item marked with the player in its room,
  a type-715 room marked, and the marking's wait through a TELEPORT TO with a mark pending
  (three teleports were made, none with one).
- **Limits:** two stand-ins of one kind for different locations within reach share the nearer
  one's name (not seen), and its pickup event; and Scrap Energy in another seed's save would
  look the same, so the
  client sends labels only while this seed's save is loaded.

Working in game: 2026-10-23 ("Other players' items"); 2026-10-26, a three-game seed: no owner
on the map cursor, other Zexion players' supplies minor ("Map markers").

### Other Zexion players' items, as themselves

- **What:** for a cell labeled `@N`, every frame call (`src/looks.cpp`) dresses the stand-in
  objects there, in the game's pool of live item objects, as item N: N's palette, and the
  frame-list base moved so the frame the game computes lands on N's. The record, and so the
  pickup, stay the stand-in's.
- **Scrap Energy and Scrap Chunk:** for another Zexion player's (`@47`, `@48`), N's record is
  a stand-in's, restyled to the logo, so their look is taken as the game loads it (frames 134
  and 135 or 142 and 143, palette 8: `kStandInOwnFrames`, `src/apitem.h`), as their names are,
  not the logo in palette 8 (`docs/re/memory.md`, "Other players' items"). Working in game for
  Scrap Chunk (`@48`), 2026-11-09 (B1, `zexion_ap.dll` `5f761061…`, develop 88bebfb, the seed
  `lab/ap/live57`, slot 6, the owner's report): another Zexion player's at Kresezoa 19,11, a sky
  room, showed as a Scrap Chunk (at develop 5597753 it had shown the logo in the room's
  colors), and its pickup showed its popup for that player (the text not quoted); a
  progression stand-in at Guardia 22,7 still drew the logo in its own colors. Not run in game:
  Scrap Energy (`@47`), and the seed's other such cell (R'Talva 30,45, out of logic then).
- **Safety:** it writes only stand-ins with the template's base and palette 8 or 26 (the
  helper's own colors, "Other players' items"), or ones it dressed (the same spawn); and, for
  R'Talva X34 Y51's marker (below), only its shown byte (`+0x120`), cleared and set again, only
  while it is the spawn it hid; the same byte, the same way, for a reversed-gravity room's
  item object over a collected square (below).
- **Other pool:** some rooms show an item through the general object pool; a scan of that pool
  every 31st frame call finds any live object drawing a logo frame and dresses it the same way
  (R'Talva X34 Y51's marker, below, is hidden instead, and so is a reversed-gravity room's
  item object over a collected square, whatever frame it draws).
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
  through their own palette. **A reversed-gravity room's item over a collected square** is
  hidden the same way, in the same pass: the general object that draws the item there (type
  2026, known by its first script state, 13867, at `+0x32E`) is not ended when the client
  marks its square collected, so it is left showing a picture that gives nothing when touched
  (in game, 2026-11-15, Exodia X22 Y1; the owner's request to hide it). While the stand-ins
  are restyled and the object's map cell reads collected (`0x1B`), every frame call clears
  its shown byte, on whatever frame it shows (one made while the stand-ins were not restyled,
  a load from a menu, shows the game's scrap, not a logo) and unless it holds its cell's item
  (its `+0x32C`: then it is the pickup, whatever the map says); it is shown again when the
  cell no longer reads collected (a reload from before the mark), when a client goes and on
  an unload. Under a menu, where the records and every other look are put back for the
  menu's own screens, it stays hidden while it is still that object over a collected cell
  (`ItemLooks::restore`, `keep_collected`; the third review's finding, 2026-11-15: it would
  have shown under the pause or map screen). An unload is refused outright while a menu is
  open over a save; the hidden object counts among what a frame must put back first only
  for an unload no frame comes for (`ap_items_restyled`). A pickup of the game's own ends
  the object, so nothing is hidden then. A boss's reward over a marked square is left
  showing, as its touch is what turns the warp gate on: R'Talva X34 Y51's is never marked by
  the client since (`ids.PICKUP_ROOMS`), so that is left for a save marked before then. The
  hidden object can still be touched, which does what it did shown: it ends, giving nothing.
  A new object of the kind (the room made again by a RELOAD FROM SAVE beside it) shows until
  the next scan of the general pool, up to 30 frame calls.
  Stand-in test (`tests/game_tests.cpp`,
  `test_a_collected_item_in_a_reversed_gravity_room_is_hidden`, failing with the hiding taken
  out, and its part under a menu with that kept out). Working in game, 2026-11-15 (B1, `zexion_ap.dll` `5ef5eeb1…`, the build of PR #145's
  branch at 399f57e, slot 5, a new game of the seed `lab/ap/live65` with the other player's
  items all checked; the owner's report, `lab/ingame-collected-checks.md`, "Check I"): at
  Exodia X22 Y1, over its marked square, the owner found nothing ("no item is there"), and
  confirmed having done the RELOAD FROM SAVE beside it, without describing what showed after
  that reload. Again on 2026-11-15, with the helper of PR #146's branch at c7186e9 (the same
  slot and seed; the owner's report and a read-only trace of the general pool's type-2026
  objects, four samples a second, "Check P"): "nothing showed up through multiple reloads";
  each new object of the kind read shown in one sample and hidden in the next; under the
  pause menu, for 13 s, the hidden one's shown byte read 0 throughout; after the owner
  walked through it, after a RELOAD FROM SAVE made in the room and after a quit to the
  title from there, the object read freed and not shown (nothing written into it). The
  marker can show
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
  Area Maps Intel Chips) and the loaded save is the look's own (`Game::seed_look_loaded`):
  - with a client, the save of the seed whose file it has put (`offline_put seed`, the helper
    keeps the last one a client put as the client's seed until it goes away); until it has put it,
    the save of the seed the helper holds labels for (taken from whichever randomizer save is
    loaded when `ap_labels` lands, so before the file is put the look can follow another seed's
    save). With neither, the game's own: the client asks for the look from the toggle ticks, the
    seed's file follows once the server has listed the items and, for a seed with other players'
    items, what each location holds (`offline_seed`: the client looks again each poll until it
    has the text, then every 5 s; also every 5 s when the config cannot be built, and for a put
    the helper refused), and labels sent with no randomizer save loaded are for no seed. A seed
    file put after its client went away (the game was in the background) is nobody's seed. In
    that window an Area Map picked up in the seed's own save reveals as the game's does;
  - with no client (or a client that said `other_save`), the save of the seed the offline look
    has dressed ("AP saves without the client").

  It does the same for the ASSIST lock and the disk count ("The assist menu", "The OS Install
  Disks goal"): the helper does not wait for the client to say the save left, so with the server
  down another save, whether a normal one, a plain randomizer one or another known seed's, has
  the game's own Area Map, ASSIST and scanner line, and they come back when the seed's save is
  loaded. Stand-in tests only, not run in game.
- **Put back:** `intel off`, with the rest, and each frame call in a save that is not the seed's
  (above); an unload refuses while it cannot.
- **Received:** a client's `popup 49` in an `apply` with `intel` (sent for a seed whose Area
  Maps are Intel Chips) reads INTEL CHIP (Intel Chip in a translation), with `intel on` or not
  when it shows, as after a load before the client sends it again ("Popups"). Working in game
  with `intel on`, 2026-11-08 (B1, `zexion_ap.dll` `2c29d10e…`, PR #118's head `c0be163`, the
  owner's report): named as the chip over its sender, then its hint, in the default English and
  in English(HD). Not run in game: one shown with `intel off`, and other translations.
- **In a translation:** the pickup popup reads ItemNames 49: Intel Chip (in capitals outside
  English(HD): "Translations", mixed case) is written there while the record is INTEL CHIP
  ("Translations"; working in game in English(HD) 2026-11-04, B1, `zexion_ap.dll` `a7469024…`,
  develop 345fea7: an Intel Chip picked up, named Intel Chip, then its hint; a pickup in the
  other translations not run in game).

Working in game: 2026-10-25 ("Intel Chips").

### The OS Install Disks goal

While the client has sent `discs` (the count, the number needed, the scanner's line in hex, and
TOLD: 1 once flag 39 says the player has been told the number, else 0 and "OF ?"; left out, 1),
the helper:

- writes the scanner's message (Text entry 12) as the client's line (`src/scan.cpp`), in
  capitals (the client sends it in mixed case for English(HD): "Translations"), only over the
  game's text or its own;
- paints the disk glyph into font glyph `0xFB`; it is also kept, the goal on or not, while a
  received disk's popup waits or shows ("Popups"; working in game on received disks' popups,
  2026-11-08, `zexion_ap.dll` `2c29d10e…`, held ones at the next load included; one shown with
  the goal off not singled out);
- with a disk or more, wraps the pause screen's EQUIPMENT callback (`src/equip.cpp`): after the
  game builds the screen, it moves the Energy Tank left and adds a widget for the disk (the
  count as its label), showing OS INSTALL DISK, its icon and description when selected; the game
  frees it with the menu;
- makes the pause screen's KEYS row the disk row (`src/pause.cpp`): the `jne` that builds the row
  only once flag 39 is set made a `jmp`, then a disc and the count, the key slots and icons hidden.
  As soon as a save of a known disk seed (one the client left a file for) is loaded, in play or not,
  the row shows the count from that file until the client's arrives, so a pause screen opened at
  once shows the disks, not KEYS. A count that changes while one pause screen stays open is
  written into its row again, the characters alone (`PauseDiskRow::turn`): the client's count
  comes about a second after a RELOAD FROM SAVE, and a screen opened before it kept the count
  from before the reload for as long as it stayed open (the owner's report, 2026-11-14: "1 of 4"
  after a reload that put the disk back; stand-in tests, `tests/pause_tests.cpp`, failing with
  the rewrite taken out; in game since, the same day, `7859b9e7…` built from 1b04f24: a pause
  screen opened 0.6 s after such a reload read 0, whether at once or after the rewrite not
  told apart). OPTION MENU opens over the pause screen, which stays open under
  it, and the game calls the pause menu's way back (`options_back_fn`, which OPTION MENU's opener
  puts at the menu's `+0x68`) as it closes, before it draws that frame; while the row is on, that
  pointer is the helper's wrapper, which calls the game's and then turns the row in the same call
  (in a translation the game builds KEYS's translated text there again, so turning it in that call
  leaves no frame of Keys: working in game 2026-11-03, B1, `zexion_ap.dll` `fce9e73b…`, English(HD),
  `docs/re/memory.md`, "The pause screen's KEYS row"; `options_back_fn` matched once live,
  `tools/check_patterns.py --live`). The game's way back sets the pointer to its own again; the
  helper puts it back when the row turns off and before an unload, while its menu is open and still
  holds the wrapper;
- while that row is on, paints its disc, a 24 x 24 white image of the helper's own, at (128,
  1008) in the sprite texture (blank there in the game's art, checked as the glyphs are), and
  points the row's icon sprite at one frame-list entry the helper owns: a copy of the small Data
  Disc's (frame 829) with the image's place, drawn 24 x 24 at (160, 184) (`kIconY`): 4 above
  the middle of the game's four key icons, (160, 188) (`docs/re/memory.md`, "The pause screen's
  KEYS row"). What 184 itself was read from is not recorded (the code called it read live on B1,
  2026-10-24); the disc there looked right in game (2026-10-26). The entry is allocated once and
  kept for good, never freed, so a pause screen still holding it after an unload points at live
  memory; whenever the image is not painted (the row off, before an unload, a texture it could
  not paint into) the entry shows frame 829's place instead, the game's disc enlarged until the
  screen is closed. A pause screen opened before the image is painted shows frame 829 as is;
- on `mission_prompt COLUMN ROW` (sent after each save at the Landing Pod with enough disks, in a
  save past its ending too, with home: the Landing Pod's save point in tiles, 191 329, whichever
  room the seed starts in; the column below 54 × 14 and the row below 54 × 20, or `error bad
  mission_prompt request`), once idle in play with no popup of its own to show and the game's last
  message (GAME SAVED, 180 frames) down to its last 150 frames, half a second in
  (`kFadeMessageLeft`, chosen in game, 2026-10-27: `docs/re/memory.md`, "The mission prompt"; a
  popup of the helper's own is never cut short: while the game's last message is still the helper's,
  the prompt waits for its end, read from that message's frames left, so in game frames), opens the
  game's YES/NO prompt as RETURN HOME WITH THE DISKS? (`src/mission.cpp`) the way the game opens it
  as SKIP ESCAPE AND FINAL BATTLE?: it runs the game's fade to black (area event 3667, checked byte
  for byte, through the game's handler for its type) and sets its opener as the transition 60 frames
  away (the pause is blocked while that counts; popups and received deaths wait). Its YES closes the
  prompt through the game's NO and then starts the ending once no popup of the helper's shows (one
  up is never cut short; a message of the game's, GAME SAVED, does not hold it: the ending routine
  ends every message itself as it starts, `docs/re/memory.md`, "The mission prompt"); NO fades the
  screen back in, as the game's NO does there. NO's close puts back, inside its call, the fade the
  prompt's builder saved (the level −1, then toward 4 a step every 6 frames: `docs/re/memory.md`,
  "The mission prompt"), so right after it YES turns that fade off with the screen still black (the
  fade's step, countdown and target set as event 3667's fade ends): no fade back into play between
  YES and the ending. Only a fade that reads as NO's close leaves it is turned off (`mission_status`
  then says `yes: ending when idle, the screen kept black`); anything else fades back in as before.
  While it is black, received deaths wait (`refused RETURN HOME WITH THE DISKS? ending`). Popups
  waiting at YES or arriving after it, the screen kept black or not, wait too and do not hold the
  ending (which starts from black when kept so): they show at the save's next load, as those arriving during the ending do (their items are given
  already). The screen is brought back in (as NO's close would have) if the ending has not started
  within 240 game frames in play (`kDarkFrames`: a safety that errs long, as after YES the ending
  waits only for a menu to close, a popup of the helper's already up and the gap after it; counted from the
  game's frame clock, `frame_clock`, which stands still while a menu has the game paused, so frames
  with a menu open, the pause, which saves the fade and puts it back, don't count), and when the
  prompt is canceled (the save left, `discs` off, the client gone); asked for with a menu open, the
  fade back waits until it has closed and put its saved black back. Another fade since (a death's)
  is left alone, and an unload is refused while the screen is kept black (below, "Unloading");
  leaving the save (the title, which owns the fade) drops it. `mission_black` says how the last
  black ended (`kept black` while it lasts; `ended: the ending, faded in from black`, `ended:
  kDarkFrames in play with no ending, faded back in`, `ended: canceled…`, `ended: the save left`, or
  another `ended: …` note: another fade, the ending not faded in or with the fade no longer the
  helper's), for a live check. Two things could end the black before the ending, by disassembly: the
  limit above (when it counted the helper's frame calls, which a fast monitor runs out first, a
  run's numbers fit that as the cause: `docs/re/memory.md`, "The mission prompt"), and the ending
  routine itself, which sets the level to 4 as it warps the player to the ending's first scene, in a
  frame the game draws before the helper's frame call (not seen in game: the wrapper hides it). For
  that one the helper starts the ending through a wrapper of its own, set as the transition with
  timer 1 in the routine's place: the game runs it, it runs the game's routine, and, while the black
  is still the helper's, it sets the level back to black at once (event 3667 aimed from −1 to 4, a
  step every 8 frames, `kEndingFadeStep`) before that frame is drawn: the ending fades in from
  black, at the normal level 40 frames in. The black ends there. A wrapper still the transition
  after the routine (not as traced) becomes the idle routine; one an unload finds not yet run
  becomes the game's routine (the ending then comes without the fade in). Anything else in the
  wrapper's place before it ran (a save at the save point, which sets the pause opener back and
  leaves the fade alone; a door, timer 70; a death): the ending is asked for again, the black kept
  while it is still the helper's (with its 240 game frames), else the other transition's fade left
  alone. From the frame the helper sets the ending's wrapper until another save is loaded
  (`save_loaded` changes, as at MISSION COMPLETE) or another transition takes its place as above,
  `mission_status` says `ending started`, a `mission_prompt` is ignored (the ending's cutscenes read
  as play and its own save as a save at the pod), `apply`, `shield` and `die` are refused (`RETURN
  HOME WITH THE DISKS? ending`: the client gives them in the save's next load), and popups already
  queued wait for the seed's next load. Stand-in tests (`native/tests/mission_tests.cpp`,
  `game_tests.cpp`). Working in game: black from YES until the ending, which fades in from it,
  2026-11-02 (B1, `zexion_ap.dll` `d179d32d…` on a first ending and a replay, then `28ab9954…` with
  the ending starting under GAME SAVED, on a replay), and on a first ending on the 1.0.0 RC,
  2026-11-03 (`fce9e73b…`, develop 76df074, English(HD), the owner's report); the ending played
  again on a save past it, with nothing new sent and the Feats note shown, the goal sent on a save
  before it, items received during either held and their popups shown on the next load, a death
  received during the first ending dropped, and the pause opened in the black after YES with EXIT
  GAME going to the title with no black left and `/reload_mod` not refused after that, 2026-11-02
  (`946b08a1…`) (`docs/re/memory.md`, "The mission prompt"); two received items' popups waiting at
  YES, the ending straight after it and both popups at the next load, 2026-11-04 (`a7469024…`,
  develop 345fea7, English(HD)). Not run in game: a door or a death right after YES, and the prompt
  asked for again. If event 3667 or its handler is not as traced, the prompt opens at once with no
  fade, as before. A request not yet opened lapses (`lapsed: the player left the landing pod`) once
  the player is in play outside home's cell (the request's row / 20 and column / 14 against the
  player state's cell, as the client tests a save at the pod: `docs/re/memory.md`, "The mission
  prompt"; a cell off the map, as the raw cell reads for a moment, counts as not known), as when
  popups held it while the player walked on; the client offers it again after the next save there.
  Until 1.0.0 home was the seed's start cell, read from the loaded section's start record; the
  in-game runs from 2026-10-27 and earlier all started at the Landing Pod, where the two are the
  same. Home given by the request, from a seed started elsewhere: working in game 2026-11-02 (an
  OHKO seed started at Open Cave, B1, `zexion_ap.dll` `1c286c57…`: "The mission prompt"). Once its
  opener is set, it opens where the player is when its 60 frames are up. An opener the helper takes
  back before it runs (the save left, `discs` off, an unload) turns the fade back to the normal
  level; so does one the game runs with the player not in play (a death during the fade: asked
  again), but only while the fade is still event 3667's, as below, not a fade the death set. One
  another transition took the place of (a save at the save point again, which sets the pause opener
  back and brings no fade; a door; a death) is asked again once idle (and lapses if the player is
  then outside home's cell), and its fade is turned back only while the game's fade (the `fade` and
  `fade_level` targets) is still event 3667's: target −1, a step every 8 frames from level 4, or
  done at −1, and no further back than the helper last saw it with its opener in place, nor the
  transition timer higher (a save leaves it counting). Anything else, another transition's fade even
  with 3667's own values (it starts again from level 4, its timer at 70), is left alone. The same
  when `discs` turns off after such a save. Working in game, 2026-10-27 (0.30.0, the owner's
  reports: "The mission prompt"): the fade turned back after saves at the pod during it; a request
  lapsing once the player left the pod's cell while twelve received items' popups held it, and one
  left during GAME SAVED alone, each offered again after the next save there; the ending after YES
  waiting for five received items' popups, then starting (since changed: popups waiting at YES now
  wait for the save's next load, working in game 2026-11-04, above). Stand-in tests only: the fade
  turned back when the helper takes its opener back, another transition's fade left alone, a death's
  own fade left alone, and `discs` turned off after such a save.

For every seed it has a file for, with a client or none, it shows each save's own count on the
file select, COPY FILE and DELETE FILE in place of the keys (`src/files.cpp`, wrapping each
entry's handler): DISK over KEYS, and the count. The count's two digits sit 5 px below their row
(`kCountDrop`), centered on the items bracket beside them (`0xD9` over `0xDA`, rows 3 and 4: its
middle 7.5 px below row 3's top, a digit's 3, so 4.5; 5 chosen in game). Nothing shows for a save
with no disk. The count is the save's own: what the seed's file holds (the disks the slot starts
with and those received, which the client keeps up to date while connected) plus each of this
player's own disks the map of the record the entry shows reads collected (`FileEntries`,
`DiskCount`, read from the entry's own block each frame: on the Linux build that block is the
slot's checkpoint record while it is in use, which can hold a disk the full save does not,
`docs/re/linux.md`, "Mapped by disassembly: the file select's AP logo and disk counts"; whether
the Windows build's entry does the same is not recorded, `docs/re/memory.md`, "The file select",
and the entry's block is read either way). This read is stand-in tested
(`native/tests/files_tests.cpp`) and ran in the game on the Linux build, 2026-11-20
(`docs/re/linux.md`, "Seen in the running game, 2026-11-20": a checkpoint's disk shown that the
full save did not hold); on Windows not run since: the in-game runs there (2026-10-24 to
2026-11-04) were of the earlier read, the slot array's full save.

**The count follows the save** (the owner's decision, 2026-11-14): the disks the slot starts
with, those received from other players, and this world's own disk locations collected in the
loaded save (`client/discs.py`, `owned`). So it drops after a death or a reload from before an
own disk's pickup, the disk lies in its room again, and picking it up again counts it again and
shows its count again (`client/ticks.py`, `followups_tick`: a count notice for each own disk
picked up while the load is watched; one the save already held at the load's first two looks,
picked up with no client or before a reconnect, shows none from the client, nor one another
slot of the seed holds when it is loaded). With no client attached the helper shows that count
itself at the pickup, with the disk's pickup sound (`Game::watch_own_disks`, the owner's
request, 2026-11-14): in a known
disk seed's save, an own disk's cell turning collected after two frames in play, as for an own
trap, queues "OS Install Disk 3 of 15" ("of 15" once the scanner has told), the count from the
seed's file and the save's map (`offline_disk_count`), behind the game's pickup popup; a load
is no pickup, and a count not shown yet is withdrawn when a load puts a disk back or the save
is left. One queued stays when a client attaches, and the cells are watched with a client
too, so a disk picked up with one is no pickup once it goes; a client that lost the helper
takes the disks the save holds for counted when it is back (`drop_helper`, and, as a poll
with no helper fills that set again, `attach_helper` once more when the helper is attached:
an outage longer than a poll; not when that attach loaded the helper itself, none having
been in the game to count), so a disk picked up meanwhile gets the helper's count alone
(the re-auditor's findings, 2026-11-14 and 15: two count popups for one pickup; stand-in
tests on both sides). A count is still missed, the screens' count being right all the
same, when the helper in the game had no file of the seed during the outage (the client's
first put not made yet, or a file too large to keep, which the helper has no more once the
client goes), and in the moment after the helper is configured, a client's again and before
that client's first look (stand-in test, `tests/game_tests.cpp`,
`test_an_own_disk_shows_its_count_with_no_client`, failing with the watch taken out of
`watch_all`; in game 2026-11-14, `7859b9e7…`, no client: the count popup showed at the pickup,
silent then; its sound, `Popup::sounds`, added after, has its stand-in check only and is not
run in game). A received disk's notice is worked out before the poll's look at
the save, so right after a reload that put an own disk back it can read one too many once;
the count on the screens is the save's. The goal is sent for an ending in
a save that holds enough. A save past its ending that holds too few can only come from
1.1.0, whose count every save of the seed shared (a disk found in another save counted):
under this goal the keys are not in the seed and RETURN HOME is offered only with enough
disks, so a save that ended under the save's own count holds them (by the code; when the
ending writes its save was not measured). In such an old save the goal is sent as its last disk is picked up, before the
game has saved the pickup: kept so (the owner's decision, 2026-11-15: once the ending has
begun, how the count is reached no longer matters). The server's checked locations and the
client's found record no longer count a disk. Stand-in tests (`tests/test_ticks.py`,
`test_discs.py`, `test_zexion_client.py`; `tests/offline_tests.cpp` for the file select).
Working in game, 2026-11-14 (B1, slot 6, the seed `lab/ap/live64`; the owner's reports and a
read-only watcher's times, `lab/ingame-collected-checks.md`). With the client and
`zexion_ap.dll` `5f19e4d4…` (bbaf3c5): the own disk at Kresezoa 24,24 picked up, reloaded
from before it and picked up again "worked as expected" (the reload RELOAD FROM SAVE, by
the owner's later words; what the second pickup showed was not described apart, so the
count showing again there rests on those words alone), but for a
pause screen opened within a second of the reload, which still read 1 of 4 (fixed after
that run: the row follows a changed count). With the client and `7859b9e7…` (1b04f24): the
disk picked up, its count popup with its sound; RELOAD FROM SAVE (through the menu in
1.5 s); the pause screen opened 0.6 s later read 0. With no client, the same helper: the
count popup at the pickup (silent then); the disk was picked up a second time after a
RELOAD FROM SAVE, by the watcher, and what showed then was not said. Not run in game: the
count dropping with no client (the pause row, the file select), the file select's count as
it is now, the goal sent from a save's count, RETURN HOME WITH THE DISKS?, the equipment
screen's count after a reload, a death, and a disk received right after a reload; nor the
second review round's fix (the counted set started over when the helper is attached again,
unless that attach loaded it), stand-in tested only.

The scanner's line, the equipment screen's disk, the pause row's count and the disk glyph show
only while the loaded save is the look's own (`Game::seed_look_loaded`, "Intel Chips"); in
another save or none the game's own are back whatever the client last sent (stand-in tests only,
not run in game). The scanner's text is looked at every 30th frame call (`kScanFrames`), so its
line follows the save up to 30 frame calls late; the rest follow it in the same frame call.
`discs_status` describes the last equipment screen built in such a save, so it is not updated
while another save's screen is up. Another slot of the same seed loaded during a drop shows the
last slot's count (the client's, for the slot it set it in).

Put back: `discs` with no arguments, and with the rest. An unload refuses while
the scanner's text or the pause screen's branch is still the helper's, or the pause menu's way
back from OPTION MENU is still its wrapper.

In a translation ("Translations"): the scanner's line goes into SmallText 12 too; the equipment
screen's disk (its name after the HD disk icon) and RETURN HOME WITH THE DISKS?'s question are
built again in the translation's font; the pause row's count is KEYS's translated text built
again ("2 of 15", in capitals outside English(HD): "Translations", mixed case; centered on the
row as the game centers a prompt's line, built again
whenever the game builds KEYS again: in the same call on OPTION MENU's way back, above, and in the
next frame call otherwise), the key slots' characters hidden.
Working in game 2026-11-02 in English(HD) (B1; "Translations", "Run
in game" and "Run in game again"): the received disks' count popups and the reminder in the HD font,
NO, and YES sending the goal (`zexion_ap.dll` `1c286c57…`); the equipment screen's OS Install Disk
after the HD disk icon and its description wrapped inside its box, the question centered, and the
pause row's count ("4 of ?") in the HD font (`946b08a1…`); the scanner's line, 2026-11-04
(`a7469024…`, develop 345fea7).

Working in game, under the count as it was until 2026-11-14 (shared by every save of the seed,
from the found record; the file select's for a client's seed sent by the client: what those
runs showed of the screens, the scanner and the prompt stands; the count's source as it is
now ran in game 2026-11-14, above, the file select's count with a client has not): 2026-10-24 and
2026-10-25 ("The scanner", "The equipment screen", "The mission
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
  no client, and normal and plain randomizer saves are never marked. Not the slot a new game is
  starting in (`newgame_fn` queued, `current_slot`): the game marks that slot in use for the
  fade, and a deleted AP save there keeps its section, so it would show that save's logo and
  count until the new game loads (`Game::slot_section`; stand-in tests; Linux B1's marking by
  disassembly, Windows' not checked). The entry must also show a randomizer save in use itself
  (`in_use`, the record the entry holds, which is the checkpoint record on Linux while that is in
  use), as well as the slot's full save being a known seed's. Whether a copy can leave an entry
  showing a checkpoint record of another game over a slot whose full save is an AP save's (then
  the logo and the count would be this seed's over that game's record) is not traced; cosmetic.
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
  Install Disks (the count every save shares: the disks the slot starts with and those
  received; the number needed; the scanner's line; and the cells of this player's own disks,
  each counted in a save whose map shows it collected), this player's own traps (each one's
  cell and item: "Traps") and the item labels.
- **Where:** `offline_put` checks each file as the reader will (the config with at least 8
  function checks; NAME in `<seed>-<team>-<slot>` form) and keeps none over 48 KB; the client
  sends none whose request is over the pipe's 64 KB. A seed's
  file that parses is handed to the running helper before the checks that concern keeping it
  alone (the folder, the size, the write: "Traps", "This world's own"). The helper
  writes it on the pipe's thread into `%LOCALAPPDATA%\ZexionAP` (`config-<build>.txt`,
  `seeds\<NAME>.txt`; an absolute `LOCALAPPDATA` only), and a copy of the file of each seed a save
  uses into the game's save folder ("Seed files in the save folder"), nothing else there. The helper
  writes them, not the client: a client run from a packaged app has its AppData writes
  redirected (inferred: `docs/re/memory.md`, "AP saves without the client").
- **Reading:** when it starts and each time a client goes away (anything that used the pipe,
  `tools/helper.py status` too), at most 256 seed files, newest first, from its own folder and
  from the save folder, the two copies of a seed made one ("Seed files in the save folder").
  Seed files read with no
  client replace the last ones, a dressed save's seed kept dressed if they still hold it (its
  record as it was); those read while a client is
  connected, and any seed file a client puts, are added to them from the next frame call
  (`Game::merge_offline`: a seed already there takes its old record's place, unless a save of it
  is dressed), so another seed's save started earlier in the same client run is known as one
  (`other_save`). The last seed a client puts is also its look's seed ("Intel Chips"). With no
  client yet, the helper's own scan configures it ("Finding its own addresses"); only if that
  finds no address does the cache, if it is for its build and holds every required key ("The
  pipe") and the game holds every checked function's bytes. So the offline look, the map
  markers, the title lines and the text window's repair all run with no client from the first
  start, before the client has ever sent a file (stand-in tests; not yet in the game from a first
  start), and with a cache alone only while the scan fails.
- **The look:** with no client, or a client that said the loaded save is another seed's
  (`other_save`), every 16th frame call a save of one of the seeds is dressed as the client would,
  once it has been in play since it loaded (a menu or a death later keeps the same save, so it is
  dressed at once when a client goes away with a menu up): the item labels, INTEL CHIP, the ASSIST
  lock (the file free once the slot has received Assist Unlock: `client/ticks.py`, `offline_seed`;
  stand-in tests only), the disk count (the file's, plus each own disk the map shows collected)
  and the scanner's line, with a notice that the client is not connected (or, for `other_save`, is
  for another seed; only for a save of a seed it has a file for, so a plain randomizer game gets
  no notice): once a load, and again each time a client that configured during the load goes away
  (working in game 2026-10-29, 1.0.0 (b790be9): `docs/re/memory.md`, "AP saves without the
  client"). While the save stays dressed, each look sets that look again where it was changed: a
  client of another seed, attaching with the save in play, takes its own look back, which is not
  this save's (stand-in tests, `tests/offline_tests.cpp`; working in game 2026-10-26). A client
  with no server connection (the server down, say) lets the helper go, as if it had closed (on
  Linux it closes the Linux helper's connection, and configures it again on connecting, before
  that poll's step: the step waits for it as after an attach), while
  a save it has not dressed is loaded: one loaded after a drop's save was left, or the one in play
  when the player disconnects. Such a save then gets this look and its notice too. The client
  keeps the helper for a save it dressed before a drop (its look stays, in that seed's save only:
  another save loaded during the drop, a normal one or another seed's, has the game's own Intel
  Chip, ASSIST and scanner line, put back by the helper itself; stand-in tests only, not run in
  game), in the ending (its hold),
  and at the title, where after a drop an armed new game stays armed until a save is loaded. It
  attaches again on connecting, before it gives any item; an attach that fails, for a helper that
  may still come ("Getting in"), holds the items back for up to 30 s (counted afresh at each
  connection: a drop during it starts it over), then they are given as with no helper, but for
  the traps while an old helper's unload was refused ("Getting in"; "Traps", "Once")
  (`client/zexion_client.py`, `leave_helper_tick` and `poll`; stand-in tests,
  `tests/test_zexion_client.py`). Working in game 2026-10-29 (the Launcher's client): a save
  loaded during a drop and a Disconnect in play, each given this look and its notice, then the
  client's again on connecting, with CLIENT CONNECTED and received items' popups; 2026-11-04 (B1,
  `zexion_ap.dll` `a7469024…`, develop 345fea7), three Disconnects and Connects in one load, and a
  Disconnect then Connect during an ending played again, the item received meanwhile popping up at
  the next load. Not seen in game: a failed attach on connecting, an ending that begins during a
  drop, and an armed new game kept at the title during a drop. The file select shows each such
  save's count, from the map of the record its entry shows, and the AP logo ("AP saves on the
  file select").
- **Put back:** leaving the save undresses it: no save loaded, or in play reading as no known seed.
  With a menu up, a save in play since its load that reads as no known seed is held as it is until
  play, so the lock never lifts in the options menu for one read (none such is known; stand-in
  tests, `tests/offline_tests.cpp`). A client's `config` takes the offline look off first and
  withdraws the notices not yet shown (the not-connected warning, and the last client's other-seed
  notice and CLIENT CONNECTED.); the client then asks `client_back`, which pops up ARCHIPELAGO
  CLIENT CONNECTED. once if that save was dressed. It asks once a poll has found the save in play,
  at the start of the next poll, before that poll gives anything (`client/ticks.py`,
  `client_back_tick`), so the notice comes before the popups of the items received while the client
  was away, and the follow-ups of the finds made meanwhile come after it (`followups_tick`). Before
  704bd25 it came after those items' popups, which the same poll handed over first (stand-in tests,
  `tests/test_zexion_client.py`). Working in game 2026-11-11 (B1, `zexion_ap.dll` `a8939f69…` built
  from 932411b, `lab/ap/live62`, slot 5, the owner's report; `lab/ingame-popup-order-checks.md`):
  the client closed in play, a Missile Container, a Scrap Metal and an Energy Tank sent, the client
  started again: CLIENT CONNECTED first, then the three in the order sent ("I got them in the order
  sent", "client connected was first"). Popups the helper queued before the client went, still
  waiting, show before it (by the queue's order; not run in game). Each notice is for the load it
  was queued in: one not shown when the save is left (no save loaded) is withdrawn, not shown in the
  next save, and the next load is told again. The other-seed notice and CLIENT CONNECTED. are also
  for their client: one not shown when it goes away is withdrawn. A not-connected warning withdrawn
  before it showed is given again when the save is next dressed with no client.
- **Waits for the client:** received items, hints, the goal and RETURN HOME WITH THE DISKS?.
  This world's own traps do not: the helper sets each off at its pickup ("Traps").
  What was picked up is on the save's map, and the client sends those checks when it next
  connects with the save loaded.

Working in game: 2026-10-25 ("AP saves without the client"); 2026-10-29, a Secret Shuffle
seed's file (a 155-byte table, one `table` line): the save dressed with the client closed, its
notice shown, the map's unseen squares named as the game names its own (develop e088773,
`9b609b92…`; "AP saves without the client"); 2026-11-04 (B1, `zexion_ap.dll` `a7469024…` and
`version.dll` `88d0de4c…`, develop 345fea7), the helper the proxy loaded configured from a cache
holding every key, with no client, at two starts of the game and one after a close while
disconnected.

### Seed files in the save folder

`src/offline.cpp` (`save_folder`, `format_offline_seed`, `synced_file_text`, `parse_synced_file`,
`merge_offline_seeds`, `SavedSeeds`, `join_saves`, `join_synced`, `restrict_to_test_folder`),
`Game::saved_seeds` (`src/game_calls.cpp`), `src/dllmain.cpp` (`save_dir`, `offline_dir`,
`snapshot_saves`, `copy_thread`), the Linux helper's `g_save_folder`, `g_saved` and `copy_loop`;
`docs/re/save-format.md`, "Steam Auto-Cloud syncs only `*.sav` and `*.png` in the save folder
itself". Working in game on Windows, between two PCs (2026-11-20, B1 inferred: the helper hooks only B1, develop bf86eb5,
`zexion_ap.dll` `2d8b696f…`, `version.dll` `b5a196b2…`, `zexion.apworld` `a2bcc022…`; the owner's
report, `docs/re/save-format.md`, "The helpers keep the file of each seed a save uses here"): AP
saves made on one PC showed as AP saves on the other after Steam Cloud synced them. Seen once,
Windows to Linux (2026-11-21, a CachyOS PC, B1 assumed; the owner's report: AP saves synced from
Windows showed as AP saves there, after the owner cleared some things out, what not reported;
"Seen on a Steam Deck, and a Linux PC's sync", below). Not run in the game, so stand-in tests only: Linux's
own writing of the copies and Linux to Windows, whether a client ran on the second PC first,
which seeds and slots (neither was reported), the new-seed step below, the merge of two machines'
copies, a cut copy's refusal and that `helper.py status` writes nothing.

- **Why:** Steam Cloud syncs the game's saves, so a save made on one machine is on the next, but
  the seeds' files, which make it an AP save, are in a folder it does not sync. On that machine
  the save was a plain randomizer game until a client had connected for its seed (needs the AP
  room up). So each helper also keeps a seed's file in the game's save folder, where Steam's
  Auto-Cloud rule (`*.sav`, that folder only: measured) syncs it with the saves.
- **The file:** `ZexionAP-seed-<NAME>.sav` (`kSyncedPrefix`, `kSyncedSuffix`; NAME as the local
  file's: `<seed>-<team>-<slot>`, `good_seed_name`), the seed's own file text, then one last line
  `crc XXXXXXXX`: the CRC-32 of every byte before it. The reader refuses a copy whose last line is
  missing, wrong or not last, so one cut short by a sync (a labels block, which runs to the end of
  the file, would otherwise parse) is refused, never read as another seed. The name starts with a
  prefix no file of the game's has, and ends in the suffix the rule syncs. The helpers never write
  any other name there, never a `Save Game`, `GameUnlocks` or `Preferences` file, and delete
  nothing. Its temporary file is `...sav.tmp`, which no rule syncs.
- **Which seeds (the owner's decision, 2026-11-20):** a copy is written only while one of the
  game's 30 save slots uses the seed: a randomizer save in use (the marker set, the slot not marked
  empty, not the slot a new game is starting in) whose item table and start are the seed's, the match
  the file select makes (`offline_matches` against `offline_seed_of` of the slot's section). The
  slots are read **from the game's memory** (`Game::saved_seeds`: `slot_section` for each of the 30
  slots), never from the save files: the game opens a save to write with no share mode (SDL's
  `"wb"`), so a read open by the helper, on its pipe thread, could fail the game's save. The frame
  call (the game's thread) takes the list on its first frame and every 16th (30 markers and sections,
  cheap) and publishes it (`SavedSeeds`, under a lock the game thread only tries) for the other
  threads: a put and a read of the files decide from the last list published, and none is known (the
  helper not configured yet) writes no copy. That bounds the copies written and refreshed at a time
  at 30; copies a deleted save leaves, or a seed played out, are read but not refreshed, and
  nothing is deleted, so more than 30 may stand over time and stay on every machine until a person
  deletes them (the setup guide says so). A put with no slot using its seed yet (the client's first
  put comes before the new game's first save) writes no copy then; **the copy thread** writes it:
  a thread of the helper's own (never the frame call) that looks at the list's generation every 250
  ms and, when it changed (a save made, a new game, a deleted save), runs a pass of the files (as a
  read that writes does), so the copy follows within a moment of the slot showing the seed, whether
  or not a client is still connected, with no further put. A put whose own file could not be kept
  (no folder, a full disk) is kept in a pending list with its text, which the pass also checks against
  the slots (`g_pending`; an entry goes once its copy is written or a local file for it exists). On
  Windows it is started with the helper and stopped before the DLL can unload, with a bounded wait
  ("Unloading"); on Linux it stays for the game's run. **When a new game's slot shows the seed in
  memory** (after the new game's fade, from `slot_section`, which leaves out the slot a new game is
  starting in while it is queued) is measured on Linux by disassembly (`docs/re/linux.md`) and
  inferred for Windows, not checked there; so the copy may be written before the game's first save of
  that slot reaches the disk, or only after it (a quit in between leaves a copy and no save, or a
  save and no copy until the next start: the in-game check in the PR is for this). That check (a
  new seed, one save, a quit to the desktop from play, then a look for the copy) has not been run.
  The new-game timing is not run in the game (stand-in game tests only: Windows
  `apply_in_frames` through the loaded helper, Linux `tests/test_native_linux.py` through the
  socket).
- **Where:** the game's own folder, as SDL names it (`SDL_GetPrefPath("Gallant Leaf", "Zexion")`,
  the call the game makes, B1: `docs/re/save-format.md`), found without calling SDL, which makes
  the folder: on Windows the Roaming AppData known folder (`SHGetKnownFolderPath`, asked of
  shell32 at run time) and those names, else `%APPDATA%`, found once, the first time it is asked
  (the helper asks at start); on Linux `$XDG_DATA_HOME`, else `~/.local/share`, then those names,
  found once in the constructor with the helper's own folder, before the game's threads run (under
  Flatpak Steam, the sandbox's home). The helper never makes the folder: with none (no saves yet)
  nothing is written, and nothing said. A program that is not the game (a production helper loaded
  into a test host, say: no known build) has no save folder and answers `offline_put` with `error
  not the game`, so it writes no folder at all.
- **Test builds** (`zexion_ap_hooktest.dll` and the Linux `libzexion_ap_hooktest.so`) take every
  folder they write through `restrict_to_test_folder`: an absolute, normalized path strictly inside
  the system's temporary folder (`GetTempPathW`; on Linux `$TMPDIR`, else `/tmp`, links resolved)
  and never the folders the OS itself names for this user (`os_save_folders`, `os_offline_folders`: a
  test points `%LOCALAPPDATA%` and `$XDG_DATA_HOME` at its own, which is allowed inside the
  temporary folder); else none, and nothing is read or written. Their save folder is only what
  `ZAP_TEST_SAVE_DIR` names (not `ZEXION_SAVE_DIR`, which the repo's tools define as the real save
  folder, nor `$HOME`), under the same rule. The rule resolves links (a Windows junction or `subst`
  drive, a Linux symbolic link: the final path of the longest part that exists), refuses any `..` in
  the path (never folded as text) and returns the resolved path the caller must use, and refuses
  everything when a real folder cannot be named or resolved. The real folders are the OS's own:
  Windows' known folders; on Linux the passwd home's `.local/share`, Flatpak Steam's
  `.var/app/com.valvesoftware.Steam/.local/share` and its default `XDG_DATA_HOME`,
  `.var/app/com.valvesoftware.Steam/data`. The rule is also in the write
  primitives themselves, in the **test-only build of the core** (`zap_core_test`, Linux
  `zap_shared_test`, `ZAP_TEST_BUILD` set by `src/offline_test_build.cpp`), which every test
  program and the test builds of the helpers link; the release build does not. A canary in CTest
  (`native/tests/canary.cpp`, a fixture every test requires) and in pytest (`tests/conftest.py`)
  lists the real folders (the seed copies in each real save folder, every file in each real
  per-user folder, absent ones recorded as absent, on Linux the run's own `$XDG_DATA_HOME` too) and
  fails the run if any changed.
- **Written:** by the pipe thread, never the frame call, through a temporary file (flushed to
  disk) renamed over the old one (on Windows with write-through), and only when the text differs
  (no churn for Steam to sync): at an `offline_put seed` (after it is made one with the copy
  already there), and at a read of the files that writes: at the helper's start (which writes local
  files only: no slot list is known yet, so no copy is written then; the copy thread rewrites the
  copies a moment after the game's first frames, if a slot uses their seeds), and when a client
  that set something goes (a request that is no probe: not `hello`, `ping` or a `*_status`, so
  `helper.py status` writes nothing), and by the copy thread when the game's slots change. One
  thread at a time reads or writes the seed files (a mutex inside `put_offline_file` and
  `read_offline_files`). A read that writes makes a seed's missing copy for a seed a slot uses, and
  a seed that has no local file its file. A copy that does not read is not trusted:
  a good local file replaces it (when a save uses the seed), and with none it is left alone. A
  local file is never written from a copy over one that is there and was not read (past the 256
  read, or one that does not parse). A write that fails is said (`read_error` of `hello` on
  Windows, stderr on Linux) and the rest goes on: the local file, which stays as before, is the
  fallback.
- **Two copies of a seed made one** (`merge_offline_seeds`; the same table and start; the copy
  last written is the newer, a put one always newer; a name that holds another seed's file is not
  merged, and said): the newer one's fields, except that Assist Unlock is never taken back
  (`assists free` wins), the larger disk count stands with its own-disk cells and scanner's line (a
  tie: the newer's), the labels with more `label` lines stand, and the traps are the older's if the
  newer has none. The result is written to whichever copy differs from it, so both converge. What
  changes as a seed is played is the disk count (which only grows), the ASSIST lock (once) and the
  labels; the table, start, Intel Chip setting and own traps are the slot data's, the same on every
  machine. Steam itself keeps one side's copy whole at a conflict.
  - **With no client** the disk count in a seed file changes only when a client puts it: a disk
    picked up on the second machine with no client is counted in that save by its map (`unfound`)
    and writes no file. So the merge makes the richer fields of both hold only for counts a client
    put (`discs`), the labels and the ASSIST lock.
  - **A rehosted room, or a server restored from an older `.apsave`,** cannot take back a free
    ASSIST menu or a larger disk count through a put: the merge keeps them (the merge is not
    changed for it). A client connected to the new room does not lower them in a file either; the
    setup guide says so in one sentence.
  - **A tie in the number of labels** is decided by which copy was written last, which across
    machines is by the file's modification time (assumed to be what Steam sets on a download: not
    measured): cosmetic (the labels of two
    copies of one seed differ only in what each has seen), and only with no client.
- **Limits:** 48 KB a file (`kMaxFile`), and 256 seed files read from each folder. Steam's quota
  for the game is 97.7 MiB and 640 files, and it tracks the 30 saves and two more already.
- **Tests:** `native/tests/offline_tests.cpp` (the text, the crc, the merge, the slots' seeds from
  the stand-in game's memory and `SavedSeeds`, the put and the read with a stand-in save folder and
  lists, `restrict_to_test_folder` with junctions, the write primitives' refusal),
  `apply_test.cpp` (through the loaded helper: the frame calls publish the slots and the copy thread
  writes the copy), `native/linux/tests/offline_tests.cpp` (the POSIX half, symbolic links),
  `tests/test_native_linux.py` (through the socket on the stand-in game: a put, then a slot, then
  the copy, byte for byte against `client.offline.seed_text`; the test build ignoring `$HOME` and
  refusing a folder outside the temporary one or a link out of it), `tests/test_native.py` (a
  production helper in a test host takes no seed), `tests/test_cross_checks.py` (the name, the
  writer's lines against the client's, that the helpers never open the game's saves).

### The map

- **What:** in an AP save holding the seed the labels are for, in play or in a menu (a plain
  randomizer save is never touched), raises the map squares of item 48 at the section's
  locations (154, or 155 with Secret Shuffle; one labeled this player's own Scrap Metal is
  lowered to a minor's instead: "Other players' items") (this player's OS
  Install Disks and own traps, and other players' progression items and any game's traps, but
  not a Zexion player's supplies: `worlds/zexion/world.py`, `important_elsewhere`) from the game's
  basic marker to its important one (`src/mapmarks.cpp`), obscured or shown as the game's own majors
  are under the seed's Obscure Majors, Obscure Minors and Obscure Class. Under Majors on with Minors
  and Class off a stand-in shown as a minor in a square a hint, a map room or Hints Revealed
  revealed becomes an obscured major and one seen directly a shown major; the value cannot tell an
  Area Map's reveal (shown) from a hint's ("Map markers"). While an item is on screen the game
  writes its square every frame as a shown item of its ID's class, which took a stand-in back to a
  minor (measured live), and its minimap dot's border was seen to flash (assumed to be this); so the
  helper also patches that write (`seen_marker_code`, 50 bytes) to give item 48 a major's class, as
  an item on screen of IDs 3 to 35 gets (`0x1A`, or `0x0E` in a square a hint revealed). The patch
  is the game's code with one more compare: it calls and reads nothing of the helper's. While the
  map is open, a wrapper on the map's per-frame handler writes the square's label's name over SCRAP
  ENERGY or SCRAP CHUNK after the game writes the cursor's name, in the same frame; in a translation
  it builds the cursor's translated text again as the label's first line ("Translations"; working in
  game 2026-11-02 in English(HD): AP ITEM); since, without the logo, and in English(HD) in the case
  the client sent it (AP Item, Progression Item, OS Install Disk) or, for another Zexion player's
  item, the translation's own name for it (working in game 2026-11-03, B1, `zexion_ap.dll`
  `12f090c6…` ("Map markers"): AP Item on a DLCQuest player's square, its translated glyphs read
  back with a read-only probe as A, P, I, t, e, m, shown, no logo among them, the pixel ones hidden;
  another Zexion player's items by the game's names, the owner's look; Progression Item and OS
  Install Disk, first line only and no logo: 2026-11-04, B1, `zexion_ap.dll` `a7469024…`, develop
  345fea7, the owner's report). Since 2026-11-04 another Zexion player's item takes the
  translation's own name in every translation, not only English(HD): on its pickup popup in French,
  working in game 2026-11-04 (the same build: Wall Jump Boots as BOTTES DE SAUT MURAL, the owner's
  report); on the map cursor, not run in game.
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

Working in game (B1; "Map markers"): 2026-10-25; 2026-10-26, two three-game seeds, the markers
raised in a pause right after loading, and with the two-look check the unseen squares named as the
game names its own and the markers still raised after a save, a return to the title and a load;
2026-10-27, two Custom seeds under Obscure Majors on and Minors off, Class off and on, the stand-ins
marked as the game's own majors, a stand-in's minimap dot flashing while on screen; 2026-10-28, with
the on-screen write's patch (`tests/mapmarks_tests.cpp` runs the game's bytes and the patch side by
side), no flash; again with the patch in `CodePatch` (`src/game_mem.h`), 2026-11-08
(`zexion_ap.dll` `0f51bb6f…`, develop 5597753, the seed `lab/ap/live57`, slot 6, the owner's
report): another player's progression item on its stand-in (Guardia 25,11) a major with the
Progression Item label on the map, and on the minimap, with the item on screen, a major whose
border did not flash.

### The title screen's lines

- **What:** adds ARCHIPELAGO MOD and its version, and ©2026 MCAWFUL, centered under the title's
  copyright line, and above them, only while no client is active, START THE AP CLIENT TO PLAY A
  SEED (`src/title.cpp`). It makes them with the game's own text functions in that line's
  palette and style, and the game frees them with the title.
- **When:** while configured (by a client, the helper's own scan or the cache) and no save is loaded, every 9th
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
  title (`docs/re/memory.md`, "The title screen"). Working in game 2026-11-03, as under "Shown".

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
    sections (`Game::ap_file_slots`, as for the file select's logo, every 16th frame call, which
    leaves out the slot a new game is starting in: stand-in tested, Linux B1's marking of it by
    disassembly, Windows' not checked). It
    keeps the table while a save loads, and clears it once a frame call finds a save loaded that
    is not an AP save. The slot is hooked whenever the stub can block: the guard on,
    or the table not empty. Since the table is filled only with no save loaded, a helper loaded
    in the middle of a save has none until the title comes back. The helper hooks the slot only if:
    - the import table names it for that function;
    - it still holds steam_api64's own function (so another hook is never replaced);
    - the context's init function asks for `STEAMUSERSTATS_INTERFACE_VERSION`.
  - **The Feats of Natu screen.** It still follows an AP ending, with nothing flashing: MISSION
    COMPLETE's builder always leads on to it (by disassembly). The helper writes ARCHIPELAGO SAVE:
    NO FEATS EARNED, centered, into the blank last line of its title label, under FEATS OF NATU!
    (`feats_screen_note`). It writes only on that screen as it follows an ending: the top menu
    with 4 panels (the title's 3 widgets, EXIT, and two rows of Feats: 7, or 6 on top and 5 or 6
    below when the game hides some: 6 and 5 seen), a live title widget whose label is three equal
    lines (the middle one naming NATU), and MISSION COMPLETE's statistics under it: a panel of 10
    lines and, seen after an ending in a seed with Allow Assists on, ASSISTS USED's 8 as a second
    (read live on B1, 2026-10-27; what makes the game add it is not traced on Windows; on Linux it
    is: `docs/re/linux.md`, "Mapped by disassembly: the Feats of Natu screen's note"). With the
    secret row (a secret Feat earned, or the title screen's code typed since the game started) the
    top menu has a fifth panel of 5 Feats and the game moves the title up 8 and the rows up 16
    (read live, 2026-10-28): the last line is still blank, and the note goes there (working in
    game, 2026-10-28; 0.30.0 showed none there). It writes the whole line or nothing, only over a
    line that is blank or already its note, and only if every glyph of it is writable and has the
    font's frames. In a translation, while the note (or the title written over the hint) is there,
    the title's translated text is built again as LargeText 220's (the game's Feats of Natu!) with
    "Archipelago save: no Feats earned" (in capitals outside English(HD): "Translations", mixed
    case) under it, wrapped, the shorter line centered on the longer with leading spaces, the two
    centered and kept at the title's middle as the game lays out its title ("Translations"); the
    game's own entry (220, or 219 for the hint) again when the note goes. Without LargeText's
    count, or with too little room, the title's pixel glyphs are shown in its place instead, the
    whole title in the pixel font. (Working in game in English(HD): the title and the note in the
    HD font, 2026-11-02, B1, `zexion_ap.dll` `946b08a1…`; the shorter line centered, 2026-11-03,
    `fce9e73b…`, the 1.0.0 RC at develop 76df074, the owner's screenshot: `docs/re/memory.md`,
    "Where a rebuilt label goes".)
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
  - a seed file read from `%LOCALAPPDATA%\ZexionAP` or the save folder's copy (at start and each
    time a client goes away);
  - one a client has put since (`offline_put seed`), known from the next frame call;
  - the seed the labels were set for.

  That covers the client's own seed, a save played with no client, and another seed's save
  with a client connected (`other_save`). It reuses what the offline look matches saves by,
  rather than the client's word, so it needs no client: the ending can run offline, or while a
  client of another seed is connected. A plain randomizer save matches no seed and is left
  alone. Not covered: an AP save played without the helper, or one whose seed the helper has
  no file for (every new game from ARCHIPELAGO is made with a client, which leaves the file).
- **When:** every frame call while configured. It goes on in the first frame call that finds an
  AP save loaded: a load writes the section and the marker in one game call, so a frame call
  never sees half of one (`docs/re/memory.md`, "The loaded save's randomizer section is at
  +0xCDD6D0"). It goes off at once: no save loaded (MISSION COMPLETE onward, the title), a normal save, a
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
- **An unload leaves a gap:** from the unload to the next helper's first frame call (the
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

Working in game (B1; `docs/re/memory.md`, "Feats and achievements"):

- 2026-10-26: two AP endings through RETURN HOME WITH THE DISKS? with the client connected (the
  ending jumped over, the goal sent, no Feat earned, `GameUnlocks.sav` unchanged, the note on the
  second), a normal ending earning its Feats as usual, and the guard on in an AP save before a
  client connected; with the Steam stub as it was before the load test.
- 2026-10-27 (0.30.0, the stub as it is now): AP endings through RETURN HOME WITH THE DISKS?, one
  with the client disconnected and connected again during its cutscenes; an achievement reaching
  Steam at the title after an AP save was quit (13SecretCode, typed at the title; Steam's record),
  Steam's slot on the stub at that title; a plain randomizer save in play with a client connected
  left alone (Steam's slot on steam_api64, the ending's call the game's own, no notice, no look); an
  AP ending through the escape skip (the helper's offer then, retired since) with no client, flag
  251 left clear and `GameUnlocks.sav` unchanged, and, from d451e6b, one with a client of another
  seed connected, the same and the note shown over ASSISTS USED's panel.
- 2026-10-28: the note over the secret row, and after a restart with no code typed in the usual
  layout; in a fresh save folder (no Ending B, no Feats on record), FEATS OF NATU! and the note in
  place of the hint, over rows of 6 and 5 and, with the code typed, on the upper two lines over the
  secret row, and the game's hint put back on an unload with that screen up; each time flag 251
  clear and `GameUnlocks.sav` given no Feat (in the fresh folder, no Ending A or B entry and no
  `+0xCE00A8` either).

Not seen in game:
- Steam's stand-in stopping an achievement. No achievement the owner has left is cheap to earn in
  play (13SecretCode is taken at the title only), so this is tested only with the stand-in;
- the load test: a fully explored AP save loaded from the file select earning no 15Explorer
  (by disassembly and the stand-in only; the owner's 15Explorer is earned already);
- the note over a bottom row of 6 (by disassembly and stand-in tests only);
- the client's warnings (`state.FeatsCheck`, `ticks.feats_tick`): their decisions on stand-in
  tests, the asking itself not under test. They could not be arranged on 2026-10-27: a connected
  client gives the helper its seed's file, so the guard is on in this seed's save.

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
    on the next line, as mixed case below has it: "from Bob" in English(HD), "FROM BOB" in any
    other translation, as in the default English; a notice's text likewise, the client's and the
    helper's own (NOT CONNECTED, CLIENT CONNECTED, the other seed's). Laid out as the font draws it (`FontMetrics`, `layout_popup`: the
    builder's advances, read from the font as loaded): no line wider than a pixel popup's 192
    pixels, words whole; a popup's lines centered on the widest with leading spaces (the game
    starts every line at the same x); a notice's left as they wrap; without the font's metrics,
    wrapped at 30 characters. A popup that cannot go there (longer than the
    run, its area still taken after the 240 frames, the run not as traced, the tables
    unreadable with the flag set, or from an unload's put-back on) shows the helper's pixel
    text, with the mark the game's last translated message left in its slot cleared (told by
    the HD object's serial), so it shows, in its box.
  - **Mixed case** (English(HD) only, the owner's rule, 2026-11-02; `mixed_case`, by the
    language's name): the client writes its text in mixed case (hints, the disks' counts and
    reminder, the scanner's line, the labels, "from Bob": `client/hints.py`, `client/discs.py`,
    `client/aplabels.py`, `client/session.py`), and the helper writes its own in mixed case too
    (the notices, the title lines, Archipelago, Intel Chip, Return home with the disks?, OS
    Install Disk and its description, the pause row's "2 of 15", the Feats note). Each is kept
    once, in that case, and put in capitals only where it is drawn so: in the 8 x 8 font
    (`in_capitals`, and `Capitals` for the fixed texts, `src/text.h`), and in every translation
    but English(HD) (`hd_case`), whose fonts may draw no lower case. Only letters change: a
    record's icon byte (0x6E and 0x77 lie in the lower-case range) goes before the text, never
    through it. Until 2026-11-04 the helper's fixed texts showed in mixed case in every
    translation; they follow the rule since (working in game 2026-11-04 in English(HD) and
    French: "Run on develop 345fea7", below). The labels working in game 2026-11-03 (B1,
    `zexion_ap.dll` `12f090c6…`): the pickup popup's Progression Item over for AneziaDS2, the map
    cursor's AP Item ("Other players' items", "Map markers"); a hint, the scanner's line and "from
    Bob", 2026-11-04 (the same run); the disks' popups and reminder and the notices' case: not
    singled out in game.
  - **Names the game reads while they show,** written into the tables in place and cut to each
    entry's room: the stand-ins' labels into ItemNames 47 and 48 (both lines, centered on the
    wider with leading spaces as a received item's popup's, since the pickup popup starts each
    line at one x, where those spaces fit the entry's room, else as they are so no name is cut)
    while their records are restyled in play, for their pickup popup, each held as it is while
    a pickup message bound to it shows in the AP save (the HD message reads it again every frame:
    `docs/re/memory.md`, "Translated messages"), so walking into another cell or pausing during
    the popup leaves its name (another cell: working in game 2026-11-04 in English(HD), "Run on
    develop 345fea7", below), unless another stand-in of that record is within reach, whose
    name the next pickup needs. A newer pickup message of that record (one no scan had found
    within reach: the message routine counts its serials up, `0x18284F` to `0x18285A`) gets the
    frame's name, then that is held. While the map is open the game's own name is back, for the
    map cursor's squares of the player's own Scrap Energy and Scrap Chunk, and the held one again
    once it closes (stand-in tests only, not run in game); Intel Chip into 49 while the Area
    Map's record is INTEL CHIP; the scanner's line into SmallText 12 (its message event's entry)
    while the Text table's entry is the helper's.
  - **Menu labels built again** with the game's builder (`hd_build_fn`, the string from
    `wstring_new_fn`), laid out as the game lays out that label's own (`Rebuild`;
    `docs/re/memory.md`, "Where a rebuilt label goes"): ARCHIPELAGO (Archipelago; SmallText
    354's text again when RANDOMIZER goes back, not through the new game's fade) and Return home
    with the disks? (each as mixed case above has it, as are the rest of the helper's texts below)
    where the game's attach would build them now (the label's first pixel glyph
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
  English, even with a translation's tables still loaded), unconfigured, or with the object's
  fields not where the other keys say (`translation_keys`).
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
  and a DLC Quest player (`docs/re/memory.md`, "Translations" and the sections after it).
  - Working in English(HD): the title lines; received popups in the HD font with their box, icon,
    the HD name and the suffix on the next line, singly after a save's GAME SAVED, in batches of
    5 and 8 (every one shown), and after a language change; notices (an Intel Chip's hint, long
    ones too; NOT CONNECTED; CLIENT CONNECTED; the disk reminder); a stand-in's pickup popup and
    the map cursor on its square ("Other players' items"); the pause row's count and the locked
    ASSIST in the pixel font (in the translation's font since: the run after, below); the
    equipment screen's name; the question's text, NO and YES; the Feats of Natu screen's note
    (likewise); `/reload_mod` (the unload and the load again; its refusal while a popup shows
    was not tried).
  - A language change mid-play, with popups showing: English(HD) to French and back, then
    Japanese, then the default English, with no crash; the popups after each change shown, in
    French and Japanese with the translation's name and FROM … in English. The client's warning
    once, for French, and none in English(HD) or the default English; `lang=-` in the default
    English and `English(HD)` in English(HD) (`tools/helper.py status`).
  - Bugs (English(HD)), fixed since and working in the run after (below): ARCHIPELAGO's label
    not on the game-mode panel (built at the window's top-left corner instead, and RANDOMIZER's
    likewise when the client went away); the question left of center; the equipment screen's
    description past the box's right edge; ASSIST's padlock past the options box's right edge
    ("The assist menu"; `docs/re/memory.md`, "Where a rebuilt label goes").
  - The default English, the same run: received popups singly and in a batch of 5, NOT
    CONNECTED and CLIENT CONNECTED, ARCHIPELAGO on the panel, the locked ASSIST, the pause row,
    the equipment screen's disk, RETURN HOME's question (centered) and NO, the map cursor and a
    stand-in's pickup popup (with its logo glyph), all as before.
  - Not run in game: the scanner's line, an Intel Chip picked up in the world (ItemNames 49),
    the unload's refusal while a popup shows, Assist Unlock in a translation, a save-state load.
- **Run in game again:** 2026-11-02, later that day, B1, `zexion_ap.dll` `946b08a1…` and
  `version.dll` `7a1b2b40…` (develop ea1a801) loaded by the proxy, the client from source, the
  same seed again and a disks seed (`docs/re/memory.md`, "Where a rebuilt label goes" and "The
  translations' icons": the owner's screenshots).
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
  - Bugs (English(HD)), all fixed since: the locked ASSIST taking the selection's colors after
    another menu (Video, the map), and once showing unlocked (white, selectable, no padlock)
    after going through menus, not reproduced; its padlock below the text's baseline, against
    the box's right border; the AP logo before AP ITEM on the map cursor, where the game's own
    names carry no icon; a stand-in's pickup popup (AP ITEM over FOR BOB) with both lines at the
    same x. The last two: working in game 2026-11-03 (`zexion_ap.dll` `12f090c6…`, "Other
    players' items"). The rest: stand-in tests only, not run in game yet (the locked look in the
    options entries' style at alpha 0.45, the padlock on the text's line with no space before
    it, the offline look kept through menus: "The assist menu", "AP saves without the client");
    the ASSIST lock in English(HD) working on the 1.0.0 RC since (below), no part of it singled
    out.
  - The default English: the title, ARCHIPELAGO on the panel with its hover border, a received
    popup (FROM ARCHIPELAGO in capitals), RETURN HOME's question centered and NO, as before.
  - Not run in game: as in the run before, and French and Japanese since the HD icons (their
    FROM … in capitals).
- **Run on the 1.0.0 RC:** 2026-11-03, B1, `zexion_ap.dll` `fce9e73b…` and `version.dll`
  `2a640209…` (develop 76df074) loaded by the proxy, English(HD): the client from source, then
  the owner's own checks.
  - Working in English(HD): the Feats of Natu screen's title and note both centered
    (the owner's screenshot); no Keys frame on the pause row after Option with the client
    connected, and the frame of Keys before it, over a cache from before the
    `options_back_fn` key ("The OS Install Disks goal"); by the owner's report, the
    assist lock ("The assist menu"), the D2 disk icon (the helper's icons, above), the title
    lines only on the title ("The title screen's lines") and YES on a first ending ("The OS
    Install Disks goal").
  - Not run in game: as before, and French and Japanese since the HD icons.
- **Run on develop 345fea7:** 2026-11-04, B1, `zexion_ap.dll` `a7469024…` and `version.dll`
  `88d0de4c…` loaded by the proxy, the client from source, `lab/ap/live53` (a disks seed, another
  Zexion player and a DLC Quest player), the owner's report (`docs/re/memory.md`, "Translations").
  - Working in English(HD), mixed case in the HD font: the title lines, ARCHIPELAGO on the
    panel, a received popup ("Missile Box" over "from Bob"), the pickup popups of another game's
    item (the logo, Progression Item over for Alice) and of another Zexion player's (its icon and
    name over for Bob), the name kept on walking into the next cell during the popup, an Intel
    Chip picked up (Intel Chip, then its hint), the scanner's line, the pause row's count, the
    equipment screen's OS Install Disk, the question, the Feats note, and the locked ASSIST with
    no stray pixel letters.
  - Working in French, the helper's text in capitals: FROM BOB, INTEL CHIP (a received one's
    popup), AP ITEM and
    PROGRESSION ITEM, the title lines, ARCHIPELAGO, the pause row's count, the equipment screen's
    disk and its description in its box, the question (centered), and the Feats note on its own
    line, neither wrapped nor cut (the owner's screenshot); the icons as before; another Zexion
    player's pickup by the French name.
  - The default English, the same run: a received popup, a pickup label and the pause row in
    capitals in the pixel font, the title lines and ARCHIPELAGO, as before.
  - Not run in game: in French, the map cursor's name for another Zexion player's item, the
    scanner's line and an Intel Chip picked up (ItemNames 49); Japanese since the HD icons; the
    unload's refusal while a popup shows; Assist Unlock in a translation; a trap's popup (its name
    in the translation's case, `layout_popup`: "Traps"); a save-state load.

## The Linux helper

`libzexion_ap.so` (`linux/`) is the helper of the game's Linux build (`docs/re/linux.md`):
it reads and writes the running game's memory for the client, counts its frames, starts new games,
ARCHIPELAGO on the menu or RANDOMIZER ("New games"), gives the client's items with their popups,
scrap's drops and Energy Shields (`apply`, `shield`: "Grants", "Popups", "Received scrap",
"Received Energy Shields"), deals DeathLink's deaths (`die`: "DeathLink"), names Intel Chips
(`intel on`: "Intel Chips"), locks ASSIST (`assists lock`: "The assist menu"), shows other
players' items as Archipelago items (`ap_labels`: "Other players' items", "Other Zexion players'
items, as themselves"), sets off traps ("Traps"), shows its text in a translation's font
("Translations"), runs the OS Install Disks goal, its count and RETURN HOME WITH THE DISKS? ("The
OS Install Disks goal"), keeps AP saves' look without the client ("AP saves without the client"),
puts the AP logo and the disk counts on the file select ("The file select's AP logo and disk
counts", below), fills the major-item list the file select's ITEMS row draws ("The major-item
list", below), shows the title screen's lines ("The title screen's lines"), keeps Feats and Steam
achievements out of AP saves ("Feats and achievements in AP saves"), and repairs tall popups' text
("Popups", "Tall text"). It holds the Windows helper's `Game` (`src/game_calls.h`) and runs every
part of it but unloading (it stays for the game's run): below. The client reads and writes the
game through it (`game.Game.attach_linux`), gives its items, labels, seed, shields, deaths, Intel
Chips, the ASSIST lock, the disks' count, the finds' follow-ups and the Landing Pod's prompt
through it and asks it the language (`client/linux_helper.py`'s `LinuxMod`), and otherwise runs as
without a helper; `tools/helper.py status` talks to it too. Tested against a stand-in game
(`tests/test_native_linux.py`).

Working in game: 2026-11-18, B1 (Linux), Flatpak Steam on the test PC, the branch's build of
that day (the helper of 647b3a2, then with `callers`, as committed in 6dfc54a) copied into the
game's folder: loaded by the launch options, it served `hello` (`build=B1 hooked=1 folder=1`),
counted the frame calls (165 in a second on a 165 Hz display, all from `SWHardwareEnd`, the game
window's swap, at the title screen and in play: `callers`) and served reads that matched the
binary (`docs/re/linux.md`, "Read from the running game").

Working in game: 2026-11-19, B1 (Linux), Flatpak Steam on the test PC (CachyOS), the helper of
207af39 (`libzexion_ap.so` `d6e91080…`), then with the popup's steps (`b98752ac…`, built from the
commit that records this run), a seed of `samples/zexion-standard.yaml` hosted locally, the client
from source (`lab/run/`). Seen by the owner: the title screen's lines once the client configured
it; ARCHIPELAGO on the game-mode panel, and the seed's new game started from it in an empty slot
(the client: "The new game holds the AP seed (applied slot 5)"); the AP logo on that slot's entry
on the file select; a received Energy Tank's and Missile Container's popups with their items (the
first, before the steps, popping in and out whole: `kPopupStepX` in `src/text.h`); the client's
notices; and, the game started again with the seed's file kept and no client yet, the save dressed
by the helper alone (configured from the game's symbols), then ARCHIPELAGO CLIENT CONNECTED. once
the client was back (the client: "This save was played without the client: told the client is
back"). Read live through the helper: the game state, the pickup counter and the inventory
(`tools/grant.py watch`), and `menu_state` in play with the mouse used (0; non-zero only in menus:
`docs/re/linux.md`, `menu_state`). Then, received through the server's console (`/send`), each
with its popup: a Disorient Trap (the controls reversed) and a Disruption Trap (energy weapons not
firing), each going off as its popup showed and wearing off; an Energy Shield, up around the
player (the client: "A received Energy Shield is up."); an Intel Chip, its popup named INTEL CHIP
and its hint's notice after it (the server's hint: the Progressive Lance at Pulsava 34,15); and,
in English(HD) and in Japanese, an Energy Tank's and a Missile Container's popups in the
translation's font, opening and closing (Japanese with the client's warning that the mod's own
text is English there). Two items picked up in the world reached the server as found (Kresezoa
25,25 and Machinus 24,29), and after a save at a save point the file select's ITEMS row showed the
received items too. The client's own lines also said other players' items' labels and the ASSIST
lock were set for the seed (not looked at in the game). Not checked: the popups' sounds and the
HUD after the grants.

Run 2, the same day and setup, the helper of 4e0203f's sources (no hash recorded: which binary ran
is known by its sources only), a two-slot seed (`lab/run2/`: RunA
played, RunB a plain Archipelago client with DeathLink), both with the OS Install Disks goal (16,
3 required), DeathLink and the Landing Pod as start. Seen by the owner: the pause screen's disk
row from 0; received disks' popups with their count notices, the pause row and the file select's
DISK count following; this world's own two disks by the pod counted at their pickup (the client
attached); a death RunB sent dealt in the game (the client: "the received death was dealt."), and
the owner's own death reaching RunB ("RunA died in Zexion."), not dealt back; a received Scrap
Energy's drop near the player once settled; tall popups' text fine; at 3 disks and a save at the
pod, RETURN HOME WITH THE DISKS? offered, its YES starting the ending through the helper's
wrapper, the credits, MISSION COMPLETE and the Feats of Natu screen with ARCHIPELAGO SAVE: NO
FEATS EARNED, no Feat earned and no Steam achievement shown; the server: "RunA (Team #1) has
completed their goal." `GameUnlocks.sav` as the ending wrote it was byte for byte the backup taken
before the run (`cmp`; the title's fold of a run's Feats comes later, and an unlock already set
before could not show). Not run in the game yet: this world's own traps and disks counted with no
client, the scanner's line and the equipment screen's disk (not reached), the Steam Runtime
without Flatpak, a Steam Deck (a Steam Deck run came later: "Seen on a Steam Deck, and a Linux
PC's sync", below). What both runs saw, with the build and what was not seen, is
recorded in `docs/re/linux.md`, "Seen in the running game, 2026-11-19".

Both runs predate the socket's token, the client's keep-alive and let-go, and the review fixes
that followed: the put-back of whatever a connection set when it goes, `write` taking only the
ranges the client writes, the auth deadline, the token candidates tried in turn, the narrower map
cache, the Steam check's retry, the client's settings sent only on the configured connection, a
RETURN HOME prompt put back being offered again, and the look's gate on the seed the client put
(`offline_put`: Intel Chips, the ASSIST lock and the disks' count only in that seed's save, #162).
Run 3 (2026-11-20, develop at 5611378) checked the attach with the token, #162, the file select's
fixes (#164, #165), DeathLink's death count (#169) and the client's seed and labels (#170, #171)
in the game (`docs/re/linux.md`, "Seen in the running game, 2026-11-20"); the rest of the list has
run on stand-ins only.

**Seen on a Steam Deck, and a Linux PC's sync (2026-11-20 and 2026-11-21).** Two short runs the
owner reported (the Deck's helper is `develop` at 246c850, with the self-configure of #189 and the
seed copies of #187; the CachyOS PC's build was not reported). Recorded in `docs/re/linux.md`,
"Seen on a Steam Deck and a CachyOS PC, 2026-11-20 and 2026-11-21", with what was not reported:

- *Steam Deck, late on 2026-11-20 (local time).* Zexion's native Linux version installed on the SD
  card; the helper of 246c850, built by CI in the Steam Runtime (`libzexion_ap.so` SHA-256
  `92cc1b438ba154c66fc989929f6bddd1cb37378ca7db609236ca2d4165b84364`), copied into the game's
  folder by the owner. The absolute launch option the guide gave did not match the SD-card
  install; `LD_PRELOAD="./libzexion_ap.so:$LD_PRELOAD" %command%` "seems to work just fine": the
  mod loaded. That is all that was reported: the first run of #189's helper, and the first on a
  Deck. Not recorded: whether the title lines
  showed with no client, whether synced AP saves showed as AP saves there, the game's build id.
- *CachyOS PC, 2026-11-21.* A machine that had run an earlier version of the Linux helper. At
  first the save sync did not work: AP saves synced from Windows did not show as AP saves. The
  owner then "cleared some stuff out and it worked fine": AP saves synced through Steam Cloud
  showed as AP saves (the Linux half of "Seed files in the save folder", below). Not recorded:
  what was cleared, whether Steam is native or Flatpak there, which helper build ran, which
  saves. (A guess, not seen: that the earlier helper, which ignores the `ZexionAP-seed-*.sav`
  copies yet reports the same 1.2.0, was still the one loaded until it was replaced.)

**Running it in the game.** A build's first run, in this order. A step that changes the game,
its folder or a save is the owner's, asked first (`CLAUDE.md`, "Ask first"):

1. Build it on Linux with the Linux helper's CMake commands of `CLAUDE.md`, "Checks" ("The
   Linux helper, on Linux", with `.venv/bin/cmake`): `native/build/libzexion_ap.so`. It loads
   only under a glibc at least as new as the newest symbol version it asks for: `objdump -T
   native/build/libzexion_ap.so | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1` prints that
   version. Under Flatpak's Steam the game ran on the Flatpak runtime's glibc, 2.44 on the test
   PC (`org.freedesktop.Platform` 26.08, 2026-11-18; not the host's), and a build asking for up
   to `GLIBC_2.38` loaded; the runtime the game asks for has 2.31, which a newer host glibc may
   stand in for (assumed without Flatpak: "Its own runtime", below). A newer version than the
   game's: it does not load (step 4). A release's build, made in the Steam Runtime's SDK, asks for
   2.31 at most (`tools/build_apworld.py --check-linux-helper` checks a file).
2. The owner closes the game, then copies `libzexion_ap.so` into the game's folder (the guard
   hook blocks Claude's writes into the install); under Flatpak's Steam on the test PC,
   `~/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/Zexion`
   (`docs/re/builds.md`, "B1 on Linux"). Never over a copy a running game has mapped: the helper
   can't be unloaded, so each build takes a fresh launch. It must be of the client's version: a
   client of another version refuses it (the client says it can't reach the running game, and its
   log names both versions).
3. The owner sets the game's launch options in Steam, the helper first in `LD_PRELOAD` ("Getting
   in", below): the relative `./libzexion_ap.so` the setup guide gives, or, as the fallback, its
   full path in that folder (the same inside Steam's container).
4. Start the game and read its stderr (Steam passes it on as its own: start Steam from a
   terminal; assumed, not tried here). Each of the helper's lines begins `zexion-ap <version>: `:
   `serving build B1` once it is in; `not a known build of Zexion (build id <hex>)` for another
   build; `socket zexion-ap-<pid> not opened: <why>`, `no SDL_GL_SwapWindow after the helper's`
   or `the game's symbols were not found: no new games` for what failed, and `no token: <why>`
   (three kinds: the kernel's random bytes, no folder to put it in, the file not written),
   `memory reads refused: <why>` and `failed to start`, the last three stopping before `serving
   build` (the helper serves no one). Before it, `write: N ranges, M bytes allowed` (the client's
   writes the helper takes) and, for a range the game's layout leaves out, `write: 0x... is
   outside gGameState ...: the client's writes there are refused`. Later, with no client (a seed's
   file or not): `configured from the game's symbols, no client` (with `, no seed file taken yet`
   at its end when no seed file had been taken by the frame it configured on: on a machine no
   client has run on, or when the first frames come before the start's read of the files), or
   `not configured with no client: <why>`, and `seed files: <why>` for files it could not read. No `serving build` and no
   failure line: not loaded, turned off by a `zexion_ap.disabled` beside it, or the socket's
   creation or the serving thread's start failed (the two silent paths in `start`); glibc's loader says why it
   did not load (`ERROR: ld.so: object … from LD_PRELOAD cannot be preloaded (…): ignored.`,
   glibc's message, not seen here). Seen 2026-11-18 before the file was copied: no helper mapped
   in the game (stderr not read; `docs/re/linux.md`, "Read from the running game").
5. `.venv/bin/python tools/helper.py status`: the game found, its build checked, the socket the
   game's, the frame calls by call site, and `.rodata` read through the helper.
6. Asked first, the client from source, as the same user as the game (the helper serves only a
   client of its own user, `SO_PEERCRED`; the abstract socket is in the game's network namespace,
   which Flatpak's Steam shared with the host, 2026-11-18), against a seed with a Zexion slot
   generated and hosted with `.ap/` (`samples/` holds a YAML per mode):
   `.venv/bin/python -m client.zexion_client --nogui --verbose archipelago://NAME@localhost:38281`
   (`CLAUDE.md`, "The client"). New games and grants go into an empty lab slot (`/save-lab`;
   `save_lab.py install` is asked first too).
7. Then check the seeds' files the client put: the game's `$XDG_DATA_HOME/ZexionAP/seeds/`, else
   `~/.local/share/ZexionAP/seeds/`, which under Flatpak's Steam is its sandbox's home's:
   `~/.var/app/com.valvesoftware.Steam/.local/share/ZexionAP/seeds/` (seen there, 2026-11-20:
   "Seed files", below), `<seed>-<team>-<slot>.txt` (`client/offline.py`, `seed_file_name`), and
   the stderr line `configured from the game's symbols, no client` once the game runs with it and
   no client (which no longer waits for a seed's file: on a machine no client has run on, the line ends
   `, no seed file taken yet`). Only the helper writes them (`CLAUDE.md`, "Rules").

What to try first, each before the next:

1. `helper.py status` (step 5).
2. The client's attach: `Attached to Zexion.` in its log, and no "Can't reach the running
   game"; its step waits until the Linux helper is configured, or 30 s (`docs/DEVELOPING.md`,
   "Platforms").
3. A new game: ARCHIPELAGO on the game-mode panel, and the seed's placement (`newgame_status`;
   "New games", below).
4. The socket's token, with native Steam and with Flatpak Steam (with Flatpak Steam run in the
   game, 2026-11-20 (`docs/re/linux.md`, "Seen in the running game, 2026-11-20"); with native
   Steam not yet): the stderr line `serving build B1` and no `no token`; `helper.py status`
   printing the game, the helper and `memory: readable`; the line `write: 5 ranges, 29222 bytes
   allowed` with no line saying a range is outside `gGameState` (else the layout is not the one
   recorded, and the client's writes there are refused); `ls -l` of the folder holding `token`
   showing the file `-rw-------` and the folder `drwx------`; and another connection (`python3 -c`
   with a socket to `\0zexion-ap-<inner pid>` sending `read 0x400000 16`) refused with `error auth
   required`.
5. A grant with its popup, in a lab slot: an item received in play, its popup, sound and HUD
   ("Grants and popups", below).
6. Then the rest, each part's bullet below saying what it does.

The first failure it was feared to be: on Linux `menu_state` is the game's cursor mode
(`gCursorMode`), which the mouse may set to 1; set in play, `apply` would answer `refused not in
play` and popups wait. On the test PC it stays 0 in play, the game tracking the mouse only in
menus (2026-11-19: `docs/re/linux.md`, `menu_state`). On another setup, the check before any
grant: load a save, and with no menu open move the mouse, reading the state through the helper:
`tools/grant.py watch` prints `state: in play -> menu` if the mouse sets it. With no client
connected: the helper serves one at a time, so `grant.py` (`set` too) waits while the client is.

- **Getting in.** The game's launch options in Steam preload it. The setup guide gives the
  relative form, `LD_PRELOAD="./libzexion_ap.so:$LD_PRELOAD" %command%`, which finds the file
  in Zexion's folder wherever the Steam library is; it relies on Steam starting the game with
  its install folder as the working directory (inferred from the run that worked, not measured on
  its own). A Steam Deck with Zexion on the SD card ran it so, the mod loading (2026-11-20, late,
  local time; "Seen on a Steam Deck, and a Linux PC's sync", above). The absolute form,
  `LD_PRELOAD="/path/to/libzexion_ap.so:$LD_PRELOAD" %command%`, which the guide gives as the
  fallback (the folder's full path from the file manager's address bar), was the one run on
  Flatpak's Steam: the game's folder is at the same path inside Steam's runtime container as
  outside, and the container passed the preload on (seen with Flatpak's Steam, 2026-11-18). The
  Deck's SD-card install did not match the absolute path the guide gave then, which is why the
  relative form replaced it. The whole launch chain inherits the preload, and a program started
  in another working directory can't find `./libzexion_ap.so`: glibc's loader prints a warning for
  it and goes on (expected from glibc's behavior, not seen). Every program that does find it
  loads it, so it does
  nothing unless the program it is in is a known Linux build, told by the main program's GNU
  build id (`src/core.cpp`, `kKnownBuilds`, kept in step with `KNOWN_LINUX_BUILDS` in
  `client/patterns.py`; `tests/test_cross_checks.py` checks them). In a known build it writes
  `zexion-ap <version>: serving build <name>` to stderr; in a program named `Zexion` of another
  build, `zexion-ap <version>: not a known build of Zexion (build id <hex>)`, the line a player
  sees after a game update; elsewhere nothing. A preloaded library can't be unloaded: there is
  no `unload`, and no load into a running game. A `zexion_ap.disabled` beside the `.so` turns
  it off, as beside the proxy on Windows. It exports `SDL_GL_SwapWindow` and
  `SteamInternal_ContextInit` alone, the calls it stands in for (`linux/exports.map`): the C++
  runtime linked into it stays its own, never standing in for the game's `libstdc++` (a test
  reads the build's `.dynsym`).
- **The frame call.** It defines `SDL_GL_SwapWindow`, which the game calls through its PLT
  from two places (`docs/re/linux.md`, "The per-frame call": in the game the frame's swap was
  `SWHardwareEnd`'s, once a frame, and `UpdatePalette`'s swaps another window), so the preloaded one is the game's; it counts the call and passes it on to the next
  one, SDL's or another preloaded library's (Steam's overlay, say), found by
  `dlsym(RTLD_NEXT)` before the game starts, and looked for again if it was missing then (said
  on stderr). It returns the next one's result as an `int`: nothing for SDL2's callers, SDL3's
  `bool` for a program of SDL3 in the same launch. It counts the calls by call site too (the
  return address, in a lock-free table of eight; `callers`), which tells a frame's swap from
  another window's. In another program, or another build, it only passes the call on. Frame work
  runs one frame at a time, as the Windows helper's: a swap that a game function the work called
  makes again (a draw in a menu builder, say) finds the work running and is only passed on. If
  none of the first 1200 calls comes from the frame function (`SWHardwareEnd`) while the game's
  symbols give it, another library's `SDL_GL_SwapWindow` stands in front of the helper's, and it
  says so on stderr once, naming that library (`put libzexion_ap.so first in LD_PRELOAD`): every
  frame request would be refused, though `hello` says `hooked=1`. The exception is one level of
  wrapper: a call whose caller is called from `SWHardwareEnd` also counts as a frame call, so
  a library in front that calls the helper's from its own code does no harm. The caller's caller
  is found with the unwinder on every library's `.eh_frame` (a wrapper built without frame
  pointers keeps none), only for a call that did not come from the frame function itself, so
  frame work still runs once for a call at most; a stack the unwinder cannot read, or a
  wrapper called from anywhere but `SWHardwareEnd` (`UpdatePalette`'s swap, say), is only
  passed on. That the wrapper has `.eh_frame` (the unwinder's one source) is assumed, not
  checked: without it the walk ends and the call is only passed on, as with no wrapper handling.
  A wrapper that passes a swap on more than once for one of its own calls (a frame-duplicating
  layer) has frame work taken for one of them. The helper cannot tell the game's frames apart
  from the wrapper's calls: the game's `frame_clock` (`gGameState+0xB0`, `docs/re/linux.md`) is
  recorded, but stands still while a menu pauses the game, so it cannot key the work. So a call
  through a wrapper runs the work only if 100 microseconds (`kWrapperGapUs`) had passed, when it
  began, since the last call through a wrapper, of any kind, returned from the real swap. Every
  such call stamps its return, so a third or fourth call follows the second's return as the
  second followed the first's, however long the real swap waits for the refresh (it probably
  waits: inferred from the game's measured call rate, `docs/re/linux.md`, not seen). A backstop
  keeps a tight loop of calls (a game loop with nothing between its swaps), which is never that
  far apart, from stopping the work: it runs anyway once 50 ms have passed since the last run
  began. The premise, an assumption: the game's work for a frame, from one swap's return to the
  next call, takes longer than 100 microseconds. Two limits, both timing: a wrapper that does
  something slow between its repeated calls still has the work run for each (the frame-counted
  timers, the popup gap, file slots and Steam retry, then run fast); and two real frames closer
  than that (`loadState`'s swap, then the frame's) lose the second's work, which waits one
  frame. Neither touches a call from the frame function itself. Stand-in tested
  (`tests/test_native_linux.py`,
  `test_frame_work_runs_for_a_wrapper_the_frame_function_calls`, the tests for a wrapper that
  repeats its call two, three and four times, with the real swap returning at once and waiting,
  for a tight loop of calls, for one that calls once and loses no frame's work, and for one
  called from another function, with
  `native/linux/tests/swap_wrapper.cpp`, built with no frame pointer); no such library has been
  in front of the helper in the game. **Known limit:** `SWHardwareEnd` is also called from
  `loadState`, which is not traced (`docs/re/linux.md`, "The per-frame call"); the helper tells
  that call from the others only by the return address inside `SWHardwareEnd` (or, through a
  wrapper, the wrapper's caller's), which is the same, so frame work runs at that swap too. A
  grant that lands in the middle of a load can then be overwritten by the load's copy of the
  inventory, with its popup (and a trap riding on it) already out; the next poll gives the item
  again (the client's reconcile), so none is lost, and the extra popup is the cost. Telling a
  load apart needs `loadState`'s own state or a second return address (the stack's, with no
  frame pointers in the game), neither traced.
- **Start-up.** The constructor does its work inside a `try`: whatever it throws leaves the helper
  off (a line on stderr), never an aborted program. It reads one of its own constants through
  `process_vm_readv` first, and if the kernel or a sandbox's filter refuses, says
  `memory reads refused: <why>` and does not serve (grants would otherwise land with no
  popups and no labels). It starts the serving thread, then the copy thread (`copy_loop`: "Seed files in the save
  folder"), from the library's constructor, which runs ahead of the default-priority dynamic initializers of the helper's file and of the
  shared sources: every global with a constructor that the thread reads (the offline folder, the
  token, the save folder, the slots' seeds `g_saved`, the mailbox, `Game`, the seeds' lists and lock, and the memory layer's map, its lock and
  its program spans) is `Kept` (built in place, never destroyed) or has `init_priority(101)`, so
  it is built before the constructor sets it, whatever the compiler's library builds at load time.
  `tests/test_native_linux_init_order.py` reads `helper.cpp` and `game_mem_linux.cpp` and fails
  for any object in them that is none of constant by its type, `Kept` or `init_priority(101)`,
  and checks the Steam export's stand-in holder is `constinit`; the other shared sources'
  globals (`feats.cpp`'s others, `offline.cpp`) are read on the game's thread or by the export,
  by reading the code, not by the test.
- **The socket.** An abstract unix socket, `zexion-ap-<pid>`, `SOCK_SEQPACKET`: one request a
  message, one reply a message, one client at a time, as the Windows pipe. The next client's
  connection waits in the queue, unanswered (`client/linux_helper.py` says the helper is busy
  when its first request has no reply), and a client that sends nothing for 30 s is dropped (the
  client sends `ping` every 10 s while it is configured on the connection:
  `ticks.linux_keepalive_tick`, which runs apart from the game loop).
  The pid is the game's own, which inside Flatpak's Steam is not the host's (seen: 873 for
  the host's 33866): the client takes it from `NSpid`. An abstract socket
  has no file permissions, and any process can bind a name first: the helper serves only a
  client of its own user (`SO_PEERCRED`) and drops the others; the client talks only to a server
  that is the game's own process, of its user (`SO_PEERCRED` again: the pid as the client sees
  it), whose `hello` names the game's build. A connection is served nothing but `hello` until it
  has sent the helper's token ("Who may connect", below). A request over 64 KB drops the client.
  The helper refuses to start, saying so on stderr, if the name is taken. The serving thread catches every
  exception, so none reaches the game (`serve`,
  `read_offline_guarded`): one thrown before a request's work went to the game thread is answered
  `error exception` (nothing done, so the client tries again); one thrown after it, or while
  serving a request of its own (a `write`, say), drops that client, which reads it as a lost
  reply (maybe applied, never refused), and the next client is served (the Windows pipe thread,
  catching one, stops serving instead). The mailbox never unwinds with its work still posted: it
  takes it back, or waits for the frame running it. The globals that
  thread uses (the mailbox, the `Game`, the seed files read and the seeds put, the offline folder)
  are `Kept<T>`: built before the helper's constructor and never destroyed, so the game's exit
  does not destroy them under it. Requests:

  | Request | Reply |
  |---|---|
  | `hello` | `zexion-ap <version> build=<B1\|unknown> hooked=<0\|1> platform=linux folder=<0\|1>` (`folder=1`: the `.so` is in the game binary's own folder) |
  | `auth TOKEN` (first, after `hello` if the client likes) | `ok`, or `error auth refused` and the connection closed; every request below is `error auth required`, and the connection closed, before it |
  | `ping` | `pong frames=<calls since load>` |
  | `callers` | `callers` and `0x<return address>=<calls>` for each call site of the frame call, lowest first (eight at most; ` other=<calls>` for the calls from sites past that) |
  | `read ADDRESS SIZE` (hex after `0x`, or decimal; 1 to 32768 bytes) | `data ` and the bytes, or `error <why>` |
  | `write ADDRESS HEX` (1 to 16384 bytes, as hex) | `written <bytes>`, or `error <why>` (`error not_in_the_game's_writable_data` outside it, `error not_in_what_the_client_writes` inside it, outside the ranges the client writes ("Reading and writing the game")) |
  | `config name=ADDRESS …` (frame) | `ok`, or `error <why>`: the 90 addresses of the parts it runs (`linux/src/helper.cpp`, `kConfigKeys`), each the one the helper finds itself from the game's own symbols (read from `/proc/self/exe` at start-up), each data one inside the game's `.bss` at its full size (the inventory with the player state before it) and each function in its code, all or none |
  | `ap_newgame` and its lines (frame) | `armed`, `refused not configured`, `refused no frame`, `error <why>`: as the Windows helper's ("New games") |
  | `apply` and its lines (frame) | as the Windows helper's ("The pipe", "Grants"), traps among the popups ("Traps") |
  | `ap_labels` and its lines (frame) | as the Windows helper's ("The pipe", "Other players' items") |
  | `offline_put seed NAME` and the seed's file | `stored`, `unchanged`, or `error <why>`: the seed parsed and its file written as the Windows helper's ("AP saves without the client"), into the helper's folder ("Seed files", below), and its copy into the game's save folder when a save uses the seed ("Seed files in the save folder"; a copy that fails is said on stderr, not in the reply), and the seed the running helper's from the next frame call whether kept or not (`error no folder`, `error too large`), for this world's own traps ("Traps") and own disks ("This world's own disks, counted at their pickup", below); `offline_put config` is `error not on the Linux build` |
  | `trap_status` (frame) | as the Windows helper's ("Traps") |
  | `discs` and `discs_status` (frame) | as the Windows helper's ("The pipe", "The OS Install Disks goal") |
  | `drop_status` (frame) | as the Windows helper's ("The pipe", "Received scrap") |
  | `shield` (frame) | as the Windows helper's ("The pipe", "Received Energy Shields") |
  | `die` (frame) | as the Windows helper's ("The pipe", "DeathLink") |
  | `other_save`, `client_back` (frame) | as the Windows helper's ("The pipe", "AP saves without the client") |
  | `feats_status` (frame) | the Feats guard as the Windows helper's `hello` gives it (`feats=`): `off`, `on`, or `on:` and why a change could not be made |
  | `title_status` (frame) | the title's lines as the Windows helper's `hello` gives them (`title=`): `1` shown, `0` not, or what failed |
  | `intel on`, `intel off` (frame) | as the Windows helper's ("The pipe", "Intel Chips") |
  | `assists lock`, `assists free` (frame) | as the Windows helper's ("The pipe", "The assist menu") |
  | `mission_prompt COLUMN ROW`, `mission_status`, `mission_black` (frame) | as the Windows helper's ("The pipe", "The OS Install Disks goal") |
  | `lang` (frame) | `lang <name>`: the language the game shows, spaces as underscores (the Windows helper's `hello`'s `lang`); `lang ?` unconfigured or with no name readable |
  | `newgame_status`, `disarm` (frame) | as the Windows helper's |

  Anything else gets `error unknown request`.
- **Who may connect.** The socket is reachable by every process of the user in the host's
  network namespace, a Flatpak app with network access too (`docs/re/linux.md`, "Read from the
  running game": the game under Flatpak's Steam shared the client's network namespace), and
  through `read` and `write` a client has the game's memory. `SO_PEERCRED`'s user check keeps
  other users out and nothing else, so the helper serves a connection only once it has shown a
  **token**:
  - The helper makes 32 random bytes from the kernel in its constructor, before it listens, and
    writes them as 64 hex digits into `token` of its data folder, `ZexionAP` in `$XDG_DATA_HOME`,
    else `~/.local/share`, the folder of the seed files ("Seed files", below), made with mode 0700
    if it is not there: a new file (mode 0600, never through a link) renamed over the last run's.
    The folder is the game's own view of it, so under Flatpak's Steam it is the sandbox's
    (`~/.var/app/com.valvesoftware.Steam/...` on the host), which another Flatpak app can't see
    unless it was given the user's home, and an app that was can read the saves and the game's
    files anyway. With no absolute data folder, or if the token can't be written, the helper says
    so on stderr (`no token`) and serves no one: it passes the frame call on as in another
    program.
  - The first request of a connection is `hello` (answered: the build and the version, so a
    client can tell a helper of another version from a wrong token) or `auth TOKEN`; anything
    else, and a token that is not the helper's, is answered `error auth required` or
    `error auth refused` and the connection is closed. `hello` is answered once: a second before
    the token closes the connection too. The token must come within 5 s of the connection being
    accepted, one deadline whatever is sent meanwhile (after the token, the 30 s of silence
    above). The comparison does not stop at the first byte that differs. The serving thread serves
    one connection at a time, so a process that connects, says nothing and connects again holds it
    up to 5 s at a turn, and the client's connection waits behind it (its first request then goes
    unanswered, and it says the helper is busy, as it does behind another client): a nuisance, not
    a way in.
  - The client (`client/linux_helper.py`, `find_tokens`) reads the token after `hello` has shown the
    server is the game's own process of its user and the right build and version, and sends it to
    that process only. It looks in the game's own view first: `/proc/<pid>/root` and the game's
    `XDG_DATA_HOME` or `HOME` from `/proc/<pid>/environ` (a process of the same user may follow
    both, as it reads `/proc/<pid>/exe`), which is the sandbox's folder under Flatpak's Steam;
    then the host's own `$XDG_DATA_HOME` or `~/.local/share`, and Flatpak Steam's
    `~/.var/app/com.valvesoftware.Steam/.local/share` and `.../data`. It uses only a plain file
    (not a link) that no one but its user can read, holding exactly 64 lower-case hex digits. It
    tries each token it finds, the most recently written first (the helper writes its own when the
    game starts, so a stale one, from an earlier run in another data folder, is older), on a
    connection of its own, since the helper closes one that shows a wrong token; when none is
    taken, its error names the files the helper refused, or, with none found, the places it looked.
  - A connection that set something is put back when it goes (`sets_client_state`: `config`,
    `ap_newgame`, `ap_labels`, `discs`, `mission_prompt`, `other_save`, `client_back`, `assists`,
    `intel` and `offline_put`, which a client sends after its `config`, but a connection made again
    in the middle of a poll may send before it): the helper's plan, labels, Intel Chips, ASSIST
    lock and disk count, the client's seed (`forget_client_seed`), and the seed files read
    again.
    It is put back at the next frame call or before the next request's work on the game thread
    (`client_gone_if_left`), whichever comes first, and the serving thread marks the client gone
    before it takes the next one: so a client that comes back at once (a request with no reply
    ends a connection and the client makes another) never has its `config` followed by the last
    one's clean-up. A connection that set nothing (`tools/helper.py status`, a probe: `hello`,
    `ping`, `callers`, `read`, the `*_status` requests, `lang`) leaves nothing to put back, and
    its closing changes nothing in the game or in the look of an AP save with no client.
  - The handshake through `/proc/<pid>/root` with Flatpak's Steam run in the game, 2026-11-20
    (`docs/re/linux.md`, "Seen in the running game, 2026-11-20"): `helper.py status` and the
    client reached the game with it. The refusals and the file's modes are stand-in tested only.
- **Reading and writing the game.** The helper reads the game's memory itself, for `read`, with
  `process_vm_readv` on its own pid: the kernel copies it, so an address not mapped is an
  error (`error Bad_address`), not a fault. It writes it for `write` with `process_vm_writev`,
  and only inside the zero-filled part of the game binary's writable segments (its `.bss`: from
  `p_filesz` to `p_memsz` of each `PT_LOAD` with `PF_W`, read at start-up), where `gGameState`
  and the other state the client writes live: never its code, its constants, its initialized
  data (`.data`'s function pointers) or its import slots (`.got.plt`, writable in a binary
  without `BIND_NOW`, as B1's is), nor a library's memory, and within that `.bss` only the
  ranges the client writes (`client_write_ranges`, `src/core.cpp`: offsets from `gGameState`; the
  player's inventory bytes 0 to 49 and its six supply numbers, the map cells' whole 9-byte records
  (54 x 54 of them, 26244 bytes) and the map grid; `tests/test_linux_helper.py` checks them against `client/game.py`'s own), any other
  `error not_in_what_the_client_writes`. At start-up it says on stderr how many ranges it keeps and,
  for each one of them that is outside `gGameState`'s recorded extent or the game's data (the game
  not as recorded), which: `write: 5 ranges, 29222 bytes allowed`, `write: 0x... is outside
  gGameState ...: the client's writes there are refused`. The game calls procedures through pointers all over its
  `.bss` (`gExitActionP` and its table, the move, area-event, enemy-action and sprite-draw
  tables, each sprite record's move procedure at `+0x168`), so a list of those to leave alone
  could not be known whole; this one keeps a client, or whoever got past its token, from turning
  a write into a call except through what the player's record's supply and inventory fields
  hold (numbers, not shown to hold anything else). The helper's own parts write the game
  directly and are not asked. The client's
  `game.Game` reads and writes through these (`Game.attach_linux`, `client/linux_helper.py`'s
  `LinuxProcess`), reading back every write as on Windows. It never opens the game's memory
  to another process through the kernel (no `prctl(PR_SET_PTRACER)`), whatever the kernel's ptrace
  scope: the client reads only through it, and the helper serves `read` and `write` to a client
  that has the token alone ("Who may connect"; `docs/re/linux.md`, "Reading the running game").
  Stand-in tests: a separate process
  is refused the stand-in game's memory with a client connected or not (on the test PC, scope 1,
  2026-11-18), and reads through the helper return the stand-in's bytes, one piece or two;
  `game.Game` writes and reads back through it, a write to the stand-in's `.rodata` or `.data`
  is refused and leaves it as it was, a dropped connection is made again, and the game's exit
  is noticed (by its pid's start time).
- **New games.** The Windows helper's new-game logic (`src/newgame.cpp`: the plan, the seed's
  placement over the game's, the RANDOMIZER route) runs here unchanged once a `config` gave its
  addresses (with the rest of the 90), which the client resolves from the game's symbols
  (`client/linux_targets.py`, `helper_config`) and the helper checks against its own reading of
  them, so a client and a helper that disagree change nothing (the client also refuses a helper of
  another version, and one of this version built before the traps, which answers `trap_status`
  with `error unknown request`: it would refuse every batch with a trap's popup; any other reply
  but `queued …`, such as `refused no frame`, is a configuration tried again). "Frame" requests
  and the watch run on the game's thread through `src/mailbox.h` (`src/mailbox_linux.cpp`), only
  at `SWHardwareEnd`'s swap, by its return address (not the palette window's: `docs/re/linux.md`,
  "The per-frame call"), with nothing they throw reaching the game. As on Windows, a client that
  goes takes its plan with it: the helper disarms at the next frame, and puts back the choice's
  hooks in an open game-mode panel (`Game::put_back_client_menus`, which `put_back_game` calls on
  Windows). The ARCHIPELAGO choice on the game-mode panel is `src/choice.cpp`'s, shared too: the
  same relabel, panel hook and callback (the game calls a menu callback with no arguments on
  Linux, and the helper's reads none), at the frame call, in a translation too ("Translations",
  below). The choice is tested on Windows' stand-in menu (`tests/menu_tests.cpp`,
  `game_tests.cpp`); on Linux only built, and configured through the socket. The shared sources
  build on Linux through `src/platform.h` and the Linux parts of the memory layer,
  `src/game_mem_linux.cpp`: fault-safe reads through the kernel, writability from the process's
  memory map (kept 250 ms between reads, and only for a range in the program's own segments or
  `[heap]`: below), the game's code from
  its program headers, code patches with `mprotect`, the page's own protection put back. `Game`
  and every part it holds build there too, the Windows-only pieces of those left out:
  `Game::configure` takes the helper's checked addresses instead of resolving a client's, the
  offline cache reads and writes the seeds' files alone, with no config file (`src/offline.cpp`;
  "Seed files", below); the Feats guard's Steam stand-in runs as an export and its ending's patch
  over the Linux ending's own call ("Feats and Steam achievements in AP saves", below). Tested on
  stand-ins (`linux/tests/newgame_tests.cpp`, and through the socket in
  `tests/test_native_linux.py`); a seed's new game from ARCHIPELAGO run in the game, 2026-11-19
  ("The Linux helper", above). Not known: whether an exception the game's placement threw would be
  caught here (the helper's own C++ runtime is linked in, the game's is its own: "refused the
  placement failed" is unverified); a fault there is the game's crash, as on Windows.
- **Grants and popups.** `Game::apply` and `Game::pump_popups` run here as on Windows
  ("Grants", "Popups"): the `cas` writes all or none, the counts and the HUD refreshed (the
  number display's third argument, `true`, as the game passes it: `docs/re/linux.md`, "Mapped
  by disassembly: HUD, items, sound, popups, deaths"), Magnus Armor's suit color, then the
  popups one a frame call apart as there, each with its item's sound, waiting for their own save.
  What differs: `menu_state` is the cursor mode on Linux, which the mouse can set to 1, but
  in play it read 0 with the mouse used (2026-11-19: `docs/re/linux.md`, `menu_state`); if it
  read 1 in play, an `apply` would be refused (`refused not in play`) and popups wait.

  A client that goes takes its plan with it, but its popups stay queued for their save, as on
  Windows. Tested on the stand-in game through the socket (`tests/test_native_linux.py`: an
  `apply` refused unconfigured, by a `cas` that differs, and out of play; one taken writes the
  byte, and its popup, at a later frame call, carries the record's name and the suffix and plays
  the record's sound); run in the game, 2026-11-19 ("The Linux helper", above), but its sound and
  the HUD not checked.
- **Other players' items.** `Game::watch_ap_items` runs at each frame call as on Windows ("Other
  players' items", "Other Zexion players' items, as themselves"): the stand-ins' records named
  after the labels and restyled, the logo painted into the sprite texture, the pickup popup as
  wide as the label, the stand-ins' own colors, other Zexion players' items dressed as
  themselves, R'Talva X34 Y51's marker and a reversed-gravity room's collected item hidden, and
  this player's own Scrap Metal picked up as the game's, on the same object and sprite fields
  (`docs/re/linux.md`, "Mapped by disassembly: item looks"). What differs on Linux:
  - the popup's width is the immediate of `mov esi, 24` (`BE`), not `mov edx, 24` (`BA`):
    `Game::set_popup_width` checks the opcode of the build it is built for;
  - OpenGL's functions come from the game's own `libGL.so.1`, found with `dlsym` (`load_gl`;
    `docs/re/linux.md`, "Mapped by disassembly: item looks": four of them assumed). As the
    painting is not proven there, the records are restyled only while the logos are in the
    texture the game draws from (`ApItems::logo_shown`): with none painted (an OpenGL function
    missing, the texture not passing its check) the stand-ins stay the game's Scrap Energy and
    Scrap Chunk, named and drawn as the game has them, rather than drawing their empty frames;
  - the writability checks before the records' writes read `/proc/self/maps`, which the memory
    layer keeps for 250 ms between reads, only for a range that lies wholly in the game
    binary's own segments or `[heap]` (whose protection does not change under the game) and is
    still mapped (`mincore`), and drops after its own `mprotect` (`src/game_mem_linux.cpp`): any
    other range (a large allocation's own mapping, a stack, a library) is read afresh each
    time, as Windows' `VirtualQuery` asks, since another mapping may take a freed one's
    addresses with another protection within the 250 ms, and the write after the check is a
    plain store (a window of microseconds is left, as on Windows);
  - a client that goes takes its labels with it, the records, the width and the pixels put back at
    the next frame call; the look without the client (below) then dresses a known seed's save
    again with its seed file's labels. The client sends them again on a new connection (a request
    with no reply ends one: `LinuxMod.current`).

  Tested on the stand-in game through the socket (`tests/test_native_linux.py`: `ap_labels`
  refused unconfigured, then a label for the player's cell names Scrap Energy's record after it,
  the stand-in's `mov esi, 24` is patched to the label's width, and both are put back once the
  client goes, the logos painted first through the stand-in's OpenGL; with its texture gone,
  the stand-ins left as the game's; and in `linux/tests/newgame_tests.cpp`, a range unmapped
  since the last map read is not writable). Not run in the game.
- **The map.** `Game::watch_map_names` and `watch_map_marks` run at each frame call as on
  Windows ("The map"): in the labels' seed's save the squares of item 48 raised to a major's,
  the item update's on-screen write patched to keep them so, and, while the map is open, the
  cursor's name written over SCRAP ENERGY or SCRAP CHUNK by a wrapper on the map's move
  procedure, put back in an open map when a client goes (`Game::put_back_client_menus`, as
  `put_back_game` does on Windows). What differs on Linux:
  - the on-screen write's patch: the Linux build tests the item's ID twice in
    `PowerUpMoveProc`, once for each base of the square (`0x53DB6C` and `0x53DC7E`), so it is
    two patches (`src/mapmarks.h`, `SeenMarker`), each rewriting one 23-byte test as "48, or 3
    to 35" (`cmp ax, 0x30; je`, `sub eax, 3; cmp ax, 0x20; ja`, a nop) where it reads as B1's,
    each held and put back on its own; the code between them is never written;
  - the wrapper is called with the map's child alone, as the game calls its move procedures
    (`map_hook`, `linux/src/helper.cpp`).

  Tested: `linux/tests/patch_tests.cpp` runs the game's two tests and the patched ones side by
  side for every ID from 0 to 255 (only 48 changes, from the minor's write to the major's); and
  on the stand-in game through the socket, the stand-in's copies of the two tests patched once
  the labels' seed's save has been loaded for two looks, put back once the client goes. The
  cursor's names: built only (the stand-in has no map). Not run in the game.
- **Translations.** `Game::watch_translation` runs at each frame call, and the popups, names and
  labels above take the translation's font as on Windows ("Translations"): popups in SmallText 66
  to 72, the stand-ins' labels in ItemNames 47 and 48, the Area Map's as Intel Chip in ItemNames
  49 while Intel Chips are on, ARCHIPELAGO and the map cursor's name built again (the game's
  `MapMoveProc` builds its own translated name first, as Windows': `docs/re/linux.md`, "Mapped
  by disassembly: the helper's translated text"), the helper's
  icons in the icon sheets (painted through the same unproven OpenGL path as the logos, into
  definitions 101 and 104, the game's count of definitions plus 5 and 8: the helper reads that
  count, `gSpriteDefCount`, at each `config` and paints no icons unless it is 96,
  `Game::hold_icon_sheets`); and the client asks the language
  (`lang`) to warn in any but English(HD), as from the Windows helper's `hello`. What differs on
  Linux (`docs/re/linux.md`, "Mapped by disassembly: translations"):
  - the tables hold 4-byte characters, which is `wchar_t` in the helper's GCC build, so the same
    code reads and writes them; the translation object is `0x40150` bytes (`tr::kSize`);
  - the font's glyph records are `0x40` bytes, their two fields 64-bit at `+0x10` and `+0x20`
    (FreeType's `long`s), so `FontMetrics` reads that layout for the popups' wrapping and
    centering (`docs/re/linux.md`, "Mapped by disassembly: the helper's translated text");
  - the label builder, `generateText`, takes the caller's `std::wstring` by reference and the
    caller destroys it: the helper builds its own (libstdc++'s, as the game's, on the same heap)
    and passes it, with no `wstring_new_fn` (`build`, `src/translation.cpp`);
  - the message call, `createTranslationGameMessage`, reads the ints the game passes with a
    popup, so the helper passes the pickup's (24, 160, 88, 100, 1, 180), not zeros
    (`PopupAreas::show`);
  - a client that goes takes the helper's translation changes with it, as on Windows
    (`Game::restore_translation`: the names in the tables, entry 70's offset once the popup
    areas are free, the icons);
  - the Feats of Natu screen's title with the note builds its text through the shared code too
    (`large_count` configured; `docs/re/linux.md`, "Mapped by disassembly: the Feats of Natu
    screen's note"); RETURN HOME's question is built and centered in a translation through the
    shared code (`docs/re/linux.md`, "Mapped by disassembly: the RETURN HOME prompt"), the locked
    ASSIST entry's text built again with the HD padlock ("Mapped by disassembly: the ASSIST
    lock"), and the title lines' translated text ("Mapped by disassembly: the title screen's
    lines"), none run in the game; the equipment screen's disk name and description and the pause
    row's count, in a translation, go through the same code as on Windows, the entries they read
    (SmallText 139, ItemNames 44, LargeText 87) and the game's rebuilds of them assumed as
    `docs/re/memory.md` has them, not mapped on Linux;
  - where a rebuilt label goes, the font's other fields and the icons' characters are as on
    Windows (measured by disassembly: the same section); that the game reads none of SmallText
    66 to 72 is assumed for its data-driven entries, as on Windows.

  Tested: `linux/tests/translation_tests.cpp` builds a label through a stand-in builder of
  `generateText`'s signature (the helper's string, longer than libstdc++ holds in place, reaches
  it whole) and reads a glyph's advance from a record of the Linux layout; on the stand-in game
  through the socket, a received popup in a translation goes into SmallText 66 or 70, its message
  given it with the pickup's ints and the translation's name for the item, the entry holding the
  text; and one that cannot go there (the run not as traced) shows the pixel text, its slot's mark
  cleared. Popups in English(HD) and Japanese run in the game, 2026-11-19 ("The Linux helper",
  above); the rest not.
- **The OS Install Disks' count.** `Game::watch_discs` and `watch_equipment` run at each frame
  call as on Windows ("The OS Install Disks goal"), from the client's `discs`: the scanner's
  message as the client's line, the disk glyph (for a received disk's popup too), the equipment
  screen's disk widget and the pause screen's disk row. What differs on Linux
  (`docs/re/linux.md`, "Mapped by disassembly: the disks' count"):
  - the KEYS row's branch is `jne +0x3C` (`75 3C`), made `EB 3C` (`src/pause.cpp`);
  - the equipment screen's new child array is glibc's `malloc`, freed with glibc's `free`, as the
    game's own `calloc` and `free` (`src/equip.cpp`): no allocator slots;
  - EQUIPMENT's callback is called with no arguments (`equipment_hook`); the Energy Tank panel's
    handler takes `(widget, event)` and the pause menu's way back its menu, as on Windows
    (`equipment_panel_hook`, `options_back_hook`, `linux/src/helper.cpp`);
  - a client that goes turns the count off and puts its changes back (`set_discs`, then
    `restore_discs`), as `drop_save_look` does on Windows; the look without the client (below)
    then sets its seed file's count again for a known seed's save;
  - the count shows only in the loaded save of the look's own seed (`Game::seed_look_loaded`, as
    on Windows: "Intel Chips"), which on Linux is the seed the client put with `offline_put`
    (`take_offline` notes the last one put, on the game thread, as `note_client_seed`; the
    client-gone work forgets it, `forget_client_seed`, for any connection that set something,
    `sets_client_state`, offline_put among them; one that only looks leaves it alone). Until
    the client has put its seed, which on Linux is from the connection's first poll
    (`ticks.linux_seed_tick`), the count is hidden in the seed's own save too.

  Tested on the stand-in game through the socket (`tests/test_native_linux.py`, in the client's
  seed's save: `put_clients_seed`): `discs` writes
  the scanner's Text entry as the client's line in capitals and makes the stand-in's KEYS branch a
  `jmp`, both put back once the client goes. The equipment screen and the pause row: built only
  (the stand-in has no menus), and in a translation assumed ("Translations", above). The pause row
  and the count notices run in the game, 2026-11-19 ("The Linux helper", above: run 2), which
  predates the gate on the client's seed; the gate ran in run 3, 2026-11-20 (another save: no disk
  row on the pause screen, `docs/re/linux.md`, "Seen in the running game, 2026-11-20"); the
  scanner's line and the equipment screen not reached.
- **This world's own disks, counted at their pickup.** `Game::watch_own_disks` runs at each frame
  call as on Windows ("The OS Install Disks goal", "The count follows the save"): in a known disk
  seed's save, an own disk's cell turning collected after two frames in play queues "OS Install
  Disk N" ("of M" once the scanner has told) with the disk's pickup sound, only with no client
  attached; with one, its follow-up shows the count, and the cells are watched all the same, so a
  disk picked up with a client is no pickup once it goes. The seed's own disks' cells and the
  count every save shares come with the seed's text the client hands over (`offline_put seed`,
  `ticks.linux_seed` once the items are listed, then the whole seed file: `offline_seed`), which
  the helper keeps in its seed file ("Seed files", below): so it counts in any later run of the
  game too, with no client at all (the helper configured from the game's own symbols), once a
  client has put the seed with the items listed; a disk picked up before the first such put is not
  counted, and nothing makes it up later. A client that goes leaves the helper with none attached
  (`Game::client_left`, which `client_gone` does on Windows), and so does any connection that
  closes (a request with no reply), until the next `config`: so a client configuring on another
  connection than before takes the disks the save holds for counted at its next look
  (`ticks.linux_newgame_tick`, as `attach_helper` does on Windows), and gives no follow-up through
  a connection not configured yet (`linux_finds_tick`), so one pickup shows one count; one picked
  up with the client attached just before its connection closed, its follow-up not run yet, shows
  none, as on Windows. Tested on the stand-in game through the socket
  (`tests/test_native_linux.py`: a pickup with a client queues nothing, one after it has gone
  queues the count) and with stand-ins for the client (`tests/test_zexion_client.py`); with a
  client attached run in the game, 2026-11-19 ("The Linux helper", above: run 2), with none not
  yet.
- **RETURN HOME WITH THE DISKS?** `Game::watch_mission` runs at each frame call and
  `mission_prompt` is taken as on Windows ("The OS Install Disks goal"): the prompt opened as the
  escape skip's after event 3667's fade, its YES keeping the screen black until the ending
  (through the helper's ending wrapper), its NO fading back in, a request lapsing away from home;
  and while it is on its way or its ending runs, `apply` is refused and popups and traps wait, as
  there. What differs on Linux (`docs/re/linux.md`, "Mapped by disassembly: the RETURN HOME
  prompt": the fade block, event 3667, the prompt, the ending, the pause and the frame clock as
  on Windows):
  - YES is a menu callback, which the game calls with no arguments, so the helper's
    (`mission_yes_hook`) takes none and calls the game's NO with none; the opener and the
    ending's wrapper take none on either build;
  - the prompt checks that event 3667's handler is in the game's code through a handle that
    stands for the main binary (`main_image`, `src/game_mem_linux.cpp`);
  - a client that goes puts back what the prompt holds, as `put_back_game` does on Windows
    (`Game::put_back_client_menus`, before the disks' count goes off): a prompt on its way is
    canceled (the screen faded back in if it was kept black), an ending its YES set going is left
    to the game's own routine (it still comes, without the fade in), and an open prompt's YES is
    no longer the helper's. The Windows helper then asks for a prompt on its way again; the Linux
    helper does not, and the look without the client (below) does not ask for it either. A Linux
    client's connection closes with any request that
    gets no reply, so the client follows a prompt it queued while it is on its way
    (`mission_status`, `ticks.linux_finds_tick`) while it is on its way (`asked`, `opening…`) and
    after a YES until the ending has started (`yes: ending when idle…`): one the cancel dropped
    (`not asked`), or left up with YES put back to NO's callback (`put back: …`, which
    `MissionPrompt::restore` sets only for a prompt still open, so a NO the player pressed, which
    leaves `shown`, is not taken for it; or `put back: YES was pressed…`, which `cancel` sets
    for a YES whose ending had not started, so one the client had read only as `shown` is not
    taken for a NO), is offered again at once. A prompt read as `shown` is
    asked about once more on each new connection (`ReturnHome.shown_on`), not every poll, and
    then reading `not asked` (a NO closed it, the cancel reset the status) is not offered again;
    one lapsed or with its ending started is not followed any more, so a NO is never undone;
  - the frame call also comes from a swap inside the game's `loadState()` (`docs/re/linux.md`,
    "The per-frame call"), so `watch_mission` may run mid-load, as the grants, popups, drops and
    traps may: by the code the prompt gives up an opener or a fade another transition took
    (`taken_over`, `fade_still_ours`); not measured.

  The client's side runs on Linux too: the finds' follow-ups (the found disks' counts, the Area
  Maps' hints) and the Landing Pod's reminder and prompt (`ticks.linux_finds_tick`), and the
  ending again on a save past it (`linux_replay_tick`). Tested on the stand-in game through the
  socket: `mission_prompt` taken once configured, lapsing away from home's cell, waiting at home
  (the stand-in has no event 3667 or prompt to open), and dropped when `discs` turns off. The
  prompt itself: stand-in tested on Windows only (`tests/mission_tests.cpp`); the prompt at the
  pod and YES's ending run in the game, 2026-11-19 ("The Linux helper", above: run 2).
- **Received scrap's drops.** `Game::pump_drops` runs at each frame call, after the popups, as on
  Windows ("Received scrap"): a received Scrap Metal's, Scrap Energy's or Scrap Chunk's drop is
  spawned once its popup has shown, the player in play in one cell for a second in the save the
  popup was for, through the game's type-11 handler with the item's event, each checked first, and
  not through an ending or RETURN HOME's. Nothing the drops rely on differs on Linux but the
  handler's arguments, SysV's registers (`docs/re/linux.md`, "Mapped by disassembly: received
  scrap's drops", which also records the spawn's and the runaway check's insides; what clears flag
  211 as the ending begins, which the wait through an ending rests on, is not traced there).
  Tested on the stand-in game through the socket (`tests/test_native_linux.py`: Scrap Metal's,
  Scrap Energy's and Scrap Chunk's spawns called with the player's cell and their events, a notice
  spawning nothing); a received Scrap Energy's drop run in the game, 2026-11-19 ("The Linux
  helper", above: run 2).
- **Received Energy Shields.** `shield` runs `Game::spawn_shield` as on Windows ("Received Energy
  Shields"): event 3553 through the game's type-11 handler, as the drops' (above), once the player
  has been in play in one cell for a second, not through RETURN HOME's ending. The client asks for
  one (`ticks.shields_tick`, through `LinuxMod` when no Windows helper is there), then reads it up
  through the helper's reads as on Windows: the player's link and the shield's own handle
  (`docs/re/linux.md`, "Mapped by disassembly: received Energy Shields"). A request with no reply
  leaves it looked for, as a reply lost on Windows (one that failed in making the connection again
  never went out: not looked for); the Linux helper is dropped (`linux_mod_lost`) either way. A
  Linux helper of this version built before shields answers `error unknown request`: said once,
  and received shields wait for a newer one. Tested on the stand-in game through the socket
  (`tests/test_native_linux.py`: `shield` refused unconfigured and until settled, then event 3553
  run with the player's cell; a received one's popup spawning nothing) and with stand-ins for the
  client (`tests/test_ticks.py`, the poll's gate in `test_zexion_client.py`); run in the game,
  2026-11-19 ("The Linux helper", above).
- **DeathLink's deaths received.** `die` runs `Game::kill` as on Windows ("DeathLink"): once the
  player has been in play in one cell for a second, not while the game is halted (`halt_flag`,
  `gEditorMode`), a message shows, or RETURN HOME's fade, black or ending runs, the player's
  energy set to 0 and `PlayerDestruction(player, 0)` called, as the game's damage code does
  (`death_fn`, `docs/re/linux.md`: its kind-0 callers, the halt byte, and the death's popup
  written through unchecked when every message slot is busy, which the message check keeps from).
  The client deals the queued ones through `LinuxMod` when no Windows helper is there
  (`ticks.deathlink_tick`): a request with no reply leaves it as being dealt, and no other is
  asked for until it is settled (`DeathLink.watching`): a death seen within `DEALT_SECONDS` of the
  first in-play or dead state read after the reply was lost is it, as a reply lost on Windows,
  but counted from that read, not from the loss (the next poll first configures the helper
  again, which can take longer than the window, and the socket's timeout is itself longer), and
  so is one the save's death count shows (`discs.deaths`: it went up since the ask, the player
  having respawned before the read, at any delay; two or more, read while the player is dead:
  their own death besides, which is sent, and so is a further rise while they stay dead). The
  count is Windows B1's by disassembly (a death writes it into the slot's block at its end,
  before the respawn: "A save at the Landing Pod" in `docs/re/memory.md`, not seen live).
  **On the Linux build that the record's field is written so, and when, is assumed and
  untraced** (`docs/re/linux.md` records the live counter `+0xAD4` only), and so is that one
  death with its respawn adds exactly one (on Windows B1 `docs/re/memory.md` records that
  `0xA2E50` adds one, at `0xA2EBB`, and not traced is whether it runs after the kind-0 death that
  `die` deals; the respawn routine `0x9F3B0` is not traced for an add). **If either is wrong,
  the lost reply's death can be echoed to the other players, or dealt twice**, as it could
  before the count was used. On Windows B1 RELOAD FROM CHECKPOINT (not RELOAD FROM SAVE) and the
  player's own death raise the count too, with these limits, all rare and with no cheap fix:
  - one in the gap is taken for the dealt one, so a death the helper never got can go undealt;
  - a rise of two read in play is not sent, whether it is a dealt death and a reload, or a dealt
    death and a death of the player's own that they already respawned from (that death is not
    sent);
  - a dealt death, then a RELOAD FROM CHECKPOINT, then the player's own death read during its
    animation, sends two deaths for one (the first rise is settled as the dealt death, the second
    is taken for the player's own, though the reload caused it).

  With no count to go by, a first read more than `LOST_SECONDS` after the loss leaves it queued,
  as no death seen is told from the player's own by then. One that never went out stays queued,
  and a Linux helper of this version from before deaths is said once, the deaths waiting for a
  newer one; the Linux helper is dropped (`linux_mod_lost`) either way. Tested on the stand-in
  game through the socket (`tests/test_native_linux.py`: `die` refused unconfigured and halted,
  then energy 0 and the death routine with the player and kind 0) and with stand-ins for the
  client (`tests/test_ticks.py`); both ways run in the game, 2026-11-19 ("The Linux helper",
  above: run 2). With the death count (#169) both ways ran again, 2026-11-20; the lost reply's
  handling is stand-in tests only (`tests/test_deathlink.py`, `tests/test_ticks.py`), not run in
  the game.
- **Intel Chips.** `intel on` and `intel off` are taken as on Windows ("Intel Chips"), and
  `Game::watch_intel` runs at each frame call: the Area Map's record (49) named INTEL CHIP, its
  map reveal (3551) off, while on and only if the record reads as the game loads it or as the
  helper left it; a client that goes puts the game's back (`restore_intel`), and the look without
  the client (below) then names it again for a known seed's save. Only in the look's own save
  (`Game::seed_look_loaded`, "Intel Chips", above): on Linux the seed a client put (`offline_put`,
  noted by `take_offline`, forgotten by the client-gone work, as for the disks' count). Nothing
  else differs on Linux: the game loads the `Objects` records as they are, and the installed
  file's record 49 reads as `kAreaMap` (whether anything writes the records after the load is
  not swept: the check before each write leaves one that reads otherwise alone)
  (`docs/re/linux.md`, "Mapped by disassembly: Intel Chips"). A received one's popup and its name in a translation were shared already ("Grants
  and popups", "Translations"). The client sends it through `LinuxMod` (`ticks.linux_intel_tick`);
  a Linux helper of this version built before Intel Chips refuses it, which the client asks again
  every few seconds, saying nothing. Tested on the stand-in game through the socket
  (`tests/test_native_linux.py`: refused unconfigured, named with its event off in the client's
  seed's save only, not before the helper has the seed, in a normal save or another seed's, put
  back by `intel off` and by the client going, and not named for the next client until it has put
  its seed; stand-in tests only, not run in game on Linux) and with stand-ins for the client
  (`tests/test_zexion_client.py`); a received chip and its hint run in the game, 2026-11-19 ("The
  Linux helper", above).
- **The ASSIST lock.** `assists lock` and `assists free` are taken as on Windows ("The assist
  menu"), and `Game::watch_assists` runs at each frame call: while locked, the options menu's
  ASSIST entry found afresh each frame call, drawn in STYLE: AUTO's style with the padlock after
  it (painted through the same unproven OpenGL path as the logos: "Other players' items" here),
  its callback cleared and moved past the panel's selectable entries, and all of it put back when
  freed or when a client goes (the look without the client, below, then locks it again for a
  known seed's save that disallows assists); in a translation, the entry's text
  built again with the HD padlock; the lock and the padlock apply only in the look's own save
  (`Game::seed_look_loaded`, "The assist menu", "Intel Chips"), on Linux the seed the client put
  with `offline_put` (stand-in tested on Windows only, not run in game). Nothing the lock
  reads or writes differs on Linux but the
  callback's arguments, none, which the lock only clears and puts back; the options menu has one
  more way to be built again (the translation-mod screens' way back), which finding the entry
  afresh each frame call covers (`assist_menu_fn`, `AssistMenu`; `docs/re/linux.md`, "Mapped by
  disassembly: the ASSIST lock"). The client sends it through `LinuxMod`
  (`ticks.linux_assists_tick`); a Linux helper of this version built before the lock refuses it,
  which the client asks again every few seconds, saying nothing. Tested through the socket only
  for the requests (`tests/test_native_linux.py`: refused unconfigured, then taken; the stand-in
  opens no options menu) and with stand-ins for the client (`tests/test_zexion_client.py`); the
  lock itself is stand-in tested on Windows (`tests/menu_tests.cpp`); not run in the game.
- **Seed files.** `offline_put seed` writes the seed's file as the Windows helper does ("AP saves
  without the client", `src/offline.cpp`), into `$XDG_DATA_HOME/ZexionAP/seeds/NAME.txt`, else
  under `~/.local/share` (an absolute path only): the base the game's own saves use, as SDL finds
  it in the same process, so under Flatpak's Steam, whose home is
  `~/.var/app/com.valvesoftware.Steam`, its `.local/share/ZexionAP` (seen there, the seeds'
  files under `seeds/`, 2026-11-20: `docs/re/linux.md`, "Seen in the running game, 2026-11-20";
  the folder is found once, in the helper's constructor, before the game's threads run), whole,
  through a temporary file made
  afresh (not through a link) and synced, then renamed over it, the folders made for the user
  alone (mode 0700); a file is read only if it is a plain file, through no link. The serving
  thread reads them, at most 256, newest first, when the helper starts and each time a client
  that set something goes (`sets_client_state`; not a probe such as `tools/helper.py status`),
  saying on stderr a
  file it could not read; the same copies in the game's save folder as the Windows helper's are
  kept and read, found by the same rule (`g_save_folder`, "Seed files in the save folder"; stand-in
  tests only); the same folder holds the client's `token` ("Who may connect"), which
  the seed reader ignores (it reads `seeds/` alone); the next frame call takes them, in place of the
  last ones with no client and added to them with one (`Game::set_offline`, `merge_offline`); with a
  client, the last one put is also its seed for the look (`note_client_seed`, as on Windows: "AP
  saves without the client"). A
  seed taken but not kept (`error too large`, a write that failed: the client takes each for
  taken, and puts it again only when it changes; `error no folder` is not reached on Linux, where
  a helper with no data folder has nowhere for its token and serves no one) stays the helper's
  until the game closes, added again after each read. No config is kept: with no client yet,
  the helper configures itself from the game's own symbols (`configure_self`: the addresses a
  client's `config` must give, checked as one, said on stderr), with no client attached, so this
  world's own traps and disks work with no client, and with no seed file at all (a machine no
  client has run on: the stderr line then ends "no seed file taken yet"). The icon sheets' hold
  (`hold_icon_sheets`, from the game's sprite definition count) is decided at each config, so
  for the helper's own at the frame it configures on, which may come before the game counts its
  definitions (not seen either way); a client's `config` decides it again. Configured so, or once
  its client has gone, it touches the game only while the game's
  player pointer (`gMainCharacterSpriteP`) reads the player state the config's `inventory` lies
  in, as the client checks before it attaches. With a disk seed's file, the pause screen's disk
  row shows its save's count with no client, and in a translation the helper's icons are painted
  into the icon sheets in any save, as on Windows with its own addresses. The guard hook and
  `tools/protected.py` protect the folder as they do the Windows one (the save folder's copies are
the helper's own write: `CLAUDE.md`, "Rules"). Tested on the stand-in game
  through the socket (`tests/test_native_linux.py`: a seed put stored in the stand-in's own data
  folder, `unchanged` the second time; with a seed file at start and no client, an own trap going
  off at its pickup, and not while the player pointer reads elsewhere; a seed not kept still the
  helper's after its client went; with no seed file, no data folder and no copy in the save
  folder, the helper configured at the first frames (a request that needs a config served, not
  refused), a plain randomizer save left alone with Steam's achievements reaching Steam, a save
  slot in use seen and a seed file for it copied, none before one, and a client's `config` still
  taking over; the stand-in has no title, so the title lines are not shown by these; the client's
  attach tests for an apply, for labels and for a client that goes with its plan run both from a
  helper that has not configured and from one that configured itself. The test build configures
  itself like the release helper unless `ZAP_TEST_NO_SELF_CONFIG=1`; `tests/test_native_linux.py`
  sets it to 1 for every test except those that pass `SELF_CONFIG` (the `self-configured`
  parameter), so a client's `config` and the requests refused before it stay tested);
  the helper configured from a seed's file, run in the game, 2026-11-19 ("The Linux helper",
  above); configured with no seed file at all is stand-in tests only, not yet run in the game.
  The same holds for the title lines on a first start, below.
- **The look without the client.** `Game::watch_offline` runs at each frame call, before the
  watches that apply what it sets (on Windows `watch_all` runs INTEL CHIP's and the disks' watches
  before it, so they apply its changes a frame later there) ("AP saves without the client"): with
  no client (configured from the game's symbols, or its client gone), or with one that said
  `other_save`, a known seed's save is dressed once in play since it loaded, from its seed file:
  the item labels, INTEL CHIP, the ASSIST lock and the disk count and scanner's line, with the
  not-connected notice (or, for `other_save`, the other-seed notice) once a load; left, it is put
  back. A client's `config` takes the look off first and withdraws the notices not shown yet; the
  client then asks `client_back`, which pops up ARCHIPELAGO CLIENT CONNECTED. once if that save
  was dressed (`ticks.linux_client_back_tick`, before the poll's step), and `other_save` for
  another seed's save in play (`linux_other_save_tick`). The client hands over the whole seed
  file, its labels included, once known (`offline_seed`, as on Windows), and what the slot data
  alone gives until then, never after the whole file went in that client run (a reconnect lists
  the items afresh: `ZexionContext.linux_full_seeds`), and a new client run's first put, before
  the scouts answer, carries the labels, the disk counts and the free ASSIST menu of the file the
  helper already keeps under that name (`offline.keep_richer`: the client reads it beside the
  token the helper took, `ticks.linux_stored_seed`, once a run; the helper has no request that
  says what its file holds), so a client that closes before the whole text is known leaves the
  later runs with no client the labels they had, where the client can read the helper's token
  folder (where it can't, the poorer text replaces the file as before). A text the helper cannot
  parse is put again as the slot data's alone. The stored file's labels kept run in the game,
  2026-11-20 (`docs/re/linux.md`, "Seen in the running game, 2026-11-20") (a client restarted, the
  file unchanged); the merged text put again is stand-in tests only. The file select's counts,
  from the save slots, and its logo show as on Windows ("The file select's AP logo and disk
  counts", below). What differs on Linux:
  - a client that loses its server connection lets the Linux helper's connection go, as the
    Windows client lets its helper go (`leave_helper_tick`: under the same conditions, with
    `LinuxProcess.disconnect`), so the helper takes it for a client that went (below) and this
    look dresses the save, its notice with it; the client configures the helper again, and
    tells it all afresh, on its first poll with a connection, before that poll's step (stand-in
    tests only, not run in the game). Until then it sends no `ping`, as there is no connection
    to keep;
  - the settings the helper keeps past a connection's close (Intel Chips, the ASSIST lock, the
    disk count) are sent only on the connection it was configured on (`LinuxMod.current`, the
    client's connection that sent `config` and is still open), as the finds', labels',
    client-back and other-save ticks do: a connection made again by a read in the same poll never
    sets them (stand-in tests only, not run in the game);
  - a client that goes leaves the look to dress the save again from scratch at the helper's next
    look, its not-connected notice with it, as `client_gone` does on Windows: its own parts put
    back and the look left (`leave_offline`, `linux/src/helper.cpp`), its notices not shown yet
    withdrawn.

  Tested on the stand-in game through the socket (`tests/test_native_linux.py`: with a seed file
  and no client, the save dressed (INTEL CHIP) with the not-connected notice; a client's config
  taking the look back, `client_back` shown once, `other_save` dressing it again with the
  other-seed notice, and that client gone, dressed afresh with the not-connected notice again) and
  with stand-ins for the client (`tests/test_zexion_client.py`: the whole seed file not replaced
  by the slot data's after a reconnect); run in the game, 2026-11-19 ("The Linux helper",
  above). The stored file's labels kept in a new run's first put are stand-in tests only
  (`tests/test_zexion_client.py`, `tests/test_offline.py`).
- **The title screen's lines.** `Game::watch_title` runs at each frame call with no save loaded,
  as on Windows ("The title screen's lines"): ARCHIPELAGO MOD and the version (set in the helper's
  constructor), ©2026 MCAWFUL, and with no client attached the hint to start it, made with the
  game's own text functions under the title's copyright line (`draw_layer`, `gStatsSpriteLayerP`;
  `text_new_fn`, `newSpriteString`; `text_place_fn`; `text_link_fn`, `addSpriteString`), only
  while no menu is open, and in English(HD) in the translation's font through the shared label
  builder (`docs/re/linux.md`, "Mapped by disassembly: the title screen's lines"). Configured from
  the game's symbols with no client (with or without a seed file read), the lines show with the
  hint as they do on Windows with its own addresses (found by its scan, "Finding its own
  addresses"), while the game's player pointer reads the player state the config's `inventory`
  lies in ("Seed files."): it did at the title screen before any save, and in play
  (`docs/re/linux.md`, "Read from the running game"), so from the first title, on a machine no
  client has run on too (the lines ran in the game, 2026-11-19, once the client had configured the
  helper; configured by the helper alone, with a seed file read or with none, they are stand-in
  tests only, not yet run in the game). If
  the mouse sets `menu_state` to 1 at the title (open: `docs/re/linux.md`), the lines wait unmade,
  or the translated ones hidden, until a key is pressed. `title_status` says what the watch last
  found (for checks). Tested through the socket only for the watch and the request
  (`tests/test_native_linux.py`: the stand-in has no title, so nothing is made, its version line
  set all the same, which `title_status` says as `no version` when it is not: the helper's globals
  are built before its constructor sets it, `init_priority`); the lines themselves are stand-in
  tested on Windows (`tests/title_tests.cpp`); run in the game, 2026-11-19 ("The Linux helper",
  above).
- **Feats and Steam achievements in AP saves.** `Game::watch_feats` runs at each frame call, as on
  Windows ("Feats and achievements in AP saves"): the guard on while an AP save is loaded, and the
  load test over the slots holding AP saves, filled with no save loaded from the slots' own copies
  of their sections (`slot_array`, `gSaveGames`; `rando_slots`, `gSavedRandos`). What differs on
  Linux (`docs/re/linux.md`, "Mapped by disassembly: the Feats guard's Steam and load test"):
  - no slot is written and no code made: the helper exports `SteamInternal_ContextInit` itself, so
    the dynamic linker binds the game's call (from its two accessors, `SteamUserStats()` and
    `SteamUtils()`, on the frame call's thread) to it, and `feats_context_init` (`src/feats.cpp`)
    answers the stats context (`SteamUserStats()`'s, from the game's own symbols) with a stand-in
    interface whose every method returns 0 while the guard blocks: on, or the load test's save
    loaded, marker 1 and slot an AP save's, read at the call; every other call goes on to
    `libsteam_api`'s own (`dlsym(RTLD_NEXT)`, else the library's own if it is loaded but not
    global; looked for again while not found, never the helper's), the game's first, before any
    config, too, and in any other program the preload reaches; where there is none to go on to
    (a program without `libsteam_api`: the game has it `NEEDED`) the answer is the stand-in's
    holder, never null, as the game's accessor reads through it (stand-in tested,
    `native/linux/tests/feats_tests.cpp`), built at load time (`constinit`) so no constructor
    finds it zero, and saying so once on stderr. It fits the game's one-pointer accessor only: an
    accessor that reads more of a context block, an interface of more than 64 methods, or a
    method whose result is a pointer would not be served. If `real_init` failed in the game, Steam
    would be stubbed that way, silently for the player (no achievement outside AP saves), with
    that stderr line and `feats_status`'s "libsteam_api's SteamInternal_ContextInit is not found"
    the only signs; the guard reports a fault
    (`feats_status`) unless the game's call binds to the helper's export (the first definition in
    the global scope: another preload ahead of it would take the calls) and the library's is
    found (a check that fails is made again every `kSteamRetryFrames`, 60 frames, so a library loaded later still counts); `watch_feats` runs before the frame's player-pointer gate, as it only reads;
  - the ending's patch is made over the Linux ending's own first achievement call, not a call
    through the import slot: `mov edi, 13` (14Victory) and `call setTheAchievement` (the game's
    own function, from its symbols) at `ending_unlocks` (`EndingCutscene` + `0x3B`), the six-byte
    `mov word [rbp-0x26], 0` after them, which the patch's last two bytes land in (nothing jumps
    there); flag 251's store just before `ending_join` (`EndingCutscene` + `0x39C`), and flag
    250's at it, not `0x11` bytes after (`docs/re/linux.md`, the `ending_unlocks` and `ending_join`
    rows). The same 12 bytes, `mov byte [flag 251], 0` and `jmp ending_join`, put back as on
    Windows;
  - an AP ending the patch did not jump over (one refused: `feats_status` says why) earns that
    run's Feats (flag 251 set), so Steam then stays blocked from that ending until a save is
    loaded again (the Feats of Natu screen after it, with no save loaded, would set the flashing
    Feats' achievements), the title's own achievements meanwhile with it. Flag 251 does not tell
    this run's Feats from a saved one: an AP save that reached its ending with no patch (an
    earlier helper, or none) holds it, so leaving that save for the title holds Steam there too,
    until a save loads (a code typed at the title meanwhile earns no 13SecretCode);
  - the Feats of Natu screen's note runs as on Windows (`feats_screen_note`, with LargeText's count
    configured for a translation's title: `large_count`), the screen as `feats.cpp` checks it on
    Linux too: MISSION COMPLETE's statistics always lead on to it, its panels, rows, title label
    and the hint at the title, and the six achievement calls it makes, each behind this run's Feat
    byte the ending's patch leaves unset (`docs/re/linux.md`, "Mapped by disassembly: the Feats of
    Natu screen's note"). The Linux stand-in game has no menus: the note is stand-in tested on
    Windows only (`tests/feats_tests.cpp`).

  Tested on the stand-in game through the socket (`tests/test_native_linux.py`: its stand-in
  Steam's achievement set before a config and with a normal save, not with an AP save loaded, and
  not after an unpatched AP ending until a save loads; the stand-in's ending, the Linux site's
  bytes, patched while an AP save is loaded and put back after); an AP save's ending, its note and
  no achievement run in the game, 2026-11-19 ("The Linux helper", above: run 2).
- **Tall text.** `Game::watch_text_cut` runs in the Linux helper's frame as in `watch_all`
  ("Popups", "Tall text"), with or without a client: the text window's top cut patched once
  configured. The Linux build's cut is other code (`text_cut_code`, `setGameMessageStringClipping`
  + `0x661`, 14 bytes, the window's edge and the row's 10 x row on the stack: `docs/re/linux.md`),
  so `src/text.cpp` builds its own patch there: the edge less the row's top, as the Windows
  patch's. The cut's second branch, for a glyph whose `+0x38` reads `0x5A` (cut mod 9), is left
  alone (what those glyphs are, and whether popups draw them: not traced). The client no longer
  says a part is missing on Linux: the Linux helper runs every part (`native.attach` refuses the
  Windows helper there, `native.NoWindowsHelper`, said nothing of, and the client goes on with the
  Linux helper). Tested beside the game's bytes (`linux/tests/patch_tests.cpp`: for rows 0 to 31
  and every edge the cut is taken at, the game's gives the edge mod 8 and ours the edge less the
  row's top) and on the stand-in game through the socket (patched once configured); tall popups
  run in the game, 2026-11-19 ("The Linux helper", above: run 2).
- **The major-item list.** `Game::watch_pickups` runs in the Linux helper's frame as in
  `watch_all` ("Grants", "The major-item list"), in play in an AP save, with or without a client:
  each owned item the game's pickup would list goes in the list (`pickup_list`, `gGameState` +
  `0xAE0`, configured from the game's symbols), but what the new game started with, so the file
  select's ITEMS row shows it once the game saves or reaches a checkpoint (the entry shows the
  checkpoint's record while it is in use). The pickup's rule, the load, the save, the ITEMS row's
  fill, the records' icons and the start-with array are as `watch_pickups` expects on Linux too
  (`docs/re/linux.md`, "Mapped by disassembly: the major-item list"). Tested on the stand-in game
  through the socket (`tests/test_native_linux.py`: an owned item listed once an AP save is
  loaded, one started with and a key left out); the ITEMS row run in the game, 2026-11-19 ("The
  Linux helper", above).
- **The file select's AP logo and disk counts.** `Game::watch_files` runs in the Linux helper's
  frame as in `watch_all` ("AP saves on the file select"), after the look without the client has
  set the counts, with or without a client: each entry of the file select, COPY FILE and DELETE
  FILE gets the helper's handler (`linux/src/helper.cpp`, `file_entry_hook`: the game's, then the
  entry rewritten after its fill, event `0xF`; handlers take (widget, event) on Linux), the logo
  on AP saves and each disk seed's save its own count, the four handlers configured from the
  game's symbols (`file_panel_fn`, `file_entry_fn`, `copy_entry_fn`, `delete_entry_fn`). The Linux
  helper is never unloaded, so nothing puts the handlers back but the watch itself when both are
  off. The entries, their fill and the fills that bypass the handler, the logo's blank cells, the
  font's empty glyphs and the new game's fade are as `files.cpp` expects on Linux too
  (`docs/re/linux.md`, "Mapped by disassembly: the file select's AP logo and disk counts"; that
  the logo's style shows white and the painting itself are not measured there). The Linux stand-in
  game has no menus: the rewrite is stand-in tested on Windows only (`tests/files_tests.cpp`); the
  logo run in the game, 2026-11-19 ("The Linux helper", above), the disk counts too (run 2: of the
  earlier read, the slot array's map; the entry's own block's, since #164, run in the game,
  2026-11-20 (`docs/re/linux.md`, "Seen in the running game, 2026-11-20"): a checkpoint's disk
  shown).
- **Traps.** `Game::pump_traps` runs at each frame call as on Windows ("Traps"), after
  `note_play` (the quarter second in play), `watch_own_traps` and `watch_trap_patches`: a
  received trap goes off as its popup shows, in its turn among the popups, and this world's own
  at its pickup, with the same writes, sounds, timers and holds (one of each kind at a time, a
  transition counting down, the halt byte, the game's ending). What differs on Linux:
  - the AILMENTS tests the patches skip are `weaponCool`'s, in another shape (`src/trap.h`:
    `0x35` and `0x38` bytes, the second jumps at `+0x33` and `+0x36`; `docs/re/linux.md`,
    "Mapped by disassembly: traps"), checked byte for byte but the loads, which must lead to
    `ailments`, as on Windows;
  - this world's own traps come from the seed's text the client hands the helper (`offline_put
    seed`, `ticks.linux_seed_tick`) at its connection's first poll (at first its identity and
    traps from the slot data, and the disks once the items are listed: `ticks.linux_seed`; then
    the whole seed file once known: `offline_seed`), kept in its seed file ("Seed files", above),
    so they go off with no client in later runs too; one picked up before a client first put the
    seed does nothing, and nothing makes it up later;
  - nothing differs for RETURN HOME WITH THE DISKS? or the game's own ending: traps are held
    through them as on Windows;
  - there is no unload, so nothing waits for a running trap's patch: it is put back once its
    timer has run out.

  Tested on the stand-in game through the socket (`tests/test_native_linux.py`: a received
  Disruption Trap goes off as its popup shows, DISRUPTION TRAP over FROM TESTER with sound 26, its
  timer at 660 and the stand-in's `weaponCool` tests patched, put back once the timer is 0; this
  world's own Disorient Trap goes off at its pickup, its timer at 660 with sound 82, from a seed
  the helper took). Both received traps run in the game, 2026-11-19 ("The Linux helper", above);
  this world's own not.
- **Its own runtime.** The C++ runtime is linked in, so it needs only glibc from the game's
  container, which may be older than the one it was built with: `libc`, `libm` and `ld-linux`
  (its dynamic loader), and on a glibc before 2.34, which keeps them out of `libc`, `libdl` and
  `libpthread` too (`CMakeLists.txt` links both; `tools/build_apworld.py` also allows `librt`). A
  build on a newer glibc asks for its newer symbol versions and would fail to load under an older
  one, so a release builds it in the Steam Runtime's SDK, sniper, the runtime the game asks Steam
  for (`docs/re/builds.md`, "B1 on Linux"), in CI too (`native-linux-runtime`, then
  `native-linux-runtime-check`), and `tools/build_apworld.py` refuses one that needs a glibc
  newer than 2.31, a glibc version with no number (`GLIBC_PRIVATE`), or a version of any library
  but glibc's, libstdc++'s among them (its versions read from `.gnu.version_r`, `client/elf.py`).
  The SDK is the image `registry.gitlab.steamos.cloud/steamrt/sniper/sdk:3.0.20261021.266025`,
  pinned by its digest
  `sha256:bb30053d3cd19d35c76ae69b2790dbaa1430448006f8240cb6d7e723f2d1940e` (the registry's
  `Docker-Content-Digest` for that tag, read 2026-11-20). Its versions are the snapshot's
  published package list, `com.valvesoftware.SteamRuntime.Sdk-amd64,i386-sniper.manifest.dpkg`
  in <https://repo.steampowered.com/steamrt-images-sniper/snapshots/3.0.20261021.266025/> (read
  2026-11-20): `libc6` 2.31-13+deb11u14, `gcc-14` and `g++-14` 14.2.0-19~steamrt3.1, `cmake`
  3.25.1-1~bpo11+1+bsrt3.1 and `ninja-build` 1.10.1-1. CI's build there (the `libzexion_ap-runtime`
  artifact of e9cfad2's run, `libzexion_ap.so` `fba8ed52…`) ran in the game, 2026-11-20, B1 on
  Linux, Flatpak Steam on the test PC (the game on the Flatpak runtime's glibc, `/run/parent/…`):
  `helper.py status` answered (`zexion-ap 1.2.0 build=B1 hooked=1`, 165 frame calls a second from
  `SWHardwareEnd`), the helper dressed run 3's save before the client attached, then, with it,
  ARCHIPELAGO CLIENT CONNECTED., a received Missile Container's popup opening and closing, and the
  title screen's lines, as the owner saw them; native Steam not tried (a Steam Deck run of a later CI build, 2026-11-20, loaded the mod: "Seen on a Steam Deck, and a Linux PC's sync", above). Read on
  the
  test PC, 2026-11-18: the Steam Runtime installed there (sniper 3.0.20261021) ships glibc 2.31
  (its `usr-mtree.txt.gz` lists `libc-2.31.so`); Flatpak's Steam runs on the Flatpak runtime
  `org.freedesktop.Platform` 26.08, whose glibc is 2.44 (`strings` of its `libc.so.6`); a build
  there on glibc 2.44 asks for up to `GLIBC_2.38` (`objdump -T`) and nothing from libstdc++
  (`readelf -d`: `libc`, `libm` and `ld-linux` only, glibc 2.44 having `libdl` and `libpthread`
  in `libc`). Under Flatpak's Steam the game ran on the
  glibc the container takes from outside it (`/run/parent/usr/lib/x86_64-linux-gnu/libc.so.6` in
  its `/proc/<pid>/maps`), and the build needing 2.38 loaded (2026-11-18). That the container
  takes the newer of the runtime's and the host's is assumed (pressure-vessel's documented rule);
  without Flatpak it is not checked. Checking a local build's versions before a run: "Running it
  in the game", step 1, above.

## Building

Windows: Visual Studio 2022 or later with the C++ workload, and CMake 3.20 or later, with a
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

Linux: GCC or Clang, and CMake 3.20 or later (`uv pip install --python .venv cmake` puts one
in the venv). From the repo root, the same folder, one config:

```sh
cmake -S native -B native/build
cmake --build native/build -j
ctest --test-dir native/build --output-on-failure
.venv/bin/python -m pytest tests/test_native_linux.py
```

That builds `native/build/libzexion_ap.so` (the Linux helper), the test build
`libzexion_ap_hooktest.so`, which serves any program, and the stand-in game `zap_fake_game`, a
fixed-address executable as the game is, calling `SDL_GL_SwapWindow` from a stand-in
`libzap_sdl_stub.so`. ctest runs `tests/core_tests.cpp` (build ids, replies);
`tests/test_native_linux.py` preloads both builds into the stand-in game ("The Linux helper"),
`zap_newgame_tests` runs the shared new-game logic (`linux/tests/newgame_tests.cpp`), and
`zap_patch_tests` runs the Linux build's code patches side by side with the game's bytes
(`linux/tests/patch_tests.cpp`, "The map"), `zap_translation_tests` the label builder's call
(`linux/tests/translation_tests.cpp`, "Translations") and `zap_feats_tests` the Steam export's
answer in a program with no `libsteam_api` (`linux/tests/feats_tests.cpp`);
`libzap_swap_wrapper.so` (`linux/tests/swap_wrapper.cpp`) is a stand-in for another library's
`SDL_GL_SwapWindow`, preloaded ahead of the helper by the wrapper tests (a frame function's
call through it, a repeated call, a call from another function: "The Linux helper", "The frame
call"), and
`tests/test_native_linux_init_order.py` reads the helper's sources for globals built too late
("The Linux helper", "Start-up", for what it covers);
`ZEXION_NATIVE_BUILD` points it at another build folder than `native/build`.
CI runs all of it on Ubuntu, with `ZEXION_REQUIRE=helper`.

CI builds both, runs the ctest tests, and runs `tests/test_native.py` on Windows, which loads
the build into a sleeping Python process (no known build, so nothing is hooked) and checks the
pipe and unloading, and that a production helper in a host that is not the game answers
`offline_put` with `error not the game` (it writes no folder there). That test skips without a build. In CI, where `ZEXION_REQUIRE=helper`
is set, a missing build fails the job instead (`test_the_helper_is_built_where_ci_requires_it`).

**The tests never write the real folders** ("Seed files in the save folder", "Test builds"). Every
test program and the test builds of the helpers (`zexion_ap_hooktest.dll`, `libzexion_ap_hooktest.so`)
link **the test-only build of the core**, `zap_core_test` (Linux `zap_shared_test`), which compiles
`src/offline.cpp` through `src/offline_test_build.cpp` with `ZAP_TEST_BUILD` set: each write
primitive in it (`write_whole`, `make_folder`, `make_dir`) refuses a folder that is not strictly inside
the temporary folder, links resolved, or is one of the real folders (`restrict_to_test_folder`), so a
unit test handed a real folder writes nothing. The release build (`zap_core`, `zap_shared`) is
compiled without it. **The canary** (`tests/canary.cpp`, `zap_canary`) is a CTest fixture every test
requires: `canary_before` records the real save folders' seed copies and the real per-user folders'
every file (name, size, write time; absent folders too) and `canary_after`, run last whatever
failed, fails if any changed; `tests/conftest.py` has the same for pytest as a session fixture.
Run the tests with `ZEXION_SAVE_DIR` unset and `LOCALAPPDATA` or `XDG_DATA_HOME` pointed at a scratch
folder if you like: the canary finds the real folders through the operating system.

Most ctest tests run against **the stand-in game**: the test program's own data and functions,
laid out as the helper expects the game's (`tests/stand_in.h`, `tests/stand_in_menu.h`) and
configured by RVA like the real game. A test's source is `tests/<name>_tests.cpp`
(`apply_test.cpp`, `unload_test.cpp` and `proxy_test.cpp` for `apply_in_frames`,
`unload_under_load` and `proxy`). They share `tests/check.h` (with one test of a code patch on a
page of its own, `check_code_patch`, that `game` and `mapmarks` run on theirs); `tests/fake_gl.h` is
the fake OpenGL of `apitem` and `offline`; `tests/pipe_client.h` is the pipe client of `parts`,
`apply_in_frames` and `unload_under_load`, and the last two's busy threads and wait for an unload.
A test reads the stand-in's code the helper patches (`.zapcode` in `tests/stand_in.h`, a section
that is read and execute) only through volatile (`code_byte`, `code_is` in `tests/check.h`),
never with a plain `memcmp`. The `game` and `offline` tests failed in CI jobs whose MSVC was
19.52.37029.0, with the same sources passing on 19.51 (four runs: 4a32d47, c5500c7, e9ecd2f,
9809090); the cause is not reproduced. The working guess is that the optimizer took such an array,
not writable, as holding its initializer, so a patch the helper made read as not made (the
text-cut and seen-marker checks inside them). The first guess, the linker folding identical
code (`/OPT:NOICF`), was disproved by the c5500c7 run. With the reads through volatile, CI runs
44643254927 (61ecc7c) and 44643374457 (99f12f3) compiled the Windows native job with MSVC
19.52.37029.0 and passed: that is the evidence for the fix, not a confirmed cause.

- `parts`: the import-table hook and the trampoline (an exception raised behind it caught in
  front), `game_mem.h`'s menu, panel and label readers, its handler-slot swap, its memory check
  (page by page, a guard page left armed), a sprite's frame bounds and the code patches' put-back
  (one that fails held), the build check, the pipe and `CellWatch` (cell_watch.h: a pickup
  needs two looks in play, a list that changed or a reset starts over, a cell put back is told).
- `requests`: the request parsers, the popup text's layout and the mailbox.
- `game`: the `discs` and `@N NAME` parsers, configuring, applying, the major-item list's fill (an
  AP save's only, each icon once, by item ID, up to 26), R'Talva X34 Y51's marker hidden (every
  frame while flag 154 is clear, by its shown byte; shown again on the flag, off the logo frame and
  on restore; no stand-in for names; another type left alone), this player's own Scrap Metal
  (`test_own_scrap_metal_is_picked_up_as_the_games`: a stand-in's record naming Scrap Metal's event
  while named after such a cell, its popup with no icon;
  `test_a_scrap_square_is_left_while_its_item_is_live`; `mapmarks` has
  `test_own_scrap_metals_square_is_a_minors`), a reversed-gravity room's item hidden over a
  collected square (`test_a_collected_item_in_a_reversed_gravity_room_is_hidden`: shown again when
  the square no longer reads collected, or it holds the item; kept hidden under a menu), the
  stand-ins drawn in palette 26 at style 34 (their first style kept; a dressed one, one not as
  expected and this world's items left alone; put back when not restyled and on restore), the popup
  queue, the text window's cut repaired and put back (and a model of the game's update and window,
  its lines flashing without the repair), the new-game watch, a whole ARCHIPELAGO press, what an
  unload's put back keeps while putting the game's memory back (and a refused unload's watches apply
  again, a client gone drops), the received shield, scraps' drops, traps and death, this world's own
  traps and disks watched by their map cells (a pickup, a load that is none, a pickup a load undid,
  made again before play settled, or carried to another slot's save, and a TELEPORT TO's countdown
  and load; an own disk's count with no client), the ASSIST watch and the scanner's text, the Intel
  Chip, ASSIST lock, scanner line, equipment screen and pause row applied only in the look's own
  save (a client's seed put or its labels, another known seed's save, a normal save, none, the
  client gone, the offline look in its save), and RETURN
  HOME WITH THE DISKS? through the frame call (its fade and popups, a save or a door in its place, a
  request lapsing outside home's cell, at a start that is not home too, the `mission_prompt` parser,
  a death's own fade left alone, the helper's own popup holding the fade while it is the game's last
  message, however many frame calls pass, the ending after YES starting with a message of the game's
  still up but waiting for the helper's own popup, and the ending's wait that an unload refuses for;
  the screen kept black after YES into the ending with a popup waiting (shown at the next load), and
  the same popup wait after a YES not kept black, received deaths refused (`RETURN HOME WITH THE
  DISKS? ending`), and no prompt, popup or death while the ending started runs, until another save
  is loaded). `mission` covers the black after YES on its own: kept, not kept for a fade not as NO
  leaves it, ended by kDarkFrames game frames in play (from a stand-in frame clock, with frame calls
  coming faster than game frames, as at 165 Hz), another fade or cancel, and what `mission_black`
  then says; and the ending out of it through the helper's wrapper, in a stand-in main loop that
  draws each frame after the transition and the stepper: the routine's frame drawn black, then the
  fade in, and the wrapper never left as the transition (unload, a routine not as traced, none in
  the table).
- `menu`: the game-mode panel and the options menu (relabeling, putting back, closed menus and
  released widgets, the ASSIST lock's look and cursor order, the options menu built again after
  Video, GENERAL or the assist menu with another widget in the locked ASSIST's place).
- `apitem`: the stand-in records restyled and named (one named after a cell labeled this
  player's own Scrap Metal naming Scrap Metal's pickup event:
  `test_own_scrap_metal_names_scrap_metals_event`), the logo (in its own colors, the arrow
  yellow then white), the font glyphs, the pause
  row's disc and the file select's logo in its colors painted through a fake OpenGL, the
  translations' icon sheets painted and cleared (and left alone where not as traced; each
  sheet's icons made once), the label parser.
- `equip`, `pause`, `files`, `intel`, `title`, `mapmarks`, `mission`, `offline`: the part of that
  name (`files`: the disk count and the AP logo; `title`: also its two lines built in English(HD)'s
  font and back to pixels, hidden there under a menu (Option, the file select, a client connecting
  meanwhile, English(HD) picked there), none made under a menu, with `menu_state` unreadable and
  with the game's line gone, and the default English's lines following the © whatever the menu;
  `offline`: the seed file, its match and disk count, the folder, the cache check, `offline_put`, a
  stand-in game dressed and undressed, the look held through a menu (the files read again, a read of
  no known seed, a client going away, ASSIST locked again in the same frame call), a notice
  withdrawn with its save, the not-connected warning again after a client came and went, a seed put
  with a client, the AP saves' slots, the slots' seeds from the stand-in game's memory
  (`Game::saved_seeds`) and `SavedSeeds`, the seed copies in a stand-in save folder (a put, a read,
  a put waiting for a slot, `restrict_to_test_folder` with a junction, the write primitives'
  refusal), and the file select's logo through the frame call: painted,
  whitened through a new game's fade, and cleared by `put_back_files`, and a cache missing a key
  refused).
- `findaddr`: the helper's own address scan ("Finding its own addresses") on synthetic code: both
  kinds, the addend, exactly once (none, twice, overlapping matches), wildcards, the ends of the
  code, patterns that disagree, texts it cannot read, each of the generated table's patterns on its
  own readable and its names once each, the comparison of a client's addresses with the scan's, and an unknown
  build refused. With `ZEXION_EXE` naming a copy of the game's exe, it also scans that exe's
  `.text`, and `find_in_image` on the copy mapped as an image, and fails unless every target is
  found, at the same addresses by both (`tests/test_native_scan.py` compares the
  addresses with the client's); it skips that without one, so never in CI. `apply_in_frames`
  checks `hello`'s `scan` and `addresses` in a stand-in game, which no scan can read, and that the
  loaded helper's copy thread writes a seed's copy into the test's save folder once the stand-in
  game's slot holds the seed (the frame calls publish the slots).
- `translation`: the translations, on a stand-in English(HD) (its tables as the game's parser leaves
  them): the text helpers, the tables' entries and their room, the popups in the borrowed areas
  (taking turns, waiting, emptying, the whole run, too long for it: the pixel text and the slot's
  mark cleared; the same with the tables unreadable, while unloading and with text in the run's
  entry 68: no split), a language change mid-popup and between a name's write and its put-back (the
  old tables left alone), the scanner's line, ARCHIPELAGO built again and back on the panel (not
  where the game first built it), a label with no first pixel glyph or too little room left alone,
  the locked ASSIST's translated text with its padlock (no space before it in English(HD), one in a
  pixel font; left out at a narrow box, built again over the game's, its style 0x15 and alpha 0.45
  pinned, back when unlocked at full alpha), the map cursor's name without the logo, the helper's
  icons as characters and through the game (painted while a translation shows, cleared when the
  default English is picked and before an unload, a sheet the game uses left alone), the popups'
  layout (measured widths, centering, the cap, the fallback, the suffix's case, a stand-in's name's
  lines centered, and through the game into ItemNames, not where the spaces would pass its room, and
  held while its pickup message shows (another cell, paused; not with no save loaded; another of its
  record within reach takes it, even with one of the other nearer; a newer message of its record
  named, then held; the game's name while the map is open; a language change keeps it); the mixed
  case in English(HD) and capitals in French and the default English for a client's notice, the
  helper's own, a label and the scanner's line; a popup through the game measured with its icon's
  width, a long notice wrapped), and the default English (the flag clear, or nothing loaded) left as
  it was (`mission`, `equip`, `pause` and `feats` cover RETURN HOME WITH THE DISKS?'s question,
  centered, the equipment screen's disk, its description wrapped, the pause row's count and the
  Feats title with the note in a translation; `apitem` the icon sheets' painting and the HD
  padlock's place in its cell).
- `feats`: Feats and achievements in AP saves. It covers the AP-save test in each case (no config, a
  plain randomizer save, a normal save, a put seed, a seed file with no client and with a client of
  another seed, the labels' seed), the guard on and off, and a stand-in ending run with and without
  the patch. It also covers the Steam import hooked and passed through, the stub's test while an AP
  save loads (by slot, marker and context, kept and dropped, off with `release_steam`), the
  refusals, putting back with and without a frame, and the Feats of Natu screen's note (on a
  stand-in screen, after an AP ending only). The program imports `SteamInternal_ContextInit` by name
  from a stand-in `steam_api64.dll` (`tests/steam_stub.cpp`), as the game does. Both are built into
  `build/feats/`, apart from the helper.
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
| `game_calls.cpp` | `Game`: configure, grants, popups, shields, deaths, traps, each part's watch |
| `trap.h`, `trap.cpp` | the traps' codes, names, the player state's fields and sounds the game's touch sets, and the AILMENTS tests' patches |
| `addresses.h` | `GameAddresses`: the configured addresses, one per `config` key, which every part reads |
| `offline_test_build.cpp` | `offline.cpp` built with `ZAP_TEST_BUILD`, for the test-only core (`zap_core_test`): its write primitives refuse any folder that is not a test's own ("Seed files in the save folder", "Test builds") |
| `game_offline.cpp` | `Game`'s offline look, and its watches of this world's own traps and disks by their map cells (`watch_own_traps`, `watch_own_disks`), which run with a client too |
| `cell_watch.h` | `CellWatch`: a list of map cells looked at every frame, which turned collected or back since the look before, and after how many looks in play (the two watches above share it) |
| `requests.cpp` | the requests' parsers |
| `newgame.h`, `newgame.cpp` | new games (`NewGames`: the plan, the seed's placement, the RANDOMIZER route), which `Game` and the Linux helper share |
| `choice.h`, `choice.cpp` | the ARCHIPELAGO choice on the game-mode panel and its start (`start_archipelago`), shared the same way |
| `platform.h` | `<windows.h>`, or on Linux the few Windows type names the shared sources use |
| `text.cpp` | the popups' text layout, the labels' and the capitals the 8 x 8 font draws; the text window's cut patch (`text_cut_patch`) |
| `pipe.cpp`, `mailbox.cpp` | the pipe; the hand-off to the game's thread (`mailbox_linux.cpp`: the Linux helper's) |
| `iat.cpp` | the import-table hook and trampoline |
| `build.cpp` | the known builds |
| `findaddr.h`, `findaddr.cpp`, `findaddr_patterns.h` | the helper's own address scan: the rules, and `client/patterns.py`'s patterns as C++ (generated by `tools/gen_native_patterns.py`; never edit it) |
| `game_mem.h`, `game_mem.cpp`, `game_layout.h` | fault-safe memory access (`game_mem_linux.cpp`: its Linux parts); code patched and put back (`game_mem.h`'s `CodePatch`, for `TextCut`, `SeenMarker` and, while a trap runs, `trap.h`'s `HitPatch` and `WavePatch`); the layouts several parts share |
| `apitem`, `looks`, `menu`, `intel`, `scan`, `equip`, `pause`, `files`, `mission`, `offline`, `mapmarks`, `title`, `feats`, `translation` | one part each, as named above (`menu`: the ASSIST lock; `feats`: Feats and achievements in AP saves; `translation`: Translations) |

`proxy/` is the proxy `version.dll`.

`linux/` is the Linux helper: `src/core.cpp` (the known builds, the build id note, the replies),
`src/helper.cpp` (the constructor, the socket's thread, the frame call, `config` and the
requests that act on the game), and in `tests/` the ctest tests, the stand-in game and its
stand-in SDL. It also builds every source of `src/` but `dllmain.cpp`, `iat.cpp`, `pipe.cpp`,
`build.cpp`, `findaddr.cpp` and `mailbox.cpp`, with `game_mem_linux.cpp` and `mailbox_linux.cpp` for the
Windows parts (`src/platform.h` gives them the few Windows type names they use).
