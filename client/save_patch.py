"""A seed's plan from the slot data, the randomizer save's layout, and an Archipelago seed
written into a fresh Zexion randomizer save.

``SeedPlan`` is what the client works from; ``section_matches`` tells whether a loaded section
holds the seed. The client writes no saves: ``patch`` is the reference the tests hold
client/newgame.py's edits to, and ``check_fresh`` is also tools/gen_world_data.py's check.
Offsets and evidence: docs/re/save-format.md. Pure functions over bytes.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

from . import ids
from .ids import (
    ENERGY_TANK,
    KEYS,
    LOCATIONS,
    MISSILE_BOX,
    MISSILE_CONTAINER,
    SECRET_SHUFFLE,
    WRITABLE_ITEMS,
)

SAVE_SIZE = 78108  # a randomizer save
RANDO_MARKER = 0x9402  # 16-bit: 1 in a randomizer save, 0 in a normal one or a cleared slot

# File offsets (docs/re/save-format.md).
INVENTORY = 0x92F3  # + item ID, items 0-49
MAP_GRID = 0x877F  # 54 x 54, row-major
# The four keys' map markers: flags 40 to 47, a (y, x) cell per key (Alpha, Beta, Gamma,
# Omega), 0xFF for none; the seed's own key cells in a fresh save (docs/re/save-format.md).
KEY_MARKERS = 0x2A
NO_MARKER = 0xFF
PICKUP_COUNTER = 0x935A  # 16-bit
ENERGY_NOW = 0x9350  # 16-bit; a fresh save holds Energy Tanks x 16
MISSILES_NOW = 0x9352  # 16-bit; a fresh save holds containers + 10 x Missile Boxes
CONTINUE = 0x935C  # tile row, tile column (16-bit each)
SECTION = 0x12814  # the randomizer section, as loaded to +0xCDD6D0
ITEM_TABLE = 0x12A80  # item ID per location
START_RECORD = 0x12B24  # start-candidate index - 1, tile column, tile row
SEED_HASH = 0x12B1C  # the seed text's hash, 64-bit, which the game's random numbers start from
SEED_TEXT = 0x12E6C  # the 16-character seed text the menu showed (section +0x658)
SEED_DIGITS = 16
HEX_DIGITS = frozenset("0123456789ABCDEF")
SECOND_START = 0x12EE0  # the same, for the second teleport target
AMOUNTS = 0x12ED4  # starting and pool Energy, missiles, bombs (six 16-bit values)
OPTIONS = 0x1308F  # setting t at OPTIONS + t
GUNS_FIRST = 10  # setting t10: the item redirects (docs/re/randocells.md)

SAVE_ROOM = 0x22  # the value a fresh save's start cell holds
TILES_X, TILES_Y = 14, 20  # tiles per map cell
# Where the save keeps current bombs is not known, so ``patch`` refuses starting bombs (and
# worlds/zexion/starting.py refuses them in generation).
BOMB_ITEMS = (ids.MEGA_BOMB, ids.BOMB_BOX)


class PatchError(ValueError):
    """The save is not one this seed can be written into."""


@dataclass(frozen=True)
class MenuSetup:
    """The randomizer menu as the seed's settings, which the helper sets the menu to on either
    path, ARCHIPELAGO or RANDOMIZER (client/newgame.py, ``menu_lines``), from the slot data."""

    seed_text: str  # 16 characters, 0-9 and A-F
    start: bytes  # the menu's 27-entry start-with array (0/1)
    pool: bytes  # its 27-entry pool array (0/1)
    settings: bytes  # settings 10 to 73
    # What START GAME writes in the second target in a mode with one start; None in a mode
    # with a second teleport target, whose second start is the seed's own.
    second_target: tuple[int, int, int] | None
    # The randomizer menu's start table, as it builds it when it opens: each candidate room's
    # map cell (x, y) and flag (1 if it holds object 25). Given for a mode with a second
    # target, whose placement reads it.
    start_table: tuple[tuple[int, int, int], ...] = ()
    table_build: str = ""  # the start of the SHA-256 of the exe the table was measured on


def game_order(cells) -> tuple[int, ...]:
    """The location indices of ``cells`` (by index, as the slot data lists them) in the game's
    list order: grid order, a cell (a, b) by b then a, the game's X then Y (X = b, Y = a:
    docs/re/randocells.md, "Grid order" and "The pool"). The identity for the game's 154; with
    Secret Shuffle the 155th, location index 154, goes to list position 134."""
    return tuple(sorted(range(len(cells)), key=lambda i: (cells[i][1], cells[i][0])))


@dataclass(frozen=True)
class SeedPlan:
    """What the seed needs in the save; built from the AP slot data."""

    location_items: tuple[int, ...]  # item ID per location index, as the game should hold it
    start_items: dict[int, int]  # item ID -> count
    start: tuple[int, int, int]  # start-candidate index - 1, tile column, tile row
    second_start: tuple[int, int, int] | None
    amounts: tuple[int, ...]  # the mode's six amounts, as at AMOUNTS
    options: dict[int, int]  # settings the logic depends on: option index -> value
    menu: MenuSetup
    # The locations' cells by location index (slot data "locations"); empty: the game's list
    # order already (the 154 alone).
    cells: tuple[tuple[int, int], ...] = ()

    @property
    def list_size(self) -> int:
        """The locations the game's list holds for this seed: 154, or 155 with Secret Shuffle
        (setting t37)."""
        return LOCATIONS + bool(self.options.get(SECRET_SHUFFLE, 0))

    @property
    def order(self) -> tuple[int, ...]:
        """The location indices in the game's list order (``game_order``)."""
        return game_order(self.cells) if self.cells else tuple(range(len(self.location_items)))

    @property
    def table(self) -> bytes:
        """The item table as the game holds it: ``location_items`` in the game's list order."""
        return bytes(self.location_items[i] for i in self.order)

    def check_size(self) -> str:
        """What is wrong with the seed's list of locations for its settings, or ""."""
        n = self.list_size
        if len(self.location_items) != n or (self.cells and len(self.cells) != n):
            return f"the seed lists {len(self.location_items)} locations, not {n}"
        return ""

    @property
    def guns_first(self) -> bool:
        """Guns First (setting t10): a weapon and its upgrade are one progressive item. On in
        every preset, and in slot data from before Custom mode, which does not list it."""
        return bool(self.options.get(GUNS_FIRST, 1))


