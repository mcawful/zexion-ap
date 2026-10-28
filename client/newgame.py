"""The AP seed as edits to the randomizer section of a new game, for the in-game helper.

When the player starts a new randomizer game, the game builds the new game, and writes its
first save, from the randomizer section in memory (``+0xCDD6D0``): the item table, the start,
the starting items and amounts (docs/re/memory.md, "Starting a new game"). The helper writes
the edits built here into that section just before, so the new game holds the AP seed.

The starting items have to be expressed the way the new-game routine reads them:
- most upgrades as entries of the 27-byte start-with array (1 = start with it);
- Energy Tanks as the starting Energy amount;
- missiles as one amount, Missile Containers + 10 x Missile Boxes, which the game splits back
  into boxes (tens) and containers, so at most 9 containers;
- Mega Bombs and Bomb Boxes likewise;
- keys as a count, the first N keys from the Alpha Key on.

Anything else (filler, other key sets, more than 9 containers or Mega Bombs) cannot be
expressed: ``NewGameError``. The world refuses such starting items in generation
(worlds/zexion/starting.py), so the client meeting one is a bug to report.

The helper runs the game's placement itself with the randomizer menu set to the seed's
settings (its mode's, or Custom's: the ``menu`` lines), checks that the section then holds the
seed's amounts and the settings the logic reads (``expect``, as ``save_patch.check_mode`` does
for a save file), and writes the AP seed over it. So the new game is the game's own for the
seed's text, settings and start, down to the Hints' map rectangles, which the placement draws
from where its random numbers ended (docs/re/randocells.md, "What each setting does in the
game"). The request also carries what START GAME's skipped first step would write: the second
target, or for Double Teleport (OHKO) the start table the placement reads, used only on the
build it was measured on (docs/re/memory.md, "The game-mode panel"). Two ways in:

- with ``offer_choice``, the ``choice`` line: ARCHIPELAGO on the game-mode panel;
- always, the ``identify`` line: a new game the player starts from RANDOMIZER with the seed's
  text typed in is placed again as the seed, whatever else the player's menu holds. Its plan
  carries no start table: the game's own, built when the menu opened, is used, as its START
  GAME uses it (ARCHIPELAGO falls back to RANDOMIZER when the two differ).

Pure functions; the offsets are file offsets in docs/re/save-format.md minus the section's
start (``save_patch.SECTION``).
"""

from __future__ import annotations

import struct

from . import ids
from . import save_patch as sp
from .ids import BOMB_BOX, ENERGY_TANK, KEYS, MEGA_BOMB, MISSILE_BOX, MISSILE_CONTAINER

# The start-with array's items, in order (the new-game routine at 0xA0570 copies entry i to
# that item's inventory byte; docs/re/memory.md, "Starting a new game").
START_WITH_ORDER = (
    16, 19, 21, 32, 3, 4, 5, 6, 7, 11, 22, 20, 17, 9, 14, 15, 10, 12, 34, 24, 26, 25, 13,
    33, 8, 23, 18,
)  # fmt: skip
START_WITH = 0x12E9D  # file offset of the start-with array
START_KEY_OPTIONS = (34, 35, 36)  # settings bits: keys count = bit0 + 2*bit1 + 4*bit2

# The randomizer menu's state: seed text, start-with and pool arrays, amounts, setting t at
# MENU_SETTINGS + t (docs/re/memory.md, "The game-mode panel").
MENU_SEED, MENU_START, MENU_POOL, MENU_AMOUNTS, MENU_SETTINGS = 0x0, 0x51, 0x6C, 0x88, 0xCA
MENU_FIRST_SETTING, COLOR_SETTING = 10, 12
HEX_DIGITS = frozenset("0123456789ABCDEF")
START_TABLE_MAX = 64  # the room count the menu's builder allows


class NewGameError(ValueError):
    """The seed's starting items cannot be given through a new game."""


def section(file_offset: int) -> int:
    return file_offset - sp.SECTION


