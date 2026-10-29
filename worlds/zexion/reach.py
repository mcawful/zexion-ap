"""What a seed's settings and items reach, worked out when the seed is generated.

The world keeps the locations the game's logic reaches with every item from the seed's start
(and its second teleport target), creates the regions that reach takes, and judges start rooms
by what they reach (worlds/zexion/world.py, ``passes``; docs/re/locations.md, "Which start rooms
the world offers"). All of it depends on the seed's settings (``logic.Settings``: the Energy
total, Safety, code 68's settings and Gates Open), so it is worked out here, over the region
graph ``data.py`` holds, rather than read from per-mode lists. ``data.MODES`` still lists each
mode's results, computed cell by cell by ``tools/gen_world_data.py``; the world's tests compare
these with them.

The flood is the game's (``tools/randocells.py``, ``reachable_cells``) on regions instead of
cells: a region is cells joined by passages that are always open, so reaching one cell of it
reaches all. A way between regions passes when its requirement list does, checked where it
leaves from; a list with codes 64 to 67 looks at whether a neighboring cell is reached, so the
flood repeats until nothing new is reached. Under Gates Open, once a gate cell is reached the
flood goes on from the gate cells too, as the game's logic does (``gates_triggered``); a second
teleport target counts as the logic counts it (``second_counts``). The two agree over the
graph's cells, those every item reaches in some mode, or with X Crystals, X Blocks or Secret
Shuffle on; a passage to a cell outside it is not followed, so where Abridged Finale leads into
the core (docs/re/locations.md, "The region graph holds what the settings reach"), the result is
less than the game's, never more. Results are cached by settings, inventory and starts, so a
multiworld of many Zexion players with one mode works each out once.

Pure Python with no Archipelago imports.
"""

from __future__ import annotations

from collections.abc import Callable, Iterable, Mapping
from dataclasses import dataclass
from functools import cache

from . import data, logic

Cell = tuple[int, int]
Inventory = tuple[tuple[int, int], ...]  # (item code, count), sorted, no zero counts
Way = tuple[tuple[logic.Entry, ...], Cell | None]

# The earlier rule for a start room, still the tests' check on data.MODES (offered_starts): every
# item reaches all the game's locations from it, and the starting items at least this many
# (data.MIN_SPHERE_ZERO is the generator's, the same).
MIN_SPHERE_ZERO = 3


@dataclass(frozen=True)
class Reach:
    """The regions reached, and the indices of the locations whose items can be collected."""

    regions: frozenset[int]
    locations: frozenset[int]


def inventory(counts: Mapping[int, int]) -> Inventory:
    return tuple(sorted((code, n) for code, n in counts.items() if n))


def collected(start_inventory: Mapping[int, int], guns_first: bool = True) -> dict[int, int]:
    """Starting items by item code, counted as the game counts them (``logic.collect``): with
    Guns First, a progressive weapon held twice as the weapon and its upgrade."""
    have: dict[int, int] = {}
    for code, n in sorted(start_inventory.items()):
        for _ in range(n):
            logic.collect(have, code, guns_first)
    return have


def every_item(
    start_inventory: Mapping[int, int], pool: Iterable[int], guns_first: bool = True
) -> Inventory:
    """The starting items and every item of the pool, counted as the game's generator counts
    them (with Guns First, a weapon picked up twice as its upgrade: ``logic.collect``)."""
    have = collected(start_inventory, guns_first)
    for item in pool:
        logic.collect(have, item, guns_first)
    return inventory(have)


def _neighbor_codes(entries: tuple[logic.Entry, ...]) -> bool:
    return any(64 <= code <= 67 for code, _ in entries)


@cache
def _ways(settings: logic.Settings, have: Inventory) -> tuple[dict, list]:
    """The region graph's ways for one inventory: the regions each region opens onto whatever
    else is reached, and the ways whose lists look at a neighboring cell (codes 64 to 67),
    which the flood checks as it goes."""
    counts = dict(have)
    fixed: dict[int, set[int]] = {}
    looking: list[tuple[int, int, Way]] = []
    for (a, b), ways in data.ENTRANCES.items():
        for way in ways:
            if way is None:
                fixed.setdefault(a, set()).add(b)
                continue
            entries, cell = way
            if _neighbor_codes(entries):
                looking.append((a, b, way))
            elif logic.satisfied(entries, counts, settings, cell):
                fixed.setdefault(a, set()).add(b)
    return fixed, looking


