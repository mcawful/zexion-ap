"""What a seed's settings and items reach, worked out when the seed is generated: the locations
the world keeps, the regions it creates and what its start rooms are judged by (worlds/zexion/
world.py). The flood is the game's (``tools/randocells.py``, ``reachable_cells``) on the region
graph ``data.py`` holds instead of on cells, under the seed's ``logic.Settings``; how it works,
and where it stops short of the game's, never past it: docs/re/locations.md, "The world works
out what a seed reaches when it is generated" and "The region graph holds what the settings
reach". ``data.MODES`` keeps each mode's results computed cell by cell, for the tests. Results
are cached by settings, inventory and starts, so many Zexion players with one mode work each
out once.

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
            if any(64 <= code <= 67 for code, _ in entries):
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
# but in game the gate appears only after a boss, so the world leaves it out (a guard, decided
# 2026-10-28: docs/re/locations.md, "Where the world departs from the game").
RTALVA_GATE = (51, 34)
GATE_TRIGGERS = tuple(t for t in logic.GATE_TRIGGERS if t[0] != RTALVA_GATE)


@cache
def reach(settings: logic.Settings, have: Inventory, starts: tuple[Cell, ...]) -> Reach:
    """What ``have`` reaches from the start cells ``starts`` (one, or a start and its second
    teleport target) under ``settings``; under Gates Open, from the gate cells too once one is
    reached (``logic.gates_triggered``, without R'Talva's gate: GATE_TRIGGERS)."""
    fixed, looking = _ways(settings, have)
    counts = dict(have)
    reached = {data.CELL_REGION[cell] for cell in starts}
    gates = [data.CELL_REGION[cell] for cell in logic.GATE_TARGETS]

    def at(cell: Cell) -> bool:
        return data.CELL_REGION.get(cell) in reached

    def gate(cell: Cell) -> bool:
        return collectible(LOCATION_AT[cell], counts, settings, at)

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
        if logic.gates_triggered(settings, at, gate, GATE_TRIGGERS):
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
        # Every item: the starting items, then the pool, as the game's generator counts them
        # (with Guns First, a weapon picked up twice as its upgrade: ``logic.collect``).
        guns_first = settings.guns_first
        have = collected(start_inventory, guns_first)
        start = inventory(have)
        for item in pool:
            logic.collect(have, item, guns_first)
        return cls(settings, start, inventory(have))

    def every(self, *starts: Cell) -> Reach:
        """What every item reaches from these starts."""
        return reach(self.settings, self.every_item, starts)

    def sphere0(self, *starts: Cell) -> Reach:
        """What the starting items alone reach from these starts."""
        return reach(self.settings, self.start_items, starts)

    def every_from(self, start: dict, second: dict | None = None) -> Reach:
        """What every item reaches from a start room and its second teleport target, the
        target counted as the logic counts it (``logic.second_counts``)."""
        return self.every(*_cells(self.every_item, start, second))

    def sphere0_from(self, start: dict, second: dict | None = None) -> Reach:
        """What the starting items alone reach from a start room and its second teleport
        target, the target counted as the logic counts it (``logic.second_counts``)."""
        return self.sphere0(*_cells(self.start_items, start, second))


def flagged(room: dict) -> bool:
    """A room the menu's start table flags (it holds object 25): Random Start leaves it out
    below a starting score of 27, and as a second teleport target the logic waits for the
    useful-item score (docs/re/randocells.md, "Starting Location and the second teleport
    target")."""
    return bool(data.START_TABLE[room["index"]][2])


def _cells(have: Inventory, start: dict, second: dict | None) -> tuple[Cell, ...]:
    if second is None or not logic.second_counts(dict(have), flagged(second)):
        return (start["cell"],)
    return (start["cell"], second["cell"])


def second_starts(seed: Seed, start: dict, rooms: Iterable[dict]) -> list[dict]:
    """The rooms that can be the second teleport target for ``start``: every room but the start,
    as the game draws, but a flagged room only when the logic counts it from the start
    (``logic.second_counts`` with the starting items; a guard, decided 2026-10-28:
    docs/re/locations.md, "OHKO: two starts, and only the locations they reach"), and of those
    the rooms with which the starting items collect a location (``Seed.sphere0_from``), the
    game's one-location rule for a start."""
    alone = bool(seed.sphere0(start["cell"]).locations)
    return [
        room
        for room in rooms
        if room["cell"] != start["cell"]
        and logic.second_counts(dict(seed.start_items), flagged(room))
        and (alone or bool(seed.sphere0_from(start, room).locations))
    ]
