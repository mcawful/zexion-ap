"""The Zexion world."""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

from BaseClasses import Item, ItemClassification, Region
from worlds.AutoWorld import World

from . import data, rules, web_world
from .items import (
    FILLER_NAME,
    GAME,
    KEYS,
    NAMES,
    ZexionItem,
    classification,
    code_of,
    item_name_to_id,
)
from .locations import ZexionLocation, location_name, location_name_to_id
from .options import ZexionOptions

GOAL_CELL = (32, 45)  # the core, where Zexion is grabbed


class ZexionWorld(World):
    """Zexion is a metroidvania by Gallant Leaf with a built-in randomizer. Explore a 54 by 54
    map of rooms, find upgrades and work your way to the end."""

    game = GAME
    web = web_world.ZexionWebWorld()
    options_dataclass = ZexionOptions
    options: ZexionOptions
    item_name_to_id = item_name_to_id
    location_name_to_id = location_name_to_id
    item_name_groups = {"Keys": set(KEYS)}
    origin_region_name = "Menu"

    created_regions: set[int]
    start: dict

    def generate_early(self) -> None:
        self.start = data.STARTS[self.options.start_room.value]

    def create_item(self, name: str) -> Item:
        return ZexionItem(name, classification(code_of[name]), item_name_to_id[name], self.player)

    def get_filler_item_name(self) -> str:
        return FILLER_NAME

    def create_regions(self) -> None:
        cells = rules.cell_region()
        self.created_regions = set(self.start["regions"])
        menu = Region("Menu", self.player, self.multiworld)
        self.multiworld.regions.append(menu)
        by_index = {}
        for r in sorted(self.created_regions):
            region = Region(rules.region_name(r), self.player, self.multiworld)
            by_index[r] = region
            self.multiworld.regions.append(region)
        menu.connect(by_index[cells[tuple(self.start["cell"])]], "Start")

        for (a, b), ways in data.ENTRANCES.items():
            if a not in self.created_regions or b not in self.created_regions:
                continue
            for k, way in enumerate(ways):
                name = f"{rules.region_name(a)} to {b}" + (f" #{k + 1}" if len(ways) > 1 else "")
                if way is None:
                    by_index[a].connect(by_index[b], name)
                    continue
                entries, cell = way
                entrance = by_index[a].connect(
                    by_index[b], name, rules.requirement(self, entries, cell, cells)
                )
                for r in rules.neighbor_regions(entries, cell, cells):
                    if r in self.created_regions:
                        self.multiworld.register_indirect_condition(by_index[r], entrance)

        for i, cell in enumerate(data.LOCATIONS):
            region = by_index[cells[tuple(cell)]]
            location = ZexionLocation(
                self.player, location_name(i), location_name_to_id[location_name(i)], region
            )
            entries = data.LOCATION_RULES.get(i)
            if entries is not None:
                location.access_rule = rules.requirement(self, entries, tuple(cell), cells)
            region.locations.append(location)

        # The goal: grab Zexion in the core (docs/re/memory.md, "The goal: Zexion's inventory
        # byte at the grab"). The four keys open the core; RandoCells does not model them.
        core = by_index[cells[GOAL_CELL]]
        goal = ZexionLocation(self.player, "Zexion", None, core)
        goal.place_locked_item(Item("Victory", ItemClassification.progression, None, self.player))
        core.locations.append(goal)

    def create_items(self) -> None:
        for code, count in data.START_INVENTORY.items():
            for _ in range(count):
                self.multiworld.push_precollected(self.create_item(NAMES[code]))
        pool = [self.create_item(NAMES[code]) for code in data.POOL]
        self.multiworld.itempool += pool

    def set_rules(self) -> None:
        self.get_location("Zexion").access_rule = lambda state: state.has_all(KEYS, self.player)
        self.multiworld.completion_condition[self.player] = lambda state: state.has(
            "Victory", self.player
        )

    def fill_slot_data(self) -> Mapping[str, Any]:
        return {
            "start_index": self.start["index"],
            "start_cell": list(self.start["cell"]),
            "start_column": self.start["column"],
            "start_row": self.start["row"],
            "start_inventory": {str(k): v for k, v in data.START_INVENTORY.items()},
            "locations": [list(c) for c in data.LOCATIONS],
        }