# Every location by location index: the game's 154, then Secret Shuffle's (index 154).
ALL_LOCATIONS: tuple[Cell, ...] = (*data.LOCATIONS, data.SECRET_LOCATION)
LOCATION_AT = {cell: i for i, cell in enumerate(ALL_LOCATIONS)}
SECRET_SHUFFLE = 37  # the setting (t37) that makes the 155th a location


def seed_locations(settings: logic.Settings) -> tuple[Cell, ...]:
    """The seed's locations by location index: the game's 154, and with Secret Shuffle the
    155th as index 154. The game's own list holds it at position 134, in grid order
    (docs/re/randocells.md, "The pool"); the client works in location indices and puts the
    game's list in its order itself (client/save_patch.py, ``game_order``)."""
    return ALL_LOCATIONS if settings.options[SECRET_SHUFFLE] else ALL_LOCATIONS[:-1]


def collectible(
    i: int, counts: Mapping[int, int], settings: logic.Settings, at: Callable[[Cell], bool]
) -> bool:
    """Whether location ``i``'s item can be collected once its cell is reached: its own list,
    if it has one."""
    rule = data.LOCATION_RULES.get(i)
    return rule is None or logic.satisfied(rule, counts, settings, ALL_LOCATIONS[i], at)


# R'Talva's gate: the game's logic lets reaching it open the gate travel (logic.GATE_TRIGGERS),
# but its script does nothing until its visited flag 307 is set, which outside Unlocked waits
# for flag 155 (by the menu's blurb, Aesos beaten; docs/re/randocells.md, "What each setting
# does in the game"). The world leaves it out, stricter than the game's logic (decided, the
# owner, 2026-10-28): in game the fourth gate appears only after a boss (2026-10-28), so a seed
# that counted on it early could not be beaten.
RTALVA_GATE = (51, 34)
GATE_TRIGGERS = tuple(t for t in logic.GATE_TRIGGERS if t[0] != RTALVA_GATE)


def gates_triggered(
    counts: Mapping[int, int], settings: logic.Settings, at: Callable[[Cell], bool]
) -> bool:
    """Under Gates Open, whether a gate cell counts as reached, as the game's logic reads it,
    but for R'Talva's (GATE_TRIGGERS): then the flood goes on from logic.GATE_TARGETS too."""
    return settings.gates_open and any(
        at(cell) and (not as_location or collectible(LOCATION_AT[cell], counts, settings, at))
        for cell, as_location in GATE_TRIGGERS
    )


@cache
def reach(settings: logic.Settings, have: Inventory, starts: tuple[Cell, ...]) -> Reach:
    """What ``have`` reaches from the start cells ``starts`` (one, or a start and its second
    teleport target) under ``settings``; under Gates Open, from the gate cells too once one is
    reached (``gates_triggered``)."""
    fixed, looking = _ways(settings, have)
    counts = dict(have)
    reached = {data.CELL_REGION[cell] for cell in starts}
    gates = [data.CELL_REGION[cell] for cell in logic.GATE_TARGETS]

    def at(cell: Cell) -> bool:
        return data.CELL_REGION.get(cell) in reached

    while True:
        todo = list(reached)
        while todo:
            for b in fixed.get(todo.pop(), ()):
                if b not in reached:
                    reached.add(b)
                    todo.append(b)
        opened = [
            b
            for a, b, (entries, cell) in looking
            if a in reached
            and b not in reached
            and logic.satisfied(entries, counts, settings, cell, at)
        ]
        if gates_triggered(counts, settings, at):
            opened += [b for b in gates if b not in reached]
        if not opened:
            break
        reached.update(opened)
    locations = frozenset(
        i
        for i, cell in enumerate(seed_locations(settings))
        if at(cell) and collectible(i, counts, settings, at)
    )
    return Reach(frozenset(reached), locations)


