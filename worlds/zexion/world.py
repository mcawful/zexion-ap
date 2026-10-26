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

from . import data, logic, rules, starting, web_world
from .items import (
    AREA_MAP,
    DATA_DISC,
    DRAWN_AS,
    ENERGY_SHIELD,
    FILLER_NAME,
    GAME,
    GAME_ITEMS,
    INTEL_CHIP,
    ITEM_ID_BASE,
    KEY_CODES,
    KEYS,
    NAMES,
    PROGRESSIVE,
    SCRAP_CHUNK,
    SCRAP_ENERGY,
    SCRAP_METAL,
    SUPPLIES,
    ZexionItem,
    classification,
    code_of,
    item_name_groups,
    item_name_to_id,
)
from .locations import (
    ZexionLocation,
    cell_name,
    location_name,
    location_name_groups,
    location_name_to_id,
)
from .options import AUTOMATIC, START_CELLS, Goal, ZexionOptions

GOAL_CELL = (32, 45)  # the core, where Zexion is grabbed
AUTOMATIC_START = (13, 16)  # where one of the owner's seeds started
# Filler item codes, in the order a left-out location removes them from the pool.
FILLER_ORDER = (SCRAP_METAL, SCRAP_ENERGY, SCRAP_CHUNK, AREA_MAP)
# What the game holds at a location whose item is not this world's to hand out natively.
# Another player's item: Scrap Energy, or Scrap Chunk for a progression item, which the
# client's helper shows as an Archipelago item (the progression one with its up arrow), named at
# pickup (client/aplabels.py). A location left out, and this world's own Scrap Energy and Scrap
# Chunk, are Scrap Metal, so 47 or 48 in a location always means another player's item, or (48)
# one of this world's own OS Install Disks, which the game must not hand out (location_items);
# all are filler the game counts as Scrap Metal.
FOREIGN, FOREIGN_PROGRESSION = SCRAP_ENERGY, SCRAP_CHUNK
# Supplies (items.SUPPLIES) are progression for the rules' counts, but not worth an Area Map's
# hint. The OS Install Disks are hinted: each is a step to the goal (the owner's call).
NOT_HINTED = SUPPLIES
# With the OS Install Disks goal, what each disk takes the place of, in this order: Scrap (the keys'
# places become Scrap Metal first), then Area Maps, then Energy Shields. No rule reads any of
# them (worlds/zexion/data.py).
DISC_REPLACES = (SCRAP_METAL, SCRAP_ENERGY, SCRAP_CHUNK, AREA_MAP, ENERGY_SHIELD)


