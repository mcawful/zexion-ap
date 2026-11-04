"""A seed's plan from the slot data, and the randomizer save's layout.

``SeedPlan`` is what the client works from; ``section_matches`` tells whether a loaded section
holds the seed. The client writes no saves (the helper makes a new game: client/newgame.py);
``check_fresh`` is tools/gen_world_data.py's check of a fresh save. Offsets and evidence:
docs/re/save-format.md. Pure functions over bytes.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

from . import ids
from .ids import LOCATIONS, SECRET_SHUFFLE

SAVE_SIZE = 78108  # a randomizer save
RANDO_MARKER = 0x9402  # 16-bit: 1 in a randomizer save, 0 in a normal one or a cleared slot

# File offsets (docs/re/save-format.md).
INVENTORY = 0x92F3  # + item ID, items 0-49
MAP_GRID = 0x877F  # 54 x 54, row-major
PICKUP_COUNTER = 0x935A  # 16-bit
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
# The same places from the section's start, as the game holds it loaded (game.Game.rando_blob).
TABLE_AT, START_AT, OPTIONS_AT = ITEM_TABLE - SECTION, START_RECORD - SECTION, OPTIONS - SECTION

SAVE_ROOM = 0x22  # the value a fresh save's start cell holds
TILES_X, TILES_Y = 14, 20  # tiles per map cell


class PatchError(ValueError):
    """The save is not a fresh randomizer save (check_fresh)."""


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
    cells: tuple[tuple[int, int], ...]  # the locations' cells by location index ("locations")

    @property
    def list_size(self) -> int:
        """The locations the game's list holds for this seed: 154, or 155 with Secret Shuffle
        (setting t37)."""
        return LOCATIONS + bool(self.options.get(SECRET_SHUFFLE, 0))

    @property
    def order(self) -> tuple[int, ...]:
        """The location indices in the game's list order (``game_order``)."""
        return game_order(self.cells)

    @property
    def table(self) -> bytes:
        """The item table as the game holds it: ``location_items`` in the game's list order."""
        return bytes(self.location_items[i] for i in self.order)

    def check_size(self) -> str:
        """What is wrong with the seed's list of locations for its settings, or ""."""
        n = self.list_size
        if len(self.location_items) != n or len(self.cells) != n:
            return f"the seed lists {len(self.location_items)} locations, not {n}"
        return ""

    @property
    def guns_first(self) -> bool:
        """Guns First (setting t10): a weapon and its upgrade are one progressive item. On in
        every preset."""
        return bool(self.options[GUNS_FIRST])


def plan_from_slot_data(slot: dict) -> SeedPlan:
    """The seed's plan; KeyError for slot data without a key it needs (state.UnsupportedSeed)."""
    second = slot["second_start"]
    menu = slot["menu"]
    options = {int(k): v for k, v in slot["options"].items()}
    if GUNS_FIRST not in options:  # every world since Custom mode sends it (SeedPlan.guns_first)
        raise KeyError(f"options lack Guns First (setting {GUNS_FIRST})")
    return SeedPlan(
        location_items=tuple(slot["location_items"]),
        start_items={int(k): v for k, v in slot["start_items"].items()},
        start=(slot["start_index"], slot["start_column"], slot["start_row"]),
        second_start=None if second is None else (second["index"], second["column"], second["row"]),
        amounts=tuple(slot["amounts"]),
        options=options,
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


def section_matches(section: bytes, plan: SeedPlan) -> bool:
    """Whether a randomizer section (as loaded in memory, from SECTION) holds this seed: its
    list's size (Secret Shuffle's byte), item table and start."""
    if bool(section[OPTIONS_AT + SECRET_SHUFFLE]) != (plan.list_size > LOCATIONS):
        return False
    table = section[TABLE_AT : TABLE_AT + plan.list_size]
    start = struct.unpack_from("<3H", section, START_AT)
    return bytes(table) == plan.table and tuple(start) == plan.start
