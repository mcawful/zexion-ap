"""client/save_patch.py on synthetic saves (no game, no real save)."""

from __future__ import annotations

import struct

import pytest
from fakes import (
    CELLS,
    SECRET_CELLS,
    SECRET_LIST,
    fresh,
    plan,
    secret_plan,
    section,
    slot_data,
)

from client import save_patch as sp


@pytest.mark.parametrize(
    "spoil, message",
    [
        (lambda s: s.extend(b"\0"), "not a randomizer save"),
        (lambda s: s.__setitem__(sp.RANDO_MARKER, 0), "marker is not set"),
        (lambda s: struct.pack_into("<H", s, sp.PICKUP_COUNTER, 1), "picked up"),
        (lambda s: s.__setitem__(sp.MAP_GRID + 5, 0x1B), "explored"),
    ],
)
def test_a_save_that_is_not_fresh_is_refused(spoil, message) -> None:
    save = fresh()
    sp.check_fresh(bytes(save))
    spoil(save)
    with pytest.raises(sp.PatchError, match=message):
        sp.check_fresh(bytes(save))


def test_a_save_without_a_revealed_cell_is_fresh() -> None:
    sp.check_fresh(bytes(fresh(None)))  # a fresh OHKO save reveals none


def test_section_matches_its_seed_only() -> None:
    p = plan()
    loaded = section(p)
    assert sp.section_matches(loaded, p)
    assert not sp.section_matches(loaded, plan(start=(6, 1, 1)))
    assert not sp.section_matches(loaded, plan(location_items=(46,) * sp.LOCATIONS))


def test_the_games_order_is_grid_order() -> None:
    # The 154 are in it already; Secret Shuffle's 155th, listed last, goes to its place.
    assert sp.game_order(CELLS) == tuple(range(sp.LOCATIONS))
    order = sp.game_order(SECRET_CELLS)
    assert order.index(154) == 100 and [SECRET_CELLS[i] for i in order] == SECRET_LIST


def test_a_secret_shuffle_plans_table_is_in_the_games_order() -> None:
    p = secret_plan()
    assert p.list_size == 155 and p.check_size() == ""
    assert p.table[100] == p.location_items[154] and p.table[101] == p.location_items[100]
    assert p.table[:100] == bytes(p.location_items[:100])
    assert plan().table == bytes(plan().location_items)  # the 154 alone: the game's order already
    # A list that is not the settings' size.
    assert secret_plan(options={42: 2}).check_size()
    assert plan(options={sp.SECRET_SHUFFLE: 1}).check_size()
    assert secret_plan(cells=tuple(CELLS)).check_size()


def test_a_secret_shuffle_section_matches_its_seed_only() -> None:
    p = secret_plan()
    loaded = section(p)
    assert sp.section_matches(bytes(loaded), p)
    # The location-index order is not the game's.
    assert not sp.section_matches(bytes(loaded), secret_plan(cells=tuple(SECRET_LIST)))
    loaded[sp.OPTIONS_AT + sp.SECRET_SHUFFLE] = 0
    assert not sp.section_matches(bytes(loaded), p)


def test_plan_from_slot_data() -> None:
    p = sp.plan_from_slot_data(slot_data())
    settings = bytes([1] + [0] * 31 + [2] + [0] * 31)
    assert p.menu == sp.MenuSetup(
        "0123456789ABCDEF", bytes(27), bytes([1] * 27), settings, (0, 20, 509)
    )
    assert p.second_start is None and p.cells == tuple(CELLS) and p.guns_first
    menu = {**slot_data()["menu"], "second_target": None}
    slot = slot_data(
        start_items={"0": 3, "3": 1},
        second_start={"index": 3, "column": 151, "row": 464},
        menu={**menu, "start_table": [[1, 25, 1]], "table_build": "6d8a94bd4d64"},
    )
    p = sp.plan_from_slot_data(slot)
    assert p.start_items == {0: 3, 3: 1}
    assert p.start == (5, 326, 449)
    assert p.second_start == (3, 151, 464)
    assert p.options == {10: 1, 42: 2}
    assert p.menu.start_table == ((1, 25, 1),) and p.menu.table_build == "6d8a94bd4d64"
    for key in ("second_start", "menu", "locations"):
        with pytest.raises(KeyError):
            sp.plan_from_slot_data({k: v for k, v in slot.items() if k != key})
    with pytest.raises(KeyError, match="Guns First"):  # a world from before Custom mode
        sp.plan_from_slot_data(slot_data(options={"42": 2}))
