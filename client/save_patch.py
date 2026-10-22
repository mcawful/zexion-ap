"""Write an Archipelago seed into a fresh Zexion randomizer save.

The player starts a new randomizer game in the mode the seed uses and saves at once; this
module checks that save and rewrites it to hold the seed: the item at each location, the start
room (and the second teleport target in OHKO), the continue point, the revealed map cell (if the
game revealed one), the four keys' map markers, and the starting items with their current
energy and missiles. Everything else stays as the game wrote it. Offsets and their evidence
are in docs/re/save-format.md; the item table, the start (record, continue point and revealed
cell, changed together) and the second teleport target were each loaded in game, and a whole
Standard save patched by this module loaded with its seed, start and starting items
(docs/re/memory.md, "The client in a live game"). Its current energy and missiles were the
fresh save's own values, so writing different ones has not been loaded, nor have OHKO and Easy
saves patched this way, nor the key markers.

Pure functions over bytes: the client reads and writes the file (after a backup), and the
tests run here without the game.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

LOCATIONS = 154
MAP_W = 54
SAVE_SIZE = 78108  # a randomizer save
RANDO_MARKER = 0x9402  # 16-bit: 1 in a randomizer save, 0 in a normal one or a cleared slot

# File offsets (docs/re/save-format.md).
INVENTORY = 0x92F3  # + item ID, items 0-49
MAP_GRID = 0x877F  # 54 x 54, row-major
# The four keys' map markers: flags 40 to 47, a (y, x) cell per key (Alpha, Beta, Gamma,
# Omega), 0xFF for none; the seed's own key cells in a fresh save (docs/re/save-format.md).
KEY_MARKERS = 0x2A
KEYS = (27, 28, 29, 30)
NO_MARKER = 0xFF
PICKUP_COUNTER = 0x935A  # 16-bit
ENERGY_NOW = 0x9350  # 16-bit; a fresh save holds Energy Tanks x 16
MISSILES_NOW = 0x9352  # 16-bit; a fresh save holds containers + 10 x Missile Boxes
CONTINUE = 0x935C  # tile row, tile column (16-bit each)
SECTION = 0x12814  # the randomizer section, as loaded to +0xCDD6D0
ITEM_TABLE = 0x12A80  # item ID per location
START_RECORD = 0x12B24  # candidate index, tile column, tile row
SECOND_START = 0x12EE0  # the same, for the second teleport target
AMOUNTS = 0x12ED4  # starting and pool Energy, missiles, bombs (six 16-bit values)
OPTIONS = 0x1308F  # setting t at OPTIONS + t

SAVE_ROOM = 0x22  # the value a fresh save's start cell holds
TILES_X, TILES_Y = 14, 20  # tiles per map cell
WRITABLE_ITEMS = range(0, 50)  # item 50's byte is not understood (client/game.py)
ENERGY_TANK, MISSILE_CONTAINER, MISSILE_BOX = 0, 1, 36
# Where the save keeps current bombs is not known, so a seed cannot start with bombs.
BOMB_ITEMS = (2, 37)  # Mega Bomb, Bomb Pack


class PatchError(ValueError):
    """The save is not one this seed can be written into."""


@dataclass(frozen=True)
class SeedPlan:
    """What the seed needs in the save; built from the AP slot data."""

    location_items: tuple[int, ...]  # item ID per location, as the game should hold it
    start_items: dict[int, int]  # item ID -> count
    start: tuple[int, int, int]  # candidate index, tile column, tile row
    second_start: tuple[int, int, int] | None
    amounts: tuple[int, ...]  # the mode's six amounts, as at AMOUNTS
    options: dict[int, int]  # settings the logic depends on: option index -> value


def plan_from_slot_data(slot: dict) -> SeedPlan:
    second = slot.get("second_start")
    return SeedPlan(
        location_items=tuple(slot["location_items"]),
        start_items={int(k): v for k, v in slot["start_items"].items()},
        start=(slot["start_index"], slot["start_column"], slot["start_row"]),
        second_start=None if second is None else (second["index"], second["column"], second["row"]),
        amounts=tuple(slot["amounts"]),
        options={int(k): v for k, v in slot["options"].items()},
    )


def cell_of(column: int, row: int) -> tuple[int, int]:
    return (column // TILES_X, row // TILES_Y)


def check_fresh(save: bytes) -> None:
    """A randomizer save made at the start of its seed, nothing picked up yet."""
    if len(save) != SAVE_SIZE:
        raise PatchError(f"not a randomizer save ({len(save)} bytes, expected {SAVE_SIZE})")
    if struct.unpack_from("<H", save, RANDO_MARKER)[0] != 1:
        # A cleared randomizer slot keeps its size; the game would not load a seed from it.
        raise PatchError("not a randomizer save (its randomizer marker is not set)")
    if struct.unpack_from("<H", save, PICKUP_COUNTER)[0] != 0:
        raise PatchError("something has been picked up in this save: start a new game")
    revealed = sum(1 for v in save[MAP_GRID : MAP_GRID + MAP_W * MAP_W] if v)
    if revealed > 1:
        raise PatchError(f"{revealed} map rooms are explored: start a new game and save at once")


def check_mode(save: bytes, plan: SeedPlan) -> None:
    """The save's mode and logic settings must be the seed's: they cannot be patched in."""
    amounts = struct.unpack_from("<6h", save, AMOUNTS)
    if tuple(amounts) != plan.amounts:
        raise PatchError(
            f"this game's Energy, missile and bomb amounts {amounts} are not the seed's "
            f"{plan.amounts}: start the new game in the seed's mode"
        )
    wrong = [t for t, v in plan.options.items() if save[OPTIONS + t] != v]
    if wrong:
        raise PatchError(f"this game's settings differ from the seed's at options {wrong}")


def patch(save: bytes, plan: SeedPlan) -> bytes:
    """The fresh save with the seed written in. Raises PatchError if it cannot be."""
    check_fresh(save)
    check_mode(save, plan)
    if len(plan.location_items) != LOCATIONS:
        raise PatchError(f"the seed lists {len(plan.location_items)} locations, not {LOCATIONS}")
    bad = [i for i, n in plan.start_items.items() if n and i not in WRITABLE_ITEMS]
    bad += [i for i in BOMB_ITEMS if plan.start_items.get(i)]
    if bad:
        raise PatchError(f"starting items {sorted(bad)} cannot be written")
    out = bytearray(save)
    out[ITEM_TABLE : ITEM_TABLE + LOCATIONS] = bytes(plan.location_items)
    struct.pack_into("<3H", out, START_RECORD, *plan.start)
    if plan.second_start is not None:
        struct.pack_into("<3H", out, SECOND_START, *plan.second_start)
    _, column, row = plan.start
    struct.pack_into("<2H", out, CONTINUE, row, column)
    # A fresh Easy or Standard save reveals its start cell as SAVE_ROOM; a fresh OHKO save
    # reveals none. Keep whichever the game wrote, at the seed's start.
    revealed = any(save[MAP_GRID : MAP_GRID + MAP_W * MAP_W])
    grid = bytearray(MAP_W * MAP_W)
    if revealed:
        x, y = cell_of(column, row)
        grid[y * MAP_W + x] = SAVE_ROOM
    out[MAP_GRID : MAP_GRID + len(grid)] = grid
    # The key markers must show where this seed puts the keys, not where the fresh game's own
    # seed did; a key that is in another world, or owned from the start, has none.
    cells = [struct.unpack_from("<HH", save, SECTION + 4 * i) for i in range(LOCATIONS)]
    for k, key in enumerate(KEYS):
        at = [i for i, item in enumerate(plan.location_items) if item == key]
        marker = (NO_MARKER, NO_MARKER)
        if at and not plan.start_items.get(key):
            x, y = cells[at[0]]
            marker = (y, x)
        out[KEY_MARKERS + 2 * k : KEY_MARKERS + 2 * k + 2] = bytes(marker)
    for item in WRITABLE_ITEMS:
        out[INVENTORY + item] = plan.start_items.get(item, 0)
    tanks = plan.start_items.get(ENERGY_TANK, 0)
    missiles = plan.start_items.get(MISSILE_CONTAINER, 0) + 10 * plan.start_items.get(
        MISSILE_BOX, 0
    )
    struct.pack_into("<H", out, ENERGY_NOW, tanks * 16)
    struct.pack_into("<H", out, MISSILES_NOW, missiles)
    return bytes(out)


def section_matches(section: bytes, plan: SeedPlan) -> bool:
    """Whether a randomizer section (as loaded in memory, from SECTION) holds this seed."""
    table = section[ITEM_TABLE - SECTION : ITEM_TABLE - SECTION + LOCATIONS]
    start = struct.unpack_from("<3H", section, START_RECORD - SECTION)
    return bytes(table) == bytes(plan.location_items) and tuple(start) == plan.start