def start_with(start_items: dict[int, int]) -> tuple[bytes, dict[int, int]]:
    """The start-with array, and the amounts (start Energy, missiles, bombs) and key count, from
    the seed's starting items."""
    items = {item: n for item, n in start_items.items() if n}
    array = bytearray(len(START_WITH_ORDER))
    for i, item in enumerate(START_WITH_ORDER):
        if items.pop(item, 0):
            array[i] = 1
    tanks = items.pop(ENERGY_TANK, 0)
    containers, boxes = items.pop(MISSILE_CONTAINER, 0), items.pop(MISSILE_BOX, 0)
    bombs, packs = items.pop(MEGA_BOMB, 0), items.pop(BOMB_BOX, 0)
    if containers > 9 or bombs > 9:
        raise NewGameError("more than 9 Missile Containers or Mega Bombs at the start")
    keys = [k for k in KEYS if items.pop(k, 0)]
    if keys != list(KEYS[: len(keys)]):
        raise NewGameError("starting keys other than the first ones from the Alpha Key")
    if items:
        raise NewGameError(f"starting items {sorted(items)} cannot be given in a new game")
    amounts = {"energy": tanks, "missiles": containers + 10 * boxes, "bombs": bombs + 10 * packs}
    return bytes(array), {**amounts, "keys": len(keys)}


def plan_text(plan: sp.SeedPlan, build_sha256: str = "", offer_choice: bool = True) -> str:
    """The helper's ``ap_newgame`` request for this seed, in the game build whose exe has the
    SHA-256 ``build_sha256``. With ``offer_choice`` ARCHIPELAGO is offered too; without, the
    seed is started from RANDOMIZER only."""
    if len(plan.location_items) != ids.LOCATIONS:
        raise NewGameError(f"the seed lists {len(plan.location_items)} locations")
    seed_text = plan.menu.seed_text
    if len(seed_text) != sp.SEED_DIGITS or not set(seed_text) <= HEX_DIGITS:
        raise NewGameError(f"the seed text {seed_text!r} is not 16 hex digits")
    array, start = start_with(plan.start_items)
    table = start_table(plan, build_sha256)
    lines = ["ap_newgame"]
    # A RANDOMIZER new game is this seed's when the player typed in its text.
    lines.append(f"identify {section(sp.SEED_TEXT):#x} {seed_text.encode('ascii').hex()}")
    # After the seed's placement the section must hold its amounts and the settings the logic
    # reads.
    lines.append(f"expect {section(sp.AMOUNTS):#x} {struct.pack('<6h', *plan.amounts).hex()}")
    for t, value in sorted(plan.options.items()):
        lines.append(f"expect {section(sp.OPTIONS + t):#x} {value:02x}")
    # The AP seed.
    lines.append(f"write {section(sp.ITEM_TABLE):#x} {bytes(plan.location_items).hex()}")
    # The seed text and its hash: ARCHIPELAGO's placement already ran from them; a RANDOMIZER
    # game ran from the menu's own text, so it gets the seed's, as RANDO SETTINGS shows it and
    # Colorizer's colors follow it. (The Hints' map rectangles the placement drew stay.)
    lines.append(f"write {section(sp.SEED_TEXT):#x} {seed_text.encode('ascii').hex()}")
    hashed = struct.pack("<Q", sp.seed_hash(seed_text)).hex()
    lines.append(f"write {section(sp.SEED_HASH):#x} {hashed}")
    lines.append(f"write {section(sp.START_RECORD):#x} {struct.pack('<3H', *plan.start).hex()}")
    if plan.second_start is not None:
        # With the start table, also the byte after it, as START GAME writes it: the second
        # start room's flag.
        second = struct.pack("<3H", *plan.second_start)
        if table is not None:
            second += bytes([table[plan.second_start[0]][2]])
        lines.append(f"write {section(sp.SECOND_START):#x} {second.hex()}")
    lines.append(f"write {section(START_WITH):#x} {array.hex()}")
    for k, name in enumerate(("energy", "missiles", "bombs")):
        lines.append(
            f"write {section(sp.AMOUNTS + 4 * k):#x} {struct.pack('<h', start[name]).hex()}"
        )
    for bit, t in enumerate(START_KEY_OPTIONS):
        lines.append(f"write {section(sp.OPTIONS + t):#x} {(start['keys'] >> bit) & 1:02x}")
    lines += menu_lines(plan, table if offer_choice else None, need_table=offer_choice)
    if offer_choice:
        lines.append("choice")
    return "\n".join(lines)