class ZexionWorld(World):
    """Zexion is a metroidvania by Gallant Leaf with a built-in randomizer. Explore a 54 by 54
    map of rooms, find upgrades and work your way to the end."""

    game = GAME
    web = web_world.ZexionWebWorld()
    options_dataclass = ZexionOptions
    options: ZexionOptions
    item_name_to_id = item_name_to_id
    location_name_to_id = location_name_to_id
    item_name_groups = item_name_groups
    location_name_groups = location_name_groups

    created_regions: set[int]
    start: dict
    second: dict | None  # the second teleport target, in a mode that has one
    kept_locations: set[int]  # location indices this world creates
    mode_name: str
    mode: dict
    logic_settings: logic.Settings  # not World.settings, the host.yaml settings

    def generate_early(self) -> None:
        self.mode_name = self.options.mode.current_key
        self.mode = data.MODES[self.mode_name]
        self.logic_settings = logic.Settings(
            self.mode["energy_total"], self.mode["mode69"], self.mode["options"]
        )
        self.check_start_inventory()
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
                f"Zexion ({self.player_name}): start room {cell_name(cell)} is not offered "
                f"in {self.mode_name} mode; using {cell_name(replacement)}"
            )
            cell = replacement
        self.start = starts[cell]
        self.second = None
        self.kept_locations = set(range(len(data.LOCATIONS)))
        if self.mode["double_teleport"]:
            self.pick_second_start(starts)
        if self.disc_hunt:
            self.check_discs()

    def check_start_inventory(self) -> None:
        """Refuse a start_inventory the client could not give (worlds/zexion/starting.py)."""
        counts = Counter(self.mode["start_inventory"])
        for name, n in self.options.start_inventory.value.items():
            counts[code_of[name]] += n
        found = starting.problems(counts, self.disc_hunt)
        if found:
            raise OptionError(
                f"Zexion ({self.player_name}): the client cannot start a seed with this "
                f"start_inventory: {'; '.join(found)}"
            )

    @property
    def disc_hunt(self) -> bool:
        return self.options.goal.value == Goal.option_os_install_disks

    def disc_room(self) -> int:
        """How many OS Install Disks this mode's pool has room for: its Scrap, the keys' places,
        Area Maps and Energy Shields, less the filler that left-out locations take out."""
        pool = Counter(self.mode["pool"])
        room = sum(pool[c] for c in DISC_REPLACES) + sum(pool[c] for c in KEY_CODES)
        return room - (len(data.LOCATIONS) - len(self.kept_locations))

    def check_discs(self) -> None:
        placed = self.options.os_install_disks.value
        required = self.options.os_install_disks_required.value
        if required > placed:
            raise OptionError(
                f"Zexion ({self.player_name}): OS Install Disks Required ({required}) is "
                f"more than OS Install Disks ({placed})"
            )
        # Every mode has room for the option's most (30) as the data stands, so this guards
        # only data regenerated with less room (tools/gen_world_data.py).
        room = self.disc_room()
        if placed > room:
            raise OptionError(
                f"Zexion ({self.player_name}): {self.mode_name} mode has room for {room} OS "
                f"Install Disks, not {placed}"
            )

    def pick_second_start(self, starts: dict) -> None:
        """Pick the second teleport target: a room that, together with the start, lets the
        starting items reach at least data.MIN_SPHERE_ZERO locations. Only locations every item
        reaches from one of the two are kept (docs/re/locations.md)."""

        def sphere0(a: dict, b: dict) -> int:
            return len(set(a["sphere0_indices"]) | set(b["sphere0_indices"]))

        partners = [
            b
            for c, b in sorted(starts.items())
            if b is not self.start and sphere0(self.start, b) >= data.MIN_SPHERE_ZERO
        ]
        if not partners:
            pairs = [
                (a, b)
                for a in starts.values()
                for b in starts.values()
                if a is not b and sphere0(a, b) >= data.MIN_SPHERE_ZERO
            ]
            if not pairs:
                raise OptionError(f"Zexion: {self.mode_name} mode has no pair of start rooms")
            a, b = self.random.choice(pairs)
            logging.warning(
                f"Zexion ({self.player_name}): no second target suits start room "
                f"{cell_name(self.start['cell'])} in {self.mode_name} mode; "
                f"starting at {cell_name(a['cell'])} instead"
            )
            self.start = a
            partners = [b]
        self.second = self.random.choice(partners)
        self.kept_locations = set(self.of_start(self.start, "location_indices")) | set(
            self.of_start(self.second, "location_indices")
        )

    def of_start(self, start: dict, key: str) -> list[int]:
        """A start's regions or location indices: the mode's, which data.py holds once when
        every start has the same, else the start's own."""
        shared = self.mode[key]
        return start[key] if shared is None else shared

    def create_item(self, name: str) -> Item:
        return ZexionItem(name, classification(code_of[name]), item_name_to_id[name], self.player)

    def get_filler_item_name(self) -> str:
        return FILLER_NAME

    def create_regions(self) -> None:
        cells = rules.cell_region()
        self.created_regions = set(self.of_start(self.start, "regions"))
        if self.second is not None:
            self.created_regions |= set(self.of_start(self.second, "regions"))
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
        # escape needs one of several combinations of items, the Zenith Thrusters alone being
        # one (reported by the owner). The four keys open the core; RandoCells does not model
        # them. With the OS Install Disks goal there is no event: the disks are the goal
        # (set_rules).
        if self.disc_hunt:
            return
        core = by_index[cells[GOAL_CELL]]
        goal = ZexionLocation(self.player, "Zexion", None, core)
        goal.place_locked_item(
            ZexionItem("Victory", ItemClassification.progression, None, self.player)
        )
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
        if self.disc_hunt:
            # The keys' places become Scrap Metal, then each disc takes one of DISC_REPLACES.
            codes = [c for c in codes if c not in KEY_CODES] + [SCRAP_METAL] * len(KEY_CODES)
            for _ in range(self.options.os_install_disks.value):
                codes.remove(next(c for c in DISC_REPLACES if c in codes))
            codes += [DATA_DISC] * self.options.os_install_disks.value
        if self.options.area_map_hints:
            # Each Area Map left is a hint: an Intel Chip, which the game holds as the Area Map.
            codes = [INTEL_CHIP if c == AREA_MAP else c for c in codes]
        pool = [self.create_item(NAMES[code]) for code in codes]
        self.multiworld.itempool += pool

    def set_rules(self) -> None:
        if self.disc_hunt:
            required = self.options.os_install_disks_required.value
            self.multiworld.completion_condition[self.player] = lambda state: state.has(
                NAMES[DATA_DISC], self.player, required
            )
            return
        self.get_location("Zexion").access_rule = lambda state: state.has_all(KEYS, self.player)
        self.multiworld.completion_condition[self.player] = lambda state: state.has(
            "Victory", self.player
        )

    def write_spoiler_header(self, spoiler_handle: TextIO) -> None:
        spoiler_handle.write(f"Mode:                            {self.mode_name}\n")
        spoiler_handle.write(f"Start room:                      {cell_name(self.start['cell'])}\n")
        if self.second is not None:
            target = cell_name(self.second["cell"])
            spoiler_handle.write(f"Second teleport target:          {target}\n")

    def fill_slot_data(self) -> Mapping[str, Any]:
        return {
            "mode": self.mode_name,
            "start_index": self.start["index"],
            "start_column": self.start["column"],
            "start_row": self.start["row"],
            "second_start": None
            if self.second is None
            else {k: self.second[k] for k in ("index", "column", "row")},
            "kept_locations": sorted(self.kept_locations),
            "locations": [list(c) for c in data.LOCATIONS],
            "location_items": self.location_items(),
            "start_items": {str(k): v for k, v in sorted(self.start_items().items())},
            "amounts": list(self.mode["amounts"]),
            "options": {str(t): v for t, v in self.mode["checked_options"].items()},
            "menu": self.menu_setup(),
            # Other players' items are this item in location_items (client/aplabels.py).
            "stand_in": FOREIGN,
            "stand_in_progression": FOREIGN_PROGRESSION,
            "death_link": bool(self.options.death_link),
            "allow_assists": bool(self.options.allow_assists),
            "hints": self.hint_targets() if self.options.area_map_hints else [],
            # The Area Maps are Intel Chips (items.py): the helper renames the game's.
            "intel_chip": bool(self.options.area_map_hints),
            # The OS Install Disks goal (client/discs.py): the disks are counted by the client
            # and never written into the game's inventory, so this world's own are stand-ins
            # in location_items, listed here by location index.
            "goal": self.options.goal.current_key,
            "discs_required": self.options.os_install_disks_required.value if self.disc_hunt else 0,
            "discs_placed": self.options.os_install_disks.value if self.disc_hunt else 0,
            "discs_start": self.precollected_discs(),
            "disc_locations": self.disc_locations(),
        }

    def precollected_discs(self) -> int:
        precollected = self.multiworld.precollected_items[self.player]
        return sum(1 for item in precollected if item.name == NAMES[DATA_DISC])

    def disc_locations(self) -> list[int]:
        """The location indices holding this world's own OS Install Disks."""
        out = []
        for i in range(len(data.LOCATIONS)):
            if i not in self.kept_locations:
                continue
            item = self.get_location(location_name(i)).item
            if item is not None and item.player == self.player and item.name == NAMES[DATA_DISC]:
                out.append(i)
        return out

    def hint_targets(self) -> list[list[int]]:
        """Where each of this player's progression items is, as [player, location id, item
        code], in a random order: the client hints the next one the player does not have for
        each Intel Chip (client/hints.py). Supplies (NOT_HINTED), starting items, which are in
        no location, and events are left out."""
        out = []
        for location in self.multiworld.get_filled_locations():
            item = location.item
            if (
                item.player != self.player
                or not item.advancement
                or item.code is None
                or location.address is None
                or item.code - ITEM_ID_BASE in NOT_HINTED
            ):
                continue
            out.append([location.player, location.address, item.code - ITEM_ID_BASE])
        out.sort()  # the multiworld's order, so the shuffle below alone decides
        self.random.shuffle(out)
        return out

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
        copy the upgrade, and Scrap Energy and Scrap Chunk as Scrap Metal), FOREIGN where another
        world's item is (FOREIGN_PROGRESSION for a progression item, and for this world's own
        OS Install Disk, which the game must not hand out), and Scrap Metal where the location was
        left out."""
        out = []
        for i in range(len(data.LOCATIONS)):
            if i not in self.kept_locations:
                out.append(SCRAP_METAL)
                continue
            item = self.get_location(location_name(i)).item
            if item is None or item.player != self.player or item.code is None:
                out.append(
                    FOREIGN_PROGRESSION if item is not None and item.advancement else FOREIGN
                )
                continue
            code = code_of[item.name]
            if code == DATA_DISC:
                out.append(FOREIGN_PROGRESSION)
                continue
            code = DRAWN_AS.get(code, code)  # an Intel Chip: the Area Map
            # Scrap Energy and Chunk, and an item that is not the game's (Assist Unlock, only
            # ever placed by plando), are handed out as Scrap Metal.
            own_scrap = code in (SCRAP_ENERGY, SCRAP_CHUNK) or code not in GAME_ITEMS
            out.append(SCRAP_METAL if own_scrap else code)
        return out

    def start_items(self) -> dict[int, int]:
        """Inventory bytes for everything this player starts with: the mode's starting items
        and any start_inventory, a progressive weapon's second copy as its upgrade."""
        counts = Counter(
            code_of[item.name]
            for item in self.multiworld.precollected_items[self.player]
            if item.code is not None
            and code_of[item.name] in GAME_ITEMS
            and code_of[item.name] != DATA_DISC
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
