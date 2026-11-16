"""Item plando (``plando_items``): what the world checks of it.

Archipelago's own item plando places this world's items, and other worlds' items in this world's
locations, as any world's: the client and the game then treat a plando'd item as any other
(world.py's ``location_items``; client/reconcile.py). The world adds three checks, all here:

- ``left_out``, from generate_basic, just after Archipelago reads the plando: a warning for each
  plando that names a location of this world the seed does not have, saying why: a location its
  settings do not reach from the start (a left-out location, which holds Scrap Metal: world.py's
  ``fit_pool``), or Secret Shuffle's location in a seed without it. Archipelago itself only says
  it could not place the items (and stops, with ``force: true``). A plando for several worlds
  (``world: null`` or a list) gets no warning for a location another of them has, where
  Archipelago places it.
- ``area_maps_away``, from generate_basic too: a warning for each plando of this player's Area
  Maps into other worlds only, while Area Map is in the player's ``local_items`` (Area Map
  Hints off puts it there: world.py's ``generate_early`` and ``place_area_maps``), unless the
  plando's ``force`` is ``silent``. Archipelago does not place them there and, with
  ``force: true``, stops with only "Could not place".
- ``problems``, from pre_fill, once Archipelago has placed the plando: why the player's items
  (in the pool, in any world's locations and in the start inventory: ``own_items``) are more than
  the game holds or a kind it does not hold, one line each, where plando made them more than the
  world and the item links did (``made_items``, from generate_basic, and ``linked_items``); the
  world refuses the seed with them. Plando can make items: ``from_pool: false`` makes new ones,
  and ``from_pool: true`` makes one where the pool holds no more of it (Archipelago warns "Could
  not remove ... from pool"). What the world makes alone is never refused here: its pool keeps
  the game's limits, and start_inventory's supplies on top of the pool, which may pass them, are
  the options' (checked in generate_early: worlds/zexion/starting.py). Nor is what an item link
  adds: its replacement items, a second copy of an item held once among them (the client writes
  none above 1). The limits are those of the starting items, for the same reasons, where they
  apply to the items found as well:

  - an item the game holds once (all but the supplies, the filler, the traps and the OS Install
    Disks): one;
    with Guns First on (every preset), a progressive weapon two (the weapon and its upgrade) and
    no upgrade as an item of its own. The client writes no byte above 1 (client/reconcile.py),
    so a copy past those would be a location that gives nothing; what the game does with a
    second copy it hands out itself is not measured (docs/re/save-format.md, "Inventory: one
    byte per item ID at 0x92F3");
  - Energy Tanks 40 at most, and missiles and Mega Bombs 999 each in all (10 per box), the
    starting ones included: the most the game's randomizer menu starts and places
    (``custom.problems``); more is not measured in game;
  - OS Install Disks only with the OS Install Disks goal, and no more than its options make
    (``os_install_disks``, with the start options'), so the goal's count, the slot data's
    ``discs_placed`` and the 99 the screens show hold;
  - with Area Map Hints on, no Area Map (they are Intel Chips); with it off, no Intel Chip, which
    would hint nothing (the slot data's ``hints`` is empty).

Pure but for ``own_items``, ``made_items``, ``linked_items``, ``left_out`` and
``area_maps_away``, which read the multiworld.
"""

from __future__ import annotations

from collections import Counter
from collections.abc import Mapping
from typing import TYPE_CHECKING

from .custom import ENERGY_SHIELD, MAX_AMMO, MAX_ENERGY, SCRAP_METAL
from .items import (
    AREA_MAP,
    ASSIST_UNLOCK,
    DATA_DISC,
    INTEL_CHIP,
    NAMES,
    PROGRESSIVE,
    SCRAP_CHUNK,
    SCRAP_ENERGY,
    TRAPS,
    UPGRADES,
    code_of,
)
from .locations import LOCATION_ID_BASE, location_name_to_id
from .logic import BOMB_BOX, ENERGY_TANK, MEGA_BOMB, MISSILE_BOX, MISSILE_CONTAINER
from .starting import COUNTED

