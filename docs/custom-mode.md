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

`mode` keeps its three presets, each exactly as today: the item pool and starting items of one
fresh save of that mode, and its settings. A new `custom` value turns on every other option
below. While `mode` is a preset, the Custom options are not read, as the game's presets fix
every setting; the world logs a warning for each one set to something other than `preset`.

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
  start_room: automatic       # the existing option; 'random' becomes the game's Random Start
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

  # In every mode, not only Custom (below):
  abridged_finale: false      # the game's own: victory when Zexion is first grabbed

  # Unchanged: goal, os_install_disks, os_install_disks_required, area_map_hints,
  # allow_assists, death_link
```

**The game's own words.** Each option is named as the game labels it (its `display_name` is
the menu's label: "Box Compression", "Hints", "Start Keys"), and its description is the blurb
the game shows when the setting is selected, word for word; a setting with several values
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

**Abridged Finale in every mode.** The game's Abridged Finale ("Victory is achieved when you
first grab Zexion in the core.") does what an `escape_skip` option the owner has discussed (not
in the repo) was for: no SKIP ESCAPE prompt (measured by disassembly) and, assumed from the code
and not run, the ending soon after the grab with no escape and no final battle. So it would
replace `escape_skip`, as the game's own setting. It is offered with the presets too: the game
would then call the mode Custom, but the preset's pool and every other setting stay, and the
logic only gains code 68's Abridged Finale entries, which the world works out with the rest
at generation time. Whether the helper keeps offering its escape skip when Abridged Finale is
off (today's behavior) is the owner's call. Colorizer stays out of the YAML: the game treats it as the player's
own (the mode ignores it, the mode buttons leave it), and the client leaves it as the menu has
it, as it does today.

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
- `random`: the game's rule: the unflagged rooms unless the starting score (starting Energy +
  missiles + bombs + starting items) is 27 or more; a room from which the starting items reach
  at least one location; with Safety other than Light, rooms 1, 4, 11 and 14 kept one time in
  three.
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
| 2 | Obscure Majors, Minors, Class | low | low | Pass through the menu's settings (the client's `menu` lines already carry all but Colorizer). The helper's raising of item-48 squares assumes Majors and Minors both on (`memory.md`, "Map markers"): it must read the three bytes and handle the mixed cases. |
| 3 | Show Obtainable | low | low | Pass through. Display only, but what the game's map flood shows over an AP item table (other players' items as Scrap Chunk) is not known: check in game. |
| 4 | Hints | low | low | Pass through: the game's own placement, which the helper runs, makes the rectangles. Revealed sets flag 291 from the start, which also sets the scanner's flag 39, so the OS Install Disks goal's count message shows as already seen (`memory.md`, "The scanner"): check that the client tells the player the count another way. |
| 5 | Safety | low | low | `logic.Settings.mode69` already; the rest is the prerequisite. |
| 6 | Start Keys | low | low | Start with the first *k* keys from the Alpha Key; the pool drops the Omega Key first. The client already writes keys as a count (`client/newgame.py`, `start_with`). |
| 7 | X Crystals, X Blocks | low | medium | Code 68 already in `logic.py`; pass through. The removal runs in the new game's event chain from the section's settings, so it comes with the menu's settings. Assumed: the slot state 4 means "gone". Check in game, and that the core's 2095 object under X Blocks changes nothing for the goal. |
| 8 | Abridged Finale | low | medium | Code 68; pass through; in every mode, in place of the discussed `escape_skip`. Assumed, not run: the ending starts after the grab with flag 250 and flag 211 clear, which is the client's goal. Run it once first. With the OS Install Disks goal it would end the game at the grab without the disks: refuse the pair, or let it be (the owner's call). |
| 9 | Box Compression, Bulk Missiles, Bulk Bombs, the amounts | medium | low | The pool rules above, in the world, with tests from fresh saves of new seeds (Box Compression off, each Bulk on, both amounts 20 or more): generate them in the menu and read the saves, read-only. Starting missiles are given as the new game splits them, tens as Missile Boxes and the rest as up to 9 containers (`client/newgame.py`, `start_with`), so the world must hand them out in that form; starting Mega Bombs need the client's new-game bombs (`worlds/zexion/starting.py`: not supported yet). |
| 10 | The 27 items, Random Gun | medium | low | The four states and Random Gun's draw in the world; the start-with array already carries any of the 27. An excluded item is simply absent. |
| 11 | Starting Location, Double Teleport | medium | medium | The game's room rules above at generation time; the flagged second target's score rule as a new entrance rule. The start table stays B1-only (`client/newgame.py`). |
| 12 | Gates | low (Off, Vanilla, Unlocked); medium (Open) | medium | Pass through. Open adds the game's gate floods to the logic: entrances among (24, 44), (12, 30) and (14, 2) once one of them or (51, 34) is reached. Not known: what stands between the player and the core under Off and Unlocked (`randocells.md`, "Not yet found"); play each once. |
| 13 | Guns First off | high | medium | With it off the game applies no item redirects: each of the five upgrades is its own item, useless without its gun, and the guns are no longer progressive. New items for Pulse Wave, Pentashot, Tunneler, Focal Crystal and Plasma Burst; `logic.py`'s redirects conditional; the client's inventory writes and reconciliation for upgrades as their own bytes. A mistake here can lose or double an item. |
| 14 | Secret Shuffle | high | high | A 155th location, the Bomb Box at X48 Y20, enters the game's location list at position 134 and moves the 20 after it up by one. The client, the helper and the save patch index everything by that list and count 154 (`client/ids.py`, `save_patch.py`, `newgame.py`). The location ids would stay by cell, with a per-seed map to the game's list. A mistake sends or gives the wrong item. |
| — | No Guarantee | none | none | Not offered (not on B1's menu). |

## The new game

**ARCHIPELAGO** already sets the whole menu state (seed text, both item arrays, the six amounts
and settings t10 to t73 but Colorizer) from the slot data, runs the game's placement, checks
the section and writes the seed over it (`memory.md`, "The game-mode panel"). A Custom seed's
slot data carries its own settings there instead of a preset's; the checks (`expect` lines)
grow to every setting the logic reads: t30 to t33, t37, Safety, Gates and the amounts.

**RANDOMIZER**, the fallback, has the player set the menu by hand: easy for a preset, slow for
Custom (20 settings, 27 items, six amounts). The game's Paste←Clipboard takes the seed text and
the settings code ("The settings code"), whose format is known exactly, so the client could put
that on the clipboard, or print it, for the player to paste.

## Open questions for the owner

- Refuse a preset with Custom options set, or only warn (this draft: warn)?
- Abridged Finale with the OS Install Disks goal: refuse, or allow?
- `start_room: random`: the game's Random Start rule (this draft), or today's rule (any room
  every item reaches all locations from, with at least three for the starting items)?
- Which settings to build first: this draft would start with Abridged Finale's in-game run,
  then the prerequisite and ranks 2 to 10.
- With Abridged Finale off, keep the helper's escape skip offer, or drop it to follow the game?
