# Memory

Where the running game keeps the state the client needs. Addresses are module-relative
(`Zexion.exe+offset`) and good for build B1 only; code finds them by the byte patterns in
"Byte patterns" below (`client/patterns.py`), never by these numbers. Format per
`docs/re/README.md`.

## Save data lives in Zexion.exe's static data

| Field | |
|---|---|
| Value | The save state is in the module image (writable static data), not the heap, so its offsets from the module base are fixed for a build:<br>• One block per save slot, in an array: slot N's first save block is at `Zexion.exe+0x230600 + (N−1) × 0x9408`. Each matches the first block of `Save Game N.sav` (file offsets `0x0002` to `0x9409`) byte for byte; checked for slots 1, 2, 3, 28, 29 and 30.<br>• The loaded randomizer save's appended section (file `0x12812` onward, without its `E8 03` header) is mirrored from `Zexion.exe+0x357E18` (location cells), with the item table at `+0x358084` and the seed at `+0x358120`.<br>• The seed also appears at `+0x356F10` and `+0xCDD9D8`. Other near-copies of slot 30's block are at `+0xDDAC58` (37,845 of 37,896 bytes equal; file offset `0x129` = 1) and `+0xDED468` (37,881 equal; `0x129` = 0). Neither is the live play state: the live map and the live inventory were found elsewhere (see "The live map grid flips at the moment of pickup" and "Live inventory: writing an item's byte grants it"). |
| Status | measured |
| Method | memory scan with `tools/memscan.py` (search for the seed and the all-Scrap table, then whole-block comparison against backup copies of the saves) |
| Build | B1 |
| Evidence | 2026-10-20, slot 30 loaded. Saves compared from `lab/backups/20261020-213828/` and `lab/backups/20261020-213055/`. None of the copies changed over a 3-second window, with the game possibly paused. |

## A pickup counter updates the instant an item is collected

| Field | |
|---|---|
| Value | `Zexion.exe+0x3D0168` counts pickups in the current session: it went 0 → 1 at the first pickup in slot 30 and 1 → 2 at the second, at the moment of collection (within 0.1 s, the sampling interval), with no pause or save involved. Its neighbors read as a record of 32-bit fields. It is a 16-bit value (the code reaching it uses word-sized `dec`, `movsx` and `mov`), the pause screen's `ITEMS: x/150` count (it read 1 with one pickup made, and a granted item did not change it). The save keeps it as a 16-bit value at file `0x935A`: live and saved agreed (1) with slot 28 loaded, and the owner's far-along slot 1 holds 150, matching ITEMS 150/150. It says *that* something was collected, not *which* location, and not every location counts: the pickup routine skips items 27 to 31 and 38 to 45 ("Granting each kind of item, and what keeps it"). A client detects checks from the map grid instead. |
| Status | measured |
| Method | `tools/memscan.py snap --module-only` twice while standing still, then a 10 Hz recorder over the module logging bytes that had been stable; the first pickup by snapshots, the second by the recorder |
| Build | B1 |
| Evidence | Snapshots `lab/mem/s30-*` (first pickup; deleted after analysis, 1.1 GB) and `lab/mem/p2-before*`, log `lab/mem/p2-record.log` (second pickup at about 9.1 s), 2026-10-20. |

## Saving copies the live state into the slot array

| Field | |
|---|---|
| Value | When the game saved slot 30, its block in the slot array (`+0x33CAE8`) changed in 46 bytes at once, about 12 s after the pickup, and the slot 30 file changed at the same time. Read as file offsets, those 46 bytes are map exploration (`0x80xx`, `0x8Cxx`), flags near the start (`0x0004` to `0x0006`, `0x001C`, `0x00E3`, `0x43C7`, `0x4589`) that the earlier slot 28 and slot 30 saves also set, and values around `0x9321` to `0x935F` that behave like counters and stats (some identified since: current energy and missiles at `0x9350` and `0x9352`, and the continue point at `0x935C` and `0x935E`, `save-format.md`). `0x9321` turned out to be Scrap metal's inventory byte (item 46; `save-format.md`). This comparison could not place the live state (matching the array's changes against earlier ones gave several equally likely positions); the live map and inventory were found later by other means (below). |
| Status | measured (array updated on save, the 46 offsets); assumed (what each offset means) |
| Method | the 10 Hz recorder above; byte comparison of slot 30 saves |
| Build | B1 |
| Evidence | `lab/mem/p2-record.log`; saves `lab/work/slot28-all-scrap.sav` (as loaded), `lab/backups/20261020-215709/` (after two pickups), `lab/backups/20261020-213055/` and `lab/backups/20261020-213828/`, 2026-10-20. |

