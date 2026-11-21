"""The world tests' shared pieces: the test bases, the fakes and the generation steps they use.

Archipelago's test base runs the generation steps up to the fill but not what its Main.py does
around them (the start options' items, item links, locality and exclusion, plando), so those
steps are here, once: ``precollect_start_options``, ``take_from_pool`` and ``generate``.
"""

import random
import types
import unittest
from collections import Counter
from dataclasses import replace
from typing import Any

from BaseClasses import CollectionState, MultiWorld, PlandoOptions
from Fill import (
    distribute_planned_blocks,
    parse_planned_blocks,
    resolve_early_locations_for_planned,
)
from Options import OptionError
from test.bases import WorldTestBase
from test.general import setup_multiworld
from worlds.AutoWorld import World, call_all
from worlds.generic.Rules import exclusion_rules, locality_rules

from .. import custom, data, reach
from ..items import NAMES
from ..locations import LOCATION_ID_BASE
from ..world import ZexionWorld

# The start table's flagged rooms, by index.
FLAGGED = {i for i, (_, _, flag) in enumerate(data.START_TABLE) if flag}
NO_OPTION = types.SimpleNamespace(value={})  # a start option a world does not have

# Standard's Random Gun draws its starting gun among these four (the Machine Gun start-only and
# the other three pool-only in its menu; the Arc Plasma, set both ways, always goes in the pool),
# with the start rooms each gun's starting items take (docs/re/locations.md, "Which start rooms
# the world offers").
STANDARD_GUNS = {
    "Progressive Machine Gun": 17,
    "Progressive Force Blaster": 19,  # the fresh save's draw (data.MODES)
    "Progressive Snake": 17,
    "Progressive Lance": 20,
}


def preset(name: str) -> reach.Seed:
    """A preset mode's seed: its menu's settings, and its fresh save's items (data.MODES)."""
    mode = data.MODES[name]
    settings = custom.Menu.preset(name).logic_settings()
    return reach.Seed.of(settings, mode["start_inventory"], mode["pool"])


def every_list() -> list[tuple]:
    """Every requirement list in the data: the ways', the locations' and the core's."""
    lists = [way[0] for ways in data.ENTRANCES.values() for way in ways if way is not None]
    return lists + list(data.LOCATION_RULES.values()) + [way[0] for way in data.CORE["needs"]]


def with_settings(seed: reach.Seed, **changes) -> reach.Seed:
    return replace(seed, settings=replace(seed.settings, **changes))


def fake_world(**options: Any) -> ZexionWorld:
    """A world with these options, every other one at its default, to run generate_early alone."""
    world = ZexionWorld.__new__(ZexionWorld)
    world.options = types.SimpleNamespace(
        **{
            name: option.from_any(options.get(name, option.default))
            for name, option in ZexionWorld.options_dataclass.type_hints.items()
        }
    )
    world.random = random.Random(1)
    world.player = 1
    world.multiworld = types.SimpleNamespace(get_player_name=lambda player: "Z")
    return world


def drawn_gun(world: ZexionWorld) -> str:
    """The gun a Standard world's starting items hold."""
    (gun,) = [NAMES[c] for c in world.start_inventory if NAMES[c] in STANDARD_GUNS]
    return gun


def seed_drawing(gun: str) -> int:
    """The first seed whose Standard draw, with the default options, starts with ``gun``."""
    for seed in range(100):
        probe = fake_world()
        probe.random = random.Random(seed)
        probe.generate_early()
        if drawn_gun(probe) == gun:
            return seed
    raise AssertionError(f"no seed drew the {gun}")


def drawing(gun: str, **options: Any) -> ZexionWorld:
    """A Standard fake_world with these options whose Random Gun drew ``gun``, generate_early
    run. The seed is found with the default options: a start_room that suits the first draw
    does not change it (asserted)."""
    world = fake_world(**options)
    world.random = random.Random(seed_drawing(gun))
    world.generate_early()
    assert drawn_gun(world) == gun, (gun, options)
    return world


def disks(mode: str = "standard", placed: int = 20, required: int = 15) -> dict[str, Any]:
    """Options for the OS Install Disks goal."""
    return {
        "mode": mode,
        "goal": "os_install_disks",
        "os_install_disks": placed,
        "os_install_disks_required": required,
    }


