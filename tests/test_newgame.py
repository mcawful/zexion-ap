"""client/newgame.py: the AP seed as section edits for a new game (no game)."""

from __future__ import annotations

import importlib.util
import struct
from pathlib import Path

import pytest
from fakes import fresh, menu_setup, plan

from client import newgame as ng
from client import save_patch as sp

MENU_SIZE = 0x114  # the randomizer menu's state (native/src/requests.h, kMenuSize)


def apply_text(text: str, save: bytearray, menu: bytearray | None = None) -> list[str]:
    """Apply an ap_newgame request to a save's section (and its menu lines to `menu`), as the
    helper does; the failed expectations, if any."""
    lines = text.splitlines()
    assert lines[0] == "ap_newgame"
    failed = []
    for line in lines[1:]:
        op, offset, data = line.split()
        at = sp.SECTION + int(offset, 0)
        raw = bytes.fromhex(data)
        if op == "expect" and save[at : at + len(raw)] != raw:
            failed.append(offset)
        if op == "menu":
            assert menu is not None, "menu lines without a menu"
            start = int(offset, 0)
            assert start + len(raw) <= MENU_SIZE
            menu[start : start + len(raw)] = raw
    if not failed:
        for line in lines[1:]:
            op, offset, data = line.split()
            if op == "write":
                at = sp.SECTION + int(offset, 0)
                save[at : at + len(bytes.fromhex(data))] = bytes.fromhex(data)
    return failed


def test_the_plan_writes_what_save_patch_writes_into_the_section() -> None:
    p = plan(second_start=(9, 151, 464))
    save = fresh()
    assert apply_text(ng.plan_text(p, offer_choice=False), save) == []
    patched = sp.patch(bytes(fresh()), p)
    for start, size in (
        (sp.ITEM_TABLE, sp.LOCATIONS),
        (sp.START_RECORD, 6),
        (sp.SECOND_START, 6),
        (sp.SEED_TEXT, 16),
        (sp.SEED_HASH, 8),
    ):
        assert save[start : start + size] == patched[start : start + size]


def test_the_seed_text_and_its_hash_are_the_seeds() -> None:
    # Two saves of 2026-10-27 (lab/ap/live37): their seed texts and the hashes the game stored.
    assert sp.seed_hash("6D03FB8F34D40BCA") == 0xACB04D43F8BF30D6
    assert sp.seed_hash("A923AEC2C81578A7") == 0x7A87518C2CEA329A
    save = fresh()
    p = plan()
    assert apply_text(ng.plan_text(p, offer_choice=False), save) == []
    at = sp.SEED_TEXT
    assert bytes(save[at : at + 16]).decode() == p.menu.seed_text
    assert struct.unpack_from("<Q", save, sp.SEED_HASH)[0] == sp.seed_hash(p.menu.seed_text)


def test_starting_items_become_the_array_amounts_and_keys() -> None:
    array, start = ng.start_with({0: 3, 1: 3, 36: 1, 3: 1, 13: 1, 27: 1, 28: 1, 2: 2})
    assert {ng.START_WITH_ORDER[i] for i, v in enumerate(array) if v} == {3, 13}
    assert start == {"energy": 3, "missiles": 13, "bombs": 2, "keys": 2}
    # Easy's start: 5 tanks, 3 containers, Machine Gun, Power Cartridge, Navcore.
    array, start = ng.start_with({0: 5, 1: 3, 3: 1, 13: 1, 32: 1})
    assert start == {"energy": 5, "missiles": 3, "bombs": 0, "keys": 0}
    assert sum(array) == 3


def test_the_amounts_and_key_bits_are_written() -> None:
    save = fresh()
    apply_text(
        ng.plan_text(plan(start_items={0: 5, 1: 3, 27: 1, 28: 1, 29: 1}), offer_choice=False), save
    )
    assert struct.unpack_from("<6h", save, sp.AMOUNTS)[0::2] == (5, 3, 0)
    assert list(save[sp.OPTIONS + 34 : sp.OPTIONS + 37]) == [1, 1, 0]


def test_another_mode_fails_the_expectations() -> None:
    save = fresh()
    struct.pack_into("<h", save, sp.AMOUNTS + 2, 35)  # another pool of Energy
    save[sp.OPTIONS + 72] = 2
    before = bytes(save)
    assert apply_text(ng.plan_text(plan(), offer_choice=False), save) != []
    assert bytes(save) == before, "nothing written"


