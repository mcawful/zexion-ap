"""client/newgame.py: the AP seed as section edits for a new game (no game)."""

from __future__ import annotations

import struct

import pytest
from fakes import fresh, menu_setup, plan

from client import newgame as ng
from client import save_patch as sp

MENU_SIZE = 0x114  # the randomizer menu's state (native/src/game_calls.h, kMenuSize)


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
    for start, size in ((sp.ITEM_TABLE, sp.LOCATIONS), (sp.START_RECORD, 6), (sp.SECOND_START, 6)):
        assert save[start : start + size] == patched[start : start + size]


def test_starting_items_become_the_array_amounts_and_keys() -> None:
    array, start = ng.start_with({0: 3, 1: 3, 36: 1, 3: 1, 13: 1, 27: 1, 28: 1, 2: 2})
    assert {ng.START_WITH_ORDER[i] for i, v in enumerate(array) if v} == {3, 13}
    assert start == {"energy": 3, "missiles": 13, "bombs": 2, "keys": 2}


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


def test_the_three_modes_fresh_saves_express_their_own_starts() -> None:
    # Easy's start: 5 tanks, 3 containers, Machine Gun, Power Cartridge, Navcore.
    array, start = ng.start_with({0: 5, 1: 3, 3: 1, 13: 1, 32: 1})
    assert start == {"energy": 5, "missiles": 3, "bombs": 0, "keys": 0}
    assert sum(array) == 3


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


def test_the_slot_datas_menu_setup_is_read() -> None:
    slot = {
        "location_items": [0] * sp.LOCATIONS,
        "start_items": {"4": 1},
        "start_index": 4,
        "start_column": 148,
        "start_row": 869,
        "second_start": None,
        "amounts": [3, 17, 3, 122, 0, 12],
        "options": {"72": 0},
        "menu": {
            "seed_text": "00000000DEADBEEF",
            "start": [0] * 27,
            "pool": [1] * 27,
            "settings": [0] * 64,
            "second_target": [0, 20, 509],
        },
    }
    p = sp.plan_from_slot_data(slot)
    assert p.menu == sp.MenuSetup(
        "00000000DEADBEEF", bytes(27), bytes([1] * 27), bytes(64), (0, 20, 509)
    )