if TYPE_CHECKING:
    from .world import ZexionWorld

# The items a player can hold or find any number of: the counts (supplies and Scrap Metal), the
# other filler, the OS Install Disks (checked on their own), Assist Unlock (never written) and
# the traps (never held: each sets a timer off).
MANY = COUNTED | {SCRAP_ENERGY, SCRAP_CHUNK, AREA_MAP, ENERGY_SHIELD, INTEL_CHIP, DATA_DISC}
MANY |= {ASSIST_UNLOCK, *TRAPS}


def own_items(world: ZexionWorld) -> Counter[int]:
    """This player's items by item code, after plando: in the item pool (each once: the fill
    leaves placed items in the pool too), in any world's locations and in the start inventory
    (events left out)."""
    multiworld, player = world.multiworld, world.player
    items = [loc.item for loc in multiworld.get_filled_locations() if loc.item.player == player]
    items += [i for i in multiworld.itempool if i.player == player and i.location is None]
    items += multiworld.precollected_items[player]
    return Counter(code_of[item.name] for item in items if item.code is not None)


def made_items(world: ZexionWorld) -> Counter[int]:
    """This player's items as the world and the start options make them, before plando
    (``own_items`` from generate_basic). An item of start_inventory_from_pool is then both in
    the start inventory and in the pool, from which Archipelago takes it out after this step and
    puts Scrap Metal in its place (Main.py): it is counted once, and that Scrap Metal too."""
    counts = own_items(world)
    multiworld, player = world.multiworld, world.player
    in_pool = Counter(code_of[i.name] for i in multiworld.itempool if i.player == player)
    started = Counter(code_of[i.name] for i in multiworld.precollected_items[player])
    for name, n in world.options.start_inventory_from_pool.value.items():
        code = code_of[name]
        taken = min(n, in_pool[code], started[code])
        counts[code] -= taken
        counts[SCRAP_METAL] += taken
    return counts


