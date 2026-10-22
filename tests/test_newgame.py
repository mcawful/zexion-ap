"""client/newgame.py: the AP seed as section edits for a new game (no game)."""

from __future__ import annotations

import struct

import pytest
from test_save_patch import fresh, plan

import newgame as ng
import save_patch as sp


def apply_text(text: str, save: bytearray) -> list[str]:
    """Apply an ap_newgame request to a save's section, as the helper does; the failed
    expectations, if any."""
    lines = text.splitlines()
    assert lines[0] == "ap_newgame"
    failed = []
    for line in lines[1:]:
        op, offset, data = line.split()
        at = sp.SECTION + int(offset, 0)
        raw = bytes.fromhex(data)
        if op == "expect" and save[at : at + len(raw)] != raw:
            failed.append(offset)
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
    assert apply_text(ng.plan_text(p), save) == []
    patched = sp.patch(bytes(fresh()), p)
    for start, size in ((sp.ITEM_TABLE, sp.LOCATIONS), (sp.START_RECORD, 6), (sp.SECOND_START, 6)):
        assert save[start : start + size] == patched[start : start + size]


def test_starting_items_become_the_array_amounts_and_keys() -> None:
    array, start = ng.start_with({0: 3, 1: 3, 36: 1, 3: 1, 13: 1, 27: 1, 28: 1, 2: 2})
    assert {ng.START_WITH_ORDER[i] for i, v in enumerate(array) if v} == {3, 13}
    assert start == {"energy": 3, "missiles": 13, "bombs": 2, "keys": 2}


def test_the_amounts_and_key_bits_are_written() -> None:
    save = fresh()
    apply_text(ng.plan_text(plan(start_items={0: 5, 1: 3, 27: 1, 28: 1, 29: 1})), save)
    assert struct.unpack_from("<6h", save, sp.AMOUNTS)[0::2] == (5, 3, 0)
    assert list(save[sp.OPTIONS + 34 : sp.OPTIONS + 37]) == [1, 1, 0]


def test_another_mode_fails_the_expectations() -> None:
    save = fresh()
    struct.pack_into("<h", save, sp.AMOUNTS + 2, 35)  # another pool of Energy
    save[sp.OPTIONS + 72] = 2
    before = bytes(save)
    assert apply_text(ng.plan_text(plan()), save) != []
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
