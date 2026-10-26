"""The Zexion world."""

from __future__ import annotations

import hashlib
import logging
from collections import Counter
from collections.abc import Mapping
from typing import Any, TextIO

from BaseClasses import Item, ItemClassification, LocationProgressType, MultiWorld, Region
from Options import OptionError
from worlds.AutoWorld import World
from worlds.generic.Rules import add_item_rule

from . import data, logic, rules, starting, web_world
from .items import (
    AREA_MAP,
    ASSIST_UNLOCK,
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
    SCRAP_CHUNK,
    SCRAP_ENERGY,
    SCRAP_METAL,
    SUPPLIES,
    ZexionItem,
    as_inventory,
    classification,
    code_of,
    item_name_groups,
    item_name_to_id,
)
from .locations import (
    LOCATION_ID_BASE,
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
# Stand-ins where the game must not hand out the item: another player's (Scrap Chunk for
# progression, but not for another Zexion player's supplies: important_elsewhere), or this
# world's own OS Install Disk (Scrap Chunk). This world's own Scrap Energy and Chunk are written
# as Scrap Metal, so 47 and 48 always mean a stand-in (client/aplabels.py).
FOREIGN, FOREIGN_PROGRESSION = SCRAP_ENERGY, SCRAP_CHUNK
# Supplies (items.SUPPLIES) are progression for the rules' counts, but minor on the map, as the
# game marks this player's own (important_elsewhere), and not worth an Area Map's hint. The OS
# Install Disks are hinted: each is a step to the goal.
NOT_HINTED = SUPPLIES
# With the OS Install Disks goal, what each disk takes the place of, in this order: Scrap (the keys'
# places become Scrap Metal first), then Area Maps, then Energy Shields. No rule reads any of
# them (worlds/zexion/data.py).
DISC_REPLACES = (*FILLER_ORDER, ENERGY_SHIELD)


def important_elsewhere(item: Item | None) -> bool:
    """Whether another player's item gets the important stand-in, its map square raised: a
    progression item, but not a Zexion supply, which the game marks minor like this player's
    own (an Energy Tank, a Missile Container)."""
    if item is None or not item.advancement:
        return False
    return not (item.game == ZexionItem.game and code_of.get(item.name) in SUPPLIES)


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
        starts = {s["cell"]: s for s in self.mode["starts"]}
        choice = self.options.start_room.value
        cell = AUTOMATIC_START if choice == AUTOMATIC else START_CELLS[choice]
        if cell not in starts:
            # A room this mode does not offer, typed or rolled by 'random' over every mode's
            # rooms (the two cannot be told apart), or automatic's should the mode not offer
            # it: take one the mode offers instead.
            replacement = self.random.choice(sorted(starts))
            logging.warning(
                f"Zexion ({self.player_name}): start room {cell_name(cell)} is not offered "
                f"in {self.mode_name} mode; using {cell_name(replacement)}"
            )
            cell = replacement
        self.start = starts[cell]
        self.second = self.pick_second_start(starts) if self.mode["double_teleport"] else None
        self.kept_locations = set(self.mode["location_indices"])
        placed = self.options.os_install_disks.value
        required = self.options.os_install_disks_required.value
        # Starting disks count toward the goal (set_rules; the client's discs_start).
        start = self.options.start_inventory.value.get(NAMES[DATA_DISC], 0)
        if self.disc_hunt and required > placed + start:
            raise OptionError(
                f"Zexion ({self.player_name}): OS Install Disks Required ({required}) is "
                f"more than OS Install Disks ({placed})"
                + (f" plus the {start} in start_inventory" if start else "")
            )

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

    def pick_second_start(self, starts: dict) -> dict:
        """The second teleport target: a room that, together with the start, lets the starting
        items reach at least data.MIN_SPHERE_ZERO locations. Every item reaches the same
        locations and regions from each room, which the mode holds (docs/re/locations.md)."""
        reached = set(self.start["sphere0_indices"])
        partners = [
            b
            for c, b in sorted(starts.items())
            if b is not self.start
            and len(reached | set(b["sphere0_indices"])) >= data.MIN_SPHERE_ZERO
        ]
        if not partners:
            # Every OHKO room has one on build B1 (the tests try each).
            raise OptionError(
                f"Zexion: no second teleport target suits start room "
                f"{cell_name(self.start['cell'])} in {self.mode_name} mode"
            )
        return self.random.choice(partners)

    def create_item(self, name: str) -> Item:
        return ZexionItem(name, classification(code_of[name]), item_name_to_id[name], self.player)

    def get_filler_item_name(self) -> str:
        return FILLER_NAME

    def create_regions(self) -> None:
        cells = data.CELL_REGION
        self.created_regions = set(self.mode["regions"])
        menu = Region("Menu", self.player, self.multiworld)
        by_index = {
            r: Region(rules.region_name(r), self.player, self.multiworld)
            for r in sorted(self.created_regions)
        }
        self.multiworld.regions += [menu, *by_index.values()]
        menu.connect(by_index[cells[self.start["cell"]]], "Start")
        if self.second is not None:
            menu.connect(by_index[cells[self.second["cell"]]], "Second teleport target")

        for (a, b), ways in data.ENTRANCES.items():
            if a not in self.created_regions or b not in self.created_regions:
                continue
            for k, way in enumerate(ways):
                name = f"{rules.region_name(a)} to {b}" + (f" #{k + 1}" if len(ways) > 1 else "")
                rule = None if way is None else rules.requirement(self, *way)
                entrance = by_index[a].connect(by_index[b], name, rule)
                if way is not None:
                    for r in rules.neighbor_regions(*way) & self.created_regions:
                        self.multiworld.register_indirect_condition(by_index[r], entrance)

        for i in sorted(self.kept_locations):
            cell = data.LOCATIONS[i]
            region = by_index[cells[cell]]
            location = ZexionLocation(
                self.player, location_name(i), location_name_to_id[location_name(i)], region
            )
            entries = data.LOCATION_RULES.get(i)
            if entries is not None:
                location.access_rule = rules.requirement(self, entries, cell)
            region.locations.append(location)

        # The core goal is the grab of Zexion behind the four keys (RandoCells does not model
        # the keys); the client sends the goal at the ending (docs/re/memory.md, "The ending:
        # flag 250"), reached through the helper's escape skip or the escape itself. The OS
        # Install Disks goal has no event.
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
            codes.remove(next(c for c in FILLER_ORDER if c in codes))
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
        # This player's own Assist Unlock never goes to this player's own locations, by plando
        # or otherwise: the game would hand it out as Scrap Metal (location_items), and the
        # server never sends a player's own item, which the client needs to lift the assist
        # lock. Sent from the server console (its use) or found in another world, it arrives.
        assist = NAMES[ASSIST_UNLOCK]
        for location in self.multiworld.get_locations(self.player):
            if location.address is not None:
                add_item_rule(
                    location, lambda item: item.player != self.player or item.name != assist
                )
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

    @classmethod
    def stage_pre_fill(cls, multiworld: MultiWorld) -> None:
        """Refuse, naming the options, a seed whose fill would stop on AP's "Not enough filler
        items for excluded locations": excluded locations take only filler, and with the OS
        Install Disks goal each disk takes a filler item's place. The fill takes any player's
        filler, so this counts the whole multiworld's, after plando and every world's pre_fill
        but this stage, and refuses for a Zexion player whose excluded locations outnumber
        their own filler."""
        unfilled = multiworld.get_unfilled_locations()
        excluded = Counter(
            loc.player for loc in unfilled if loc.progress_type == LocationProgressType.EXCLUDED
        )
        # As Fill.distribute_items_restrictive sorts the pool: filler is neither kind.
        filler = Counter(
            item.player for item in multiworld.itempool if not item.advancement and not item.useful
        )
        if sum(excluded.values()) <= sum(filler.values()):
            return
        for world in multiworld.get_game_worlds(GAME):
            n, have = excluded[world.player], filler[world.player]
            if n <= have:
                continue
            fix = "exclude fewer locations (exclude_locations)"
            if world.disc_hunt:
                disks = world.options.os_install_disks.value
                fix = f"lower OS Install Disks ({disks}: each takes a filler item's place) or {fix}"
            locations = "location" if n == 1 else "locations"
            raise OptionError(
                f"Zexion ({world.player_name}): {n} excluded {locations}, but only {have} filler "
                f"items (Scrap, Area Maps or Intel Chips) to put there, and the other players' "
                f"filler does not make up the difference: {fix}"
            )

    def write_spoiler_header(self, spoiler_handle: TextIO) -> None:
        spoiler_handle.write(f"Mode:                            {self.mode_name}\n")
        spoiler_handle.write(f"Start room:                      {cell_name(self.start['cell'])}\n")
        if self.second is not None:
            target = cell_name(self.second["cell"])
            spoiler_handle.write(f"Second teleport target:          {target}\n")

    def fill_slot_data(self) -> Mapping[str, Any]:
        start_names = [item.name for item in self.multiworld.precollected_items[self.player]]
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
            "discs_start": start_names.count(NAMES[DATA_DISC]),
            "disc_locations": self.disc_locations(),
        }

    def disc_locations(self) -> list[int]:
        """The location indices holding this world's own OS Install Disks."""
        found = self.multiworld.find_item_locations(NAMES[DATA_DISC], self.player)
        return sorted(loc.address - LOCATION_ID_BASE for loc in found if loc.player == self.player)

    def hint_targets(self) -> list[list[int]]:
        """Where each of this player's progression items is, as [player, location id, item
        code], in a random order: the client hints the next one the player does not have for
        each Intel Chip (client/hints.py). This player's items include those of an item link
        this player is in: the group's item goes to every member, from the location listed
        here. Supplies (NOT_HINTED), starting items, which are in no location, and events are
        left out."""
        mine = {self.player} | {
            group_id
            for group_id, group in self.multiworld.groups.items()
            if self.player in group["players"]
        }
        out = []
        for location in self.multiworld.get_filled_locations():
            item = location.item
            if (
                item.player not in mine
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
        double = self.mode["double_teleport"]
        setup = {
            "seed_text": hashlib.sha256(name).hexdigest()[:16].upper(),
            # start, pool and settings
            **{key: list(value) for key, value in self.mode["menu"].items()},
            "second_target": None if double else list(self.mode["second_target"]),
        }
        if double:
            setup["start_table"] = [list(room) for room in data.START_TABLE]
            setup["table_build"] = data.DATA_BUILD
        return setup

    def location_items(self) -> list[int]:
        """The item code the game holds at each location: this world's own item as its game ID
        (a progressive weapon as the weapon; Scrap Energy and Chunk as Scrap Metal), FOREIGN or
        FOREIGN_PROGRESSION for another world's item and for this world's own OS Install Disk,
        and Scrap Metal at a left-out location."""
        out = []
        for i in range(len(data.LOCATIONS)):
            if i not in self.kept_locations:
                out.append(SCRAP_METAL)
                continue
            item = self.get_location(location_name(i)).item
            if item is None or item.player != self.player or item.code is None:
                out.append(FOREIGN_PROGRESSION if important_elsewhere(item) else FOREIGN)
                continue
            code = code_of[item.name]
            if code == DATA_DISC:
                out.append(FOREIGN_PROGRESSION)
                continue
            code = DRAWN_AS.get(code, code)  # an Intel Chip: the Area Map
            # Scrap Energy and Chunk, and an item that is not the game's, are handed out as Scrap
            # Metal. (The only such item, Assist Unlock, is kept out of these locations: set_rules.)
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
        return as_inventory(counts.items())