@pytest.mark.parametrize(
    "start_items, why",
    [
        ({46: 1}, r"\[46\]"),
        ({28: 1}, "first ones"),
        ({1: 10}, "more than 9"),
        ({2: 10}, "more than 9"),
    ],
)
def test_starting_items_it_cannot_express_are_refused(start_items, why) -> None:
    with pytest.raises(ng.NewGameError, match=why):
        ng.plan_text(plan(start_items=start_items))


def test_the_menu_lines_set_the_menu_to_the_seeds_mode() -> None:
    p = plan(menu=menu_setup())
    save, menu = fresh(), bytearray(b"\xee" * MENU_SIZE)
    assert apply_text(ng.plan_text(p), save, menu) == []
    assert menu[0:16] == b"0123456789ABCDEF"
    assert menu[0x51 : 0x51 + 27] == p.menu.start and menu[0x6C : 0x6C + 27] == p.menu.pool
    assert struct.unpack_from("<6h", menu, 0x88) == p.amounts
    assert menu[0xCA + 10 : 0xCA + 12] == bytes(2)
    assert menu[0xCA + 12] == 0xEE, "the player's color setting is kept"
    assert menu[0xCA + 42] == 2 and menu[0xCA + 72] == 1 and menu[0xCA + 73] == 0
    assert MENU_SIZE - 1 == 0xCA + 73, "setting 73 is the state's last byte"
    assert struct.unpack_from("<3H", save, sp.SECOND_START) == (0, 20, 509)
    assert save[sp.SECTION + 0x6D2] == 0


B1 = "6d8a94bd4d64" + "0" * 52  # a SHA-256 the table's build prefix matches
TABLE = ((1, 25, 1), (6, 41, 0), (9, 0, 1), (10, 23, 0))


def ohko(**changes) -> sp.SeedPlan:
    fields = dict(second_target=None, start_table=TABLE, table_build="6d8a94bd4d64")
    fields.update(changes)
    return plan(menu=menu_setup(**fields), second_start=(2, 134, 15))


def test_ohko_carries_the_start_table_and_its_second_targets_flag() -> None:
    lines = ng.plan_text(ohko(), B1).splitlines()
    rooms = [line for line in lines if line.startswith("start_table")]
    assert rooms == ["start_table " + "0100190001" + "0600290000" + "0900000001" + "0a00170000"]
    save = fresh()
    others = [line for line in lines if not line.startswith("start_table")]
    apply_text("\n".join(others), save, bytearray(MENU_SIZE))
    assert struct.unpack_from("<3H", save, sp.SECOND_START) == (2, 134, 15)
    assert save[sp.SECTION + 0x6D2] == 1, "the flag of start room 2"


def test_a_one_start_mode_carries_no_start_table() -> None:
    lines = ng.plan_text(plan(menu=menu_setup(start_table=((1, 25, 1),)))).splitlines()
    assert not any(line.startswith("start_table") for line in lines)


def test_the_randomizer_path_writes_the_second_targets_flag_too() -> None:
    lines = ng.plan_text(ohko(), B1, offer_choice=False).splitlines()
    assert not any(line.startswith(("menu", "start_table")) for line in lines)
    assert f"write {ng.section(sp.SECOND_START):#x} 020086000f0001" in lines


@pytest.mark.parametrize(
    "p, build, why",
    [
        (ohko(), "ffff" + "0" * 60, "no start table measured on this game build"),
        (ohko(table_build=""), B1, "no start table measured on this game build"),
        (ohko(start_table=((1, 25, 1), (6, 41, 0), (9, 1, 1))), B1, "not room 2"),
        (ohko(start_table=((54, 0, 0),)), B1, "up to 64 rooms"),
        (ohko(start_table=((1, 2, 2),)), B1, "up to 64 rooms"),
    ],
)
def test_ohko_needs_a_table_of_this_build_that_lists_its_second_start(p, build, why) -> None:
    with pytest.raises(ng.NewGameError, match=why):
        ng.plan_text(p, build)


