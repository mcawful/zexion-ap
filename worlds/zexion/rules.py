"""Access rules: the game's own requirement lists, checked with worlds/zexion/logic.py."""

from __future__ import annotations

from collections.abc import Callable
from typing import TYPE_CHECKING

from BaseClasses import CollectionState

from . import data, logic
from .items import as_inventory, code_of
from .reach import LOCATION_AT

if TYPE_CHECKING:
    from .world import ZexionWorld


def inventory(state: CollectionState, player: int) -> dict[int, int]:
    """Item counts by the game's item IDs, as the logic reads them (``items.as_inventory``).
    Only progression items are in ``prog_items``, which is all the logic reads."""
    counts = state.prog_items[player].items()
    return as_inventory((code_of[name], n) for name, n in counts if name in code_of and n > 0)


def region_name(region: int) -> str:
    return f"Region {region}"


def requirement(
    world: ZexionWorld, entries: tuple, cell: tuple[int, int] | None
) -> Callable[[CollectionState], bool]:
    """A rule for one requirement list; ``cell`` is where it is checked (codes 64-69)."""
    player = world.player
    created = world.created_regions
    settings = world.logic_settings

    def rule(state: CollectionState) -> bool:
        def reached(other: tuple[int, int]) -> bool:
            region = data.CELL_REGION.get(other)
            return region in created and state.can_reach_region(region_name(region), player)

        return logic.satisfied(entries, inventory(state, player), settings, cell, reached)

    return rule


def neighbor_regions(entries: tuple, cell: tuple[int, int] | None) -> set[int]:
    """Regions a rule looks into through codes 64-67."""
    if cell is None:
        return set()
    out = set()
    for code, _ in entries:
        if 64 <= code <= 67:
            other = logic.neighbor(code, cell)
            if other in data.CELL_REGION:
                out.add(data.CELL_REGION[other])
    return out


def second_target(world: ZexionWorld) -> Callable[[CollectionState], bool]:
    """The rule on a flagged second teleport target: the logic counts it once the useful-item
    score reaches logic.SECOND_TARGET_SCORE (reach.second_counts)."""
    player = world.player

    def rule(state: CollectionState) -> bool:
        return logic.useful_score(inventory(state, player)) >= logic.SECOND_TARGET_SCORE

    return rule


def gate_trigger(
    world: ZexionWorld, cell: tuple[int, int]
) -> Callable[[CollectionState], bool] | None:
    """Gates Open's travel from a gate cell the game reads as "collectible": that location's
    own list, if it has one (reach.gates_triggered)."""
    entries = data.LOCATION_RULES.get(LOCATION_AT[cell])
    return None if entries is None else requirement(world, entries, cell)