def start_table(plan: sp.SeedPlan, build_sha256: str) -> tuple[tuple[int, int, int], ...] | None:
    """The menu setup's start table, if it has one measured on this build; checked for shape,
    and for the seed's second start being the room the table lists at its index."""
    menu = plan.menu
    if not menu.start_table:
        return None
    if not menu.table_build or not build_sha256.lower().startswith(menu.table_build.lower()):
        return None  # measured on another build: the game's own may differ
    table = menu.start_table
    if len(table) > START_TABLE_MAX or not all(
        len(room) == 3
        and 0 <= room[0] < ids.MAP_SIZE
        and 0 <= room[1] < ids.MAP_SIZE
        and room[2] in (0, 1)
        for room in table
    ):
        raise NewGameError("the start table is not up to 64 rooms of (x, y, flag)")
    if plan.second_start is not None:
        index, column, row = plan.second_start
        if not 0 <= index < len(table):
            raise NewGameError(f"the second start {index} is not in the start table")
        if table[index][:2] != sp.cell_of(column, row):
            raise NewGameError(f"the second start is not room {index} of the start table")
    return table


def menu_lines(
    plan: sp.SeedPlan, table: tuple[tuple[int, int, int], ...] | None, need_table: bool = True
) -> list[str]:
    """The seed's placement's lines: the menu's state as the seed's settings, and what START
    GAME's skipped first step would write: in a mode with one start, the menu setup's second
    target and 0 after it; with a second teleport target, the start table the placement reads
    (``plan_text`` writes the second start and its flag), which the helper fills in if the
    menu never built it. Without ``need_table`` a missing table is left to the game's own."""
    menu = plan.menu
    if len(menu.seed_text) != 16 or not set(menu.seed_text) <= HEX_DIGITS:
        raise NewGameError(f"the seed text {menu.seed_text!r} is not 16 hex digits")
    if any(len(a) != len(START_WITH_ORDER) or set(a) - {0, 1} for a in (menu.start, menu.pool)):
        raise NewGameError("the menu's arrays are not 27 entries of 0 or 1")
    if len(menu.settings) != 64:
        raise NewGameError(f"the menu has {len(menu.settings)} settings, expected 64")
    for t, value in plan.options.items():
        index = t - MENU_FIRST_SETTING
        if 0 <= index < len(menu.settings) and menu.settings[index] != value:
            raise NewGameError(f"the menu's setting {t} is not the seed's")
    lines = []
    if plan.second_start is not None:
        if table is not None:
            rooms = b"".join(struct.pack("<hhB", *room) for room in table)
            lines.append(f"start_table {rooms.hex()}")
        elif need_table:
            raise NewGameError("no start table measured on this game build")
    else:
        target = menu.second_target
        if target is None:
            raise NewGameError("the menu setup gives no second target")
        if len(target) != 3 or not all(isinstance(v, int) and 0 <= v <= 0xFFFF for v in target):
            raise NewGameError(f"the second target {target!r} is not three 16-bit numbers")
        second = struct.pack("<3H", *target) + bytes(1)
        lines.append(f"write {section(sp.SECOND_START):#x} {second.hex()}")
    color = COLOR_SETTING - MENU_FIRST_SETTING
    return lines + [
        f"menu {MENU_SEED:#x} {menu.seed_text.encode('ascii').hex()}",
        f"menu {MENU_START:#x} {menu.start.hex()}",
        f"menu {MENU_POOL:#x} {menu.pool.hex()}",
        f"menu {MENU_AMOUNTS:#x} {struct.pack('<6h', *plan.amounts).hex()}",
        f"menu {MENU_SETTINGS + MENU_FIRST_SETTING:#x} {menu.settings[:color].hex()}",
        f"menu {MENU_SETTINGS + COLOR_SETTING + 1:#x} {menu.settings[color + 1 :].hex()}",
    ]


def randomizer_message(plan: sp.SeedPlan) -> str:
    """How to start this seed from RANDOMIZER: the seed's text is all the menu needs, since
    the helper places the new game again with the seed's settings."""
    text = plan.menu.seed_text
    words = " ".join(text[i : i + 4] for i in range(0, len(text), 4))
    return (
        f"in an empty slot choose RANDOMIZER, enter the seed {words} (the menu's other "
        "settings don't matter: the seed's own are used), and START GAME (if the game offers "
        "to play a seed it couldn't make winnable, choose YES)"
    )
