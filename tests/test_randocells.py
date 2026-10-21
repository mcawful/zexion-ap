"""The RandoCells parser and reachability sweep, on a synthetic grid (no game data)."""

from __future__ import annotations

from collections import Counter

import pytest

import randocells as rc


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
        ([(55, 1)], {}, True),  # specials resolved by the callback (here: always)
        ([(57, 1), (17, 3)], {17: 1}, True),  # tier 3 excludes S-Blade; 57 still passes
        ([(16, 1), (16, 3)], {16: 1}, False),  # an excluded item can't satisfy the check
    ],
)
def test_satisfied(entries, have, expected) -> None:
    assert rc.satisfied(entries, Counter(have), lambda c: True) is expected


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
    got = rc.sweep(grid, locations, items, lambda c: False)
    assert got == {0, 1}


def test_item_requirements_gate_collection() -> None:
    data = blank()
    open_passage(data, 25, 25, "E")
    data[rec(26, 25) + rc.ITEM_FLAG] = 1
    data[rec(26, 25) + rc.ITEM_LIST] = 24
    data[rec(26, 25) + rc.ITEM_TIERS] = 1
    grid = rc.parse(bytes(data))
    assert rc.sweep(grid, [(26, 25)], [46], lambda c: False) == set()