@dataclass(frozen=True)
class Seed:
    """A seed's settings, its starting items and every item it has."""

    settings: logic.Settings
    start_items: Inventory
    every_item: Inventory

    @classmethod
    def of(
        cls, settings: logic.Settings, start_inventory: Mapping[int, int], pool: Iterable[int]
    ) -> Seed:
        guns_first = settings.guns_first
        start = inventory(collected(start_inventory, guns_first))
        return cls(settings, start, every_item(start_inventory, pool, guns_first))

    def every(self, *starts: Cell) -> Reach:
        """What every item reaches from these starts."""
        return reach(self.settings, self.every_item, starts)

    def sphere0(self, *starts: Cell) -> Reach:
        """What the starting items alone reach from these starts."""
        return reach(self.settings, self.start_items, starts)

    def every_from(self, start: dict, second: dict | None = None) -> Reach:
        """What every item reaches from a start room and its second teleport target, the
        target counted as the logic counts it (``second_counts``)."""
        return self.every(*_cells(self.every_item, start, second))

    def sphere0_from(self, start: dict, second: dict | None = None) -> Reach:
        """What the starting items alone reach from a start room and its second teleport
        target, the target counted as the logic counts it (``second_counts``)."""
        return self.sphere0(*_cells(self.start_items, start, second))


def flagged(room: dict) -> bool:
    """A room the menu's start table flags (it holds object 25): Random Start leaves it out
    below a starting score of 27, and as a second teleport target the logic waits for the
    useful-item score (docs/re/randocells.md, "Starting Location and the second teleport
    target")."""
    return bool(data.START_TABLE[room["index"]][2])


def second_counts(have: Inventory, room: dict) -> bool:
    """Whether the logic floods from second teleport target ``room`` with ``have``: an
    unflagged room at once, a flagged one once the useful-item score reaches
    logic.SECOND_TARGET_SCORE (0xE6D20, 0xE8850)."""
    return not flagged(room) or logic.useful_score(dict(have)) >= logic.SECOND_TARGET_SCORE


def _cells(have: Inventory, start: dict, second: dict | None) -> tuple[Cell, ...]:
    if second is None or not second_counts(have, second):
        return (start["cell"],)
    return (start["cell"], second["cell"])


def offered_starts(seed: Seed, double_teleport: bool) -> list[dict]:
    """The start rooms (``data.START_ROOMS`` entries) the world offered before it followed the
    game's rule, which ``tools/gen_world_data.py`` still computes into ``data.MODES``, so the
    tests check this module against it: those from which every
    item reaches all the game's locations and the starting items at least MIN_SPHERE_ZERO; with
    a second teleport target, every room (the pair is the world's choice: ``second_starts``)."""
    if double_teleport:
        return list(data.START_ROOMS)
    return [
        room
        for room in data.START_ROOMS
        if len(seed.every(room["cell"]).locations) == len(seed_locations(seed.settings))
        and len(seed.sphere0(room["cell"]).locations) >= MIN_SPHERE_ZERO
    ]


def second_starts(seed: Seed, start: dict, rooms: Iterable[dict]) -> list[dict]:
    """The rooms that can be the second teleport target for ``start``. The game draws from
    every room but the start, flagged ones too. The world takes a flagged room only when the
    logic counts it from the start (``second_counts`` with the starting items: a useful-item
    score of 27), since one the logic counts only later adds nothing to sphere 0, and
    Archipelago's fill then failed 28 times in 360 OHKO generations, against none in 680 with
    an unflagged room (decided, the owner, 2026-10-28; docs/re/locations.md, "OHKO: two starts,
    and only the locations they reach"). Of those, it keeps the rooms with which the starting
    items collect at least one location (``Seed.sphere0_from``), the game's one-location rule
    for a start."""
    alone = bool(seed.sphere0(start["cell"]).locations)
    return [
        room
        for room in rooms
        if room["cell"] != start["cell"]
        and second_counts(seed.start_items, room)
        and (alone or bool(seed.sphere0_from(start, room).locations))
    ]