@pytest.mark.parametrize(
    "changes, why",
    [
        (dict(menu=menu_setup(seed_text="0123456789abcdef")), "16 hex digits"),
        (dict(menu=menu_setup(seed_text="ABC")), "16 hex digits"),
        (dict(menu=menu_setup(start=bytes(26))), "27 entries"),
        (dict(menu=menu_setup(pool=bytes([2] * 27))), "27 entries"),
        (dict(menu=menu_setup(settings=bytes(63))), "64"),
        (dict(menu=menu_setup(settings=bytes(64))), "setting 42"),
        (
            dict(menu=menu_setup(table_build="6d8a94bd4d64"), second_start=(3, 151, 464)),
            "no start table",
        ),
        (dict(menu=menu_setup(second_target=None)), "no second target"),
        (dict(menu=menu_setup(second_target=(0, 20))), "three 16-bit"),
        (dict(menu=menu_setup(second_target=(0, 20, -1))), "three 16-bit"),
    ],
)
def test_a_menu_setup_that_does_not_fit_is_refused(changes, why) -> None:
    with pytest.raises(ng.NewGameError, match=why):
        ng.plan_text(plan(**changes))


def test_randomizer_steps_name_a_presets_safety() -> None:
    p = plan(options={10: 1, 30: 0, 31: 0, 32: 0, 33: 0, 37: 0, 42: 1, 43: 0, 72: 0, 73: 1})
    assert ng.randomizer_steps(p, "easy") == (
        "pick Easy mode, set Safety Early and change no other options (the seed, optional: "
        "0123 4567 89AB CDEF)"
    )


def test_randomizer_steps_for_custom_name_the_amounts_and_the_checked_settings() -> None:
    options = {10: 1, 30: 1, 31: 0, 32: 1, 33: 0, 37: 0, 42: 0, 43: 1, 72: 1, 73: 1}
    settings = bytearray(64)
    settings[34 - 10] = settings[35 - 10] = 1  # Start Keys 3
    menu = menu_setup(
        start=bytes([0] * 3 + [1] + [1, 1] + [0] * 21),
        pool=bytes([1] * 4 + [0, 1] + [1] * 18 + [0] + [1] * 2),
        settings=bytes(settings),
    )
    p = plan(options=options, amounts=(6, 20, 25, 200, 0, 30), menu=menu)
    steps = ng.randomizer_steps(p, "custom")
    assert steps.startswith(
        "set Custom, first the items (Starting Item: Machine Gun; Random: Navcore, Force "
        "Blaster; Excluded: Power Core; the rest in the Item Pool) and Start Keys 3"
    )
    assert "then Starting Items 6 Energy Tanks, 25 missiles, 0 Mega Bombs" in steps
    assert "Item Pool 20, 200, 30" in steps
    for part in ("Guns First on", "X Crystals on", "X Blocks off", "Abridged Finale on"):
        assert part in steps
    assert steps.endswith("Safety Normal, Gates Open (the seed, optional: 0123 4567 89AB CDEF)")


@pytest.mark.parametrize("text", ["6d03fb8f34d40bca", "6D03FB8F34D40BC", "6D03FB8F34D40BCG"])
def test_a_seed_text_the_game_would_read_otherwise_is_refused(text) -> None:
    with pytest.raises(ValueError):
        sp.seed_hash(text)
    with pytest.raises(sp.PatchError):
        sp.patch(bytes(fresh()), plan(menu=menu_setup(seed_text=text)))
    with pytest.raises(ng.NewGameError):
        ng.plan_text(plan(menu=menu_setup(seed_text=text)), offer_choice=False)


# --- the settings code, for RANDOMIZER's Paste<-Clipboard -----------------------------------------


