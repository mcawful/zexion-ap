"""Zexion's 154 locations, in the order of the game's own location list.

Names give the map cell in the order the game itself displays coordinates, which is the reverse
of this repo's (x, y) (docs/re/randocells.md, "Grid order"), so a player can find them on the
in-game map.
"""

from __future__ import annotations

from BaseClasses import Location

from .data import LOCATIONS
from .items import GAME

LOCATION_ID_BASE = 3392510_000 + 500


def location_name(index: int) -> str:
    x, y = LOCATIONS[index]
    return f"Map {y},{x}"


location_name_to_id = {location_name(i): LOCATION_ID_BASE + i for i in range(len(LOCATIONS))}


class ZexionLocation(Location):
    game = GAME