def block(items: dict[str, int], locations: list[str], **more: Any) -> dict[str, Any]:
    """A plando_items block, forced, its items from the pool unless ``more`` says otherwise."""
    return {"items": items, "locations": locations, "from_pool": True, "force": True, **more}


def index_of(name: str) -> int:
    return ZexionWorld.location_name_to_id[name] - LOCATION_ID_BASE


def precollect_start_options(multiworld: MultiWorld) -> None:
    """What Main.py does for start_inventory and start_inventory_from_pool after generate_early:
    each player's items from both options precollected."""
    for player, world in multiworld.worlds.items():
        for option in ("start_inventory", "start_inventory_from_pool"):
            for name, n in getattr(world.options, option, NO_OPTION).value.items():
                for _ in range(n):
                    multiworld.push_precollected(multiworld.create_item(name, player))


def take_from_pool(multiworld: MultiWorld) -> None:
    """What Main.py does after generate_basic for start_inventory_from_pool: the items taken out
    of the item pool, a filler item from the item's world in each one's place."""
    remove = {
        p: Counter(getattr(w.options, "start_inventory_from_pool", NO_OPTION).value)
        for p, w in multiworld.worlds.items()
    }
    pool = []
    for item in multiworld.itempool:
        if remove[item.player][item.name] > 0:
            remove[item.player][item.name] -= 1
            pool.append(multiworld.worlds[item.player].create_filler())
        else:
            pool.append(item)
    multiworld.itempool[:] = pool


def generate(
    options: list[dict[str, Any]], worlds: list[type[World]] | None = None, seed: int = 1
) -> MultiWorld:
    """Players with these options (Zexion players, or ``worlds``), set up as Archipelago's
    Main.py does up to the fill: the start options' items precollected, item links, locality
    and exclusion, the plando read (with its groups filled in, as Generate.py's option check
    does) and placed, then pre_fill. The test base runs none of the plando steps."""
    worlds = worlds or [ZexionWorld] * len(options)
    multiworld = setup_multiworld(worlds, steps=(), seed=seed, options=options)
    for player, world in multiworld.worlds.items():
        world.options.plando_items.verify(type(world), f"Tester{player}", PlandoOptions.items)
    multiworld.set_item_links()
    multiworld.state = CollectionState(multiworld)
    call_all(multiworld, "generate_early")
    precollect_start_options(multiworld)
    for step in ("create_regions", "create_items", "set_rules"):
        call_all(multiworld, step)
    for player, world in multiworld.worlds.items():
        exclusion_rules(multiworld, player, world.options.exclude_locations.value)
    if multiworld.players > 1:
        locality_rules(multiworld)
    multiworld.plando_item_blocks = parse_planned_blocks(multiworld)
    call_all(multiworld, "connect_entrances")
    call_all(multiworld, "generate_basic")
    take_from_pool(multiworld)
    multiworld.link_items()
    resolve_early_locations_for_planned(multiworld)
    distribute_planned_blocks(
        multiworld, [block for blocks in multiworld.plando_item_blocks.values() for block in blocks]
    )
    call_all(multiworld, "pre_fill")
    return multiworld


class RefusalTestCase(unittest.TestCase):
    def refused(self, start_inventory: dict[str, int] | None = None, **options: Any) -> str:
        """The error generate_early refuses these options with."""
        with self.assertRaises(OptionError) as caught:
            fake_world(start_inventory=start_inventory or {}, **options).generate_early()
        return str(caught.exception)


class ZexionTestBase(WorldTestBase):
    game = "Zexion"
    world: ZexionWorld


def pool_names(test: ZexionTestBase) -> list[str]:
    return [i.name for i in test.multiworld.itempool if i.player == test.player]


class ZexionStartTestBase(ZexionTestBase):
    """With what Archipelago's Main.py does for start_inventory and start_inventory_from_pool,
    which the test base does not run (as of AP 0.6.8): it precollects both options' items, and
    after generate_basic takes the from-pool items out of the item pool, a filler item from the
    world in each one's place. Done here after the test base's steps: no step of this world
    reads the precollected items before fill_slot_data."""

    def world_setup(self, seed: int | None = None) -> None:
        super().world_setup(seed)
        if not hasattr(self, "world"):
            return  # the test base skipped its setup
        precollect_start_options(self.multiworld)
        take_from_pool(self.multiworld)
