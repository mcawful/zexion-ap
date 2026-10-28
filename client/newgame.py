"""The AP seed as edits to the randomizer section of a new game, for the in-game helper.

When the player starts a new randomizer game, the game builds the new game, and writes its
first save, from the randomizer section in memory (``+0xCDD6D0``): the item table, the start,
the starting items and amounts (docs/re/memory.md, "Starting a new game"). The helper writes
the edits built here into that section just before, so the new game holds the AP seed; it
first checks that the section holds the seed's amounts and the settings the logic reads
(``expect``), as ``save_patch.check_mode`` does for a save file.

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

With ``offer_choice`` (ARCHIPELAGO) the request also carries ``menu`` lines, the randomizer
menu's state as the seed's settings (its mode's, or Custom's), which the helper sets around the
game's placement
(docs/re/memory.md, "The game-mode panel"), and what START GAME's skipped first step would
write: the second target, or for Double Teleport (OHKO) the start table the placement reads,
used only on the build it was measured on.

Without it (RANDOMIZER) the player sets the menu: ``randomizer_message`` gives the seed text and
the game's settings code as one line to paste with the menu's Paste<-Clipboard, which sets the
same menu state, and the settings to set by hand as the fallback.

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
    SHA-256 ``build_sha256``. With ``offer_choice`` it carries the
    ARCHIPELAGO choice's lines too; without, the seed is started from RANDOMIZER."""
    if len(plan.location_items) != ids.LOCATIONS:
        raise NewGameError(f"the seed lists {len(plan.location_items)} locations")
    seed_text = plan.menu.seed_text
    if len(seed_text) != sp.SEED_DIGITS or not set(seed_text) <= HEX_DIGITS:
        raise NewGameError(f"the seed text {seed_text!r} is not 16 hex digits")
    array, start = start_with(plan.start_items)
    table = start_table(plan, build_sha256)
    lines = ["ap_newgame"]
    # The new game must be the seed's: its amounts and the settings the logic reads.
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
    if offer_choice:
        lines += menu_lines(plan, table)
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


def check_menu(plan: sp.SeedPlan) -> sp.MenuSetup:
    """The seed's menu state, checked for shape and for holding the seed's checked settings."""
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
    return menu


def menu_lines(plan: sp.SeedPlan, table: tuple[tuple[int, int, int], ...] | None) -> list[str]:
    """The ARCHIPELAGO choice's lines: the menu's state as the seed's settings, and what START
    GAME's skipped first step would write: in a mode with one start, the menu setup's second
    target and 0 after it; with a second teleport target, the start table the placement reads
    (``plan_text`` writes the second start and its flag), which the helper fills in if the
    menu never built it."""
    menu = check_menu(plan)
    lines = []
    if plan.second_start is not None:
        if table is None:
            raise NewGameError("no start table measured on this game build")
        lines.append(f"start_table {b''.join(struct.pack('<hhB', *room) for room in table).hex()}")
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


# The settings code Copy->Clipboard writes and Paste<-Clipboard reads (docs/re/randocells.md,
# "The settings code"): 170 bits, lowest first, five to a character of this alphabet: t10 to t73,
# each item's start-with and pool bits, then the six amounts in these widths.
CODE_ALPHABET = "BCDFGHJKLMNPQRSTVWXZbcdfghjklmnp"
CODE_AMOUNT_BITS = (6, 6, 10, 10, 10, 10)


def settings_code(menu: sp.MenuSetup, amounts: tuple[int, ...]) -> str:
    """The game's settings code for this menu state and these six amounts: 34 characters."""
    if len(amounts) != len(CODE_AMOUNT_BITS) or not all(
        0 <= a < 1 << bits for a, bits in zip(amounts, CODE_AMOUNT_BITS, strict=True)
    ):
        raise NewGameError(f"the amounts {amounts} do not fit the settings code")
    if set(menu.settings) - {0, 1}:
        raise NewGameError("the menu's settings are not each 0 or 1")
    value, width = 0, 0

    def put(field: int, bits: int) -> None:
        nonlocal value, width
        value |= field << width
        width += bits

    for setting in menu.settings:
        put(setting, 1)
    for start, pool in zip(menu.start, menu.pool, strict=True):
        put(start, 1)
        put(pool, 1)
    for amount, bits in zip(amounts, CODE_AMOUNT_BITS, strict=True):
        put(amount, bits)
    return "".join(CODE_ALPHABET[(value >> k) & 31] for k in range(0, width, 5))


def paste_text(plan: sp.SeedPlan) -> str:
    """The text to paste with the randomizer menu's Paste<-Clipboard: the seed text and the
    settings code with nothing between them, as Copy->Clipboard writes them, since Paste reads
    the code from the character after the seed's 16th. Pasted, it sets the seed text, the
    items' states, the six amounts and every setting t10 to t73 at once."""
    return check_menu(plan).seed_text + settings_code(plan.menu, plan.amounts)