## The live map grid flips at the moment of pickup

| Field | |
|---|---|
| Value | The live map grid (54 × 54, row-major, same values as the save's grid at file `0x877F`) is at `Zexion.exe+0x3E469D`. With slot 27 loaded it matched that save's grid in all 2,916 cells and showed location #52's cell (25, 25) as `0x19`. At the pickup, that cell became `0x1B` in the same 50 ms sample as the pickup counter at `+0x3D0168` went 0 → 1, and no other cell changed. So a client can detect a collected location by polling this grid at the location cells from the randomizer list. The memory around the grid is not a copy of the rest of the save block (31,215 of 37,896 bytes equal, mostly zeros; the file-`0x4589` flag reads 0 there after the pickup), so the live inventory and flag array are elsewhere. |
| Status | measured (one location, one pickup) |
| Method | search of writable memory for each explored row of slot 27's saved grid, outside the slot array, voting on a grid start; then a 20 Hz watcher on the cell and the counter during the pickup |
| Build | B1 |
| Evidence | Slot 27 loaded from `lab/backups/20261020-221347/`; pickup at 12.45 s into the watcher, 2026-10-20. |

## A first inventory search looked for the wrong array (superseded)

| Field | |
|---|---|
| Value | Superseded by "Live inventory: writing an item's byte grants it" below: the live inventory is at `Zexion.exe+0x3ED47E + item ID`, in the module's static data. This search, made while holding the Gravity Harness (slot 28, after its first pickup), looked for a copy of the save's `0x0111` array including entry 24, a bitfield of item IDs and an array of 32-bit values per ID, found none, and wrongly concluded the inventory was on the heap; the `0x0111` array is not the inventory (`save-format.md`). Still valid from this run: `Zexion.exe+0x3CF78D` holds a save-block copy without item 24, so it looks like a copy taken at load time; and the map cell went `0x18` → `0x1A` about 0.5 s before the pickup and `0x1A` → `0x1B` at it, together with the pickup counter, so a client should treat only `0x1B` as collected. |
| Status | superseded (where the inventory is); measured (the load-time copy, the cell sequence) |
| Method | 10 Hz module recorder during the pickup; `memscan.py find` for the array, bitfield and 32-bit layouts; reading known near-copies of the save block |
| Build | B1 |
| Evidence | `lab/mem/p3-before2`, `p3-before3`, `p3-record.log`, 2026-10-20. |

## A full-memory diff of one pickup leaves thousands of candidates

| Field | |
|---|---|
| Value | Five full snapshots of writable memory around the Gravity Harness pickup in slot 28 (two before, three after, standing still) left 5,908 bytes that went 0 → 1 at the pickup and stayed: 3,322 and 2,254 in two large heap regions (graphics- or level-sized), 123 in the module, 209 in small heap regions. None of them sits in an array laid out like the save's `0x0111` array (entries 7, 15, 16, 22 at one stride), and no list of item IDs exists in 8-, 16- or 32-bit form; but that array is not the inventory, so this was a search for the wrong thing (the inventory was found later, below). Setting 267 of the small-region and module candidates back to 0 at once crashed the game, so narrowing by zeroing must use small groups, or be replaced by a debugger. Saves were not affected. |
| Status | measured |
| Method | `memscan.py snap` (full) five times; candidate filter and stride search by address; a batch write of zeros with read-back |
| Build | B1 |
| Evidence | 2026-10-20. Snapshots `lab/mem/f-*` deleted after the crash made their addresses meaningless. |

## The pickup record holds the major-item list

| Field | |
|---|---|
| Value | The save's major-item list and its length are at a fixed distance from live memory: live = file + `0x3C6E4D`. The fields around them are not (play time, file `0x9356`, is live at `+0x3D0164`), so the save writes this region field by field. At the Gravity Harness pickup, live `+0x3D0180` (file `0x9333`, the first list entry) went 0 → `0xC7` and `+0x3D019A` (file `0x934D`, the list length) went 0 → 1, in the same 0.1 s as the pickup counter at `+0x3D0168` (saved at file `0x935A`, see the counter's own section). A hardware write breakpoint on slot 28's array entry at file `0x0129` in the slot array never fired during a save, because that byte isn't what records the item (`save-format.md`). Appending a code (`0xC0`) to the live list and raising the length to 2 changed nothing in the inventory, so the list does not grant items. |
| Status | measured |
| Method | 10 Hz module recorder; x64dbg hardware write breakpoint at `Zexion.exe+0x32A3FF` during a save; save comparison |
| Build | B1 |
| Evidence | `lab/mem/p3-record.log`; saves `lab/backups/20261020-223111/` and `20261020-224429/`, 2026-10-20. |