def linked_items(world: ZexionWorld) -> Counter[int]:
    """The most items Archipelago's item links can have added for this player (by item code),
    which it does after generate_basic and before plando (``MultiWorld.link_items``, Main.py).
    A link takes the same count of the linked items from each of its players into the group,
    gives the group one of each, and fills the pool back up with each player's replacement_item
    (filler when it has none) for every player in turn, shuffling the last turn: so each player
    gets the shortfall over the players, rounded up at most. With link_replacement on, the
    replacements are the group's, not this player's. Only the last turn is not known here, so
    a plando can add at most one copy of a link's replacement item per link unrefused."""
    multiworld, player = world.multiworld, world.player
    out: Counter[int] = Counter()
    for group_id, group in multiworld.groups.items():
        # link_items keeps in "players" only those it took items from.
        players = group.get("players", ())
        if player not in players or group.get("link_replacement"):
            continue
        if "replacement_items" not in group:
            continue  # not an item link's group
        taken = Counter(loc.item.player for loc in multiworld.get_filled_locations(group_id))
        added = sum(taken.values()) - taken[player]  # all it took, less the group's own
        name = group["replacement_items"].get(player) or world.get_filler_item_name()
        out[code_of[name]] += -(-added // len(players))
    return out


def problems(
    counts: Mapping[int, int],
    made: Mapping[int, int],
    *,
    guns_first: bool,
    disc_hunt: bool,
    area_map_hints: bool,
) -> list[str]:
    """Why these items (item code -> count: ``own_items``) are more than the game holds or a
    kind it does not hold, where they are more than the world ``made`` (``made_items``), one line
    each; empty when none (the module's docstring)."""

    def more(code: int) -> int:
        """How many of ``code`` there are, where plando made more than the world; else 0."""
        n = counts.get(code, 0)
        return n if n > made.get(code, 0) else 0

    out = []
    for code in sorted(counts):
        n = more(code)
        if not n or code in MANY:
            continue
        name = NAMES[code]
        if guns_first and code in UPGRADES:
            weapon = NAMES[next(w for w, u in PROGRESSIVE.items() if u == code)]
            out.append(
                f"{name}: with Guns First on, the upgrade is a second {weapon} (the game's Guns "
                f"First seeds hold no upgrade of its own)"
            )
        elif guns_first and code in PROGRESSIVE:
            if n > 2:
                out.append(f"{n} {name}: a weapon and its upgrade are 2")
        elif n > 1:
            out.append(f"{n} {name}: the game holds one, so the others would give nothing")
    tanks = more(ENERGY_TANK)
    if tanks > MAX_ENERGY:
        out.append(
            f"{tanks} Energy Tanks, the starting ones included: the game's randomizer menu "
            f"starts and places {MAX_ENERGY} at most"
        )
    for kind, one, box in (
        ("missiles", MISSILE_CONTAINER, MISSILE_BOX),
        ("Mega Bombs", MEGA_BOMB, BOMB_BOX),
    ):
        total = counts.get(one, 0) + 10 * counts.get(box, 0)
        if total > MAX_AMMO and total > made.get(one, 0) + 10 * made.get(box, 0):
            out.append(
                f"{total} {kind} (10 per {NAMES[box]}), the starting ones included: the game's "
                f"randomizer menu starts and places {MAX_AMMO} at most"
            )
    disks = more(DATA_DISC)
    if disks and not disc_hunt:
        out.append(f"{disks} {NAMES[DATA_DISC]}: only the OS Install Disks goal counts them")
    elif disks:
        out.append(
            f"{disks} {NAMES[DATA_DISC]}s, more than the {made.get(DATA_DISC, 0)} the options "
            f"make: set how many with os_install_disks (plando with from_pool: true moves those)"
        )
    if area_map_hints and more(AREA_MAP):
        out.append(f"{NAMES[AREA_MAP]}: with Area Map Hints on, the Area Maps are Intel Chips")
    if not area_map_hints and more(INTEL_CHIP):
        out.append(
            f"{NAMES[INTEL_CHIP]}: with Area Map Hints off, an Intel Chip would hint nothing "
            f"(area_map_hints makes the Area Maps Intel Chips)"
        )
    return out


def left_out(world: ZexionWorld) -> list[str]:
    """A warning for each location of this world that a plando (any player's) names, the seed
    does not have and no other world of that plando's has, saying why (the module's
    docstring)."""
    multiworld = world.multiworld
    who = f"Zexion ({world.player_name})"
    out = []
    for player, blocks in sorted(multiworld.plando_item_blocks.items()):
        for block in blocks:
            if world.player not in block.worlds:
                continue
            for name in block.locations:
                if name not in location_name_to_id:
                    continue  # a group, early_locations or another game's location
                index = location_name_to_id[name] - LOCATION_ID_BASE
                if index >= len(world.location_cells):
                    why = "Secret Shuffle's location, in a seed without Secret Shuffle"
                elif index not in world.kept_locations:
                    why = (
                        "which this seed leaves out: its settings do not reach it from the "
                        "start, so it holds Scrap Metal"
                    )
                else:
                    continue
                if any(loc.name == name for loc in block.resolved_locations):
                    continue  # another world of the block has it, and Archipelago places there
                out.append(
                    f"{who}: {multiworld.get_player_name(player)}'s plando_items names {name}, "
                    f"{why}; Archipelago cannot place an item there"
                )
    return out


def area_maps_away(world: ZexionWorld) -> list[str]:
    """A warning for each of this player's plandos that puts Area Maps in other worlds only,
    while their ``local_items`` keeps them in this one (the module's docstring)."""
    name = NAMES[AREA_MAP]
    if name not in world.options.local_items.value:
        return []
    return [
        f"Zexion ({world.player_name}): plando_items puts an Area Map in another world, but "
        f"Area Map is in your local_items (Area Map Hints off puts it there: Area Maps reveal "
        f"the map around where they lie), so Archipelago cannot place it there"
        for block in world.multiworld.plando_item_blocks.get(world.player, [])
        if name in block.items and world.player not in block.worlds and block.force != "silent"
    ]
