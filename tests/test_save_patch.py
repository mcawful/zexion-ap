"""client/save_patch.py on synthetic saves (no game, no real save)."""

from __future__ import annotations

import struct

import pytest

import save_patch as sp

AMOUNTS = (3, 17, 3, 122, 0, 12)  # Standard's, in save order


def fresh(start_cell: tuple[int, int] | None = (13, 16)) -> bytearray:
    """A fresh randomizer save as far as the patcher looks: the mode's amounts, a few
    settings, one revealed start cell, a nonzero item table and inventory."""
    save = bytearray(sp.SAVE_SIZE)
    struct.pack_into("<6h", save, sp.AMOUNTS, *AMOUNTS)
    save[sp.OPTIONS + 42] = 2  # Safety Normal
    save[sp.OPTIONS + 72] = 1  # Gates Vanilla
    save[sp.ITEM_TABLE : sp.ITEM_TABLE + sp.LOCATIONS] = bytes([7] * sp.LOCATIONS)
    save[sp.INVENTORY + 0] = 3
    save[sp.INVENTORY + 50] = 9  # item 50 is left alone
    if start_cell is not None:
        x, y = start_cell
        save[sp.MAP_GRID + y * sp.MAP_W + x] = sp.SAVE_ROOM
    return save


def plan(**changes) -> sp.SeedPlan:
    fields = dict(
        location_items=tuple(i % 46 for i in range(sp.LOCATIONS)),
        start_items={0: 2, 1: 3, 36: 1, 3: 1},
        start=(5, 23 * 14 + 4, 22 * 20 + 9),
        second_start=None,
        amounts=AMOUNTS,
        options={42: 2, 72: 1},
    )
    fields.update(changes)
    return sp.SeedPlan(**fields)


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
    grid = out[sp.MAP_GRID : sp.MAP_GRID + sp.MAP_W * sp.MAP_W]
    assert [i for i, v in enumerate(grid) if v] == [22 * sp.MAP_W + 23]
    assert grid[22 * sp.MAP_W + 23] == sp.SAVE_ROOM


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
    assert not any(out[sp.MAP_GRID : sp.MAP_GRID + sp.MAP_W * sp.MAP_W])


def test_section_matches_the_patched_seed_only() -> None:
    p = plan()
    out = sp.patch(bytes(fresh()), p)
    section = out[sp.SECTION :]
    assert sp.section_matches(section, p)
    assert not sp.section_matches(section, plan(start=(6, 1, 1)))
    assert not sp.section_matches(section, plan(location_items=(46,) * sp.LOCATIONS))


def test_plan_from_slot_data() -> None:
    slot = {
        "location_items": [46] * sp.LOCATIONS,
        "start_items": {"0": 3, "3": 1},
        "start_index": 5,
        "start_column": 326,
        "start_row": 449,
        "second_start": {"index": 3, "column": 151, "row": 464, "cell": [10, 23]},
        "amounts": list(AMOUNTS),
        "options": {"42": 2},
    }
    p = sp.plan_from_slot_data(slot)
    assert p.start_items == {0: 3, 3: 1}
    assert p.start == (5, 326, 449)
    assert p.second_start == (3, 151, 464)
    assert p.options == {42: 2}
    assert sp.plan_from_slot_data({**slot, "second_start": None}).second_start is None


def test_starting_bombs_are_refused() -> None:
    with pytest.raises(sp.PatchError, match=r"\[2, 37\]"):
        sp.patch(bytes(fresh()), plan(start_items={2: 1, 37: 1}))
