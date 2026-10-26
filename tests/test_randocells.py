"""The RandoCells parser and reachability sweep, on a synthetic grid (no game data)."""

from __future__ import annotations

import struct
from collections import Counter

import pytest

import randocells as rc
from client import save_patch as sp

logic = rc.logic  # worlds/zexion/logic.py, as the tool loads it


def blank() -> bytearray:
    """Every cell walled off, with no lists."""
    data = bytearray(b"\xff" * (rc.W * rc.H * rc.RECORD))
    for i in range(rc.W * rc.H):
        base = i * rc.RECORD
        data[base + 0x20 : base + 0x40] = bytes(32)  # tier bytes default to 0
        data[base + rc.ITEM_FLAG] = 0
        data[base + rc.ITEM_TIERS : base + rc.ITEM_TIERS + 8] = bytes(8)
    return data


def rec(x: int, y: int) -> int:
    return (y * rc.W + x) * rc.RECORD


def open_passage(data, x, y, d, needs=()):
    data[rec(x, y) + rc.PASSAGE_OFFSET[d]] = rc.CONDITIONAL if needs else rc.OPEN
    for k, (code, tier) in enumerate(needs):
        data[rec(x, y) + rc.LIST_OFFSET[d] + k] = code
        data[rec(x, y) + rc.LIST_OFFSET[d] + 0x20 + k] = tier


def test_parse_rejects_wrong_size() -> None:
    with pytest.raises(ValueError):
        rc.parse(b"\x00" * 10)


def test_lists_and_passages_parse() -> None:
    data = blank()
    open_passage(data, 25, 25, "E", [(16, 1), (19, 1)])
    cell = rc.parse(bytes(data))[25][25]
    assert cell.passage["E"] == rc.CONDITIONAL
    assert cell.lists["E"] == [(16, 1), (19, 1)]
    assert cell.passage["W"] == rc.WALL


@pytest.mark.parametrize(
    ("entries", "have", "expected"),
    [
        ([(16, 1), (19, 1)], {19: 1}, True),  # any standalone entry
        ([(16, 2), (19, 2)], {16: 1}, False),  # AND group needs both
        ([(16, 2), (19, 2)], {16: 1, 19: 1}, True),
        ([(20, 2), (1, 43)], {20: 1, 1: 2}, False),  # Phaser and 3 missile containers
        ([(20, 2), (1, 43)], {20: 1, 1: 3}, True),
        ([(1, 22)], {1: 11}, False),  # standalone count: 12 needed
        ([(1, 22)], {1: 12}, True),
        ([(16, 1), (16, 3)], {16: 1}, False),  # tier 3: the item doesn't count here
        ([(60, 1)], {3: 1}, True),  # 60: any of five weapons
        ([(57, 1)], {17: 1}, False),  # 57 via S-Blade needs Slide Mod too
        ([(57, 1)], {16: 1, 17: 1}, True),
        ([(57, 1), (17, 3)], {16: 1, 17: 1}, False),  # ...unless S-Blade is excluded
        ([(54, 1)], {24: 1}, False),  # 54: Gravity Harness with Jump Booster
        ([(54, 1)], {24: 1, 22: 1}, True),
        ([(54, 1), (55, 1)], {24: 1, 22: 1}, False),  # 55: without the Gravity Harness
        ([(50, 1)], {26: 1}, True),  # Zenith Thrusters stands in for jump upgrades
        ([(19, 4), (16, 1)], {16: 1}, False),  # tier 4 is required, whatever else holds
    ],
)
def test_satisfied(entries, have, expected) -> None:
    assert logic.satisfied(entries, Counter(have)) is expected


def test_sweep_collects_in_order_of_access() -> None:
    data = blank()
    # Start (25,25) -> east needs item 16; (26,25) holds item 16 behind nothing, reached
    # only through (25,25) -> north (open) -> east (open) -> south (open).
    open_passage(data, 25, 25, "N")
    open_passage(data, 25, 24, "E")
    open_passage(data, 26, 24, "S")
    open_passage(data, 25, 25, "E", [(16, 1)])
    open_passage(data, 26, 25, "E")  # (27,25) only via (26,25)
    grid = rc.parse(bytes(data))
    locations = [(26, 25), (27, 25)]
    items = [16, 46]
    got = rc.sweep(grid, locations, items, (25, 25))
    assert got == {0, 1}