def randomizer_message(plan: sp.SeedPlan, mode: str) -> list[str]:
    """The client's lines for starting this seed from RANDOMIZER: the text to paste, on a line
    of its own (a click on a line in AP's client window copies it), and the menu to set by
    hand as the fallback; the steps alone if the seed's menu state has no code."""
    steps = randomizer_steps(plan, mode)
    start = (
        "To play, load this seed's save, or start a new game: in an empty slot choose RANDOMIZER"
    )
    try:
        paste = paste_text(plan)
    except NewGameError:
        return [f"{start}, {steps}, and START GAME."]
    return [
        f"{start}, copy the next line (in the client's window, click it), press the menu's "
        "Paste from Clipboard, and START GAME:",
        paste,
        f"Or, without pasting, {steps}, and START GAME.",
    ]


# What the RANDOMIZER menu must hold for the expect lines to pass: the settings the logic reads,
# by their menu labels (docs/re/randocells.md, "The randomizer menu's settings").
ON_OFF_LABELS = {
    10: "Guns First", 30: "X Crystals", 31: "X Blocks", 32: "Abridged Finale",
    33: "Double Teleport", 37: "Secret Shuffle",
}  # fmt: skip
SAFETY, GATES = (
    ((42, 43), ("Light", "Early", "Normal", "Off")),
    ((72, 73), ("Off", "Vanilla", "Unlocked", "Open")),
)
PRESETS = {"easy": "Easy", "standard": "Standard", "ohko": "OHKO"}


# The menu's 27 items, by the first screen's names, in START_WITH_ORDER's order.
ITEM_LABELS = (
    "Slide Mod", "Wall Jump Boots", "Marine Jets", "Navcore", "Machine Gun", "Force Blaster",
    "Snake", "Lance", "Arc Plasma", "Rocket Slide", "Jump Booster", "Phaser", "S-Blade",
    "Pulse Wave", "Pentashot", "Tunneler", "Focal Crystal", "Plasma Burst", "G-Diffuser",
    "Gravity Harness", "Zenith Thrusters", "Magnus Armor", "Power Cartridge", "Power Chip",
    "Power Core", "Javelin Missiles", "X-Missile",
)  # fmt: skip
START_KEYS = (34, 35, 36)


def randomizer_steps(plan: sp.SeedPlan, mode: str) -> str:
    """How to set RANDOMIZER's menu for this seed, for a player starting it by hand: a preset
    with its Safety (the one checked setting a preset may change), or for Custom the six
    amounts and every checked setting, and first the items' states and Start Keys, which the
    menu's limits on the pool missiles and Mega Bombs depend on (docs/re/randocells.md, "The
    randomizer menu's first screen: items and amounts"). The rest is not checked: the client
    writes the seed's own starting items and item table over the game's."""

    def value(bits: tuple[int, ...], labels: tuple[str, ...]) -> str:
        return labels[sum(plan.options.get(t, 0) << i for i, t in enumerate(bits))]

    safety = f"Safety {value(*SAFETY)}"
    text = plan.menu.seed_text
    # Optional: the client writes the seed's text over the menu's (plan_text); typed in, the
    # game's own placement runs from it too, as ARCHIPELAGO's does (the Hints' rectangles).
    seed = f" (the seed, optional: {' '.join(text[i : i + 4] for i in range(0, len(text), 4))})"
    if mode in PRESETS:
        return f"pick {PRESETS[mode]} mode, set {safety} and change no other options{seed}"
    menu = plan.menu
    states: dict[tuple[int, int], list[str]] = {}
    for label, start, pool in zip(ITEM_LABELS, menu.start, menu.pool, strict=True):
        states.setdefault((start, pool), []).append(label)
    groups = [
        f"{name} {', '.join(states[key])}"
        for key, name in (((1, 0), "Starting Item:"), ((1, 1), "Random:"), ((0, 0), "Excluded:"))
        if states.get(key)
    ]
    items = "; ".join(groups) or "every item in the Item Pool"
    keys = sum(menu.settings[t - MENU_FIRST_SETTING] << i for i, t in enumerate(START_KEYS))
    a = plan.amounts
    amounts = (
        f"Starting Items {a[0]} Energy Tanks, {a[2]} missiles, {a[4]} Mega Bombs; Item Pool "
        f"{a[1]}, {a[3]}, {a[5]}"
    )
    settings = [
        f"{label} {'on' if plan.options.get(t) else 'off'}" for t, label in ON_OFF_LABELS.items()
    ]
    settings += [safety, f"Gates {value(*GATES)}"]
    return (
        f"set Custom, first the items ({items}; the rest in the Item Pool) and Start Keys {keys} "
        f"(the menu limits the amounts by them), then {amounts}; Edit Options: "
        f"{', '.join(settings)}{seed}"
    )
