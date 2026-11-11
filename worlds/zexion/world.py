"""The Zexion world."""

from __future__ import annotations

import hashlib
import logging
import random
from collections import Counter
from collections.abc import Iterator, Mapping
from typing import Any, TextIO

from BaseClasses import (
    CollectionState,
    Item,
    ItemClassification,
    Location,
    LocationProgressType,
    MultiWorld,
    Region,
)
from Fill import remaining_fill
from Options import OptionError
from worlds.AutoWorld import World
from worlds.generic.Rules import add_item_rule

from . import custom, data, logic, plando, reach, rules, starting, web_world
from .items import (
    AREA_MAP,
    ASSIST_UNLOCK,
    DATA_DISC,
    DISGUISES,
    DRAWN_AS,
    FILLER_NAME,
    GAME,
    GAME_ITEMS,
    INTEL_CHIP,
    ITEM_ID_BASE,
    KEY_CODES,
    KEYS,
    NAMES,
    SCRAP,
    SCRAP_CHUNK,
    SCRAP_ENERGY,
    SCRAP_METAL,
    SUPPLIES,
    TRAPS,
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
    RANDOM_START,
    ROOM_NAMES,
    SETTINGS,
    Amount,
    Gates,
    Goal,
    StartRoom,
    ZexionOptions,
)

CORE_REGION = "The Core"  # where Zexion is grabbed: data.CORE
# The OS Install Disks goal's home, the Landing Pod: RETURN HOME WITH THE DISKS? is offered after
# a save at its save point, whichever room the seed starts in (client/discs.py, HOME).
HOME = data.START_ROOMS[ROOM_NAMES.index("landing_pod")]
# Filler item codes, in the order a left-out location removes them from the pool.
FILLER_ORDER = (*SCRAP, AREA_MAP)
# Stand-ins where the game must not hand out the item: another player's (Scrap Chunk for
# progression, but not for another Zexion player's supplies: important_elsewhere), this world's
# own OS Install Disk or trap (Scrap Chunk). This world's own
# Scrap Energy and Chunk are written as Scrap Metal, so 47 and 48 always mean a stand-in
# (client/aplabels.py).
FOREIGN, FOREIGN_PROGRESSION = SCRAP_ENERGY, SCRAP_CHUNK
# Supplies (items.SUPPLIES) are progression for the rules' counts, but minor on the map, as the
# game marks this player's own (important_elsewhere), and not worth an Area Map's hint. The OS
# Install Disks are hinted: each is a step to the goal.
NOT_HINTED = SUPPLIES
# With the OS Install Disks goal, what each disk takes the place of, in this order: Scrap (the keys'
# places become Scrap Metal first), then Area Maps, then Energy Shields. No rule reads any of
# them (worlds/zexion/data.py).
DISC_REPLACES = (*FILLER_ORDER, custom.ENERGY_SHIELD)
# The most OS Install Disks a player can hold, placed and starting: the helper's count on the
# pause screen ("NN OF NN": native/src/pause.cpp) and the equipment screen, and the file select's
# two digits (native/src/files.cpp, file_disk_digits), hold no more. Not a game rule: the game has
# no disk count.
MAX_DISKS = 99
GUN_CODES = tuple(custom.MENU_CODES[i] for i in custom.GUNS)  # the five guns' item codes
# The most draws of the menu's items (``ZexionWorld.draw_items``) tried for starting items the
# client can give with the start options' and that the named start room suits. Not a game rule:
# the game draws once per seed text.
DRAWS = 64
# How many of its own locations a Zexion player's starting state must reach before Archipelago
# places progression; below it, fill_hook places items that open more (open_the_start). Not a
# game rule: the game's generator repairs its placement instead (docs/re/locations.md).
START_REACH = 20


def important_elsewhere(item: Item | None) -> bool:
    """Whether another player's item gets the important stand-in, its map square raised: a
    progression item, but not a Zexion supply, which the game marks minor like this player's
    own (an Energy Tank, a Missile Container); and a trap, any game's, which looks important
    (the owner's choice, 2026-11-11; client/aplabels.py)."""
    if item is not None and item.trap:
        return True
    if item is None or not item.advancement:
        return False
    return not (item.game == ZexionItem.game and code_of.get(item.name) in SUPPLIES)


def kept_local(multiworld: MultiWorld, item: Item) -> bool:
    """Whether ``item`` is in its player's own ``local_items``, so only that player's locations
    take it (Archipelago's locality rules; empty with one player). An item link's group keeps
    its locality in the link's settings, not counted here."""
    return (
        multiworld.players > 1
        and item.name in multiworld.worlds[item.player].options.local_items.value
    )


