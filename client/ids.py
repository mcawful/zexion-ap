"""The numbers the client's modules share: the item and location codes the world uses
(worlds/zexion/items.py and locations.py, which must agree), and the game's own sizes.

Item codes are the game's item IDs (``ItemNames.txt``), except ASSIST_UNLOCK and INTEL_CHIP,
which are the world's own. No Archipelago imports.
"""

from __future__ import annotations

# Archipelago's ids: the world's item code + ITEM_ID_BASE, its location index + LOCATION_ID_BASE.
ITEM_ID_BASE = 3392510_000  # Steam app ID x 1000 (worlds/zexion/items.py)
LOCATION_ID_BASE = ITEM_ID_BASE + 500  # worlds/zexion/locations.py

GAME_ITEMS = range(0, 51)  # the codes that are the game's own inventory bytes
# Byte 50 is never written: an Energy Shield's pickup counts as Scrap Metal (docs/re/memory.md,
# "Other players' items").
WRITABLE_ITEMS = range(0, 50)

ENERGY_TANK, MISSILE_CONTAINER, MEGA_BOMB = 0, 1, 2
# Progressive weapon -> its upgrade (worlds/zexion/items.py).
PROGRESSIVE = {3: 9, 4: 14, 5: 15, 6: 10, 7: 12}
KEYS = (27, 28, 29, 30)  # the Alpha, Beta, Gamma and Omega Keys
ZEXION = 31  # the item the core holds: the game's victory, never given
MISSILE_BOX, BOMB_BOX = 36, 37
DATA_DISC = 44  # the game's Data Disc: the OS Install Disk of the OS Install Disks goal
SCRAP_METAL = 46
# The stand-ins the world puts in this world's locations for other players' items
# (client/aplabels.py): Scrap Energy, and Scrap Chunk for progression items (not another Zexion
# player's supplies, which stay minor) and own disks.
STAND_IN, STAND_IN_PROGRESSION = 47, 48
AREA_MAP = 49
ENERGY_SHIELD = 50

# Not items of the game's: the escape hatch from the assist lock, and the Area Map as a hint
# (the Area Map Hints option), which the game holds and draws as the Area Map.
ASSIST_UNLOCK = 100
INTEL_CHIP = 101
DRAWN_AS = {INTEL_CHIP: AREA_MAP}

LOCATIONS = 154  # the randomizer's locations (RandoCells; docs/re/randocells.md)
# With Secret Shuffle (setting t37) the game's list holds a 155th, at list position 134 in grid
# order (docs/re/randocells.md, "The pool"); the world gives it location index 154.
SECRET_SHUFFLE = 37
MAX_LOCATIONS = LOCATIONS + 1
MAP_SIZE = 54  # the map grid's cells along each side
COLLECTED = 0x1B  # a map cell's value once its item is collected (docs/re/memory.md)
SECTION_SIZE = 0x908  # the randomizer section, as loaded and in each slot's copy