def test_item_requirements_gate_collection() -> None:
    data = blank()
    open_passage(data, 25, 25, "E")
    data[rec(26, 25) + rc.ITEM_FLAG] = 1
    data[rec(26, 25) + rc.ITEM_LIST] = 24
    data[rec(26, 25) + rc.ITEM_TIERS] = 1
    grid = rc.parse(bytes(data))
    assert rc.sweep(grid, [(26, 25)], [46], (25, 25)) == set()


def test_weapon_pairs_are_progressive() -> None:
    have = Counter()
    rc.collect(have, 9)  # Pulse Wave first: counts as the Machine Gun
    assert have == Counter({3: 1})
    rc.collect(have, 3)  # then a Machine Gun: counts as the Pulse Wave
    assert have == Counter({3: 1, 9: 1})
    rc.collect(have, 24)
    assert have[24] == 1


def at(cell: tuple[int, int], *reached: tuple[int, int]) -> dict:
    """logic.satisfied's arguments for a check made in ``cell``, with these cells reached."""
    return {"cell": cell, "reached": {cell, *reached}.__contains__}


HERE = at((10, 10))


def test_neighbor_codes_look_at_reached_cells() -> None:
    assert logic.satisfied([(64, 1)], Counter(), **at((10, 10), (9, 10))) is True  # west reached
    assert logic.satisfied([(64, 1)], Counter(), **HERE) is False
    # Decided first and outright: an item that would pass does not help.
    assert logic.satisfied([(16, 1), (64, 1)], Counter({16: 1}), **HERE) is False
    # Off the map edge the code does not apply, and the rest of the list decides.
    assert logic.satisfied([(16, 1), (64, 1)], Counter({16: 1}), **at((0, 10))) is True


def test_code_68_follows_the_options() -> None:
    on = logic.Settings(options=bytes(30) + b"\x01" + bytes(33))
    assert logic.satisfied([(68, 30)], Counter(), on, **HERE) is True
    # An option that is off does not decide; the other entries do.
    assert logic.satisfied([(68, 30), (16, 1)], Counter({16: 1}), logic.Settings(), **HERE)
    assert logic.satisfied([(68, 30)], Counter(), logic.Settings(), **HERE) is False


@pytest.mark.parametrize(
    ("mode", "tier", "energy", "total", "expected"),
    [
        (0, 11, 0, 20, True),  # modes 0 and 3: always passes
        (2, 11, 10, 20, True),  # tier 11: energy above 9
        (2, 11, 9, 20, False),
        (2, 14, 25, 20, False),  # tier 14 in mode 2: above 29
        (1, 14, 25, 20, True),  # ...in mode 1: above 19
        (2, 11, 5, 10, True),  # a smaller pool scales energy up (5 x 20 / 10 = 10)
        (2, 11, 15, 40, True),  # a larger pool does not scale it down here
    ],
)
def test_code_69_checks_energy(mode, tier, energy, total, expected) -> None:
    settings = logic.Settings(energy_total=total, mode69=mode)
    have = Counter({0: energy})
    assert logic.satisfied([(69, tier)], have, settings, **HERE) is expected


def test_combat_tier_scales_energy_to_the_pool() -> None:
    have = [0] * 64
    have[0], have[3], have[13] = 12, 1, 1  # 12 Energy, Machine Gun, Power Cartridge
    assert logic.combat_tier(have, logic.Settings(energy_total=20)) == 3  # 12 > 10
    assert logic.combat_tier(have, logic.Settings(energy_total=40)) == 1  # 12 x 20 / 40 = 6


def test_starts_come_from_the_start_record_and_the_second_target() -> None:
    save = bytearray(0x13200)
    struct.pack_into("<3H", save, sp.START_RECORD, 6, 191, 329)  # cell (13, 16)
    struct.pack_into("<3H", save, sp.SECOND_START, 3, 151, 464)  # cell (10, 23)
    assert rc.start_from_save(bytes(save)) == (13, 16)
    assert rc.starts_from_save(bytes(save)) == [(13, 16)]
    save[sp.OPTIONS + rc.DOUBLE_TELEPORT] = 1
    assert rc.starts_from_save(bytes(save)) == [(13, 16), (10, 23)]