def plan_from_slot_data(slot: dict) -> SeedPlan:
    second = slot["second_start"]
    menu = slot["menu"]
    return SeedPlan(
        location_items=tuple(slot["location_items"]),
        start_items={int(k): v for k, v in slot["start_items"].items()},
        start=(slot["start_index"], slot["start_column"], slot["start_row"]),
        second_start=None if second is None else (second["index"], second["column"], second["row"]),
        amounts=tuple(slot["amounts"]),
        options={int(k): v for k, v in slot["options"].items()},
        menu=MenuSetup(
            seed_text=menu["seed_text"],
            start=bytes(menu["start"]),
            pool=bytes(menu["pool"]),
            settings=bytes(menu["settings"]),
            second_target=None if menu["second_target"] is None else tuple(menu["second_target"]),
            # Sent only for a mode with a second teleport target.
            start_table=tuple(tuple(room) for room in menu.get("start_table", ())),
            table_build=menu.get("table_build", ""),
        ),
        cells=tuple(tuple(c) for c in slot["locations"]),
    )


def seed_hash(text: str) -> int:
    """The game's hash of a seed text of 16 characters 0-9 and A-F (docs/re/randocells.md, "The
    pool"): the characters a number in base 16, the first the least significant, modulo 2**64.
    Raises ValueError for another text (the game would read a lowercase letter otherwise)."""
    if len(text) != SEED_DIGITS or not set(text) <= HEX_DIGITS:
        raise ValueError(f"the seed text {text!r} is not 16 characters 0-9 and A-F")
    value = 0
    for i, c in enumerate(text):
        digit = int(c, 16)
        value = (value + digit * 16**i) & 0xFFFFFFFFFFFFFFFF
    return value


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
    revealed = sum(1 for v in save[MAP_GRID : MAP_GRID + ids.MAP_SIZE * ids.MAP_SIZE] if v)
    if revealed > 1:
        raise PatchError(f"{revealed} map rooms are explored: start a new game and save at once")