def preset_modes() -> dict:
    """The presets' menu states and amounts, from worlds/zexion/data.py, loaded by path as
    tools/randocells.py loads logic.py (the world's package needs AP)."""
    root = Path(__file__).resolve().parents[1]
    spec = importlib.util.spec_from_file_location("zexion_data", root / "worlds/zexion/data.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.MODES


def preset_plan(mode: str) -> sp.SeedPlan:
    entry = preset_modes()[mode]
    menu = entry["menu"]
    checked = (10, 30, 31, 32, 33, 37, 42, 43, 72, 73)
    return plan(
        amounts=tuple(entry["amounts"]),
        options={t: menu["settings"][t - ng.MENU_FIRST_SETTING] for t in checked},
        menu=menu_setup(
            start=bytes(menu["start"]), pool=bytes(menu["pool"]), settings=bytes(menu["settings"])
        ),
    )


def paste(text: str) -> tuple[str, str]:
    """The seed text and the code Paste<-Clipboard takes from the clipboard (0x148140): the
    first 16 letters or digits, uppercased, then, with 44 or more characters, up to 34 from
    the one after the 16th."""
    seed, used = "", 0
    while len(seed) < 16 and used < len(text):
        if text[used].isascii() and text[used].isalnum():
            seed += text[used].upper()
        used += 1
    return seed, text[used : used + 34] if len(text) >= 44 else ""


def decode(code: str) -> tuple[bytes, bytes, bytes, tuple[int, ...]]:
    """The settings code read back: settings t10 to t73, the start-with and pool arrays, and
    the six amounts (docs/re/randocells.md, "The settings code")."""
    value = sum(ng.CODE_ALPHABET.index(c) << (5 * i) for i, c in enumerate(code))

    def take(bits: int) -> int:
        nonlocal value
        field, value = value & ((1 << bits) - 1), value >> bits
        return field

    settings = bytes(take(1) for _ in range(64))
    pairs = [(take(1), take(1)) for _ in ng.START_WITH_ORDER]
    amounts = tuple(take(bits) for bits in ng.CODE_AMOUNT_BITS)
    return settings, bytes(s for s, _ in pairs), bytes(p for _, p in pairs), amounts


def test_the_settings_code_is_the_games_for_standard_and_easy() -> None:
    # Standard's menu state read live in the menu; Easy's first 32 characters from its saves'
    # t74 to t105, which drop the last two (docs/re/randocells.md, "The settings code").
    standard, easy = preset_plan("standard"), preset_plan("easy")
    code = ng.settings_code(standard.menu, standard.amounts)
    assert code == "FCCgBLLBBBBBLcJcPcNcNcNmVLFBjFBBQB"
    assert ng.settings_code(easy.menu, easy.amounts)[:32] == "FLCgBLLBBBBBLcHcNcNcNRNRWWFBKJBB"


@pytest.mark.parametrize("mode", ["easy", "standard", "ohko"])
def test_paste_reads_back_the_seed_and_its_menu_state(mode) -> None:
    p = preset_plan(mode)
    text = ng.paste_text(p)
    assert len(text) == 50
    seed, code = paste(text)
    assert seed == p.menu.seed_text
    assert decode(code) == (p.menu.settings, p.menu.start, p.menu.pool, p.amounts)


def test_paste_reads_back_a_custom_state_with_the_largest_amounts() -> None:
    settings = bytes(i % 3 == 0 for i in range(64))
    menu = menu_setup(
        start=bytes(i % 2 for i in range(27)), pool=bytes(i % 5 != 0 for i in range(27)),
        settings=settings,
    )  # fmt: skip
    amounts = (37, 3, 999, 0, 0, 999)
    p = plan(options={}, amounts=amounts, menu=menu)
    assert decode(paste(ng.paste_text(p))[1]) == (settings, menu.start, menu.pool, amounts)


@pytest.mark.parametrize(
    ("amounts", "settings"),
    [((64, 0, 0, 0, 0, 0), bytes(64)), ((0, 0, 1024, 0, 0, 0), bytes(64)), ((0,) * 6, b"\2" * 64)],
)
def test_a_state_the_code_cannot_hold_leaves_the_steps_alone(amounts, settings) -> None:
    p = plan(options={}, amounts=amounts, menu=menu_setup(settings=settings))
    with pytest.raises(ng.NewGameError):
        ng.paste_text(p)
    (line,) = ng.randomizer_message(p, "standard")
    assert line.endswith(
        "change no other options (the seed, optional: 0123 4567 89AB CDEF), and START GAME."
    )


def test_the_randomizer_message_puts_the_paste_text_on_a_line_of_its_own() -> None:
    p = preset_plan("standard")
    first, text, steps = ng.randomizer_message(p, "standard")
    assert "choose RANDOMIZER" in first and "Paste from Clipboard" in first
    assert first.endswith(
        "the seed, which is optional: the new game gets this seed's text whatever the menu shows."
    )
    assert text == "0123456789ABCDEFFCCgBLLBBBBBLcJcPcNcNcNmVLFBjFBBQB"
    assert steps.startswith("Or, without pasting, pick Standard mode, set Safety")