def pairs_of(
    kinds: dict[tuple[int, str], list[Item]], keys: list[tuple[int, str]]
) -> Iterator[tuple[tuple[int, str], tuple[int, str], list[Item]]]:
    """Each two items of ``kinds`` (``ZexionWorld.by_kind``) among ``keys``, in their order: two
    of one kind, where it has two, or the first of each of two kinds."""
    for i, a in enumerate(keys):
        for b in keys[i:]:
            items = kinds[a][:2] if a == b else kinds[a][:1] + kinds[b][:1]
            if len(items) == 2:
                yield a, b, items


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
    location_cells: tuple[tuple[int, int], ...]  # the seed's locations by index (reach.py)
    seed_reach: reach.Seed
    mode_name: str
    menu: custom.Menu  # the randomizer menu as this seed's settings
    gates_unlocked: bool  # Gates Vanilla made Unlocked for the OS Install Disks goal (read_menu)
    # The mode's own starting items, by item code (start_inventory's and
    # start_inventory_from_pool's are on the options: reach_of_items counts all three).
    start_inventory: dict[int, int]
    pool_codes: list[int]  # the seed's item pool, one code per location, before create_items
    logic_settings: logic.Settings  # not World.settings, the host.yaml settings
    made_items: Counter[int]  # this player's items before plando, by item code (plando.py)

    def generate_early(self) -> None:
        self.mode_name = self.options.mode.current_key
        self.menu = self.read_menu()
        self.logic_settings = self.menu.logic_settings()
        self.location_cells = reach.seed_locations(self.logic_settings)
        self.draw_items()
        self.choose_starts()
        self.fit_pool()
        if not self.options.area_map_hints:
            # An Area Map reveals the map around the square it lies in (docs/re/memory.md, "Intel
            # Chips"): kept in this world (place_area_maps). Intel Chips reveal nothing.
            self.options.local_items.value.add(NAMES[AREA_MAP])
        # The core is entered from its door (create_regions); the disks' goal returns home from
        # the Landing Pod (set_rules).
        goal = HOME["cell"] if self.disc_hunt else data.CORE["door"]
        if data.CELL_REGION[goal] not in self.created_regions:
            where = "the core's door"
            if self.disc_hunt:
                where = "the Landing Pod, where the OS Install Disks goal returns home,"
            starts = f"start room {cell_name(self.start['cell'])}"
            if self.second is not None:
                starts += f" and second teleport target {cell_name(self.second['cell'])}"
            raise OptionError(
                f"Zexion ({self.player_name}): {self.mode_name} mode's settings do not reach "
                f"{where} from {starts}"
            )
        placed = self.options.os_install_disks.value
        required = self.options.os_install_disks_required.value
        # Starting disks count toward the goal (set_rules; the client's discs_start): those of
        # start_inventory on top of the placed ones, and those of start_inventory_from_pool
        # past the placed ones, which Archipelago starts with though the pool has none left.
        disk = NAMES[DATA_DISC]
        start_disks = self.options.start_inventory.value.get(disk, 0)
        start_disks += max(0, self.options.start_inventory_from_pool.value.get(disk, 0) - placed)
        if self.disc_hunt and required > placed + start_disks:
            raise OptionError(
                f"Zexion ({self.player_name}): OS Install Disks Required ({required}) is "
                f"more than OS Install Disks ({placed})"
                + (f" plus the {start_disks} in start_inventory" if start_disks else "")
            )
        if self.disc_hunt and placed + start_disks > MAX_DISKS:
            raise OptionError(
                f"Zexion ({self.player_name}): OS Install Disks ({placed}) and the "
                f"{start_disks} in start_inventory come to {placed + start_disks}, more than "
                f"the {MAX_DISKS} the game's screens can show"
            )

    def read_menu(self) -> custom.Menu:
        """The randomizer menu as this seed's settings: a preset's, with Starting Location and
        Safety the YAML's, or Custom's. Refuses a Custom-only option set with a preset, and a
        state the game's menu cannot hold or would build a broken pool from (custom.problems).
        With the OS Install Disks goal, Gates Vanilla (OHKO's, or the YAML's) becomes Unlocked,
        since Vanilla's gates take all four keys, which that goal does not place; the logic
        travels by gate only under Open, so what the seed reaches is the same
        (docs/re/locations.md, "The OS Install Disks goal: the disks, then the ending")."""
        o, name, who = self.options, self.mode_name, f"Zexion ({self.player_name})"
        is_custom = name == "custom"
        base = o.custom_base.current_key if is_custom else name
        menu = custom.Menu.preset(base)
        custom_only = (*AMOUNTS, *SETTINGS, "custom_items")
        changed = [n for n in custom_only if getattr(o, n).value != getattr(o, n).default]
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
            if not isinstance(state, str) or state not in custom.STATES:
                raise OptionError(
                    f"{who}: custom_items {item}: {state!r} is not one of "
                    f"{', '.join(custom.STATES)}"
                )
            menu = menu.with_item(entries[item], state)
        for option in (getattr(o, n) for n in (*SETTINGS, "safety")):
            if option.value != option.default:  # each a bit or bits, lowest first
                menu = menu.with_value(option.bits, option.value)
        if name == "ohko" and o.safety.current_key not in ("preset", "light", "off"):
            raise OptionError(f"{who}: OHKO mode takes Safety light or off only, as the game does")
        if self.disc_hunt and menu.setting(custom.ABRIDGED_FINALE):
            raise OptionError(
                f"{who}: abridged_finale ends the game when Zexion is grabbed, so it cannot go "
                f"with the OS Install Disks goal"
            )
        self.gates_unlocked = self.disc_hunt and menu.value(custom.GATES) == Gates.option_vanilla
        if self.gates_unlocked:
            menu = menu.with_value(custom.GATES, Gates.option_unlocked)
            # A warning where the YAML asked for Vanilla; a preset's own Vanilla (OHKO's) is no
            # choice of the YAML's, and its change is the goal's documented behavior: info.
            asked = o.gates.value == Gates.option_vanilla
            log = logging.warning if asked else logging.info
            source = "gates: vanilla" if asked else f"{base} mode's Gates Vanilla"
            log(
                f"{who}: {source} takes all four keys, which the OS Install Disks goal does not "
                f"place; the gates are Unlocked instead (no keys, two gates visited)"
            )
        if not is_custom:
            return menu
        if menu.amounts[custom.START_BOMBS]:
            raise OptionError(
                f"{who}: not supported yet: starting_mega_bombs above 0 (the client cannot give "
                "them)"
            )
        found = custom.problems(menu)
        if found:
            raise OptionError(
                f"{who}: the game's randomizer cannot hold or build this: {'; '.join(found)}"
            )
        return menu

    def option_start(self, option_name: str) -> Counter[int]:
        """A start option's items by item code: start_inventory or start_inventory_from_pool."""
        value = getattr(self.options, option_name).value
        return Counter({code_of[name]: n for name, n in value.items() if n > 0})

    def draw_items(self) -> None:
        """The seed's starting items and pool, drawn from its menu as the game draws them for a
        new game (``custom.resolve``, ``custom.pool_items``), a preset's as Custom's: Standard's
        starting gun by Random Gun, every mode's filler, and what they reach (``seed_reach``).
        A draw is drawn again, up to DRAWS times, when the client could not give its starting
        items with start_inventory's and start_inventory_from_pool's (``start_problems``), such
        as a third copy of a gun the seed started with that the YAML takes twice from the pool,
        or when the start_room it names does not suit it (``passes``), such as a room only some
        of Standard's guns suit. With none that fits, the seed is refused (docs/re/locations.md,
        "Where the world departs from the game")."""
        who, named = f"Zexion ({self.player_name})", self.named_room
        first: list[str] = []
        unsuited: set[str] = set()  # the starting guns of the draws the named room did not suit
        reaches: set[reach.Seed] = set()  # and what their items were, as the room is judged
        for _ in range(DRAWS):
            start, pool = custom.resolve(self.menu, self.random)
            self.start_inventory = custom.starting_items(self.menu, start)
            try:
                self.pool_codes = custom.pool_items(self.menu, pool, self.random)
            except custom.PoolError as error:
                raise OptionError(f"{who}: {error}") from error
            found = self.start_problems()
            if found:
                first = first or found
                continue
            self.seed_reach = self.reach_of_items()
            if named is None or self.passes(named):
                return
            guns = [NAMES[c] for c in GUN_CODES if self.start_inventory.get(c)]
            unsuited.add(" and ".join(guns) or "no gun")
            reaches.add(self.seed_reach)
        if not unsuited:
            raise OptionError(
                f"{who}: the client cannot start a seed with these starting items (the mode's "
                f"own, start_inventory and start_inventory_from_pool): {'; '.join(first)}"
            )
        goal = "the Landing Pod" if self.disc_hunt else "the core"
        tried = ""  # the draws are named only where they differed in what the room is judged by
        if len(reaches) > 1:
            guns = ", ".join(sorted(unsuited))
            tried = f" with any of the {DRAWS} draws of its items tried (starting with {guns})"
        message = (
            f"{who}: start room {StartRoom.name_lookup[self.options.start_room.value]} does not "
            f"suit {self.mode_name} mode's settings{tried}: from it the starting items reach no "
            f"location, or every item does not reach {goal}"
        )
        if first:
            message += f" (and other draws' starting items the client cannot give: {first[0]})"
        raise OptionError(message)

    @property
    def named_room(self) -> dict | None:
        """The start room start_room names, or None for random_start and automatic."""
        choice = self.options.start_room.value
        return None if choice in (RANDOM_START, AUTOMATIC) else data.START_ROOMS[choice - 1]

    def start_problems(self) -> list[str]:
        """Why the client could not give the drawn starting items with the start options' (one
        line each, empty when it can), and a start_inventory item that the pool holds too and
        the game holds once (worlds/zexion/starting.py)."""
        added = self.option_start("start_inventory")
        counts = (
            Counter(self.start_inventory) + added + self.option_start("start_inventory_from_pool")
        )
        guns_first = self.logic_settings.guns_first
        found = starting.problems(counts, self.disc_hunt, guns_first)
        # The pool as create_items makes it, as far as an item held once goes: with the OS
        # Install Disks goal without the keys.
        pool = [c for c in self.pool_codes if not (self.disc_hunt and c in KEY_CODES)]
        found += starting.pool_duplicates(added, self.start_inventory, pool, guns_first)
        return found

    def reach_of_items(self) -> reach.Seed:
        """What the seed's items reach (reach.Seed), with every item the player starts with as
        Archipelago counts it: the mode's own, start_inventory's and start_inventory_from_pool's
        (Main.py precollects both options' items). The from-pool items are taken out of the
        pool here, as Archipelago takes them out after create_items, a filler item in each one's
        place; pool_codes keeps them, one code per location, for create_items to put in."""
        from_pool = self.option_start("start_inventory_from_pool")
        start = Counter(self.start_inventory) + self.option_start("start_inventory") + from_pool
        pool = list(self.pool_codes)
        for code, n in from_pool.items():
            for _ in range(min(n, pool.count(code))):
                pool.remove(code)
        return reach.Seed.of(self.logic_settings, start, pool)

    def fill_hook(
        self,
        progitempool: list[Item],
        usefulitempool: list[Item],
        filleritempool: list[Item],
        fill_locations: list[Location],
    ) -> None:
        self.open_the_start(progitempool, fill_locations)
        self.place_area_maps(filleritempool, fill_locations)

    def place_area_maps(self, filleritempool: list[Item], fill_locations: list[Location]) -> None:
        """Place this player's Area Maps, kept in this world (``local_items``, Area Map Hints
        off), in its own locations before the main fill: placed last, among the other filler,
        one can find only other players' locations left, and the fill stops with "No more spots
        to place". Excluded locations first, as only filler goes there; then those the starting
        state (``open_the_start``'s, its items placed) does not reach, then those it does, and
        priority ones last, the last three kept for progression in that order. Archipelago's
        ``remaining_fill`` places them, in that order."""
        name = NAMES[AREA_MAP]
        if name not in self.options.local_items.value:  # emptied in a one-player seed
            return
        start = self.multiworld.state.copy()
        start.sweep_for_advancements()

        def order(loc: Location) -> int:
            if loc.progress_type == LocationProgressType.EXCLUDED:
                return 0
            if loc.progress_type == LocationProgressType.PRIORITY:
                return 3
            return 2 if loc.can_reach(start) else 1

        spots = sorted((loc for loc in fill_locations if loc.player == self.player), key=order)
        maps = [i for i in filleritempool if i.player == self.player and i.name == name]
        remaining_fill(self.multiworld, spots, maps, "Area Maps", check_location_can_fill=True)
        filleritempool[:] = [i for i in filleritempool if i.location is None]
        fill_locations[:] = [loc for loc in fill_locations if loc.item is None]

    def open_the_start(self, progitempool: list[Item], fill_locations: list[Location]) -> None:
        """Place items that open the way out of what the starting items reach, one step at a
        time, until this player's starting state reaches START_REACH of its locations that are
        not excluded (or all of them; excluded locations take no progression). Each step draws
        an item of ``progitempool``, this player's or an item link's it is in, that reaches more
        alone (``opening_step``), or else two that do together, and locks each in an empty
        location of this player's that the state already reaches and that can take it
        (``can_fill``), so the placed items can always be collected. Where none can be placed,
        it stops short with a warning naming the cause. Archipelago calls this from fill_hook,
        after plando and early items, before it places progression (docs/re/locations.md,
        "Where the world departs from the game")."""
        multiworld, player = self.multiworld, self.player
        mine = [
            loc
            for loc in multiworld.get_locations(player)
            if loc.address is not None and loc.progress_type != LocationProgressType.EXCLUDED
        ]
        state = multiworld.state.copy()
        state.sweep_for_advancements()
        while True:
            reached = [loc for loc in mine if loc.can_reach(state)]
            if len(reached) >= min(START_REACH, len(mine)):
                return
            empty = [loc for loc in reached if loc.item is None]
            step, why = self.opening_step(state, mine, len(reached), progitempool, empty)
            if not step:
                logging.warning(
                    f"Zexion ({self.player_name}): the starting items reach {len(reached)} of "
                    f"their locations, fewer than {START_REACH}, and {why}; Archipelago's fill "
                    f'may stop with "No more spots to place"'
                )
                return
            for item, spot in step:
                progitempool.pop(next(i for i, x in enumerate(progitempool) if x is item))
                fill_locations.pop(next(i for i, x in enumerate(fill_locations) if x is spot))
                spot.place_locked_item(item)
            state.sweep_for_advancements()

    def opening_step(
        self,
        state: CollectionState,
        mine: list[Location],
        reached: int,
        pool: list[Item],
        empty: list[Location],
    ) -> tuple[list[tuple[Item, Location]], str]:
        """Items in ``pool`` of this player's, or of an item link it is in, that, collected into
        ``state``, reach more of ``mine`` than ``reached``, each with a location of ``empty``
        that can take it: one drawn at random among those that do alone, or else a pair drawn
        among those that do together, in two locations. With none, an empty step and why, for
        the warning."""
        first = self.by_kind(pool)
        spots = {
            key: [loc for loc in empty if loc.can_fill(state, items[0])]
            for key, items in first.items()
        }

        def opens(items: list[Item]) -> bool:
            return self.opens(state, mine, reached, items)

        alone = [key for key in sorted(first) if spots[key] and opens(first[key][:1])]
        if alone:
            key = self.random.choice(alone)
            return [(first[key][0], self.random.choice(spots[key]))], ""
        if not empty:
            return [], "every one of those locations is excluded or already filled (plando)"
        # Every two that reach more together, then those that two locations can take: the same
        # pairs in the same order as pairs_of over the kinds with a location alone would give.
        together = [(a, b, items) for a, b, items in pairs_of(first, sorted(first)) if opens(items)]
        pairs = [
            (a, b, items)
            for a, b, items in together
            if spots[a] and spots[b] and len({id(loc) for loc in spots[a] + spots[b]}) >= 2
        ]
        if pairs:
            a, b, (one, two) = self.random.choice(pairs)
            first_spot = self.random.choice(
                [s for s in spots[a] if any(t is not s for t in spots[b])]
            )
            second_spot = self.random.choice([t for t in spots[b] if t is not first_spot])
            return [(one, first_spot), (two, second_spot)], ""
        if any(opens(items[:1]) for key, items in first.items() if not spots[key]):
            why = (
                "every item that reaches more alone is kept out of those locations "
                "(non_local_items or an item rule)"
            )
        elif not together:
            why = "no item reaches more, alone or with another"
        elif len(empty) < 2:
            why = (
                "no item reaches more alone, and too few of those locations are free "
                "(not excluded or filled by plando) for two that do together"
            )
        else:
            why = (
                "no item reaches more alone, and every two that do together are kept "
                "out of those locations (non_local_items or an item rule)"
            )
        return [], why

    def by_kind(self, pool: list[Item]) -> dict[tuple[int, str], list[Item]]:
        """The progression items in ``pool`` of this player's, or of an item link it is in, by
        owner and name."""
        groups = self.multiworld.groups
        owners = {self.player} | {
            g for g, group in groups.items() if self.player in group["players"]
        }
        out: dict[tuple[int, str], list[Item]] = {}
        for item in pool:
            if item.player in owners and item.advancement:
                out.setdefault((item.player, item.name), []).append(item)
        return out

    def opens(
        self, state: CollectionState, mine: list[Location], reached: int, items: list[Item]
    ) -> bool:
        """Whether ``items``, collected into a copy of ``state``, reach more of ``mine`` than
        ``reached``."""
        more = state.copy()
        for item in items:
            more.collect(item, True)
        if any(item.player != self.player for item in items):
            more.sweep_for_advancements()  # a link's items reach its players through events
        return sum(1 for loc in mine if loc.can_reach(more)) > reached

    @property
    def disc_hunt(self) -> bool:
        return self.options.goal.value == Goal.option_os_install_disks

    @property
    def double_teleport(self) -> bool:
        return bool(self.menu.setting(custom.DOUBLE_TELEPORT))

    def reaches_the_goal(self, room: dict, second: dict | None = None) -> bool:
        """Whether every item reaches the goal from this room (and its second teleport target,
        as the logic counts it): with the core goal the core's door, passing the core's own
        lists on the way to Zexion (data.CORE); with the OS Install Disks goal the Landing Pod,
        where the player returns home (HOME)."""
        every = self.seed_reach.every_from(room, second)
        if self.disc_hunt:
            return data.CELL_REGION[HOME["cell"]] in every.regions
        door = data.CELL_REGION[data.CORE["door"]]
        have = dict(self.seed_reach.every_item)
        return door in every.regions and all(
            logic.satisfied(entries, have, self.logic_settings, cell)
            for entries, cell in data.CORE["needs"]
        )

    def partners(self, room: dict) -> list[dict]:
        """With Double Teleport, the second teleport targets for this start: the rooms with which
        the pair suits the seed as ``passes`` judges a start (``reach.second_starts``), the
        target counted as the logic counts it. The game draws from all 33 and offers PLAY
        ANYWAY where its winnability check fails, which Archipelago's fill cannot."""
        rooms = reach.second_starts(self.seed_reach, room, data.START_ROOMS)
        if self.reaches_the_goal(room):
            return rooms
        return [other for other in rooms if self.reaches_the_goal(room, other)]

    def passes(self, room: dict) -> bool:
        """Whether a start room suits the seed, as the game's START GAME judges one: its
        starting items collect a location from it, and every item reaches the goal (the core
        and Zexion, or with the OS Install Disks goal the Landing Pod); with Double Teleport,
        the same for the start and a second teleport target (``partners``)."""
        if self.double_teleport:
            return bool(self.partners(room))
        return bool(self.seed_reach.sphere0(room["cell"]).locations) and self.reaches_the_goal(room)

    def choose_starts(self) -> None:
        """The start room by the start_room option, and with Double Teleport the second
        teleport target (docs/re/locations.md, "Which start rooms the world offers"). A named
        room suits the draw (``draw_items``)."""
        who = f"Zexion ({self.player_name})"
        choice = self.options.start_room.value
        start = self.named_room
        if choice == AUTOMATIC:
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
            left_out = len(self.location_cells) - len(every.locations)
            # With the disks, the pool's keys become Scrap Metal and Energy Shields give way too.
            spare = 0
            if self.disc_hunt:
                spare = sum(1 for c in self.pool_codes if c in (*KEY_CODES, custom.ENERGY_SHIELD))
            needed = max(left_out, left_out + disks - spare)
            try:
                codes = custom.compress(self.pool_codes, needed, FILLER_ORDER)
            except custom.PoolError as error:
                what = f"{left_out} locations the items do not reach"
                if disks:
                    what += f" and {disks} OS Install Disks"
                raise OptionError(
                    f"Zexion ({self.player_name}): the pool has too little filler for the "
                    f"{what}: {error} (fewer Item Pool Missiles or Mega Bombs, or Bulk "
                    f"Missiles and Bulk Bombs off, leave filler)"
                ) from error
            if codes == self.pool_codes:
                break
            self.pool_codes = codes
            self.seed_reach = self.reach_of_items()
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
            # The logic counts a flagged room only from a useful-item score of 27 (logic.py,
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
                self.join(by_index, by_index[a], by_index[b], name, [] if way is None else [way])

        for i in sorted(self.kept_locations):
            cell = self.location_cells[i]
            region = by_index[cells[cell]]
            location = ZexionLocation(
                self.player, location_name(i), location_name_to_id[location_name(i)], region
            )
            entries = data.LOCATION_RULES.get(i)
            if entries is not None:
                location.access_rule = rules.requirement(self, entries, cell)
            region.locations.append(location)

        # The core goal: the grab of Zexion behind the four keys, in a region of its own entered
        # from the core's door with the lists on the way in (data.CORE; docs/re/locations.md,
        # "The core goal is Zexion's cell in the core"); the escape and final battle after it
        # are no rule's. The OS Install Disks goal has no event.
        if self.disc_hunt:
            return
        core = Region(CORE_REGION, self.player, self.multiworld)
        self.multiworld.regions.append(core)
        self.join(
            by_index, by_index[cells[data.CORE["door"]]], core, "Into the core", data.CORE["needs"]
        )
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
            # A gate cell the game reads as "collectible": that location's own list, if any.
            entries = data.LOCATION_RULES.get(reach.LOCATION_AT[cell]) if as_location else None
            ways = [] if entries is None else [(entries, cell)]
            for target in targets:
                if target is not source:
                    name = f"Gate from X{cell[1]} Y{cell[0]} to {target.name}"
                    self.join(by_index, source, target, name, ways)

    def join(
        self, by_index: dict[int, Region], source: Region, target: Region, name: str, ways: list
    ) -> None:
        """Connect two regions through all of ``ways``, each a requirement list and the cell it
        is checked at (none: always open), and have Archipelago recheck the entrance when a
        region a list looks into (codes 64 to 67: rules.neighbor_regions) is reached."""
        checks = [rules.requirement(self, *way) for way in ways]
        rule = checks[0] if len(checks) == 1 else None
        if len(checks) > 1:
            rule = lambda state: all(check(state) for check in checks)  # noqa: E731
        entrance = source.connect(target, name, rule)
        for way in ways:
            for r in rules.neighbor_regions(*way) & self.created_regions:
                self.multiworld.register_indirect_condition(by_index[r], entrance)

    def create_items(self) -> None:
        for code, count in self.start_inventory.items():
            for _ in range(count):
                self.multiworld.push_precollected(self.create_item(NAMES[code]))
        codes = list(self.pool_codes)
        # A location left out takes a filler item out of the pool with it (fit_pool made sure
        # there are enough).
        for _ in range(len(self.location_cells) - len(self.kept_locations)):
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
        codes = self.add_traps(codes)
        pool = [self.create_item(NAMES[code]) for code in codes]
        self.multiworld.itempool += pool

    def add_traps(self, codes: list[int]) -> list[int]:
        """The pool with some of its Scrap (items.SCRAP: nothing reads it, unlike an Area Map's
        reveal or hint, or an Energy Shield) as traps: Trap Percentage of the Scrap,
        rounded to the nearest whole trap, each drawn by the two weights. Last, after the
        left-out locations and the disks have taken their filler, so neither runs short; and the
        filler's count stays the same, as traps are filler to Archipelago's fill (excludable):
        excluded locations have as many to take (stage_pre_fill). With no trap to add, the pool
        and the seed's random draws are as before."""
        options = self.options
        weights = [options.disruption_trap_weight.value, options.disorient_trap_weight.value]
        scrap = [i for i, code in enumerate(codes) if code in SCRAP]
        count = (len(scrap) * options.trap_percentage.value + 50) // 100
        if not count or not any(weights):
            return codes
        out = list(codes)
        for i in sorted(self.random.sample(scrap, count)):
            out[i] = self.random.choices(TRAPS, weights)[0]
        return out

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
            # The disks, then home: RETURN HOME WITH THE DISKS? comes after a save at the Landing
            # Pod (HOME), whose region generate_early made sure every item reaches.
            required = self.options.os_install_disks_required.value
            home = self.get_region(rules.region_name(data.CELL_REGION[HOME["cell"]]))
            self.multiworld.completion_condition[self.player] = lambda state: (
                state.has(NAMES[DATA_DISC], self.player, required) and home.can_reach(state)
            )
            return
        self.get_location("Zexion").access_rule = lambda state: state.has_all(KEYS, self.player)
        self.multiworld.completion_condition[self.player] = lambda state: state.has(
            "Victory", self.player
        )

    def generate_basic(self) -> None:
        # Archipelago has just read the plando and places it after this step (Main.py): say why
        # a location one names is not in this seed, or that this player's Area Maps cannot go to
        # another world, and count the items as the world made them, for pre_fill to tell
        # plando's from them and from the item links' (plando.py).
        for warning in plando.left_out(self) + plando.area_maps_away(self):
            logging.warning(warning)
        self.made_items = plando.made_items(self)

    def plando_problems(self) -> list[str]:
        """Why this player's items, after plando, are more than the game holds or a kind it
        does not hold, where the world and the item links did not make them (plando.py)."""
        return plando.problems(
            plando.own_items(self),
            self.made_items + plando.linked_items(self),
            guns_first=self.logic_settings.guns_first,
            disc_hunt=self.disc_hunt,
            area_map_hints=bool(self.options.area_map_hints),
        )

    def pre_fill(self) -> None:
        """Refuse a seed whose plando gave this player items the game does not hold, or more
        than it holds (plando.py)."""
        found = self.plando_problems()
        if found:
            raise OptionError(
                f"Zexion ({self.player_name}): plando_items gives items the game does not hold "
                f"(from_pool: false, or an item the pool holds no more of, makes new copies): "
                f"{'; '.join(found)}"
            )

    @classmethod
    def stage_pre_fill(cls, multiworld: MultiWorld) -> None:
        """Refuse, naming the options, a seed whose fill would stop on AP's "Not enough filler
        items for excluded locations": excluded locations take only filler, and with the OS
        Install Disks goal each disk takes a filler item's place. The fill takes any player's
        filler, so this counts the whole multiworld's, after plando and every world's pre_fill
        but this stage, and refuses for a Zexion player whose excluded locations outnumber
        their own filler. A player's filler in their own ``local_items`` (a Zexion player's
        Area Maps with Area Map Hints off) counts toward their own excluded locations only."""
        unfilled = multiworld.get_unfilled_locations()
        excluded = Counter(
            loc.player for loc in unfilled if loc.progress_type == LocationProgressType.EXCLUDED
        )
        # As Fill.distribute_items_restrictive sorts the pool: filler is neither kind.
        pool = [item for item in multiworld.itempool if not item.advancement and not item.useful]
        filler = Counter(item.player for item in pool)
        local = Counter(item.player for item in pool if kept_local(multiworld, item))
        shared = sum(filler.values()) - sum(local.values())
        if sum(max(0, n - local[player]) for player, n in excluded.items()) <= shared:
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
            held = sum(local.values()) - local[world.player]
            why = (
                f" ({held} of theirs stay in their own worlds by their local_items, such as a "
                f"Zexion player's Area Maps with Area Map Hints off)"
                if held
                else ""
            )
            raise OptionError(
                f"Zexion ({world.player_name}): {n} excluded {locations}, but only {have} filler "
                f"items (Scrap, traps, Area Maps or Intel Chips) to put there, and the other "
                f"players' filler does not make up the difference{why}: {fix}"
            )

    def write_spoiler_header(self, spoiler_handle: TextIO) -> None:
        spoiler_handle.write(f"Mode:                            {self.mode_name}\n")
        if self.mode_name == "custom":
            amounts = ", ".join(str(n) for n in self.menu.amounts)
            spoiler_handle.write(f"Amounts:                         {amounts}\n")
        if self.gates_unlocked:
            gates = "Unlocked (Vanilla takes the four keys, which this goal does not place)"
            spoiler_handle.write(f"Gates:                           {gates}\n")
        # Drawn per seed in every mode (Standard's starting gun among them).
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
            # By location index (the 155th, with Secret Shuffle, last): the client puts the
            # game's list in its own order (client/save_patch.py, ``game_order``).
            "locations": [list(c) for c in self.location_cells],
            "location_items": self.location_items(),
            "start_items": {str(k): v for k, v in sorted(self.start_items().items())},
            "amounts": list(self.menu.amounts),
            "options": {str(t): v for t, v in self.menu.checked().items()},
            "menu": self.menu_setup(),
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
            # This world's own traps in its own locations, [location index, item code] each:
            # the game holds a stand-in there, and the client sets the trap off once it is found
            # (client/ticks.py, followups_tick). Older seeds lack it.
            "trap_locations": self.trap_locations(),
            # The upgrade each Zexion trap in this world's locations is shown as (client/
            # aplabels.py, trap_label). Older seeds lack it.
            "trap_looks": self.trap_looks(),
        }

    def disc_locations(self) -> list[int]:
        """The location indices holding this world's own OS Install Disks."""
        found = self.multiworld.find_item_locations(NAMES[DATA_DISC], self.player)
        return sorted(loc.address - LOCATION_ID_BASE for loc in found if loc.player == self.player)

    def trap_locations(self) -> list[list[int]]:
        """The location index and item code of each of this world's own traps in its own
        locations."""
        return sorted(
            [loc.address - LOCATION_ID_BASE, code]
            for code in TRAPS
            for loc in self.multiworld.find_item_locations(NAMES[code], self.player)
            if loc.player == self.player
        )

    def trap_looks(self) -> list[list[int]]:
        """The location index and the upgrade shown for each Zexion trap (this player's own,
        another Zexion player's or an item link's) in this world's locations: drawn from
        DISGUISES less what this player starts with, so the disguise is no upgrade the player
        already holds, by random numbers of its own, so the seed's other draws stay as they are
        (with no trap, no draw)."""
        started = {
            code_of[item.name]
            for item in self.multiworld.precollected_items[self.player]
            if item.code is not None
        }
        choices = [code for code in DISGUISES if code not in started] or list(DISGUISES)
        draw = random.Random(f"{self.multiworld.seed}:{self.player}:trap looks")
        out = []
        for i in sorted(self.kept_locations):
            item = self.get_location(location_name(i)).item
            if item is not None and item.game == GAME and code_of.get(item.name) in TRAPS:
                out.append([i, draw.choice(choices)])
        return out

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
        """The randomizer menu for the client's ARCHIPELAGO choice (client/newgame.py): the
        seed's own menu state (a preset's, or Custom's), with Starting Location the world's
        start room so the pause menu's view of the settings names it, and a seed text of 16 hex
        digits from this seed and player. With Double Teleport, whose placement reads the menu's
        start table, the table in place of a second target."""
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
        FOREIGN_PROGRESSION for this world's own trap too (trap_locations), and Scrap Metal at a
        left-out location."""
        out = []
        for i in range(len(self.location_cells)):
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
            if code in TRAPS:
                out.append(FOREIGN_PROGRESSION)  # important, as the client disguises it
                continue
            code = DRAWN_AS.get(code, code)  # an Intel Chip: the Area Map
            # Scrap Energy and Chunk, and an item that is not the game's, are handed out as Scrap
            # Metal. (The only such item, Assist Unlock, is kept out of these locations: set_rules.)
            own_scrap = code in (SCRAP_ENERGY, SCRAP_CHUNK) or code not in GAME_ITEMS
            out.append(SCRAP_METAL if own_scrap else code)
        return out

    def start_items(self) -> dict[int, int]:
        """Inventory bytes for everything this player starts with: the mode's starting items
        and any start_inventory or start_inventory_from_pool (Archipelago's precollected items),
        with Guns First a progressive weapon's second copy as its upgrade."""
        counts = Counter(
            code_of[item.name]
            for item in self.multiworld.precollected_items[self.player]
            if item.code is not None
            and code_of[item.name] in GAME_ITEMS
            and code_of[item.name] != DATA_DISC
        )
        return as_inventory(counts.items(), self.logic_settings.guns_first)