## Live inventory: writing an item's byte grants it

| Field | |
|---|---|
| Value | The live inventory (the save's bytes at file `0x92F3 + item ID`) is at `Zexion.exe+0x3ED47E + item ID`, inside the player state that starts at `+0x3EC3F0` (the game reads it at offset `0x108E + code`). With slot 28 loaded it matched the save. Writing 1 to item 17's byte (`+0x3ED48F`) made the S-Blade appear in the inventory at once, with no pickup, reload or save. Writing 1 to item 16's byte (`+0x3ED48E`) made Slide Mod appear, and with it the granted S-Blade worked in play (S-Blade needs Slide Mod). The recorder had caught item 24's byte (`+0x3ED496`) going 0 → 1 at the Gravity Harness pickup. (These writes were first described as "ability flags, flag = item ID − 13", an artifact of reading the array from item 13.) The pause screen's item count stayed at 1, matching the pickup counter at `+0x3D0168`: a granted item does not count as a pickup, and the counter is the `ITEMS: x/150` number, so it counts locations collected (not all of them; see "Granting each kind of item, and what keeps it"). For counted items (Energy Tanks, Missile Containers, Mega Bombs) the byte alone is not enough; see "Granting each kind of item, and what keeps it". |
| Status | working in game (S-Blade and Slide Mod granted live; the S-Blade used in play) |
| Method | search of writable memory for the save's 32 flag bytes (two hits: slot 28 in the slot array, and this one); `memscan.py write` of one byte with read-back; the owner compared the inventory against a baseline |
| Build | B1 |
| Evidence | 2026-10-20, slot 28 loaded from `lab/backups/20261020-224429/`; writes logged by `memscan.py` (`0x7ff717cdd48f: 00 -> 01`, then `0x7ff717cdd48e: 00 -> 01`). |

## Granting each kind of item, and what keeps it

| Field | |
|---|---|
| Value | For the weapon and the key tried, the inventory byte was the whole grant. For counted items it is not: the energy, missile and bomb numbers the game plays with are separate 16-bit fields of the player state (`+0x3EC3F0`): current energy `+0xCEA`, max energy `+0xCEC`, current missiles `+0xCBC`, max missiles `+0xCC0`, current bombs `+0xCBE`, max bombs `+0xCC2`. Three places set them, from disassembly. New-game setup (RVAs `0x1E35F4` to `0x1E36F2`) writes 3 Energy Tanks, 3 Missile Containers and energy `0x30`/`0x30`. Loading a save (RVA `0x9C630`) reads current energy and missiles from the save (`save-format.md`, "Current energy and missiles") and recomputes every maximum from the counts. The pickup routine (RVA `0x1EECB`) increments the item's byte (`0x1F1D8`); sets max missiles to Missile Containers + 10 × Missile Boxes (items 1 and 36) and adds the difference to current missiles; does the same for bombs (Mega Bombs + 10 × Bomb Packs, items 2 and 37); and if Energy Tanks × 16 exceeds max energy, sets max and current energy to it. It then passes current missiles and bombs to the HUD's own number objects (`+0xB30` and `+0xBB8`, function `0x1F5B10`, which stores the value as a double at object `+0x48`). Several other routines subtract from `+0xCEA` (for example RVAs `0x1E827` and `0x1DCC43`); that this is damage is assumed. The pickup routine increments the pickup counter (`0x1F1A1`) for every item except IDs 27 to 31 (the Alpha, Beta, Gamma and Omega Keys, and Zexion: its grab left the counter unchanged, "The goal: Zexion's inventory byte at the grab") and 38 to 45 (the eggs, the Data Disc, "Gamma key no rando"), going by the item ID in the item's definition record, the same ID whose byte it increments; names from `ItemNames.txt`.<br>Granted live in slot 28, one at a time, the player standing still in a save room:<br>• **Energy Tank** 5 → 6 alone: nothing changed on screen, and no other player-state field moved. Then max and current energy 80 → 96: the HUD and inventory showed a sixth tank, full.<br>• **Missile Container** 3 → 4, max missiles 3 → 4, current 2 → 3: max 4 at once and missiles fired; the HUD number stayed at the old value until the next shot.<br>• **Force Blaster** (item 4) 0 → 1, the byte alone: in the weapon list, and it fired.<br>• **Alpha Key** (item 27) 0 → 1, the byte alone: shown on the pause and map screens.<br>• **Mega Bomb** 0 → 1, max and current bombs 0 → 1: in the inventory, but no HUD icon appeared.<br>The first four grants survived a room change, a save, and quitting to the title and loading the slot again: the saved file held the inventory bytes (`0x92F3 + ID`), current energy 96 and current missiles 4, and after the reload the player state read energy 96/96 and missiles 4/4. The Mega Bomb, granted after that save, was gone when the player died and continued without saving: its byte went 1 → 0 at the moment of continuing. So a grant lasts only once the game has saved it, and a death returns to the last save. No grant moved the pickup counter (1 throughout), as expected: a grant does not run the pickup routine. |
| Status | working in game (Energy Tank and Missile Container with their fields, Force Blaster: granted live and used; all three kept through a room change, a save and a reload); measured (Alpha Key shown on the pause and map screens and kept likewise; Mega Bomb present but without its HUD icon, and lost on a death without a save; the routines' arithmetic, from disassembly); assumed (that the subtractions from current energy are damage); not tried (the Alpha Key on its door; writing the HUD objects so the missile number and bomb icon update at once; an area load other than one room change) |
| Method | Inventory bytes with `tools/grant.py set`. The player-state fields with a one-off script, not in the repo, that located the player state through `client/game.py` (inventory − `0x108E`), compared each field with its expected value before writing any, wrote, read back, and logged through `grant.py`'s log. `grant.py watch` at 20 Hz throughout. `memscan.py snap --module-only` twice before and twice after the Energy Tank write, keeping only bytes stable within each pair. Disassembly with capstone, function by function through `tools/make_pattern.py`'s `Image`, on `lab/data/6d8a94bd4d64/Zexion.exe`. The owner reported the HUD, inventory and pause screens. `save_lab.py diff` of the saved slot against the session's backup. |
| Build | B1 |
| Evidence | 2026-10-21, slot 28 (backup `lab/backups/20261021-091726/` before the session; saved copy `lab/work/slot28-after-grants.sav`). Every write, inventory and field alike, and the watcher's changes are in `lab/grants.jsonl` (Mega Bomb 1 → 0 at 09:32:47.571); the watcher was then stopped by ending its process, so its log has no closing line. Snapshots `lab/mem/g0-before`, `g0-before2`, `g1-etank`, `g1-etank2`, `g2-energy`. |

## The pickup popup is transient UI objects, not a flag

| Field | |
|---|---|
| Value | At the Gravity Harness pickup, about 650 module bytes changed and later reverted; about 570 of them reverted together 3.2 s after the pickup, the popup's lifetime. They include repeated records holding heap addresses and small increasing IDs, and the item name as UTF-16 text: the popup creates UI objects rather than reading a "show popup" flag. A granted item (an inventory byte write) shows no popup. Showing one for a granted item would mean calling the game's own pickup or popup routine on the game thread, which needs the routine located (for example with an x64dbg write breakpoint on the pickup counter at `+0x3D0168` during a pickup) and code running inside the game (the proxy-DLL route). Not attempted. |
| Status | measured (the transient changes and their timing); assumed (that only a code call can reproduce the popup) |
| Method | bytes in `lab/mem/p3-record.log` that changed within 0.3 s of the pickup and later returned to their original value |
| Build | B1 |
| Evidence | `lab/mem/p3-record.log`, 2026-10-20. |

## The loaded save's randomizer section is at +0xCDD6D0

| Field | |
|---|---|
| Value | When a save loads, the game copies its randomizer section, 0x908 (2,312) bytes from file offset `0x12814` (location list, 4 zero bytes, item table at `+0x26C`, settings), to `Zexion.exe+0xCDD6D0`; the generator and the settings code read it there. The per-slot copies are at `+0x347830 + (slot − 1) × 0x908` (slot 30's is the `+0x357E18` mirror above). With slot 28 loaded, all 2,312 bytes at `+0xCDD6D0` equaled `Save Game 28.sav` from `0x12814`. Whether it follows a switch to another save without restarting the game is not yet shown. |
| Status | measured (the copy and its size from the code; the contents live for one loaded save) |
| Method | disassembly of RVA `0x9C630` (copy of `0x908` bytes into `0xCDD6D0`); live comparison with `client/game.py` |
| Build | B1 |
| Evidence | `lab/data/all.asm` (local); `lab/work/slot28-live-check.sav`, 2026-10-21. |

## Telling play from menus, the title screen and death

| Field | |
|---|---|
| Value | The game is in play, the state a client may write items in, when three things hold. **A save is loaded:** the byte at `+0x3D016A` is not `0xFF`. Loading a save copies the slot's empty marker (file `0x9360`, `save-format.md`) into it (read at RVA `0x9D7EC`, stored at `0x9D7F3`), the load path then sets it to 0 (`0x9F267`), four places set it to `0xFF` (`0x812E5` and `0x94849`, which run on a return to the title, and `0x78C54` and `0x944C9`, which look like reset routines, so a title screen reached at start-up probably reads `0xFF` too, not measured), and other code turns the player object off while it is `0xFF` (`0x1633C6`, `0x7A864`). **No menu is open:** the byte at `+0x345F03` is 0. It is 3 while the pause menu opens and while the quit and file-select menus are up, 2 in the pause and map screens, 1 for a moment while one closes, and 0 otherwise, including on the idle title screen. Around the S-Blade item popup or the teleport after it (which of the two is not known), it was 3 for about 1.5 s, so that moment reads as a menu. **The player is alive:** current energy (player state `+0xCEA`, signed) is above 0. It read −1 from the death until the respawn, through the whole death screen; that 0 also counts as dead is an assumption. Watched with `Game.state()`, a pause, a death, a quit to the title, file select and a reload came out as menu, in play, dead, in play, menu, no save loaded and in play, in that order. In an earlier run that recorded the three bytes but not `Game.state()`, they kept their in-play values through saving at a save point, room transitions (doors, pipes, screen edges) and a boss fight, apart from the 1.5 s above. **The load race:** the load routine (RVA `0x9C630`, also what respawns the player after a death) sets the save-loaded byte (`0x9D7F3`) and current energy (`0x9DA5A`) before it copies the inventory back from the save (from `0x9DCEE`). For that moment one reading says in play while a write to the inventory would still be overwritten, so `Game.settled_state()` reads the state twice 0.1 s apart, and `grant.py set` checks again 0.1 s after writing (from disassembly; the race itself has not been caught live). Candidates that looked like a state and are not: `+0x3E2B78` (16-bit) changes with the area (1 at the start, 22 near the boss, 2 after a teleport, 128 and 129 in another room, `0x028F` on the death screen, `0xFFFF` in menus), so it is probably the music or area; `+0x347402` only ever counts up; `+0x3CF6B0` to `+0x3CF6B4` are set by the input code (RVA `0x7F74C`, analog thresholds and button bits); `+0x3EC510` (player state `+0x120`) blinks while the player is hit. |
| Status | measured (each state's values over one session; the meaning of `+0x3D016A` from disassembly; `Game.state()` live through pause, death, title, file select and a reload; the three bytes, not `Game.state()`, through saving, room transitions and a boss fight); assumed (energy 0 as dead); not tried (cutscenes, a teleport and an item popup told apart, other item popups); seen once: the prompt after grabbing Zexion reads as menu ("The goal: Zexion's inventory byte at the grab") |
| Method | `memscan.py snap --module-only` in steady states (play in three areas, pause, map) and 130 snapshots back to back (about 0.85 s apart) across play, title, file select, a load and a death, labeled by state from the `memscan.py record` log of the leading bytes, with the combined rule then giving not-in-play for every snapshot labeled otherwise and in-play for every play snapshot; `memscan.py classify`, then a search for bytes whose play value never occurs outside play (none alone; the rule combines three); disassembly of each byte's readers and writers; `grant.py watch` printing `Game.state()` changes, polling every 20 ms |
| Build | B1 |
| Evidence | 2026-10-21, slot 28 as backed up that morning, then played forward (the S-Blade boss). Snapshots `lab/mem/st-play-*`, `st-pause-*`, `st-map-*`, `st-burst-001` to `130`; logs `lab/mem/st-rec-leads.log`, `st-rec-confirm.log`; the `Game.state()` run is in `lab/grants.jsonl` (10:42:04 to 10:43:35). The `st-dead-*` snapshots were taken after the death screen had faded and are play. |

## The goal: Zexion's inventory byte at the grab

| Field | |
|---|---|
| Value | The game's main ending starts when the player grabs Zexion (item 31) in the core, at map cell (32, 45); the game's own "Abridged Finale" setting declares victory at that moment (its text: victory "when you first grab Zexion in the core"). Getting into the core needed the four keys in the owner's normal-mode game; in the randomizer, whether the keys gate anything depends on its Gates setting (`randocells.md`, "The randomizer menu's settings"). `RandoCells` never mentions the keys, and its graph reaches (32, 45) from the default start with every item except the keys. At the grab, item 31's inventory byte (`+0x3ED47E + 31`) went 0 → 1, and the pickup counter did not change (the pickup routine skips items 27 to 31; "Granting each kind of item, and what keeps it"). About 2.8 s later `Game.state()` read "menu" ("Telling play from menus, the title screen and death"): the owner saw a prompt, "Skip Escape and Final Battle?". The byte went back to 0 about 2.5 minutes later; by the owner's account that was around boarding the ship that starts the ending, and the credits and a "Mission Complete" screen followed. The game saved the slot twice in that time, and neither save holds item 31: the file after differs from the one before the core only at `0x934E`, play time (`0x9356`), `0x9362` (25 → 26), `0x93D6`, `0x93DA`, `0x93E2` and their copies in the second block, and `0x9415`. So the only sign of the goal found is the live byte going 0 → 1, between the grab and the ship; a client has to notice it while it lasts. |
| Status | measured (the byte, its timing, the counter and the saves, one ending, normal-mode save; the `RandoCells` reachability); reported by the owner (the keys gate the core in the normal game; the ship, the credits and the screens); text only, not run (Abridged Finale, a randomizer setting) |
| Method | `grant.py watch` at 20 Hz (the byte, the counter and `Game.state()`) on a copy of the owner's finished normal-mode slot 1 installed in slot 25; `save_lab.py diff` of the slot after each save; the ending's text from `LargeText.txt` and `SmallText.txt` in the game folder (read-only); `randocells.reachable_cells` from (13, 16) with the default preset's full pool, with and without the keys |
| Build | B1 |
| Evidence | 2026-10-21. Grab at 12:42:31.084, back to 0 at 12:44:58.721 (`lab/grants.jsonl`); saves `lab/work/slot1-for-core.sav` (before), `slot25-after-zexion.sav`, `slot25-after-ending.sav`; snapshots `lab/mem/core-burst-*`. |

## Byte patterns

| Field | |
|---|---|
| Value | Seven targets, each found by two or three patterns from different code; `client/game.py` requires every pattern to match exactly once in `.text` (overlapping matches counted) and all to lead to the same address, or it refuses to run. Only instruction-pointer-relative references are used. Before scanning, it identifies the build from the PE header (TimeDateStamp `0x6A3F82DE`, SizeOfImage `0x1204000` on B1) and refuses builds not listed in `client/patterns.py`, since a patch could keep the code and change what the bytes mean. The randomizer section is validated before use (154 distinct sorted cells on the map, a zero gap, item IDs 0 to 50). On B1: **map_grid** → `+0x3E469D` (from instructions at RVA `0x16E30C`, 33 fixed bytes, and `0x1FD3E7`, 25); **inventory** → `+0x3ED47E` (from `0x1E36A9`, which writes item 2's byte, and `0x1E5BA1`, which reads a pointer field of the player state, plus `0x71E`); **pickup_counter** → `+0x3D0168` (from `0x1216F` and `0x167DB6`); **rando_blob** → `+0xCDD6D0` (from `0xEB6EC` and `0xECABD`, which take its start, and `0xA0ECF`, which reads a settings field `0x6C4` into it, so a layout change inside it makes them disagree); **slot_array** → `+0x230600` (from `0x1741C4` and `0x176140`); **menu_state** → `+0x345F03` (from `0xA905A`, which stores 2, and `0x15AAB4`, the only store of 0); **save_loaded** → `+0x3D016A` (from `0x1633C6`, which compares it with `0xFF`, `0x9F267`, the load path setting 0, and `0x94849`, a return to the title setting `0xFF`; three functions). Both state targets' patterns stay unique with short jump distances wildcarded too. The pattern bytes, with wildcards over every relative or image-base address, are in `client/patterns.py`; `tools/make_pattern.py` regenerates candidates for a new build and `tools/check_patterns.py` checks them. |
| Status | measured (offline against the B1 exe, and live: every pattern matched once in the running game and the values read through them equal the loaded save byte for byte) |
| Method | `tools/make_pattern.py` on a copy of the exe (references found by disassembling each function from the unwind table; windows grown instruction by instruction until unique, at least 12 fixed bytes); `tools/check_patterns.py lab/data/6d8a94bd4d64/Zexion.exe` |
| Build | B1 |
| Evidence | 2026-10-21. `check_patterns.py` printed "matches B1" for all five targets offline; `check_patterns.py --live` with slot 28 loaded resolved the same addresses, and the inventory (51 bytes), map grid (2,916), randomizer section (2,312) and slot 28's block in the slot array (37,896) were identical to `Save Game 28.sav` (copy `lab/work/slot28-live-check.sav`); the pickup counter read 1, as saved at `0x935A`. A second live run after the review fixes identified the build as B1, resolved all patterns including the three for the randomizer section, and the validated seed (154 locations and their items), inventory, map and counter again equaled the save (`lab/work/slot28-live-check2.sav`). The two state targets were added later that day: both matched B1 offline, and live they resolved to `+0x345F03` and `+0x3D016A` with the rest unchanged and the seed again valid. |
