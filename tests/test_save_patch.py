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
    secret_section,
    slot_data,
)

from client import ids
from client import save_patch as sp


def u16(save: bytes, offset: int) -> int:
    return struct.unpack_from("<H", save, offset)[0]


def test_patch_writes_the_seed() -> None:
    p = plan()
    out = sp.patch(bytes(fresh()), p)
    assert out[sp.ITEM_TABLE : sp.ITEM_TABLE + sp.LOCATIONS] == bytes(p.location_items)
    assert struct.unpack_from("<3H", out, sp.START_RECORD) == p.start
    assert (u16(out, sp.CONTINUE), u16(out, sp.CONTINUE + 2)) == (p.start[2], p.start[1])


def test_only_the_start_cell_is_revealed() -> None:
    out = sp.patch(bytes(fresh((13, 16))), plan())
    grid = out[sp.MAP_GRID : sp.MAP_GRID + ids.MAP_SIZE * ids.MAP_SIZE]
    assert [i for i, v in enumerate(grid) if v] == [22 * ids.MAP_SIZE + 23]
    assert grid[22 * ids.MAP_SIZE + 23] == sp.SAVE_ROOM


def test_inventory_is_the_start_items_and_nothing_else() -> None:
    out = sp.patch(bytes(fresh()), plan())
    inventory = {i: out[sp.INVENTORY + i] for i in range(50) if out[sp.INVENTORY + i]}
    assert inventory == {0: 2, 1: 3, 36: 1, 3: 1}
    assert out[sp.INVENTORY + 50] == 9


def test_current_energy_and_missiles_follow_the_start_items() -> None:
    out = sp.patch(bytes(fresh()), plan())
    assert u16(out, sp.ENERGY_NOW) == 2 * 16
    assert u16(out, sp.MISSILES_NOW) == 3 + 10


def test_second_target_is_written_only_when_there_is_one() -> None:
    save = fresh()
    struct.pack_into("<3H", save, sp.SECOND_START, 1, 2, 3)
    assert struct.unpack_from("<3H", sp.patch(bytes(save), plan()), sp.SECOND_START) == (1, 2, 3)
    out = sp.patch(bytes(save), plan(second_start=(9, 151, 464)))
    assert struct.unpack_from("<3H", out, sp.SECOND_START) == (9, 151, 464)


def test_nothing_outside_the_patched_fields_changes() -> None:
    save = fresh()
    save[0x100] = 0xAB
    save[sp.OPTIONS + 10] = 1
    out = sp.patch(bytes(save), plan())
    assert out[0x100] == 0xAB and out[sp.OPTIONS + 10] == 1
    assert len(out) == len(save)


@pytest.mark.parametrize(
    "spoil, message",
    [
        (lambda s: s.extend(b"\0"), "not a randomizer save"),
        (lambda s: s.__setitem__(sp.RANDO_MARKER, 0), "marker is not set"),
        (lambda s: struct.pack_into("<H", s, sp.PICKUP_COUNTER, 1), "picked up"),
        (lambda s: s.__setitem__(sp.MAP_GRID + 5, 0x1B), "explored"),
        (lambda s: struct.pack_into("<h", s, sp.AMOUNTS, 5), "seed's mode"),
        (lambda s: s.__setitem__(sp.OPTIONS + 72, 2), r"options \[72\]"),
    ],
)
def test_a_save_the_seed_cannot_go_into_is_refused(spoil, message) -> None:
    save = fresh()
    spoil(save)
    with pytest.raises(sp.PatchError, match=message):
        sp.patch(bytes(save), plan())


def test_a_bad_plan_is_refused() -> None:
    with pytest.raises(sp.PatchError, match="153 locations"):
        sp.patch(bytes(fresh()), plan(location_items=(46,) * 153))
    with pytest.raises(sp.PatchError, match=r"\[50\]"):
        sp.patch(bytes(fresh()), plan(start_items={50: 1}))


def test_a_save_without_a_revealed_cell_keeps_none() -> None:
    out = sp.patch(bytes(fresh(None)), plan())
    assert not any(out[sp.MAP_GRID : sp.MAP_GRID + ids.MAP_SIZE * ids.MAP_SIZE])


def test_section_matches_the_patched_seed_only() -> None:
    p = plan()
    out = sp.patch(bytes(fresh()), p)
    section = out[sp.SECTION :]
    assert sp.section_matches(section, p)
    assert not sp.section_matches(section, plan(start=(6, 1, 1)))
    assert not sp.section_matches(section, plan(location_items=(46,) * sp.LOCATIONS))


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
    assert plan().table == bytes(plan().location_items)  # no cells: the game's order already
    # A list that is not the settings' size.
    assert secret_plan(options={42: 2}).check_size()
    assert plan(options={sp.SECRET_SHUFFLE: 1}).check_size()
    assert secret_plan(cells=tuple(CELLS)).check_size()


def test_a_secret_shuffle_section_matches_its_seed_only() -> None:
    p = secret_plan()
    section = secret_section(p)
    assert sp.section_matches(bytes(section), p)
    # The location-index order is not the game's.
    assert not sp.section_matches(bytes(section), secret_plan(cells=tuple(SECRET_LIST)))
    section[sp.OPTIONS_AT + sp.SECRET_SHUFFLE] = 0
    assert not sp.section_matches(bytes(section), p)


def test_a_secret_shuffle_save_gets_its_table_in_the_games_order() -> None:
    save = fresh(cells=SECRET_LIST, secret=True)
    items = [46] * 155
    items[154] = ids.KEYS[0]  # the Alpha Key at the 155th location
    p = secret_plan(location_items=tuple(items))
    out = sp.patch(bytes(save), p)
    assert out[sp.ITEM_TABLE : sp.ITEM_TABLE + 155] == p.table
    x, y = SECRET_LIST[100]
    assert out[sp.KEY_MARKERS : sp.KEY_MARKERS + 2] == bytes((y, x))


def test_plan_from_slot_data() -> None:
    p = sp.plan_from_slot_data(slot_data())
    assert p.menu == sp.MenuSetup(
        "0123456789ABCDEF", bytes(27), bytes([1] * 27), bytes(64), (0, 20, 509)
    )
    assert p.second_start is None
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
    assert p.options == {42: 2}
    assert p.menu.start_table == ((1, 25, 1),) and p.menu.table_build == "6d8a94bd4d64"
    for key in ("second_start", "menu"):
        with pytest.raises(KeyError):
            sp.plan_from_slot_data({k: v for k, v in slot.items() if k != key})


def test_key_markers_follow_the_seed() -> None:
    save = fresh(cells=CELLS)
    save[sp.KEY_MARKERS : sp.KEY_MARKERS + 8] = bytes([1, 2, 3, 4, 5, 6, 7, 8])  # the old seed's
    items = [46] * sp.LOCATIONS
    items[60] = 27  # Alpha here, Beta in another world, Gamma here, Omega owned from the start
    items[100] = 29
    items[120] = 30
    out = sp.patch(bytes(save), plan(location_items=tuple(items), start_items={30: 1}))
    alpha, gamma = CELLS[60], CELLS[100]
    expected = [alpha[1], alpha[0], 0xFF, 0xFF, gamma[1], gamma[0], 0xFF, 0xFF]
    assert list(out[sp.KEY_MARKERS : sp.KEY_MARKERS + 8]) == expected


def test_starting_bombs_are_refused() -> None:
    with pytest.raises(sp.PatchError, match=r"\[2, 37\]"):
        sp.patch(bytes(fresh()), plan(start_items={2: 1, 37: 1}))
