"""The RandoCells parser and reachability sweep, on a synthetic grid (no game data)."""

from __future__ import annotations

import importlib.util
import struct
from collections import Counter

import pytest

import randocells as rc
from client import save_patch as sp

logic = rc.logic  # worlds/zexion/logic.py, as the tool loads it


def _load_data():
    """worlds/zexion/data.py (generated from B1's RandoCells), loaded by path: literals only."""
    spec = importlib.util.spec_from_file_location("zexion_data", rc.ROOT / "worlds/zexion/data.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


world_data = _load_data()


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


# B1's six lists with a tier-4 entry and another way to pass (worlds/zexion/data.py): the tier-4
# item alone opens none of them, as in the game's evaluator; each passes only by a tier-1 entry,
# a count or its group (docs/re/randocells.md, "Entry values").
B1_TIER_4 = [
    # X24 Y22 to X24 Y21: code 57 (here the Lance), and the Gravity Harness or the Wall Jump Boots.
    (world_data.ENTRANCES[(118, 137)][0][0], {6: 1}, False),
    (world_data.ENTRANCES[(118, 137)][0][0], {6: 1, 19: 1}, True),
    (world_data.ENTRANCES[(118, 137)][0][0], {6: 1, 24: 1}, True),
    (world_data.ENTRANCES[(118, 137)][0][0], {19: 1, 24: 1}, False),  # no code 57
    # Location 65: the Jump Booster, and 12 Missile Containers or the Arc Plasma.
    (world_data.LOCATION_RULES[65], {22: 1}, False),
    (world_data.LOCATION_RULES[65], {22: 1, 1: 12}, True),
    (world_data.LOCATION_RULES[65], {22: 1, 7: 1}, True),
    # Location 77: the Slide Mod, and code 57 or the Javelin Missiles.
    (world_data.LOCATION_RULES[77], {16: 1}, False),
    (world_data.LOCATION_RULES[77], {16: 1, 23: 1}, True),
    (world_data.LOCATION_RULES[77], {16: 1, 17: 1}, True),  # the S-Blade, with the Slide Mod: 57
    # Regions 72 to 171: the Slide Mod, and one of 52, 54, 51 or the group.
    (world_data.ENTRANCES[(72, 171)][0][0], {16: 1}, False),
    (world_data.ENTRANCES[(72, 171)][0][0], {16: 1, 19: 1, 25: 1, 0: 6}, True),
    # Regions 82 and 84, each way: the Slide Mod, and the Tunneler, the Plasma Burst or (one
    # way) the Lance with the S-Blade.
    (world_data.ENTRANCES[(82, 84)][0][0], {16: 1}, False),
    (world_data.ENTRANCES[(82, 84)][0][0], {16: 1, 6: 1}, False),
    (world_data.ENTRANCES[(82, 84)][0][0], {16: 1, 6: 1, 17: 1}, True),
    (world_data.ENTRANCES[(84, 82)][0][0], {16: 1}, False),
    (world_data.ENTRANCES[(84, 82)][0][0], {16: 1, 5: 1, 15: 1}, True),
]


def test_b1_lists_with_a_tier_4_entry_are_listed() -> None:
    lists = [w[0] for ways in world_data.ENTRANCES.values() for w in ways if w]
    lists += world_data.LOCATION_RULES.values()
    with_tier_4 = {tuple(e) for e in lists if any(t == 4 for _, t in e)}
    assert with_tier_4 == {tuple(entries) for entries, _, _ in B1_TIER_4}


@pytest.mark.parametrize(("entries", "have", "expected"), B1_TIER_4)
def test_b1_tier_4_lists(entries, have, expected) -> None:
    assert logic.satisfied(entries, Counter(have)) is expected


def test_sweep_collects_in_order_of_access() -> None:
    data = blank()
    # Start (25,25): north (open) to (25,24), which holds the item; east, the only way to
    # (26,25)'s location, needs it.
    open_passage(data, 25, 25, "N")
    open_passage(data, 25, 25, "E", [(16, 1)])
    grid = rc.parse(bytes(data))
    locations = [(25, 24), (26, 25)]
    assert rc.sweep(grid, locations, [16, 46], (25, 25)) == {0, 1}
    assert rc.sweep(grid, locations, [46, 16], (25, 25)) == {0}  # the item behind its own way


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
    rc.logic.collect(have, 9)  # Pulse Wave first: counts as the Machine Gun
    assert have == Counter({3: 1})
    rc.logic.collect(have, 3)  # then a Machine Gun: counts as the Pulse Wave
    assert have == Counter({3: 1, 9: 1})
    rc.logic.collect(have, 24)
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


def test_second_target_reads_its_flag() -> None:
    save = bytearray(0x13200)
    struct.pack_into("<3H", save, sp.START_RECORD, 6, 191, 329)
    struct.pack_into("<3H", save, sp.SECOND_START, 0, 20, 509)  # Exodia, cell (1, 25)
    assert rc.second_from_save(bytes(save)) is None
    save[sp.OPTIONS + 33] = 1  # Double Teleport, t33
    assert rc.second_from_save(bytes(save)) == ((1, 25), False)
    save[sp.SECTION + 0x6D2] = 1  # the second target's room flag
    assert rc.second_from_save(bytes(save)) == ((1, 25), True)


def test_a_custom_saves_seed_and_settings() -> None:
    save = bytearray(0x13200)
    locations, items = rc.seed_from_save(bytes(save))
    assert (len(locations), len(items)) == (154, 154)
    save[sp.OPTIONS + 37] = 1  # Secret Shuffle: the 155th location
    locations, items = rc.seed_from_save(bytes(save))
    assert (len(locations), len(items)) == (155, 155)
    struct.pack_into("<hh", save, sp.SECTION + 0x6C0, 3, 17)  # starting and pool Energy
    assert rc.settings_from_save(bytes(save)).energy_total == 20


def test_the_sweep_follows_guns_first() -> None:
    # The Pulse Wave, picked up without the Machine Gun, counts as it with Guns First only;
    # the second location needs the Machine Gun.
    data = blank()
    open_passage(data, 25, 25, "E", [(3, 1)])
    grid = rc.parse(bytes(data))
    locations, items = [(25, 25), (26, 25)], [9, 46]
    on = logic.Settings(guns_first=True)
    off = logic.Settings(guns_first=False)
    assert rc.sweep(grid, locations, items, (25, 25), settings=on) == {0, 1}
    assert rc.sweep(grid, locations, items, (25, 25), settings=off) == {0}


def test_a_flagged_second_target_waits_for_the_score() -> None:
    # A location at the second target only: reached from a flagged one once the useful-item
    # score is 27, from an unflagged one at once.
    grid = rc.parse(bytes(blank()))
    locations, items = [(40, 40)], [46]
    second = (40, 40)
    assert rc.sweep(grid, locations, items, (10, 10), {0: 26}, second=(second, True)) == set()
    assert rc.sweep(grid, locations, items, (10, 10), {0: 27}, second=(second, True)) == {0}
    assert rc.sweep(grid, locations, items, (10, 10), {0: 14, 25: 1}, second=(second, True)) == {0}
    assert rc.sweep(grid, locations, items, (10, 10), {}, second=(second, False)) == {0}


def test_gates_open_travels_between_the_gate_cells() -> None:
    data = blank()
    open_passage(data, 11, 30, "E")  # the start (11, 30) leads to the gate cell (12, 30)
    grid = rc.parse(bytes(data))
    shut = logic.Settings()
    gates = logic.Settings(gates_open=True)
    assert rc.reachable_cells(grid, Counter(), shut, (11, 30)) == {(11, 30), (12, 30)}
    got = rc.reachable_cells(grid, Counter(), gates, (11, 30))
    assert got == {(11, 30), (12, 30), (24, 44), (14, 2)}
    # (51, 34) opens the travel but is no target; nothing is reached without a gate cell.
    assert rc.reachable_cells(grid, Counter(), gates, (51, 34)) == {(51, 34), *logic.GATE_TARGETS}
    assert rc.reachable_cells(grid, Counter(), gates, (10, 10)) == {(10, 10)}


def test_gates_open_is_both_bits() -> None:
    save = bytearray(0x13200)
    save[sp.OPTIONS + 72] = 1
    assert not rc.settings_from_save(bytes(save)).gates_open
    save[sp.OPTIONS + 73] = 1
    assert rc.settings_from_save(bytes(save)).gates_open
