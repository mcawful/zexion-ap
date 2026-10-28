"""What a seed's settings and items reach, worked out when the seed is generated.

The world keeps the locations the game's logic reaches with every item from the seed's start
(and its second teleport target), creates the regions that reach takes, and offers the start
rooms the rules below allow (docs/re/locations.md, "Which start rooms the world offers"). All
of it depends on the seed's settings (``logic.Settings``: the Energy total, Safety and code
68's settings), so it is worked out here, over the region graph ``data.py`` holds, rather than
read from per-mode lists. ``data.MODES`` still lists each mode's results, computed cell by cell
by ``tools/gen_world_data.py``; the world's tests compare these with them.

The flood is the game's (``tools/randocells.py``, ``reachable_cells``) on regions instead of
cells: a region is cells joined by passages that are always open, so reaching one cell of it
reaches all. A way between regions passes when its requirement list does, checked where it
leaves from; a list with codes 64 to 67 looks at whether a neighboring cell is reached, so the
flood repeats until nothing new is reached. The two agree over the graph's cells, those every
item reaches in some mode; a passage to a cell outside it is not followed, so where X Blocks,
Secret Shuffle or Abridged Finale lead outside (docs/re/locations.md, "The region graph holds
what the settings reach"), the result is less than the game's, never more. Results are cached by
settings, inventory and starts, so a multiworld of many Zexion players with one mode works each
out once.

Pure Python with no Archipelago imports.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping
from dataclasses import dataclass
from functools import cache

from . import data, logic

Cell = tuple[int, int]
Inventory = tuple[tuple[int, int], ...]  # (item code, count), sorted, no zero counts
Way = tuple[tuple[logic.Entry, ...], Cell | None]

# A start room is offered when every item reaches all the game's locations from it, and the
# starting items at least this many; with a second teleport target, the two together reach
# this many with the starting items (data.MIN_SPHERE_ZERO is the generator's, the same).
MIN_SPHERE_ZERO = 3


@dataclass(frozen=True)
class Reach:
    """The regions reached, and the indices of the locations whose items can be collected."""

    regions: frozenset[int]
    locations: frozenset[int]


def inventory(counts: Mapping[int, int]) -> Inventory:
    return tuple(sorted((code, n) for code, n in counts.items() if n))


def every_item(start_inventory: Mapping[int, int], pool: Iterable[int]) -> Inventory:
    """The starting items and every item of the pool, counted as the game's generator counts
    them (a weapon picked up twice as its upgrade: ``logic.collect``)."""
    have = dict(start_inventory)
    for item in pool:
        logic.collect(have, item)
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


@cache
def reach(settings: logic.Settings, have: Inventory, starts: tuple[Cell, ...]) -> Reach:
    """What ``have`` reaches from the start cells ``starts`` (one, or a start and its second
    teleport target) under ``settings``."""
    fixed, looking = _ways(settings, have)
    counts = dict(have)
    reached = {data.CELL_REGION[cell] for cell in starts}

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
        if not opened:
            break
        reached.update(opened)
    locations = frozenset(
        i
        for i, cell in enumerate(data.LOCATIONS)
        if at(cell)
        and (
            i not in data.LOCATION_RULES
            or logic.satisfied(data.LOCATION_RULES[i], counts, settings, cell, at)
        )
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
        return cls(settings, inventory(start_inventory), every_item(start_inventory, pool))

    def every(self, *starts: Cell) -> Reach:
        """What every item reaches from these starts."""
        return reach(self.settings, self.every_item, starts)

    def sphere0(self, *starts: Cell) -> Reach:
        """What the starting items alone reach from these starts."""
        return reach(self.settings, self.start_items, starts)


def offered_starts(seed: Seed, double_teleport: bool) -> list[dict]:
    """The start rooms (``data.START_ROOMS`` entries) the world offers: those from which every
    item reaches all the game's locations and the starting items at least MIN_SPHERE_ZERO; with
    a second teleport target, every room, the pair being chosen by what the two reach together
    (``second_starts``)."""
    if double_teleport:
        return list(data.START_ROOMS)
    return [
        room
        for room in data.START_ROOMS
        if len(seed.every(room["cell"]).locations) == len(data.LOCATIONS)
        and len(seed.sphere0(room["cell"]).locations) >= MIN_SPHERE_ZERO
    ]


def second_starts(seed: Seed, start: Cell, rooms: Iterable[dict]) -> list[dict]:
    """The rooms that can be the second teleport target for ``start``: those that, together with
    it, let the starting items reach at least MIN_SPHERE_ZERO locations. Each room's own reach is
    combined, which can only undercount what the two reach at once (docs/re/locations.md)."""
    first = seed.sphere0(start).locations
    return [
        room
        for room in rooms
        if room["cell"] != start
        and len(first | seed.sphere0(room["cell"]).locations) >= MIN_SPHERE_ZERO
    ]
