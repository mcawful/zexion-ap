# Custom mode: design and build

**Status: steps A and B of "The build plan" built and run in game, 2026-10-27** ("The
build plan", step B, for what the run covered). `mode:
custom` and every option below are in the world (`worlds/zexion/options.py`, `custom.py`,
`world.py`), with Guns First off and Secret Shuffle on refused until step E, and starting Mega
Bombs until the client can give them. This page set out how the YAML offers every setting of
the game's own randomizer, following the game's rules for them, and records what was built. The game's side
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
settings. A preset's settings cannot be changed, as in the game, where changing one makes the
mode Custom, but for the two the game itself leaves out when it tells a preset (`0x90260`, the
masks at `0x905A5`; `re/randocells.md`, "The randomizer's modes"): `start_room` and `safety`
work with every mode (decided, the owner, 2026-10-27), Safety any value with Easy and Standard
and Light or Off with OHKO, as the game accepts. Colorizer, the third it leaves out, stays the
player's menu choice. A new `custom` value is the only way to set the other options below:
with a preset, any of them set to something other than `preset` is refused (an `OptionError`
naming it), and so is a Safety OHKO does not accept.

```yaml
Zexion:
  mode: custom                # easy | standard | ohko | custom
  custom_base: standard       # Custom only: the preset whose values 'preset' below means

  # The six amounts (the game's limits: Energy 40 in all; missiles and bombs 999 in all,
  # and the pool's no more than its free places allow, ten to a place). starting_mega_bombs
  # above 0 is refused for now: the client cannot start a game with Mega Bombs.
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
  guns_first: preset          # off refused until step E
  bulk_missiles: preset
  bulk_bombs: preset
  box_compression: preset
  hints: preset               # none | vanilla | vanilla_plus | small | normal | large |
                              # full_map | revealed
  gates: preset               # off | vanilla | unlocked | open
  start_room: automatic       # any mode. random_start (and 'random') | exodia | ... |
                              # waste_zone: the game's 34 room names (randocells.md,
                              # "Starting Location and the second teleport target"); the
                              # earlier names (kresezoa_16_13) kept as aliases; automatic:
                              # Landing Pod, or a random start where the rules refuse it
  double_teleport: preset
  start_keys: preset          # 0..4
  x_crystals: preset
  x_blocks: preset
  secret_shuffle: preset      # on refused until step E
  safety: preset              # any mode (OHKO: light | off). light | early | normal | off
  obscure_majors: preset
  obscure_minors: preset
  obscure_class: preset
  show_obtainable: preset
  abridged_finale: preset     # the game's own: victory when Zexion is first grabbed

  # Unchanged: goal, os_install_disks, os_install_disks_required, area_map_hints,
  # death_link; and allow_assists, shown as "Enable Assist Options", off by default
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
repo) was for: no SKIP ESCAPE prompt (measured by disassembly) and the ending soon after the grab with no
escape and no final battle (run in game, 2026-10-27, in a seed made in the game's menu). It is a Custom
setting like the rest: no preset has it, and turning it on in the game's menu makes the mode
Custom. Decided (the owner, 2026-10-27): Abridged Finale is the only way to skip the escape.
There is no `escape_skip` option, and the helper's escape skip offer (SKIP ESCAPE AND FINAL
BATTLE? in any game) is retired: removed 2026-10-27 (`re/memory.md`, "The escape skip"). A preset
seed with the core goal is then played as the game's own randomizer plays it: the escape and
the final battle are the player's to finish, and the world's logic makes sure only that
Zexion and the four keys can be reached: a seed can reach Zexion before the player has what the
escape needs. (The game's own winnability check, without Abridged Finale, asks instead for the
ship's cell, (1, 42), whose lists may stand for the escape's needs: assumed,
`re/locations.md`, "The core goal is Zexion's cell in the core".) Logged as a known
consequence; the player is expected to know it going in, and the world says nothing more
about it (decided, the owner, 2026-10-27). Which other features of the world, the client and
the helper the game's Custom settings make redundant: "What the settings make redundant in zexion-ap". Colorizer
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
second start): the others hold Scrap, as the game's own compression leaves them. They depend
on Safety, the Energy total (code 62 and code 69 scale by it), X Crystals, X Blocks, Abridged
Finale and Secret Shuffle (code 68) and Gates Open, too many combinations to work out ahead, so
the world works out the reachable locations, regions and start rooms when a seed is generated
(`worlds/zexion/reach.py`, step A of the build plan, for the presets too), with `logic.py` over
the region graph `data.py` holds. The per-preset lists `tools/gen_world_data.py` still writes
into `data.py` are the tests' oracles. The graph holds every cell the presets reach with every
item; with every item, no Safety or Energy total reaches a cell outside it, X Blocks reaches two
and Secret Shuffle six, none holding one of the 154 locations, and Abridged Finale the core,
which the world enters by its own entrance (`re/locations.md`, "The region graph holds what the
settings reach"). X Blocks' two cells are a shorter way between two regions (the Snake alone):
step B added them to the graph (`tools/gen_world_data.py`, `opened_cells`). Secret Shuffle's
155th location needs its cells added (step E).

**Starts, as the game picks them** ("Starting Location and the second teleport target"):

- A named room: any of the 34, as in the game, which checks nothing more than its winnability
  check. The world refuses one from which the starting items reach no location: Archipelago's
  fill cannot start there (the game would offer PLAY ANYWAY); and with the core goal one from
  which every item does not reach the core and Zexion, the world's part of that check. Built
  for every mode (`ZexionWorld.passes`): Easy takes 17 rooms, Standard 19 and OHKO all 34 (by
  the pair rule below), where the world offered 9, 9 and 34 before (the earlier rule, every
  item reaching all 154 locations and the starting items 3, stays in `reach.offered_starts` as
  the tests' check on `data.MODES`). **Archipelago's fill can fail from a room whose starting
  items reach one location:** it did in 1 to 5 of 20 solo generations for six of Standard's
  ten such rooms (the other four, and every room reaching 2 or more, 20 of 20; 2026-10-27, AP
  0.6.8, seeds 0 to 19, every other option at its default; a scratch script, not kept, so not
  reproducible as is), where the game's own placement repairs its fill. Decided (the owner,
  2026-10-27): follow the game's one location and accept an occasional failed generation,
  rather than ask for 3. Archipelago's fill succeeded from each room Standard and OHKO take
  whose starting items reach 3 or more, in tests (`worlds/zexion/test/test_custom.py`,
  `EveryStartFills`).
- `random_start` (and `random`; decided, the owner, 2026-10-27): the game's rule: the
  unflagged rooms unless the starting
  score (starting Energy + missiles + bombs + starting items) is 27 or more; a room from which
  the starting items reach at least one location; with Safety other than Light, the first of
  Upper Tower, Tower Gauntlet, Guardia Tower and Lower Tower to pass kept one time in three, once
  per seed (`custom.random_start`). The score counts the seed's own starting items, not
  `start_inventory`, as the game counts only its menu's.
- `automatic`: Landing Pod, as today, when the rules allow it; otherwise a random start, with a
  warning.
- Double Teleport: the game draws the second target from any room but the start, flagged ones
  too; the logic connects it at once if it is unflagged, and a flagged room only once the "useful
  item" score (Energy, doubled by the Magnus Armor, + missiles + bombs + ten per box + the menu
  items owned but the Navcore and the Magnus Armor) is 27 or more. Built in step D
  (`logic.useful_score`, `reach.second_counts`, the rule on the AP entrance
  "Second teleport target"), for every mode with Double Teleport, OHKO included. Where the game
  would offer PLAY ANYWAY for a pair its winnability check fails, the world draws among the
  rooms with which the pair suits the seed as a start does: the starting items collect a
  location, and with the core goal every item reaches the core's door (`ZexionWorld.partners`,
  `reach.second_starts`). OHKO still takes all 34 start rooms. **A guard the game does not
  have** (decided, the owner, 2026-10-28): the world draws a flagged room only when the logic
  counts it from the start, a starting useful-item score of 27, which no preset's starting items
  reach. A flagged room the logic counts only later adds nothing to sphere 0, and Archipelago's
  fill then failed often: of 360 OHKO generations with a flagged room drawn, 28 failed to fill (13 of 20 from White Crysmoon, 7 of 20 from Magma Chamber, whose starting items reach one location), and none of 680 with an unflagged room (`re/locations.md`, "OHKO: two starts, and only the
  locations they reach"). Random Start with Double Teleport
  does not copy the game's bug, which draws the second target in place of the start ("Starting
  Location and the second teleport target").
- **What the world adds to the game's rules, as built.** An upgrade started without its gun is
  started as the gun: the world holds each gun and its upgrade as one progressive item. Where
  the pool has too little filler for the locations left out (and the OS Install Disks), ten
  Missile Containers or Mega Bombs become a box and nine Scrap Metal, as the game's placement
  makes filler when it runs short (`custom.compress`), in every mode. A pool longer than its
  places is refused, where the game would drop entries at random. Gates follows the game:
  Open's gate travel is in the logic (step D, `reach.gates_triggered`, entrances among the gate
  cells' regions), but for R'Talva's gate (rank 12, a guard), and Off, Vanilla and Unlocked change nothing in it, as in the game's. Gates
  Off is taken with the core goal since step D: by the game's data files, the core's door is
  four key slots that read the keys alone, and only the four hyperspace gates read Gates
  (`re/memory.md`, "Keys, the gates and the key markers"), so Off should disable the gates'
  travel and not the way to the core, and Unlocked's two-gates-visited check hold back only
  that travel too (what opens the door once its slots are triggered was not traced, and
  neither has been played). A start room also needs
  every item to pass the core's own lists after the door (`data.CORE`), and the world counts a
  started gun and its upgrade as both, as the game does (found by the `logic-reviewer`,
  2026-10-27).

**The goal.** The game's winnability check needs a cell holding Zexion and the four keys:
without Abridged Finale only the ship's, (1, 42), since its logic enters the core only with it;
with it the core's, (36, 42), too. The world's core goal needs the keys and the core's cell,
entered as the game's logic enters it under Abridged Finale (`re/locations.md`, "The core
goal is Zexion's cell in the core"). Abridged Finale ends the game at the grab, with no escape
and no escape skip. With the OS Install Disks goal it would end the run at the grab without the
disks, so the world refuses Abridged Finale with that goal (an `OptionError` naming both;
decided, the owner, 2026-10-27).

## What each setting takes, ranked by cost and risk

Cost is the work in the world, the client and the helper; risk is what could go wrong in a
seed or in game. The shared prerequisite, **working out the reachable locations and the start
rooms at generation time** instead of per preset, comes first; it is medium in cost and low in
risk (the same `logic.py`, checked against the presets' precomputed data in tests). Built in
step A (`worlds/zexion/reach.py`).

| Rank | Setting | Cost | Risk | What it takes |
|---|---|---|---|---|
| 1 | Colorizer | none | none | Left to the player, as today. |
| 2 | Obscure Majors, Minors, Class | low | low | Pass through the menu's settings (the client's `menu` lines already carry all but Colorizer). The helper touches only stand-in squares (other worlds' progression and this world's OS Install Disks, item 48), raising a minor's marker to a major's (`native/src/mapmarks.cpp`, `important_value`), and reads all three settings (step C). In every preset (Majors and Minors on, Class off), with Majors off (a shown major, as the game shows its own) and with Obscure Class (the game already gives item 48 the major path, which the helper leaves as it is) the markers match the game's majors. Under Majors on with Minors and Class off a stand-in revealed by a hint, a map room or Hints Revealed holds a minor's shown value while the game's own majors there are obscured; the grid's base tells a revealed square from one seen directly (`0xC` revealed by a hint, `0x18` explored: measured in game for minors and a navigation room's hint, 2026-10-27; a map room's reveal by disassembly), so the helper obscures a revealed stand-in and shows a seen one. An Area Map's reveal (with Area Map Hints off) also leaves base `0xC` but shows majors, which the value cannot tell (`memory.md`, "Map markers"). Checked in game with Class off and on, 2026-10-27. |
| 3 | Show Obtainable | low | low | Pass through. Display only, but what the game's map flood shows over an AP item table (other players' items as Scrap Chunk) is not known: check in game. |
| 4 | Hints | low | low | Pass through: the game's own placement, which the helper runs, makes the rectangles. Revealed sets flag 291 from the start, which also sets the scanner's flag 39, so the OS Install Disks goal's count message never runs (`memory.md`, "The scanner"). The client takes flag 39 for "told" (`client/session.py`, `SCANNED_FLAG`), so the disks' popups give the number needed from the start (`client/discs.py`): check it in game with Revealed. |
| 5 | Safety | low | low | `logic.Settings.mode69` already, and the prerequisite is built (step A: Safety changes the start rooms the world offers, `worlds/zexion/test/test_reach.py`); what is left is the option. |
| 6 | Start Keys | low | low | Start with the first *k* keys from the Alpha Key; the pool keeps the Omega Key longest (it holds the keys from the Omega Key down). The client already writes keys as a count (`client/newgame.py`, `start_with`). Start Keys 4 gave all four keys in a seed made in the game's menu (2026-10-27). |
| 7 | X Crystals, X Blocks | low | medium | Code 68 already in `logic.py`; pass through. The removal runs in the new game's event chain from the section's settings, so it comes with the menu's settings. Measured in game (2026-10-27): the slot state 4 is "gone" (two rooms seen clear), and with X Blocks on, Abridged Finale's goal came as usual; its object of type 2095 at (32, 45), a Crysmoon room, changed nothing the owner saw. |
| 8 | Abridged Finale | low | medium | Code 68; pass through; Custom only, the game's own form of the discussed `escape_skip`. Run in game (2026-10-27, a seed made in the game's menu, no client): no escape and no prompt; the ending starts 8 s after the grab with flag 250 set and flag 211 clear, which is the client's goal (`re/randocells.md`, "What each setting does in the game"). The player's cell at the grab read (36, 42), Zexion's cell in the core, where the world's goal now is (`re/locations.md`, "The core goal is Zexion's cell in the core"); the game's own logic enters the core only with Abridged Finale on. Refused with the OS Install Disks goal (decided, the owner, 2026-10-27). |
| 9 | Box Compression, Bulk Missiles, Bulk Bombs, the amounts | medium | low | The pool rules above, in the world, with tests from fresh saves of new seeds (Box Compression off, each Bulk on, both amounts 20 or more): generate them in the menu and read the saves, read-only. Starting missiles are given as the new game splits them, tens as Missile Boxes and the rest as up to 9 containers (`client/newgame.py`, `start_with`), so the world must hand them out in that form; starting Mega Bombs need the client's new-game bombs (`worlds/zexion/starting.py`: not supported yet). |
| 10 | The 27 items, Random Gun | medium | low | The four states and Random Gun's draw in the world; the start-with array already carries any of the 27. An excluded item is simply absent. |
| 11 | Starting Location, Double Teleport | medium | medium | The game's room rules above at generation time; the flagged second target's score rule as an entrance rule (built in step D). The start table stays B1-only (`client/newgame.py`). |
| 12 | Gates | low (Off, Vanilla, Unlocked); medium (Open) | medium | Pass through. Open adds the game's gate floods to the logic: entrances among (24, 44), (12, 30) and (14, 2) once one of them is reached (built in step D); the game's logic also counts reaching (51, 34), which the world does not (below). Off and Unlocked act on the gates' travel only: the core's door reads the keys alone (`re/memory.md`, "Keys, the gates and the key markers", from the data files; not run in game under Off or Unlocked). Open: the game's logic lets reaching R'Talva's gate at (51, 34) open the travel, though its script waits for flag 155 (Aesos, by the menu's blurb); **a guard the game does not have** (decided, the owner, 2026-10-28): the world does not count it (`reach.GATE_TRIGGERS`), since a seed that did could not be beaten if the gate cannot be used then; whether play allows it is open (`re/randocells.md`, "Not yet found"). |
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
Install Disks goal, the map's markers for stand-ins, Intel Chips, DeathLink, the assist lock. One small addition, built in step B (`ZexionWorld.menu_setup`): the menu's
Starting Location set to the world's start room, so the pause menu's view names the room
instead of reading Random Start (seen in an AP save before it: `re/randocells.md`, "The
settings code"). Opening that view leaves the seed's state in the menu (same section), so a
RANDOMIZER game started afterwards tries only that room, not Random Start (by the code; not
tried).

**ARCHIPELAGO** already sets the whole menu state (seed text, both item arrays, the six amounts
and settings t10 to t73 but Colorizer) from the slot data, runs the game's placement, checks
the section and writes the seed over it (`memory.md`, "The game-mode panel"). A Custom seed's
slot data carries its own settings there instead of a preset's; the checks (`expect` lines)
cover every setting the logic reads: t10 (Guns First, added in step B), t30 to t33, t37,
Safety, Gates and the amounts (`custom.CHECKED`).

**The seed's settings in game.** The pause menu's OPTION MENU, RANDO, RANDO SETTINGS shows a
save's seed, mode, items, amounts and settings, decoded from the settings code the save keeps
(`re/randocells.md`, "The settings code"). The game's placement writes that code from the menu
state it ran with, so a Custom seed started through ARCHIPELAGO shows its own settings there
with nothing more to build; today's AP seeds show their preset.

**RANDOMIZER**, the fallback, needs only the seed's text (the owner's design, 2026-10-27). The
client's message gives the 16 characters to type (or paste) into an empty slot's RANDOMIZER
menu; the menu's other settings do not matter. When the player presses START GAME, the game
runs its placement from their menu; then the helper, seeing a queued new game whose section
holds the seed's text (`identify`), places it again as ARCHIPELAGO does: the menu set to the
seed's settings, the game's placement run, the player's menu put back, the checks, and the AP
seed written over it (`native/README.md`, "New games"; `client/newgame.py`). So the new game is
the same as an ARCHIPELAGO one of the seed. That includes the Hints' map rectangles: the
placement draws them from where its random numbers ended, which depends on the whole placement
run (`re/randocells.md`, "What each setting does in the game"), so writing the seed's settings
after the player's placement could not give them; running the placement again does. Its plan
carries no start table: RANDOMIZER is the fallback when the game's own table differs from the
seed's or was never measured on the build, and the game's own, built when the menu opened, is
used, as its START GAME uses it. This replaced (2026-10-27) the message's list of what to set
by hand (items, Start Keys, amounts, checked settings), with which a Custom seed set by hand
took the seed twice in game. Now working in game, 2026-10-27, B1 (helper built from `claude/randomizer-by-seed` a56a25e, client from source, `--keep-randomizer`; seed `lab/ap/live38`): a Custom seed's text typed over a menu set to Easy took the seed (slot 4), and its section was byte for byte the same as an ARCHIPELAGO game of that seed (slot 5), the Hints' rectangles included; a Standard seed's text over a menu set to OHKO took the seed (slot 6); another seed's text was refused and left the player's own OHKO game (slot 7). The live run's evidence: the slots' first saves, copied read-only to `lab/work/Save_Game_4-20261027-222549.sav`, `_5-20261027-222832`, `_6-20261027-222953` and `_7-20261027-223107`, and the client logs in `lab/ap/live38`.

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
| The escape skip offer (event 3665's else branch pointed at the offer; retired 2026-10-27) | Removed: it was `native/src/skip.cpp`, the offline look's part, the client's check and the Goal option's text | Abridged Finale (t32) | Fully in a Custom seed with it on: event 3583 sets flag 289 at the grab, and event 3668 offers nothing while 289 is set, the helper's patch included (measured by disassembly; in game, 2026-10-27, flag 289 was set at the grab and the ending followed with no escape and no prompt, the helper's patch off at the time). No for presets, and for Custom with it off | Done, 2026-10-27, by the owner's decision (below): the patch, the offline look's part, the client's check of it, their tests and the Goal text removed; the event table's targets stay, since RETURN HOME WITH THE DISKS? uses them. This review had advised keeping the patch until the escape's needs were measured and put in the goal's rule, since the world's logic does not cover the escape and the final battle | Accepted by the owner: a preset core-goal seed can reach Zexion before the player has what the escape needs ("The YAML", Abridged Finale) |
| The start room (`start_room`, `AUTOMATIC_ROOM`, the start record write) | `worlds/zexion/options.py`, `world.py` (`choose_starts`); `client/newgame.py` (`write` of `START_RECORD`) | Starting Location (t22 to t27) | Partly. The game's 34 rooms are the option's values (`options.ROOM_NAMES`), and a named value writes a start record of the same form (room index, column, row: `re/randocells.md`, "Starting Location and the second teleport target"). But ARCHIPELAGO skips the step that reads it, and the world's fill must know the room, so the world chooses it, by the game's Random Start rule (`custom.random_start`) for `random_start` | Not retirable: the world still chooses and the client still writes. What the setting adds is the view: since step B the world sets it to its room in the menu (`ZexionWorld.menu_setup`), which names the start in RANDO SETTINGS, in a preset seed too, since the mode check skips it (above) | Low |
| The second teleport target (`ZexionWorld.partners`, `reach.second_starts`; the second start and the start table) | `world.py`; `client/newgame.py`; `native/src/menu.cpp` (the start table filled in) | Double Teleport (t33) | Partly: the setting turns it on, but the game draws the room from its own seed and ARCHIPELAGO skips that draw. Since step D the rule is the game's (any room but the start; a flagged one in the logic only from a score of 27: "Starts, as the game picks them"); the world's own parts are drawing among the rooms with which the pair suits the seed, where the game would offer PLAY ANYWAY, and taking a flagged room only when it counts from the start (a guard) | The writes stay | Done (step D) |
| Starting keys | `client/newgame.py` (`start_with`; writes t34 to t36); `worlds/zexion/starting.py` (the first keys from the Alpha Key) | Start Keys (t34 to t36) | Fully, by the code: the client gives starting keys by writing the game's own Start Keys bits (`client/newgame.py`), and the new game gives the first *k* keys from them (`0xA11A5`, by disassembly: `re/randocells.md`, "What each setting does in the game"). The game's own Start Keys 4 gave the four keys in a seed made in its menu (2026-10-27); the client's write of the bits has not been run with keys | Nothing to retire. In Custom, `start_keys` is the menu's value and start_inventory keys add to the count | None |
| The key markers, the scanner | the game's new game (event chain 3329 to 3378, from the item table); `client/save_patch.py` (the offline reference only) | Start Keys, Gates | Not an AP feature: the game draws the markers from AP's item table already, so a key in another world or started with has none. Gates changes which gates pass, and with Open the logic (`re/memory.md`, "Keys, the gates and the key markers"), not the markers, and the core goal's four keys are the game's winnability check's own | — | — |
| The starting items and amounts (the start-with array and the amounts written over the placement) | `client/newgame.py` (`start_with`) | The 27 items' Start state, Random Gun, the six amounts | Partly: the menu holds the seed's starting items and amounts, and the new game gives them. But the game resolves `random` items and Random Gun from its own seed text, and the world from its random numbers, and start_inventory is AP's alone | Only if the world reproduced the game's generator from the seed text (`lab/rando-options/agentB/sim_pool.py` does steps 1 to 7) so both agree; not worth it. Keep the writes | Medium if retired: wrong starting items |
| No starting Mega Bombs | `worlds/zexion/starting.py` (`NEVER`) | Starting Mega Bombs | Closes a gap rather than replacing a feature: Custom's `starting_mega_bombs` and a start_inventory Mega Bomb both reach the game as the new game's bomb amount, which the client already writes. An in-game run of a seed starting with bombs would show the game's use of that amount | Lift the refusal after that run and after measuring where the save keeps current bombs (not known: `client/save_patch.py`, whose `patch` refuses starting bombs until then) | Low once both are measured |
| Per-preset reachability (`data.MODES`: locations kept, starts, regions, sphere 0), and OHKO's 4 locations left out | `tools/gen_world_data.py`, `worlds/zexion/data.py` | All the logic's settings, through Custom's prerequisite (working out reachability at generation time) | Partly: once the world works out the locations and starts from the settings, a preset is three settings states, and its precomputed lists become test oracles. The left-out locations already follow the game (Scrap where the game's logic reaches nothing) | Done (step A): every mode is worked out at generation time (`worlds/zexion/reach.py`); `data.py`'s per-mode lists are kept only for tests. The pools stay read from the fresh saves | Low (the tests compare both), at a medium cost |
| Area Map Hints and Intel Chips | `worlds/zexion/world.py` (`hint_targets`); `client/hints.py`, `storage.py`; `native/src/intel.cpp` | Hints (t18 to t21) | No. The game's Hints are map rectangles that navigation rooms reveal, over this world's map only; an Intel Chip is an Archipelago hint, naming where one of the player's items is in any world, given by an Area Map. With Full Map or Revealed an Area Map's own reveal adds nothing, so there Area Map Hints off leaves Area Maps no use | — | — |
| The map's markers for stand-ins (item 48 raised to the important marker; the names under the cursor) | `native/src/mapmarks.cpp` | Obscure Majors, Minors, Class (t29, t38, t41) | Partly. Under Obscure Class on, the game's reveal takes minors down the major path (`re/memory.md`, "Map markers"), so the raise has nothing to do; under the other values it is needed. The helper reads all three settings (`important_value`, step C); with Majors on and Minors off it obscures a stand-in in a hinted square and shows one in a seen square, by the value's base (`0xC` revealed by a hint, `0x18` seen: `re/memory.md`, "Map markers", measured for minors and a navigation room's hint). The names under the cursor: no setting | Nothing to retire. Checked in game, Majors on and Minors off with Class off and on, 2026-10-27 | Low |
| Other players' items (labels, the logo, other Zexion players' items as themselves) | `client/aplabels.py`; `native/src/apitem.cpp`, `looks.cpp` | — | No | — | — |
| The OS Install Disks goal (the count, the scanner's line, the pause row, the equipment screen, the file select, RETURN HOME WITH THE DISKS?) | `client/discs.py`, `ticks.py`; `native/src/scan.cpp`, `pause.cpp`, `equip.cpp`, `files.cpp`, `mission.cpp` | — | No. Abridged Finale is another ending (at the grab), not the pod. Hints Revealed sets flag 39 from the start, which the client already takes for "told" (`client/session.py`, `SCANNED_FLAG`), so the popups give the number needed from the start while the scanner's message never runs (by the code; not seen in game) | — | — |
| Allow Assists (the ASSIST lock, Assist Unlock) | `worlds/zexion/options.py`, `items.py`; `client/`; `native/src/menu.cpp` | — | No: the randomizer menu has no assist setting, and the game lets a randomizer save use ASSIST (its MISSION COMPLETE has shown ASSISTS USED: `native/README.md`, "Feats and achievements in AP saves"). By the owner's principle it is a protection the game does not have: whether to keep it is the owner's call, not a redundancy | If dropped: the option, item 100 and its rule, the client's lock and the helper's menu part, with their tests and docs; YAMLs that set `allow_assists` need updating | Low |
| No Feats or achievements in AP saves | `native/src/feats.cpp` | — | No: the game earns Feats in every randomizer ending (`re/memory.md`, "The ending: flag 250") | — | — |
| The ARCHIPELAGO new game; the RANDOMIZER fallback | `native/src/menu.cpp`; `client/newgame.py` | — | No. RANDOMIZER needs only the seed's text: the helper places the seed over the player's placement ("The new game") | — | — |
| The `expect` checks (Abridged Finale and the others the logic reads must be the seed's) | `client/newgame.py`; `worlds/zexion/custom.py` (`CHECKED`) | Every setting the logic reads | Not redundant: they check the seed's values, Custom's included, and grew by Guns First (step B, "The new game") | — | — |
| Received items and popups, Energy Shields, DeathLink, the title lines, the file select's logo, the offline look | `client/`, `native/` | — | No | — | — |

**Recommendation.** Retire nothing in the world, the client or the helper for Custom. Only the
discussed `escape_skip` option is fully redundant, and it was never built. Every other overlap
is partial, or holds only in a Custom seed with one setting on, and the presets, kept exactly as
the game has them, still need the feature. Where a setting does the job, Custom should let the
game do it and the AP feature stays out of the way, as it already does:

1. **Abridged Finale** in place of the escape skip in Custom: the helper's patch was inert there
   (by disassembly; flag 289 seen set at the grab in game). The offer has since been retired
   everywhere (below).
2. **Start Keys** through the bits the client already writes (by the code; Start Keys 4 gave
   the four keys in a seed made in the game's menu, 2026-10-27).
3. **Starting Location** set to the world's room, for the settings view, in Custom and, since
   the mode check skips it, in the presets too (the small addition in "The new game"; built
   in step B).
4. **Obscure** passed through; the helper's markers follow all three settings, Majors on with
   Minors off included (step C; working in game, 2026-10-27, `re/memory.md`, "Map markers").

The owner's decisions on the two left to them (2026-10-27):

- **The escape skip is retired.** Abridged Finale is the only way to skip the escape; a preset
  seed's escape and final battle are the player's, as in the game's own randomizer ("The
  YAML", Abridged Finale). This review's caution is logged there as a known consequence.
- **Allow Assists stays**, off by default, shown as "Enable Assist Options": ASSIST is locked in
  a seed's saves unless the YAML enables it or the host sends Assist Unlock from the server.

The largest simplification on offer is not a feature but the per-preset data: the world now
works out reachability at generation time for every mode, Custom's path for the presets too,
with `data.py`'s lists kept as test oracles (step A).

## The build plan

Decided (the owner, 2026-10-27), each step a session of its own, reviewed (CI, and the
`logic-reviewer` and `re-auditor` subagents where they apply) and run in game before the next:

1. **Measure first, at the game, with the owner's OK for each new game:** Abridged Finale's
   ending and the client's goal; X Crystals and X Blocks removing their blocks; one seed's pool
   with Box Compression off and with a Bulk setting on; and, for Obscure Majors on with Minors
   off, the map values of a square a hint reveals and of one seen directly. (The menu-only
   checks are done: `re/randocells.md`, "The randomizer menu's first screen: items and
   amounts".) **Done, 2026-10-27**, in three seeds made in the game's menu: Abridged Finale
   ends at the grab with flag 250 set and 211 clear; X Crystals and X Blocks remove their
   blocks; Box Compression off and Bulk Missiles give the counts "The pool" predicts; a hinted
   square's base is `0xC` and a seen one's `0x18` (`re/randocells.md`, "The pool" and "What
   each setting does in the game"; `re/memory.md`, "Map markers"). Found on the way: the menu's
   counter shows boxes the generator does not make when everything nearly fits, and the player's
   cell at the grab read (36, 42), not the world's goal cell then, (32, 45); the goal is now
   at (36, 42) (`re/locations.md`, "The core goal is Zexion's cell in the core").
2. **A. Reachability at generation time:** the reachable locations and the start rooms worked
   out when a seed is generated, not per preset in `data.py`; tests show the same results for
   Easy, Standard and OHKO. **Built, 2026-10-27; run in game with step B:** `worlds/zexion/reach.py`
   floods the region graph with the seed's `logic.Settings`, cached by settings, inventory and
   starts; `worlds/zexion/test/test_reach.py` finds the
   presets' start rooms, regions and locations equal to `data.MODES` from every start, and in
   OHKO from every pair of rooms, and shows Safety, the Energy total and X Crystals changing the
   results.
3. **B. `mode: custom`:** **built, and run in game 2026-10-27** (B1, helper 0.30.0 built
   from the branch with #30's retirement, client from source, seed `lab/ap/live37`, four
   players): a Custom seed with every item started, Start Keys 4, Abridged Finale, X Crystals,
   X Blocks and Hints Revealed started through ARCHIPELAGO with RANDO SETTINGS showing exactly
   its settings and Landing Pod, the four keys, 20 Energy Tanks and 25 missiles as 2 Missile
   Boxes and 5 containers, and the map revealed; at the grab of Zexion the ending began with no
   prompt and the client sent the goal (the first Abridged Finale run with the client); Easy
   with Safety Light and Vallahar read EASY MODE and started there; Custom on OHKO's base with
   Random Start started in Top of Pulsava and the pause menu offered TELEPORT TO VALLAHAR; a
   Custom seed set by hand from RANDOMIZER with the client's steps (since replaced by the seed
   text alone: "The new game") took the seed, twice, and
   one with Safety wrong was refused (the world's tests also
   generate and fill Custom seeds and check the measured seeds' pools; `test_custom.py`). The
   options in the game's words, refusing what the game's menu cannot
   hold, the game's pool rules, the game's Random Start rule, `start_room` and `safety` with the
   presets, the slot data carrying the seed's settings, and the client's checks of them in the
   new game (ranks 2 to 12 above, except Double Teleport's rule and Gates Open); with it, Allow
   Assists shown as "Enable Assist Options". The escape skip offer was retired on its own ahead
   of it, 2026-10-27 (the helper's patch, its offline part, the client's check of it, their
   tests, and the Goal option's mention of SKIP ESCAPE).
4. **C. The helper's markers** read all three Obscure settings and handle Majors on with Minors
   off (alongside B: the helper only). **Built, 2026-10-27; working in game, 2026-10-27:** `important_value`
   (`native/src/mapmarks.cpp`) takes Obscure Majors, Minors and Class (t29, t38, t41); under
   Majors on with Minors and Class off a stand-in's hinted square (`0x0D`) becomes an obscured
   major (`0x15`) and a seen one (`0x19`) a shown major (`0x1A`); the other cases as before.
   Found on the way, by disassembly: an Area Map's reveal never obscures, so with Area Map
   Hints off a stand-in it reveals is obscured where the game's majors show; a hint, a map room
   and Hints Revealed obscure (`re/memory.md`, "Map markers"). Checked in game with Majors on
   and Minors off, Class off and on: a hinted stand-in reads as the game's hinted majors
   (ADVANCED TECH, or UNKNOWN ITEM under Class), a seen one is named; on walking in it goes to
   `0x21` as a game major does, and `0x1A` once on screen. Found there: while it is on screen, the
   game writes it back as a minor, the helper raises it again, and the minimap dot's border
   flashes (`re/memory.md`, "Map markers"). Since then the helper patches the game's write of an
   on-screen item's square so a stand-in keeps a major's class there (working in game
   2026-10-28: no flash, `re/memory.md`, "Map markers").
5. **D. Double Teleport's second target** and **Gates Open's** gate travel in the logic.
   **Built, 2026-10-28; not run in game:** the useful-item score from `0xE57AB` (`logic.py`,
   `useful_score`, with the menu's 27 items from the table at `0x20DA68`); a flagged second
   target behind it (`reach.second_counts`, the AP entrance's rule), the second drawn from the
   other 33 rooms (`reach.second_starts`, the game's one-location rule instead of the world's
   3); Gates Open's floods (`reach.gates_triggered`, `ZexionWorld.connect_gates`), checked
   against `tools/randocells.py`, which models both now, in 6,120 random inventories; and Gates
   Off taken with the core goal, after the four gates' rooms and the core's key slots were
   traced in the data files (`re/memory.md`, "Keys, the gates and the key markers"). Found on
   the way: the event chain that marks the gates visited sets the fourth gate's flag 307 only
   once flag 155 is set. Two guards the game does not have (decided, the owner, 2026-10-28,
   "for the sake of AP"): a flagged second target only when it counts from the start, and no
   gate travel from R'Talva's gate (ranks 11 and 12).
6. **E. Guns First off**, then **Secret Shuffle** (ranks 13 and 14).

## Open questions for the owner

None open (2026-10-27).
