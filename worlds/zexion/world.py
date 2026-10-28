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

from . import custom, data, logic, reach, rules, starting, web_world
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
from .options import (
    AMOUNTS,
    AUTOMATIC,
    AUTOMATIC_ROOM,
    OHKO_SAFETY,
    PRESETS,
    RANDOM_START,
    SETTINGS,
    Amount,
    Goal,
    Safety,
    StartRoom,
    ZexionOptions,
)

CORE_REGION = "The Core"  # where Zexion is grabbed: data.CORE
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

    created_regions: set[int]  # what every item reaches from the start(s): reach.py
    start: dict  # a data.START_ROOMS entry
    second: dict | None  # the second teleport target, in a mode that has one
    kept_locations: set[int]  # location indices this world creates
    seed_reach: reach.Seed
    mode_name: str
    menu: custom.Menu  # the randomizer menu as this seed's settings
    start_inventory: dict[int, int]  # the seed's starting items, by item code
    pool_codes: list[int]  # the seed's item pool, one code per location, before create_items
    logic_settings: logic.Settings  # not World.settings, the host.yaml settings

    def generate_early(self) -> None:
        self.mode_name = self.options.mode.current_key
        self.menu = self.read_menu()
        self.logic_settings = self.menu.logic_settings()
        if self.mode_name == "custom":
            start, pool = custom.resolve(self.menu, self.random)
            self.start_inventory = custom.starting_items(self.menu, start)
            try:
                self.pool_codes = custom.pool_items(self.menu, pool, self.random)
            except custom.PoolError as error:
                raise OptionError(f"Zexion ({self.player_name}): {error}") from error
        else:
            # A preset is one fresh save's items, as the game generated them.
            self.start_inventory = dict(data.MODES[self.mode_name]["start_inventory"])
            self.pool_codes = list(data.MODES[self.mode_name]["pool"])
        self.check_start_inventory()
        self.seed_reach = reach.Seed.of(self.logic_settings, self.start_inventory, self.pool_codes)
        self.choose_starts()
        self.fit_pool()
        door = data.CELL_REGION[data.CORE["door"]]
        if not self.disc_hunt and door not in self.created_regions:
            # The core is entered from there (create_regions).
            raise OptionError(
                f"Zexion ({self.player_name}): {self.mode_name} mode's settings do not reach "
                f"the core's door from start room {cell_name(self.start['cell'])}"
            )
        placed = self.options.os_install_disks.value
        required = self.options.os_install_disks_required.value
        # Starting disks count toward the goal (set_rules; the client's discs_start).
        start_disks = self.options.start_inventory.value.get(NAMES[DATA_DISC], 0)
        if self.disc_hunt and required > placed + start_disks:
            raise OptionError(
                f"Zexion ({self.player_name}): OS Install Disks Required ({required}) is "
                f"more than OS Install Disks ({placed})"
                + (f" plus the {start_disks} in start_inventory" if start_disks else "")
            )

    def read_menu(self) -> custom.Menu:
        """The randomizer menu as this seed's settings: a preset's, with Starting Location and
        Safety the YAML's, or Custom's. Refuses a Custom-only option set with a preset, and a
        state the game's menu cannot hold (custom.problems)."""
        o, name, who = self.options, self.mode_name, f"Zexion ({self.player_name})"
        is_custom = name == "custom"
        menu = custom.Menu.preset(PRESETS[o.custom_base.value] if is_custom else name)
        toggles = [getattr(o, n) for n in SETTINGS if hasattr(getattr(o, n), "setting")]
        changed = [n for n in AMOUNTS if getattr(o, n).value != Amount.default]
        changed += [n for n in SETTINGS if getattr(o, n).value != getattr(o, n).default]
        changed += ["custom_items"] if o.custom_items.value else []
        if changed and not is_custom:
            raise OptionError(
                f"{who}: {', '.join(changed)} can be set with mode: custom only. Easy, Standard "
                f"and OHKO are the game's presets; leave these 'preset' (any of them changed "
                f"makes the game's mode Custom)"
            )
        for which, option_name in enumerate(AMOUNTS):
            value = getattr(o, option_name).value
            if value != Amount.default:
                menu = menu.with_amount(which, value)
        entries = {item: i for i, (item, _) in enumerate(custom.MENU_ITEMS)}
        for item, state in o.custom_items.value.items():
            if state not in custom.STATES:
                raise OptionError(
                    f"{who}: custom_items {item}: {state!r} is not one of "
                    f"{', '.join(custom.STATES)}"
                )
            menu = menu.with_item(entries[item], state)
        for option in toggles:
            if option.value != option.option_preset:
                menu = menu.with_settings({option.setting: option.value})
        for option in (o.hints, o.gates, o.safety, o.start_keys):
            if option.value != option.default:
                menu = menu.with_value(option.bits, option.value)
        if name == "ohko" and o.safety.value not in (Safety.default, *OHKO_SAFETY):
            raise OptionError(f"{who}: OHKO mode takes Safety light or off only, as the game does")
        if self.disc_hunt and menu.setting(custom.ABRIDGED_FINALE):
            raise OptionError(
                f"{who}: abridged_finale ends the game when Zexion is grabbed, so it cannot go "
                f"with the OS Install Disks goal"
            )
        if not is_custom:
            return menu
        unsupported = []
        if menu.setting(custom.SECRET_SHUFFLE):
            unsupported.append("secret_shuffle: on (a 155th location)")
        if menu.amounts[custom.START_BOMBS]:
            unsupported.append("starting_mega_bombs above 0 (the client cannot give them)")
        if unsupported:
            raise OptionError(f"{who}: not supported yet: {'; '.join(unsupported)}")
        found = custom.problems(menu)
        if found:
            raise OptionError(f"{who}: the game's menu cannot hold this: {'; '.join(found)}")
        return menu

    def check_start_inventory(self) -> None:
        """Refuse a start_inventory the client could not give (worlds/zexion/starting.py)."""
        counts = Counter(self.start_inventory)
        for name, n in self.options.start_inventory.value.items():
            counts[code_of[name]] += n
        found = starting.problems(counts, self.disc_hunt, self.logic_settings.guns_first)
        if found:
            raise OptionError(
                f"Zexion ({self.player_name}): the client cannot start a seed with this "
                f"start_inventory: {'; '.join(found)}"
            )

    @property
    def disc_hunt(self) -> bool:
        return self.options.goal.value == Goal.option_os_install_disks

    @property
    def double_teleport(self) -> bool:
        return bool(self.menu.setting(custom.DOUBLE_TELEPORT))

    def reaches_the_door(self, room: dict, second: dict | None = None) -> bool:
        """With the core goal, whether every item reaches the core's door from this room (and
        its second teleport target, as the logic counts it) and passes the core's own lists on
        the way to Zexion (data.CORE)."""
        if self.disc_hunt:
            return True
        door = data.CELL_REGION[data.CORE["door"]]
        every = self.seed_reach.every_from(room, second)
        have = dict(self.seed_reach.every_item)
        return door in every.regions and all(
            logic.satisfied(entries, have, self.logic_settings, cell)
            for entries, cell in data.CORE["needs"]
        )

    def partners(self, room: dict) -> list[dict]:
        """With Double Teleport, the second teleport targets for this start. The game draws one
        of the other 33 rooms, flagged ones too, and judges the pair by its winnability check;
        where that fails it offers PLAY ANYWAY, which Archipelago's fill cannot. So the world
        draws among the rooms with which the pair suits the seed as ``passes`` judges a start:
        with the start, the starting items collect a location, and with the core goal every
        item reaches the core, the target counted as the logic counts it (reach.second_counts);
        a flagged room only when the logic counts it from the start (reach.second_starts)."""
        rooms = reach.second_starts(self.seed_reach, room, data.START_ROOMS)
        if self.reaches_the_door(room):
            return rooms
        return [other for other in rooms if self.reaches_the_door(room, other)]

    def passes(self, room: dict) -> bool:
        """Whether a start room suits the seed, as the game's START GAME judges one: its
        starting items collect a location from it, and with the core goal every item reaches
        the core and Zexion, the world's part of the game's winnability check. From a room with
        one such location Archipelago's fill can fail (up to one time in four, solo, in tests):
        the owner chose the game's rule over a higher minimum, 2026-10-27 (docs/re/locations.md,
        "Which start rooms the world offers"). With Double Teleport, the same for the start and
        a second teleport target: ``partners``."""
        if self.double_teleport:
            return bool(self.partners(room))
        return bool(self.seed_reach.sphere0(room["cell"]).locations) and self.reaches_the_door(room)

    def choose_starts(self) -> None:
        """The start room by the start_room option, and with Double Teleport the second
        teleport target (docs/custom-mode.md, "Starts, as the game picks them")."""
        who = f"Zexion ({self.player_name})"
        choice = self.options.start_room.value
        start = None
        if choice not in (RANDOM_START, AUTOMATIC):
            room = data.START_ROOMS[choice - 1]
            if not self.passes(room):
                raise OptionError(
                    f"{who}: start room {StartRoom.name_lookup[choice]} does not suit "
                    f"{self.mode_name} mode's settings: from it the starting items reach no "
                    f"location, or every item does not reach the core"
                )
            start = room
        elif choice == AUTOMATIC:
            room = data.START_ROOMS[AUTOMATIC_ROOM]
            if self.passes(room):
                start = room
            else:
                logging.warning(
                    f"{who}: start room {StartRoom.name_lookup[AUTOMATIC_ROOM + 1]} does not "
                    f"suit {self.mode_name} mode's settings; starting in a random room instead"
                )
        if start is None:
            # The seed's own starting items, as the game's score counts them: not start_inventory.
            score = custom.score(self.start_inventory)
            safety = self.menu.value(custom.SAFETY)
            start = custom.random_start(data.START_ROOMS, score, safety, self.passes, self.random)
            if start is None:
                raise OptionError(f"{who}: no start room suits {self.mode_name} mode's settings")
        self.start = start
        self.second = None
        if self.double_teleport:
            self.second = self.random.choice(self.partners(start))

    def fit_pool(self) -> None:
        """The locations every item reaches from the start(s), which this world creates, and
        the pool made to fit them: each location left out takes a filler item with it, and with
        the OS Install Disks goal each disk takes one too (create_items). Where the pool has too
        little filler, ten Missile Containers or Mega Bombs become a box and nine Scrap Metal, as
        the game makes filler (custom.compress); that changes what the items reach, so it is
        worked out again."""
        disks = self.options.os_install_disks.value if self.disc_hunt else 0
        while True:
            every = self.seed_reach.every_from(self.start, self.second)
            left_out = len(data.LOCATIONS) - len(every.locations)
            # With the disks, the pool's keys become Scrap Metal and Energy Shields give way too.
            spare = 0
            if self.disc_hunt:
                spare = sum(1 for c in self.pool_codes if c in (*KEY_CODES, ENERGY_SHIELD))
            needed = max(left_out, left_out + disks - spare)
            try:
                codes = custom.compress(self.pool_codes, needed, FILLER_ORDER)
            except custom.PoolError as error:
                what = f"{left_out} locations the items do not reach"
                if disks:
                    what += f" and {disks} OS Install Disks"
                raise OptionError(
                    f"Zexion ({self.player_name}): the pool has too little filler for the "
                    f"{what}: {error}"
                ) from error
            if codes == self.pool_codes:
                break
            self.pool_codes = codes
            self.seed_reach = reach.Seed.of(
                self.logic_settings, self.start_inventory, self.pool_codes
            )
        self.created_regions = set(every.regions)
        self.kept_locations = set(every.locations)

    def create_item(self, name: str) -> Item:
        return ZexionItem(name, classification(code_of[name]), item_name_to_id[name], self.player)

    def get_filler_item_name(self) -> str:
        return FILLER_NAME

    def create_regions(self) -> None:
        cells = data.CELL_REGION
        menu = Region("Menu", self.player, self.multiworld)
        by_index = {
            r: Region(rules.region_name(r), self.player, self.multiworld)
            for r in sorted(self.created_regions)
        }
        self.multiworld.regions += [menu, *by_index.values()]
        menu.connect(by_index[cells[self.start["cell"]]], "Start")
        second = cells[self.second["cell"]] if self.second is not None else None
        if second in by_index:
            # The logic counts a flagged room only from a useful-item score of 27 (reach.py,
            # second_counts); the pause menu's teleport itself is always there.
            rule = rules.second_target(self) if reach.flagged(self.second) else None
            menu.connect(by_index[second], "Second teleport target", rule)
        if self.logic_settings.gates_open:
            self.connect_gates(by_index)

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

        # The core goal is the grab of Zexion in the core, behind the four keys (RandoCells
        # does not model the keys); the client sends the goal at the ending (docs/re/memory.md,
        # "The ending: flag 250"), after the escape and final battle, which no rule covers.
        # RandoCells opens the core only with Abridged Finale, so it has no region of its
        # own: it is entered from the last cell the world keeps, with the requirement lists on
        # the way to Zexion (data.CORE; docs/re/locations.md, "The core goal is Zexion's cell in
        # the core"). The OS Install Disks goal has no event.
        if self.disc_hunt:
            return
        core = Region(CORE_REGION, self.player, self.multiworld)
        self.multiworld.regions.append(core)
        ways = [rules.requirement(self, *way) for way in data.CORE["needs"]]
        entrance = by_index[cells[data.CORE["door"]]].connect(
            core, "Into the core", lambda state: all(way(state) for way in ways)
        )
        for way in data.CORE["needs"]:
            for r in rules.neighbor_regions(*way) & self.created_regions:
                self.multiworld.register_indirect_condition(by_index[r], entrance)
        goal = ZexionLocation(self.player, "Zexion", None, core)
        goal.place_locked_item(
            ZexionItem("Victory", ItemClassification.progression, None, self.player)
        )
        core.locations.append(goal)

    def connect_gates(self, by_index: dict[int, Region]) -> None:
        """Gates Open: once any gate cell counts as reached, the game's logic floods from the
        three gate cells of logic.GATE_TARGETS too, so each gate cell's region leads to theirs;
        R'Talva's gate is left out (reach.py, GATE_TRIGGERS)."""
        cells = data.CELL_REGION
        targets = [by_index[cells[c]] for c in logic.GATE_TARGETS if cells[c] in by_index]
        for cell, as_location in reach.GATE_TRIGGERS:
            source = by_index.get(cells[cell])
            if source is None:
                continue
            rule = rules.gate_trigger(self, cell) if as_location else None
            for target in targets:
                if target is source:
                    continue
                entrance = source.connect(
                    target, f"Gate from X{cell[1]} Y{cell[0]} to {target.name}", rule
                )
                if rule is not None:
                    entries = data.LOCATION_RULES[reach.LOCATION_AT[cell]]
                    for r in rules.neighbor_regions(entries, cell) & self.created_regions:
                        self.multiworld.register_indirect_condition(by_index[r], entrance)

    def create_items(self) -> None:
        for code, count in self.start_inventory.items():
            for _ in range(count):
                self.multiworld.push_precollected(self.create_item(NAMES[code]))
        codes = list(self.pool_codes)
        # A location left out takes a filler item out of the pool with it (fit_pool made sure
        # there are enough).
        for _ in range(len(data.LOCATIONS) - len(self.kept_locations)):
            codes.remove(next(c for c in FILLER_ORDER if c in codes))
        if self.disc_hunt:
            # The keys' places become Scrap Metal, then each disc takes one of DISC_REPLACES.
            keys = sum(1 for c in codes if c in KEY_CODES)
            codes = [c for c in codes if c not in KEY_CODES] + [SCRAP_METAL] * keys
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
        if self.mode_name == "custom":
            amounts = ", ".join(str(n) for n in self.menu.amounts)
            spoiler_handle.write(f"Amounts:                         {amounts}\n")
            started = ", ".join(f"{NAMES[c]} x{n}" for c, n in self.start_inventory.items())
            spoiler_handle.write(f"Starting items:                  {started}\n")
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
            "amounts": list(self.menu.amounts),
            "options": {str(t): v for t, v in self.menu.checked().items()},
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
        """The randomizer menu as this seed's settings, for the client's ARCHIPELAGO choice
        (client/newgame.py): the seed's own menu state (a preset's, or Custom's, its coin-flip
        items and amounts as the menu holds them), with Starting Location the world's start
        room, so the pause menu's view of the settings names it (the game's mode check skips
        Starting Location, so a preset still reads as itself), and a seed text of 16 hex digits
        from this seed and player. With Double Teleport, whose placement reads the menu's start
        table, the table instead of a second target (the seed's second start is written)."""
        name = f"{self.multiworld.seed_name}:{self.player}".encode()
        menu = self.menu.with_value(custom.START_LOCATION, self.start["index"] + 1)
        double = self.double_teleport
        first = data.START_ROOMS[0]  # without Double Teleport, START GAME writes room 0's place
        setup = {
            "seed_text": hashlib.sha256(name).hexdigest()[:16].upper(),
            "start": list(menu.start),
            "pool": list(menu.pool),
            "settings": list(menu.settings),
            "second_target": None if double else [0, first["column"], first["row"]],
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
        and any start_inventory, with Guns First a progressive weapon's second copy as its
        upgrade."""
        counts = Counter(
            code_of[item.name]
            for item in self.multiworld.precollected_items[self.player]
            if item.code is not None
            and code_of[item.name] in GAME_ITEMS
            and code_of[item.name] != DATA_DISC
        )
        return as_inventory(counts.items(), self.logic_settings.guns_first)
