"""The Zexion world."""

from __future__ import annotations

import hashlib
import logging
from collections import Counter
from collections.abc import Mapping
from typing import Any, TextIO

from BaseClasses import Item, ItemClassification, Region
from Options import OptionError
from worlds.AutoWorld import World

from . import data, logic, rules, web_world
from .items import (
    FILLER_NAME,
    GAME,
    KEYS,
    NAMES,
    PROGRESSIVE,
    ZexionItem,
    classification,
    code_of,
    item_name_to_id,
)
from .locations import ZexionLocation, location_name, location_name_to_id
from .options import AUTOMATIC, MODE_NAMES, START_CELLS, ZexionOptions

GOAL_CELL = (32, 45)  # the core, where Zexion is grabbed
AUTOMATIC_START = (13, 16)  # where one of the owner's seeds started
MIN_SPHERE_ZERO = 3  # as tools/gen_world_data.py requires of a single start
# Filler item codes, in the order a left-out location removes them from the pool.
FILLER_ORDER = (46, 47, 48, 49)
# What the game holds at a location whose item is not this world's to hand out natively.
PLACEHOLDER = 46  # Scrap Metal
# Items the client cannot write into the game (client/reconcile.py): placed only in this
# world, where the game hands them out itself.
LOCAL_ONLY = ("Energy Shield",)


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
    second: dict | None  # the second teleport target, in a mode that has one
    kept_locations: set[int]  # location indices this world creates
    mode_name: str
    mode: dict
    settings: logic.Settings

    def generate_early(self) -> None:
        self.mode_name = MODE_NAMES[self.options.mode.value]
        self.mode = data.MODES[self.mode_name]
        self.settings = logic.Settings(
            self.mode["energy_total"], self.mode["mode69"], self.mode["options"]
        )
        starts = {tuple(s["cell"]): s for s in self.mode["starts"]}
        if not starts:
            raise OptionError(f"Zexion: {self.mode_name} mode has no start room to offer")
        choice = self.options.start_room.value
        if choice == AUTOMATIC:
            cell = AUTOMATIC_START if AUTOMATIC_START in starts else sorted(starts)[0]
        else:
            cell = START_CELLS[choice]
        if cell not in starts:
            # A room this mode does not offer, typed or rolled by 'random' over every mode's
            # rooms (the two cannot be told apart): take one the mode offers instead.
            replacement = self.random.choice(sorted(starts))
            logging.warning(
                f"Zexion ({self.player_name}): start room map {cell[0]},{cell[1]} is not offered "
                f"in {self.mode_name} mode; using map {replacement[0]},{replacement[1]}"
            )
            cell = replacement
        self.start = starts[cell]
        self.options.local_items.value |= set(LOCAL_ONLY)
        self.second = None
        self.kept_locations = set(range(len(data.LOCATIONS)))
        if self.mode["double_teleport"]:
            self.pick_second_start(starts)

    def pick_second_start(self, starts: dict) -> None:
        """Pick the second teleport target: a room that, together with the start, lets the
        starting items reach at least MIN_SPHERE_ZERO locations. Only locations every item
        reaches from one of the two are kept (docs/re/locations.md)."""

        def sphere0(a: dict, b: dict) -> int:
            return len(set(a["sphere0_indices"]) | set(b["sphere0_indices"]))

        partners = [
            b
            for c, b in sorted(starts.items())
            if b is not self.start and sphere0(self.start, b) >= MIN_SPHERE_ZERO
        ]
        if not partners:
            pairs = [
                (a, b)
                for a in starts.values()
                for b in starts.values()
                if a is not b and sphere0(a, b) >= MIN_SPHERE_ZERO
            ]
            if not pairs:
                raise OptionError(f"Zexion: {self.mode_name} mode has no pair of start rooms")
            a, b = self.random.choice(pairs)
            logging.warning(
                f"Zexion ({self.player_name}): no second target suits start room map "
                f"{self.start['cell'][0]},{self.start['cell'][1]} in {self.mode_name} mode; "
                f"starting at map {a['cell'][0]},{a['cell'][1]} instead"
            )
            self.start = a
            partners = [b]
        self.second = self.random.choice(partners)
        self.kept_locations = set(self.start["location_indices"]) | set(
            self.second["location_indices"]
        )

    def create_item(self, name: str) -> Item:
        return ZexionItem(name, classification(code_of[name]), item_name_to_id[name], self.player)

    def get_filler_item_name(self) -> str:
        return FILLER_NAME

    def create_regions(self) -> None:
        cells = rules.cell_region()
        self.created_regions = set(self.start["regions"])
        if self.second is not None:
            self.created_regions |= set(self.second["regions"])
        menu = Region("Menu", self.player, self.multiworld)
        self.multiworld.regions.append(menu)
        by_index = {}
        for r in sorted(self.created_regions):
            region = Region(rules.region_name(r), self.player, self.multiworld)
            by_index[r] = region
            self.multiworld.regions.append(region)
        menu.connect(by_index[cells[tuple(self.start["cell"])]], "Start")
        if self.second is not None:
            menu.connect(by_index[cells[tuple(self.second["cell"])]], "Second teleport target")

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
            if i not in self.kept_locations:
                continue
            region = by_index[cells[tuple(cell)]]
            location = ZexionLocation(
                self.player, location_name(i), location_name_to_id[location_name(i)], region
            )
            entries = data.LOCATION_RULES.get(i)
            if entries is not None:
                location.access_rule = rules.requirement(self, entries, tuple(cell), cells)
            region.locations.append(location)

        # The goal is the ending (the client reads flag 250: docs/re/memory.md, "The ending:
        # flag 250"). The logic asks only for the grab of Zexion in the core: after it, the helper
        # has the game offer its skip to the ship ("The escape skip"); without the helper the
        # escape needs the Zenith Thrusters (reported by the owner). The four keys open the
        # core; RandoCells does not model them.
        core = by_index[cells[GOAL_CELL]]
        goal = ZexionLocation(self.player, "Zexion", None, core)
        goal.place_locked_item(Item("Victory", ItemClassification.progression, None, self.player))
        core.locations.append(goal)

    def create_items(self) -> None:
        for code, count in self.mode["start_inventory"].items():
            for _ in range(count):
                self.multiworld.push_precollected(self.create_item(NAMES[code]))
        codes = list(self.mode["pool"])
        # A location left out takes a filler item out of the pool with it.
        for _ in range(len(data.LOCATIONS) - len(self.kept_locations)):
            filler = next((c for c in FILLER_ORDER if c in codes), None)
            if filler is None:
                raise OptionError(f"Zexion: {self.mode_name}'s pool has too little filler")
            codes.remove(filler)
        pool = [self.create_item(NAMES[code]) for code in codes]
        self.multiworld.itempool += pool

    def set_rules(self) -> None:
        self.get_location("Zexion").access_rule = lambda state: state.has_all(KEYS, self.player)
        self.multiworld.completion_condition[self.player] = lambda state: state.has(
            "Victory", self.player
        )

    def write_spoiler_header(self, spoiler_handle: TextIO) -> None:
        x, y = self.start["cell"]
        spoiler_handle.write(f"Mode:                            {self.mode_name}\n")
        spoiler_handle.write(f"Start room:                      map {x},{y}\n")
        if self.second is not None:
            x, y = self.second["cell"]
            spoiler_handle.write(f"Second teleport target:          map {x},{y}\n")

    def fill_slot_data(self) -> Mapping[str, Any]:
        return {
            "mode": self.mode_name,
            "start_index": self.start["index"],
            "start_cell": list(self.start["cell"]),
            "start_column": self.start["column"],
            "start_row": self.start["row"],
            "second_start": None
            if self.second is None
            else {k: self.second[k] for k in ("index", "column", "row")}
            | {"cell": list(self.second["cell"])},
            "kept_locations": sorted(self.kept_locations),
            "locations": [list(c) for c in data.LOCATIONS],
            "location_items": self.location_items(),
            "start_items": {str(k): v for k, v in sorted(self.start_items().items())},
            "amounts": list(self.mode["amounts"]),
            "options": {str(t): v for t, v in self.mode["checked_options"].items()},
            "menu": self.menu_setup(),
        }

    def menu_setup(self) -> dict[str, Any] | None:
        """The randomizer menu as this seed's mode, for the client's ARCHIPELAGO choice
        (client/newgame.py): the mode's preset and a seed text of 16 hex digits from this
        seed and player. In a mode with a second teleport target, whose placement reads the
        menu's start table, the table instead of a second target (the seed's second start is
        written)."""
        name = f"{self.multiworld.seed_name}:{self.player}".encode()
        setup = {
            "seed_text": hashlib.sha256(name).hexdigest()[:16].upper(),
            "start": list(self.mode["menu"]["start"]),
            "pool": list(self.mode["menu"]["pool"]),
            "settings": list(self.mode["menu"]["settings"]),
            "second_target": None,
        }
        if self.mode["double_teleport"]:
            setup["start_table"] = [list(room) for room in data.START_TABLE]
            setup["table_build"] = data.DATA_BUILD
        else:
            setup["second_target"] = list(self.mode["second_target"])
        return setup

    def location_items(self) -> list[int]:
        """The item code the game should hold at each of its locations: this world's own items
        as the game's item IDs (a progressive weapon as the weapon; the game makes a second
        copy the upgrade), and PLACEHOLDER where another world's item is or the location was
        left out."""
        out = []
        for i in range(len(data.LOCATIONS)):
            item = self.get_location(location_name(i)).item if i in self.kept_locations else None
            own = item is not None and item.player == self.player and item.code is not None
            out.append(code_of[item.name] if own else PLACEHOLDER)
        return out

    def start_items(self) -> dict[int, int]:
        """Inventory bytes for everything this player starts with: the mode's starting items
        and any start_inventory, a progressive weapon's second copy as its upgrade."""
        counts = Counter(
            code_of[item.name]
            for item in self.multiworld.precollected_items[self.player]
            if item.code is not None
        )
        out: dict[int, int] = {}
        for code, n in counts.items():
            if code in PROGRESSIVE:
                out[code] = 1
                if n > 1:
                    out[PROGRESSIVE[code]] = 1
            else:
                out[code] = n
        return out