def check_mode(save: bytes, plan: SeedPlan) -> None:
    """The save's mode and logic settings must be the seed's: they cannot be patched in."""
    amounts = struct.unpack_from("<6h", save, AMOUNTS)
    if tuple(amounts) != plan.amounts:
        raise PatchError(
            f"this game's Energy, missile and bomb amounts {amounts} are not the seed's "
            f"{plan.amounts}: start the new game with the seed's mode and settings"
        )
    wrong = [t for t, v in plan.options.items() if save[OPTIONS + t] != v]
    if wrong:
        raise PatchError(f"this game's settings differ from the seed's at options {wrong}")


def patch(save: bytes, plan: SeedPlan) -> bytes:
    """The fresh save with the seed written in. Raises PatchError if it cannot be."""
    check_fresh(save)
    check_mode(save, plan)
    wrong = plan.check_size()
    if wrong:
        raise PatchError(wrong)
    bad = [i for i, n in plan.start_items.items() if n and i not in WRITABLE_ITEMS]
    bad += [i for i in BOMB_ITEMS if plan.start_items.get(i)]
    if bad:
        raise PatchError(f"starting items {sorted(bad)} cannot be written")
    out = bytearray(save)
    out[ITEM_TABLE : ITEM_TABLE + plan.list_size] = plan.table
    # The seed text and its hash, as an ARCHIPELAGO game has them (client/newgame.py).
    try:
        hashed = seed_hash(plan.menu.seed_text)
    except ValueError as error:
        raise PatchError(str(error)) from error
    out[SEED_TEXT : SEED_TEXT + SEED_DIGITS] = plan.menu.seed_text.encode("ascii")
    struct.pack_into("<Q", out, SEED_HASH, hashed)
    struct.pack_into("<3H", out, START_RECORD, *plan.start)
    if plan.second_start is not None:
        struct.pack_into("<3H", out, SECOND_START, *plan.second_start)
    _, column, row = plan.start
    struct.pack_into("<2H", out, CONTINUE, row, column)
    # A fresh Easy or Standard save reveals its start cell as SAVE_ROOM; a fresh OHKO save
    # reveals none. Keep whichever the game wrote, at the seed's start.
    revealed = any(save[MAP_GRID : MAP_GRID + ids.MAP_SIZE * ids.MAP_SIZE])
    grid = bytearray(ids.MAP_SIZE * ids.MAP_SIZE)
    if revealed:
        x, y = cell_of(column, row)
        grid[y * ids.MAP_SIZE + x] = SAVE_ROOM
    out[MAP_GRID : MAP_GRID + len(grid)] = grid
    # The key markers must show where this seed puts the keys, not where the fresh game's own
    # seed did; a key that is in another world, or owned from the start, has none.
    cells = [struct.unpack_from("<HH", save, SECTION + 4 * i) for i in range(plan.list_size)]
    for k, key in enumerate(KEYS):
        at = [i for i, item in enumerate(plan.table) if item == key]
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
    """Whether a randomizer section (as loaded in memory, from SECTION) holds this seed: its
    list's size (Secret Shuffle's byte), item table and start."""
    if bool(section[OPTIONS - SECTION + SECRET_SHUFFLE]) != (plan.list_size > LOCATIONS):
        return False
    table = section[ITEM_TABLE - SECTION : ITEM_TABLE - SECTION + plan.list_size]
    start = struct.unpack_from("<3H", section, START_RECORD - SECTION)
    return bytes(table) == plan.table and tuple(start) == plan.start
