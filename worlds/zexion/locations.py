"""Zexion's 154 locations, in the order of the game's own location list.

Names give the area the pause screen names (REGION:) and the map cell as the game displays it,
X then Y (a cell (a, b) here is X = b, Y = a: docs/re/randocells.md, "Grid order"), as in
"Guardia 22,7", so a player can find them on the in-game map.
"""

from __future__ import annotations

from BaseClasses import Location

from .data import CELL_AREAS, LOCATIONS
from .items import GAME, ITEM_ID_BASE

LOCATION_ID_BASE = ITEM_ID_BASE + 500


def cell_name(cell) -> str:
    """A cell (a, b) as players read it: its area, then X,Y as the game displays them."""
    a, b = cell
    return f"{CELL_AREAS[(a, b)]} {b},{a}"


def location_name(index: int) -> str:
    return cell_name(LOCATIONS[index])


location_name_to_id = {location_name(i): LOCATION_ID_BASE + i for i in range(len(LOCATIONS))}


def area_groups() -> dict[str, set[str]]:
    """One group of locations per area, as the pause screen names it (REGION:)."""
    groups: dict[str, set[str]] = {}
    for i, cell in enumerate(LOCATIONS):
        groups.setdefault(CELL_AREAS[cell], set()).add(location_name(i))
    return groups


location_name_groups = area_groups()


class ZexionLocation(Location):
    game = GAME
