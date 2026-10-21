"""Access rules: the game's own requirement lists, checked with worlds/zexion/logic.py."""

from __future__ import annotations

from collections.abc import Callable
from typing import TYPE_CHECKING

from BaseClasses import CollectionState

from . import data, logic
from .items import NAMES, PROGRESSIVE

if TYPE_CHECKING:
    from .world import ZexionWorld

_CODE_OF = {name: code for code, name in NAMES.items()}


def inventory(state: CollectionState, player: int) -> dict[int, int]:
    """Item counts by the game's item IDs, as the logic reads them.

    A progressive weapon counts as its weapon, and a second copy as its upgrade too, the way
    the game counts a weapon picked up twice. Only progression items are in ``prog_items``,
    which is all the logic reads.
    """
    have: dict[int, int] = {}
    for name, count in state.prog_items[player].items():
        code = _CODE_OF.get(name)
        if code is None or count <= 0:
            continue
        if code in PROGRESSIVE:
            have[code] = 1
            if count >= 2:
                have[PROGRESSIVE[code]] = 1
        else:
            have[code] = count
    return have


def region_name(region: int) -> str:
    return f"Region {region}"


def cell_region() -> dict[tuple[int, int], int]:
    return {tuple(c): r for r, cells in data.REGION_CELLS.items() for c in cells}


def requirement(
    world: ZexionWorld,
    entries: tuple,
    cell: tuple[int, int] | None,
    cells: dict[tuple[int, int], int],
) -> Callable[[CollectionState], bool]:
    """A rule for one requirement list; ``cell`` is where it is checked (codes 64-69)."""
    player = world.player
    created = world.created_regions
    settings = world.settings

    def rule(state: CollectionState) -> bool:
        def reached(other: tuple[int, int]) -> bool:
            region = cells.get(other)
            return region in created and state.can_reach_region(region_name(region), player)

        return logic.satisfied(entries, inventory(state, player), settings, cell, reached)

    return rule


def neighbor_regions(
    entries: tuple, cell: tuple[int, int] | None, cells: dict[tuple[int, int], int]
) -> set[int]:
    """Regions a rule looks into through codes 64-67."""
    if cell is None:
        return set()
    out = set()
    for code, _ in entries:
        if 64 <= code <= 67:
            other = logic.neighbor(code, cell)
            if other in cells:
                out.add(cells[other])
    return out
