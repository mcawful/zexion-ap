# A Custom mode: design draft

**Status: a proposal, not built.** Nothing here is in the world, the client or the helper yet.
It drafts how the YAML could offer every setting of the game's own randomizer, following the
game's rules for them, for the owner to agree before any of it is implemented. The game's side
of every statement below is recorded, with its evidence and status, in
[`re/randocells.md`](re/randocells.md), from "The randomizer menu's settings" to "What each
setting does in the game". Measured there means seen on build B1, by disassembly or live.

The owner's principle: follow the game's built-in logic and rules, and add no protection the
game does not have. So the heart of Custom is the game's own seed rules, how the settings
become a pool, the starts and the logic, copied into the world's generation ("How the world
follows the game's rules"); the YAML is only the way in.

## What the game offers

The randomizer menu has a mode (Easy, Standard, OHKO, Custom), a seed, six amounts, 27 items
each in one of four states, and 20 settings on Edit Options. The mode is not stored: the game
calls any state that is not exactly a preset Custom, so "Custom" means "every one of these is
the player's". B1 shows no way to set No Guarantee or "?????", whose text exists.

| Setting | Values (default in Standard) | What it changes |
|---|---|---|
| Starting / pool Energy Tanks | 0 to 40 together (3, 17) | the pool; code 62 and code 69's scale |
| Starting / pool missiles | 0 to 999 together (3, 122); the pool's also limited by its free places | the pool (containers and boxes) |
| Starting / pool Mega Bombs | the same (0, 12) | the pool |
| 27 items | start, random (a coin flip: start or pool), pool, excluded | the pool, the starting items |
| Random Gun | off, on (on) | which gun is started with |
| Guns First | off, on (on) | the pool, the logic's item redirects, the guns in game |
| Bulk Missiles, Bulk Bombs | off, on (off) | the pool |
| Box Compression | off, on (on) | the pool (more boxes, more filler) |
| Hints | None, Vanilla, Vanilla+, Small, Normal, Large, Full Map, Revealed (Normal) | the map's rectangles and the navigation rooms |
| Gates | Off, Vanilla, Unlocked, Open (Unlocked) | the gates in game; Open also the logic |
| Starting Location | Random Start or one of 34 rooms (Random) | the start |
| Double Teleport | off, on (off) | a second start; the logic |
| Start Keys | 0 to 4 (0) | starting keys, fewer keys in the pool |
| X Crystals, X Blocks | off, on (off) | blocks removed in game; the logic (code 68) |
| Abridged Finale | off, on (off) | victory at the grab; the logic (code 68) |
| Secret Shuffle | off, on (off) | a 155th location; the logic (code 68) |
| Safety | Light, Early, Normal, Off (Normal) | the logic (codes 62, 69); Random Start |
| Obscure Majors, Minors, Class | off, on (on, on, off) | the map's markers only |
| Show Obtainable | off, on (on) | the map only |
| Colorizer | off, on (the player's) | colors only |

## The YAML

**Decided (the owner, 2026-10-27): the presets are shortcuts, and every setting is Custom's.**
`mode` keeps Easy, Standard and OHKO for players who want a short YAML, each exactly the game's
preset, as today: the item pool and starting items of one fresh save of that mode, and its
settings. None of a preset's settings can be changed, as in the game, where changing one makes
the mode Custom. A new `custom` value is the only way to set the options below: with a preset,
any of them set to something other than `preset` is refused (an `OptionError` naming it).

```yaml
Zexion:
  mode: custom                # easy | standard | ohko | custom
  custom_base: standard       # Custom only: the preset whose values 'preset' below means

  # The six amounts (the game's limits: Energy 40 in all; missiles and bombs 999 in all,
  # and the pool's no more than its free places allow, ten to a place)
  starting_energy_tanks: preset   # or 0..40
  pool_energy_tanks: preset
  starting_missiles: preset       # 0..999
  pool_missiles: preset
  starting_mega_bombs: preset     # 0..999
  pool_mega_bombs: preset

  # Each of the 27 items, by the game's name: start | random | pool | excluded.
  # Items not listed keep custom_base's state.
  custom_items:
    Machine Gun: start
    Arc Plasma: random
    Navcore: pool

  random_gun: preset          # on | off
  guns_first: preset
  bulk_missiles: preset
  bulk_bombs: preset
  box_compression: preset
  hints: preset               # none | vanilla | vanilla_plus | small | normal | large |
                              # full_map | revealed
  gates: preset               # off | vanilla | unlocked | open
  start_room: automatic       # random_start | exodia | upper_tower | ... | waste_zone: the
                              # game's 34 room names (randocells.md, "Starting Location and
                              # the second teleport target"); today's names (kresezoa_16_13)
                              # kept as aliases; automatic: Landing Pod
  double_teleport: preset
  start_keys: preset          # 0..4
  x_crystals: preset
  x_blocks: preset
  secret_shuffle: preset
  safety: preset              # light | early | normal | off
  obscure_majors: preset
  obscure_minors: preset
  obscure_class: preset
  show_obtainable: preset
  abridged_finale: preset     # the game's own: victory when Zexion is first grabbed

  # Unchanged: goal, os_install_disks, os_install_disks_required, area_map_hints,
  # allow_assists, death_link
```

**The game's own words.** Each option is named as the game labels it (its `display_name` is
the menu's label: "Box Compression", "Hints", "Start Keys"), and its description is the blurb
the game shows when the setting is selected, word for word as the screen shows it (the game's
`Text` file, which differs from the translation's `LargeText.txt` in four blurbs); a setting with several values
gives each value's blurb (Hints, Gates, Safety, Start Keys), and each value is named by the
game's label for it (`vanilla_plus` for Vanilla+). Both are tied to the setting's byte in the
game's code, not matched by name: the screen's builder passes each widget its label and blurb
by number (`randocells.md`, "The randomizer menu's settings", which lists every label, blurb
and byte). Anything the world adds to a description (a limit, what `preset` means) follows the
game's text. The amounts and the 27 items take the first screen's labels ("Starting Energy
Tanks", "Item Pool Missiles"; ":Starting Item", ":Random", ":Excluded") and its blurbs
("Choose the items you start the game with.", "Choose the items to find throughout the
world."). `preset` is each option's default, so a Custom YAML that sets nothing is
`custom_base` exactly.

**Abridged Finale.** The game's Abridged Finale ("Victory is achieved when you first grab
Zexion in the core.") does what an `escape_skip` option the owner has discussed (not in the
repo) was for: no SKIP ESCAPE prompt (measured by disassembly) and, assumed from the code and
not run, the ending soon after the grab with no escape and no final battle. It is a Custom
setting like the rest: no preset has it, and turning it on in the game's menu makes the mode
Custom. So a Custom player gets the game's own version, and a preset player keeps what the
helper does today (it offers the escape skip in any game, `native/README.md`, "The escape
skip"). Decided (the owner, 2026-10-27): no `escape_skip` option; Abridged Finale is the
game's form of it. Which other features of the world, the client and the helper the game's
Custom settings make redundant: "What the settings make redundant in zexion-ap". Colorizer
stays out of the YAML: the game treats it as the player's own (the mode ignores it, the mode
buttons leave it), and the client leaves it as the menu has it, as it does today.

No Guarantee is not offered: B1's menu cannot set it. Archipelago's own `accessibility:
minimal` is the nearest thing, and applies already.

## How the world follows the game's rules

**Refuse what the game's menu cannot hold** (an `OptionError` naming the option and the limit),
rather than clamping. The game clamps in its menu, but a YAML has no menu to show the result:

- starting plus pool Energy Tanks above 40; starting plus pool missiles, or Mega Bombs, above
  999;
- the pool's fixed items above its places: 4 − Start Keys keys, each pooled item and the pool
  Energy Tanks, against 154 places (155 with Secret Shuffle). Above 155 the game itself stops
  (`randocells.md`, "The pool", step 4), and the helper runs the game's placement before
  writing the seed, so this one must never reach the game;
- pool missiles or Mega Bombs above ten per free place, the menu's limit
  (`randocells.md`, "The randomizer menu's first screen: items and amounts").

**Build the pool as the game does** ("The pool"): the coin flips for `random` items, Random
Gun's draw, Guns First's swap, the keys, the pooled items, the pool Energy Tanks, then missiles
and bombs split into containers, Mega Bombs and boxes by the Bulk and Box Compression rules,
then filler with the game's odds (Scrap Metal 1/2, Scrap Energy and Scrap Chunk 1/16 each, Area
Map and Energy Shield 3/16 each). The draws use the world's own random numbers: the result has
the game's counts and odds, not the game's exact seed, which needs only the menu's settings to
match. (The game's generator could be reproduced to the item from its seed text,
`lab/rando-options/agentB/sim_pool.py` does it for steps 1 to 7, but nothing needs that.) The
game's later passes, which move items and turn ten Missile Containers into a box and nine Scrap
when filler runs short, are its placement, which Archipelago's fill replaces.

**Keep the locations the game's logic can reach**, with every item from the start (and the
second start): the others hold Scrap, as the game's own compression leaves them. Today the
reachable locations and the start rooms are worked out per preset by
`tools/gen_world_data.py` into `worlds/zexion/data.py`. Custom's depend on Safety, the Energy
total (code 62 and code 69 scale by it), X Crystals, X Blocks, Abridged Finale and Secret
Shuffle (code 68) and Gates Open, too many combinations to work out ahead, so the world works
them out at generation time with `logic.py` over the region graph `data.py` already holds.

**Starts, as the game picks them** ("Starting Location and the second teleport target"):

- A named room: any of the 34, as in the game, which checks nothing more than its winnability
  check. The world refuses one from which the starting items reach no location: Archipelago's
  fill cannot start there (the game would offer PLAY ANYWAY).
- `random_start` (and `random`): the game's rule: the unflagged rooms unless the starting
  score (starting Energy + missiles + bombs + starting items) is 27 or more; a room from which
  the starting items reach at least one location; with Safety other than Light, Upper Tower,
  Tower Gauntlet, Guardia Tower and Lower Tower kept one time in three.
- `automatic`: Landing Pod, as today, when the rules allow it.
- Double Teleport: the second target is any room but the start, at random; the logic connects
  it at once if it is unflagged, and for a flagged room only once the "useful item" score
  (Energy, doubled by the Magnus Armor, + missiles + bombs + ten per box + the menu items
  owned but the Navcore and the Magnus Armor) is 27 or more. That rule is new to the world's logic.

**The goal.** The game's winnability check needs Zexion's cell and the four keys; the world's
core goal already needs the keys and the core. Abridged Finale ends the game at the grab, with
no escape and no escape skip.

## What each setting takes, ranked by cost and risk

Cost is the work in the world, the client and the helper; risk is what could go wrong in a
seed or in game. The shared prerequisite, **working out the reachable locations and the start
rooms at generation time** instead of per preset, comes first; it is medium in cost and low in
risk (the same `logic.py`, checked against the presets' precomputed data in tests).

| Rank | Setting | Cost | Risk | What it takes |
|---|---|---|---|---|
| 1 | Colorizer | none | none | Left to the player, as today. |
| 2 | Obscure Majors, Minors, Class | low | low | Pass through the menu's settings (the client's `menu` lines already carry all but Colorizer). The helper's raising of item-48 squares reads Obscure Majors and raises a shown minor too (`native/src/mapmarks.cpp`, `important_value`), and under Obscure Class the game's reveal leaves it nothing to raise; with Majors on and Minors off it would show a stand-in as a shown major where the game obscures its own (`memory.md`, "Map markers"): handle that case, and check it and Class in game. |
| 3 | Show Obtainable | low | low | Pass through. Display only, but what the game's map flood shows over an AP item table (other players' items as Scrap Chunk) is not known: check in game. |
| 4 | Hints | low | low | Pass through: the game's own placement, which the helper runs, makes the rectangles. Revealed sets flag 291 from the start, which also sets the scanner's flag 39, so the OS Install Disks goal's count message never runs (`memory.md`, "The scanner"). The client takes flag 39 for "told" (`client/session.py`, `SCANNED_FLAG`), so the disks' popups give the number needed from the start (`client/discs.py`): check it in game with Revealed. |
| 5 | Safety | low | low | `logic.Settings.mode69` already; the rest is the prerequisite. |
| 6 | Start Keys | low | low | Start with the first *k* keys from the Alpha Key; the pool drops the Omega Key first. The client already writes keys as a count (`client/newgame.py`, `start_with`). |
| 7 | X Crystals, X Blocks | low | medium | Code 68 already in `logic.py`; pass through. The removal runs in the new game's event chain from the section's settings, so it comes with the menu's settings. Assumed: the slot state 4 means "gone". Check in game, and that the core's 2095 object under X Blocks changes nothing for the goal. |
| 8 | Abridged Finale | low | medium | Code 68; pass through; Custom only, the game's own form of the discussed `escape_skip`. Assumed, not run: the ending starts after the grab with flag 250 and flag 211 clear, which is the client's goal. Run it once first. With the OS Install Disks goal it would end the game at the grab without the disks: refuse the pair, or let it be (the owner's call). |
| 9 | Box Compression, Bulk Missiles, Bulk Bombs, the amounts | medium | low | The pool rules above, in the world, with tests from fresh saves of new seeds (Box Compression off, each Bulk on, both amounts 20 or more): generate them in the menu and read the saves, read-only. Starting missiles are given as the new game splits them, tens as Missile Boxes and the rest as up to 9 containers (`client/newgame.py`, `start_with`), so the world must hand them out in that form; starting Mega Bombs need the client's new-game bombs (`worlds/zexion/starting.py`: not supported yet). |
| 10 | The 27 items, Random Gun | medium | low | The four states and Random Gun's draw in the world; the start-with array already carries any of the 27. An excluded item is simply absent. |
| 11 | Starting Location, Double Teleport | medium | medium | The game's room rules above at generation time; the flagged second target's score rule as a new entrance rule. The start table stays B1-only (`client/newgame.py`). |
| 12 | Gates | low (Off, Vanilla, Unlocked); medium (Open) | medium | Pass through. Open adds the game's gate floods to the logic: entrances among (24, 44), (12, 30) and (14, 2) once one of them or (51, 34) is reached. Not known: what stands between the player and the core under Off and Unlocked (`randocells.md`, "Not yet found"); play each once. |
| 13 | Guns First off | high | medium | With it off the game applies no item redirects: each of the five upgrades is its own item, useless without its gun, and the guns are no longer progressive. New items for Pulse Wave, Pentashot, Tunneler, Focal Crystal and Plasma Burst; `logic.py`'s redirects conditional; the client's inventory writes and reconciliation for upgrades as their own bytes. A mistake here can lose or double an item. |
| 14 | Secret Shuffle | high | high | A 155th location, the Bomb Box at X48 Y20, enters the game's location list at position 134 and moves the 20 after it up by one. The client, the helper and the save patch index everything by that list and count 154 (`client/ids.py`, `save_patch.py`, `newgame.py`). The location ids would stay by cell, with a per-seed map to the game's list. A mistake sends or gives the wrong item. |
| — | No Guarantee | none | none | Not offered (not on B1's menu). |

## The new game

**The principle (decided, the owner, 2026-10-27): the game's menu holds the seed's settings, and
AP changes only what is AP's.** A new file is made by the game's own placement from a menu state
that is exactly the seed's: a preset's, or the Custom settings. So everything the game derives
from its settings comes out as the game makes it: the settings code and the view of it in the
pause menu (OPTION MENU, RANDO, RANDO SETTINGS), the Hints' map rectangles, the blocks X Crystals
and X Blocks remove, Abridged Finale's events, Revealed's map, the Colorizer's colors, the key
markers. Then AP writes only its own part, as the client does today (`client/newgame.py`): the
item table (this world's items, and stand-ins for other worlds' items and for the OS Install
Disks), the start room and the second teleport target the world chose, the starting items and
amounts (the coin flips and Random Gun as the world resolved them, and any `start_inventory`),
and the starting keys; and the client checks that the settings the logic reads came out as the
seed's. In play, the client and the helper add what AP needs on top: received items, the OS
Install Disks goal, the map's markers for stand-ins, Intel Chips, DeathLink, the escape skip's
offer. One small addition would help: setting the menu's Starting Location to the world's start
room, so the pause menu's view names the room instead of reading Random Start (seen in an AP
save: `re/randocells.md`, "The settings code").

**ARCHIPELAGO** already sets the whole menu state (seed text, both item arrays, the six amounts
and settings t10 to t73 but Colorizer) from the slot data, runs the game's placement, checks
the section and writes the seed over it (`memory.md`, "The game-mode panel"). A Custom seed's
slot data carries its own settings there instead of a preset's; the checks (`expect` lines)
grow to every setting the logic reads: t30 to t33, t37, Safety, Gates and the amounts.

**The seed's settings in game.** The pause menu's OPTION MENU, RANDO, RANDO SETTINGS shows a
save's seed, mode, items, amounts and settings, decoded from the settings code the save keeps
(`re/randocells.md`, "The settings code"). The game's placement writes that code from the menu
state it ran with, so a Custom seed started through ARCHIPELAGO shows its own settings there
with nothing more to build; today's AP seeds show their preset.

**RANDOMIZER**, the fallback, has the player set the menu by hand: easy for a preset, slow for
Custom (20 settings, 27 items, six amounts). The game's Paste←Clipboard takes the seed text and
the settings code ("The settings code"), whose format is known exactly, so the client could put
that on the clipboard, or print it, for the player to paste.

## What the settings make redundant in zexion-ap

A review (2026-10-27) of the world's, the client's and the helper's own features against the
game's settings, to find which a Custom mode could hand back to the game. It reads the code as
it is on `develop` and the game's side as `re/randocells.md` records it; nothing here was run in
game for this review.

**Two things limit what a setting can replace.**

- **A preset seed keeps every preset value**, as decided: Easy, Standard and OHKO stay exactly
  the game's presets. A feature a Custom-only setting covers is covered only in a Custom seed
  with that setting on, and the presets still need it. One exception the game itself makes: its
  mode check skips Starting Location (t22 to t27), Safety (t42, t43; OHKO wants Light or Off)
  and Colorizer (`re/randocells.md`, "The randomizer's modes"), so a preset seed's menu can name
  a start room and still read as its preset.
- **ARCHIPELAGO skips START GAME's first step** (`re/memory.md`, "The game-mode panel"): the
  helper writes the start and the second target itself, and the game's Starting Location and
  Double Teleport draws never run on that path. So those settings can name what the world chose,
  for the settings view, but cannot choose it.

| Feature | Where it lives | Game setting | Covers it? | What retiring it would take | Risk |
|---|---|---|---|---|---|
| The `escape_skip` option (discussed, never in the repo) | — | Abridged Finale (t32) | Fully | Nothing: dropped by the owner, 2026-10-27 ("The YAML", Abridged Finale). | None |
| The escape skip offer (event 3665's else branch pointed at the offer) | `native/src/skip.cpp`; the offline look; the Goal option's text in `worlds/zexion/options.py` | Abridged Finale (t32) | Fully in a Custom seed with it on: event 3583 sets flag 289 at the grab, and event 3668 offers nothing while 289 is set, the helper's patch included (measured by disassembly; the ending's path past the grab assumed, not run). No for presets, and for Custom with it off | Keep the patch: under Abridged Finale it changes nothing. Dropping it everywhere would leave the core goal's seeds with the escape and the final battle to play, which the world's logic does not cover ("the seed only makes sure you can reach Zexion", the Goal option): their needs would have to be measured and put in the goal's rule first. The patch, the offline look's part, the client's check of it (`client/ticks.py`; `skip_check_at` in `client/state.py`), its tests and the Goal text would then go; the event table's targets stay, since RETURN HOME WITH THE DISKS? uses them | High if dropped before the escape's needs are known (a seed that reaches Zexion but cannot be finished); none if kept |
| The start room (`start_room`, `AUTOMATIC_START`, the start record write) | `worlds/zexion/options.py`, `world.py` (`generate_early`); `client/newgame.py` (`write` of `START_RECORD`) | Starting Location (t22 to t27) | Partly. The game's 34 rooms hold every room the world offers (Easy and Standard 9, OHKO all 34; `data.MODES`), and a named value writes a start record of the same form (room index, column, row: `re/randocells.md`, "Starting Location and the second teleport target"). But ARCHIPELAGO skips the step that reads it, Random Start's rule differs from the world's, and the world's fill must know the room | Not retirable: the world still chooses and the client still writes. What the setting adds is the view: setting it to the world's room names the start in RANDO SETTINGS, in a preset seed too, since the mode check skips it (above) | Low |
| The second teleport target (`pick_second_start`; the second start and the start table) | `world.py`; `client/newgame.py`; `native/src/menu.cpp` (the start table filled in) | Double Teleport (t33) | Partly: the setting turns it on, but the game draws the room from its own seed and ARCHIPELAGO skips that draw. The world's rule (the two starts reach at least `MIN_SPHERE_ZERO` locations together) is the world's own | Replacing the world's rule with the game's (any room but the start; a flagged one in the logic only from a score of 27: "Starts, as the game picks them") is in the draft already. The writes stay | Medium (a new entrance rule) |
| Starting keys | `client/newgame.py` (`start_with`; writes t34 to t36); `worlds/zexion/starting.py` (the first keys from the Alpha Key) | Start Keys (t34 to t36) | Fully, by the code: the client gives starting keys by writing the game's own Start Keys bits (`client/newgame.py`), and the new game gives the first *k* keys from them (`0xA11A5`, by disassembly: `re/randocells.md`, "What each setting does in the game"). No seed starting with keys has been run in game | Nothing to retire. In Custom, `start_keys` is the menu's value and start_inventory keys add to the count | None |
| The key markers, the scanner | the game's new game (event chain 3329 to 3378, from the item table); `client/save_patch.py` (the offline reference only) | Start Keys, Gates | Not an AP feature: the game draws the markers from AP's item table already, so a key in another world or started with has none. Gates changes which gates pass, and with Open the logic (`re/memory.md`, "Keys, the gates and the key markers"), not the markers, and the core goal's four keys are the game's winnability check's own | — | — |
| The starting items and amounts (the start-with array and the amounts written over the placement) | `client/newgame.py` (`start_with`) | The 27 items' Start state, Random Gun, the six amounts | Partly: the menu holds the seed's starting items and amounts, and the new game gives them. But the game resolves `random` items and Random Gun from its own seed text, and the world from its random numbers, and start_inventory is AP's alone | Only if the world reproduced the game's generator from the seed text (`lab/rando-options/agentB/sim_pool.py` does steps 1 to 7) so both agree; not worth it. Keep the writes | Medium if retired: wrong starting items |
| No starting Mega Bombs | `worlds/zexion/starting.py` (`NEVER`) | Starting Mega Bombs | Closes a gap rather than replacing a feature: Custom's `starting_mega_bombs` and a start_inventory Mega Bomb both reach the game as the new game's bomb amount, which the client already writes. An in-game run of a seed starting with bombs would show the game's use of that amount | Lift the refusal after that run and after measuring where the save keeps current bombs (not known: `client/save_patch.py`, whose `patch` refuses starting bombs until then) | Low once both are measured |
| Per-preset reachability (`data.MODES`: locations kept, starts, regions, sphere 0), and OHKO's 4 locations left out | `tools/gen_world_data.py`, `worlds/zexion/data.py` | All the logic's settings, through Custom's prerequisite (working out reachability at generation time) | Partly: once the world works out the locations and starts from the settings, a preset is three settings states, and its precomputed lists become test oracles. The left-out locations already follow the game (Scrap where the game's logic reaches nothing) | Compute every mode at generation time; keep `data.py`'s per-mode lists only for tests. The pools stay read from the fresh saves | Low (the tests compare both), at a medium cost |
| Area Map Hints and Intel Chips | `worlds/zexion/world.py` (`hint_targets`); `client/hints.py`, `storage.py`; `native/src/intel.cpp` | Hints (t18 to t21) | No. The game's Hints are map rectangles that navigation rooms reveal, over this world's map only; an Intel Chip is an Archipelago hint, naming where one of the player's items is in any world, given by an Area Map. With Full Map or Revealed an Area Map's own reveal adds nothing, so there Area Map Hints off leaves Area Maps no use | — | — |
| The map's markers for stand-ins (item 48 raised to the important marker; the names under the cursor) | `native/src/mapmarks.cpp` | Obscure Majors, Minors, Class (t29, t38, t41) | Partly. Under Obscure Class on, the game's reveal takes minors down the major path (`re/memory.md`, "Map markers"), so the raise has nothing to do; under the other values it is needed. The helper reads Obscure Majors and raises a shown minor (`important_value`), but with Majors on and Minors off a stand-in's square shows as a shown major where the game's own major would be obscured: the grid value does not tell the cases apart (`re/memory.md`, "Map markers", assumed). The names under the cursor: no setting | Nothing to retire. Handle the Majors-on, Minors-off case, and check it and Obscure Class in game once Custom offers them | Low |
| Other players' items (labels, the logo, other Zexion players' items as themselves) | `client/aplabels.py`; `native/src/apitem.cpp`, `looks.cpp` | — | No | — | — |
| The OS Install Disks goal (the count, the scanner's line, the pause row, the equipment screen, the file select, RETURN HOME WITH THE DISKS?) | `client/discs.py`, `ticks.py`; `native/src/scan.cpp`, `pause.cpp`, `equip.cpp`, `files.cpp`, `mission.cpp` | — | No. Abridged Finale is another ending (at the grab), not the pod. Hints Revealed sets flag 39 from the start, which the client already takes for "told" (`client/session.py`, `SCANNED_FLAG`), so the popups give the number needed from the start while the scanner's message never runs (by the code; not seen in game) | — | — |
| Allow Assists (the ASSIST lock, Assist Unlock) | `worlds/zexion/options.py`, `items.py`; `client/`; `native/src/menu.cpp` | — | No: the randomizer menu has no assist setting, and the game lets a randomizer save use ASSIST (its MISSION COMPLETE has shown ASSISTS USED: `native/README.md`, "Feats and achievements in AP saves"). By the owner's principle it is a protection the game does not have: whether to keep it is the owner's call, not a redundancy | If dropped: the option, item 100 and its rule, the client's lock and the helper's menu part, with their tests and docs; YAMLs that set `allow_assists` need updating | Low |
| No Feats or achievements in AP saves | `native/src/feats.cpp` | — | No: the game earns Feats in every randomizer ending (`re/memory.md`, "The ending: flag 250") | — | — |
| The ARCHIPELAGO new game; the RANDOMIZER fallback | `native/src/menu.cpp`; `client/newgame.py` | — | No. For RANDOMIZER only, Paste←Clipboard with the settings code could replace setting a Custom menu by hand ("The new game") | — | — |
| The `expect` checks (Abridged Finale and the others the logic reads must be the preset's) | `client/newgame.py`; `world.py` (`checked_options`) | Every setting the logic reads | Not redundant: in Custom they check the seed's values instead and grow ("The new game") | — | — |
| Received items and popups, Energy Shields, DeathLink, the title lines, the file select's logo, the offline look | `client/`, `native/` | — | No | — | — |

**Recommendation.** Retire nothing in the world, the client or the helper for Custom. Only the
discussed `escape_skip` option is fully redundant, and it was never built. Every other overlap
is partial, or holds only in a Custom seed with one setting on, and the presets, kept exactly as
the game has them, still need the feature. Where a setting does the job, Custom should let the
game do it and the AP feature stays out of the way, as it already does:

1. **Abridged Finale** in place of the escape skip in Custom: the patch is inert there (by
   disassembly; not run), so nothing changes in the helper.
2. **Start Keys** through the bits the client already writes (by the code; no seed starting
   with keys run in game yet).
3. **Starting Location** set to the world's room, for the settings view, in Custom and, since
   the mode check skips it, in the presets too (the small addition in "The new game").
4. **Obscure** passed through; the helper's markers follow Obscure Majors while Minors is on,
   and the Majors-on, Minors-off case still needs handling (`re/memory.md`, "Map markers").

Two decisions belong to the owner, under the principle:

- **The escape skip with Abridged Finale off** (the open question below): keeping it is
  recommended until the escape's and the final battle's needs are measured, since without it a
  core-goal seed can be one the player cannot finish. Following the game strictly would mean
  adding those needs to the goal's rule first.
- **Allow Assists**: a protection the game does not have. Not made redundant by any setting.

The largest simplification on offer is not a feature but the per-preset data: once the world
works out reachability at generation time for Custom, the presets can use the same path, with
`data.py`'s lists kept as test oracles.

## Open questions for the owner

- Abridged Finale with the OS Install Disks goal: refuse, or allow?
- `start_room: random_start` (and Archipelago's own `random`): the game's Random Start rule
  (this draft), or today's rule (any room every item reaches all locations from, with at least
  three for the starting items)?
- Which settings to build first: this draft would start with Abridged Finale's in-game run,
  then the prerequisite and ranks 2 to 10.
- With Abridged Finale off, keep the helper's escape skip offer, or drop it to follow the game?
  ("What the settings make redundant in zexion-ap" recommends keeping it.)
- Allow Assists, a protection the game does not have: keep it, or drop it?
