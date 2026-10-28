"""client/reconcile.py on synthetic inventories (no game)."""

from __future__ import annotations

from collections import Counter

import pytest

from client import ids
from client import reconcile as rc

CELLS = [(25, 25), (29, 24), (3, 40), (50, 1)]
TABLE = [3, 0, 46, 27]  # Machine Gun, Energy Tank, Scrap (a remote item), Alpha Key
START = {0: 3, 1: 3, 4: 1}
STARTED = dict(i0=3, i1=3, i4=1)  # START in the inventory


def inv(**items: int) -> bytearray:
    out = bytearray(len(ids.GAME_ITEMS))
    for name, n in items.items():
        out[int(name[1:])] = n
    return out


def grid(*collected: tuple[int, int]) -> bytes:
    g = bytearray(ids.MAP_SIZE * ids.MAP_SIZE)
    g[24 * ids.MAP_SIZE + 29] = 0x1A  # about to be collected: not yet
    for x, y in collected:
        g[y * ids.MAP_SIZE + x] = ids.COLLECTED
    return bytes(g)


def test_collected_locations_reads_only_collected_cells() -> None:
    assert rc.collected_locations(grid((25, 25), (50, 1)), CELLS) == {0, 3}


def test_nothing_is_given_when_the_game_is_up_to_date() -> None:
    target = rc.should_own(START, TABLE, {0, 1}, [])
    g = rc.grants(inv(i0=4, i1=3, i3=1, i4=1), target)
    assert g.inventory == {} and g.not_grantable == {}


def test_received_items_are_given_once() -> None:
    have = inv(**STARTED)
    target = rc.should_own(START, TABLE, set(), [17, 0])
    g = rc.grants(have, target)
    assert g.inventory == {17: 1, 0: 4}
    for item, value in g.inventory.items():
        have[item] = value
    assert rc.grants(have, target).inventory == {}


@pytest.mark.parametrize(
    ("collected", "received", "extra", "owed"),
    [
        # The game handed out location 1's Energy Tank: inventory and map agree.
        pytest.param({1}, [], dict(i0=4), {}, id="a-native-pickup-is-not-given-again"),
        pytest.param(set(), [18], {}, {18: 1}, id="a-received-item-lost-to-a-death-again"),
        pytest.param(set(), [], dict(i0=9, i22=1), {}, id="a-surplus-is-left-alone"),
        pytest.param(set(), [5], {}, {5: 1}, id="a-progressive-weapon-fills-the-weapon"),
        pytest.param(set(), [5, 5], dict(i5=1), {15: 1}, id="then-the-upgrade"),
        # The starting Force Blaster plus a received one is the Pentashot (14).
        pytest.param(set(), [4], {}, {14: 1}, id="a-starting-weapon-received-again"),
        # Location 0 holds the Machine Gun; the player already had it from a remote copy, so
        # the game gave the Pulse Wave (9). Nothing is owed.
        pytest.param({0}, [3], dict(i3=1, i9=1), {}, id="a-native-pickup-become-the-upgrade"),
        # If a second pickup in play raised the weapon byte instead of setting the upgrade.
        pytest.param({0}, [3], dict(i3=2), {9: 1}, id="a-weapon-byte-of-two-owes-the-upgrade"),
    ],
)
def test_what_the_inventory_lacks_is_owed(collected, received, extra, owed) -> None:
    target = rc.should_own(START, TABLE, collected, received)
    assert rc.grants(inv(**{**STARTED, **extra}), target).inventory == owed


def test_a_third_copy_of_a_weapon_is_reported() -> None:
    target = rc.should_own(START, TABLE, set(), [3, 3, 3])
    g = rc.grants(inv(**STARTED), target)
    assert g.inventory == {3: 1, 9: 1}
    assert g.not_grantable == {3: 1}


def test_items_that_cannot_be_written_are_reported() -> None:
    target = rc.should_own(START, TABLE, set(), [31])
    g = rc.grants(inv(**STARTED), target)
    assert g.inventory == {} and g.not_grantable == {31: 1}


def test_a_received_energy_shield_is_given_as_scrap_metal() -> None:
    target = rc.should_own(START, TABLE, set(), [50, 50])
    g = rc.grants(inv(**STARTED), target)
    assert g.inventory == {46: 2} and g.not_grantable == {}


def test_shields_owed_are_the_received_ones_not_yet_given() -> None:
    assert rc.shields_owed([50, 3, 50, 50], 1) == 2
    assert rc.shields_owed([50], 3) == 0
    assert rc.shields_owed([], 0) == 0


def test_should_own_counts_start_and_upgrade_codes_under_the_weapon() -> None:
    assert rc.should_own({9: 1}, TABLE, set(), [3]) == Counter({3: 2})


# Guns First off (setting t10 0): each upgrade is an item, and a byte, of its own.
@pytest.mark.parametrize(
    ("collected", "received", "extra", "owed"),
    [
        pytest.param(set(), [9], {}, {9: 1}, id="an-upgrade-without-its-weapon-is-itself"),
        pytest.param(set(), [14], {}, {14: 1}, id="the-starting-weapons-upgrade"),
        pytest.param(set(), [5, 15], {}, {5: 1, 15: 1}, id="a-weapon-and-its-upgrade"),
        # Location 0 holds the Machine Gun; the game handed it out as itself.
        pytest.param({0}, [], dict(i3=1), {}, id="a-native-pickup-is-not-given-again"),
        pytest.param({0}, [9], dict(i3=1, i9=1), {}, id="an-upgrade-held-is-not-given-again"),
    ],
)
def test_without_guns_first_every_item_is_itself(collected, received, extra, owed) -> None:
    target = rc.should_own(START, TABLE, collected, received, guns_first=False)
    have = inv(**{**STARTED, **extra})
    assert rc.grants(have, target, guns_first=False).inventory == owed


def test_without_guns_first_no_gun_or_upgrade_byte_above_one() -> None:
    # A second Machine Gun (a start_inventory or console copy) is not written as a byte of 2.
    target = rc.should_own(START, TABLE, {0}, [3, 9, 9], guns_first=False)
    assert rc.grants(inv(**STARTED), target, guns_first=False).inventory == {3: 1, 9: 1}
    # A second pickup that the menu's Guns First turned into the upgrade: nothing is owed.
    assert rc.grants(inv(**STARTED, i3=1, i9=1), target, guns_first=False).inventory == {}


def test_without_guns_first_an_upgrade_does_not_stand_for_its_weapon() -> None:
    # With Guns First, a held Pulse Wave would count as the Machine Gun owed here.
    target = rc.should_own({}, TABLE, set(), [3], guns_first=False)
    assert rc.grants(inv(i9=1), target, guns_first=False).inventory == {3: 1}
    assert rc.should_own({9: 1}, TABLE, set(), [3], guns_first=False) == Counter({3: 1, 9: 1})


BEFORE = rc.Supplies(energy=30, energy_max=48, missiles=2, missiles_max=3, bombs=0, bombs_max=0)


def test_supplies_follow_the_pickup_routine() -> None:
    have = inv(i0=3, i1=3)
    after = rc.supplies_after(have, {0: 4, 1: 4, 36: 1, 2: 1}, BEFORE)
    assert (after.energy, after.energy_max) == (64, 64)
    assert (after.missiles, after.missiles_max) == (2 + 11, 14)
    assert (after.bombs, after.bombs_max) == (1, 1)


def test_supplies_unchanged_by_other_items() -> None:
    assert rc.supplies_after(inv(i0=3, i1=3), {17: 1}, BEFORE) == BEFORE


def test_bomb_boxes_count_ten() -> None:
    after = rc.supplies_after(inv(i0=3, i1=3), {37: 1}, BEFORE)
    assert (after.bombs, after.bombs_max) == (10, 10)


def test_scrap_pickups_count_as_scrap_metal() -> None:
    # Three scrap pickups in play raised byte 46 to 3; two received scrap items are owed on top.
    target = rc.should_own(START, [47, 49, 48, 27], {0, 1, 2}, [47, 49])
    g = rc.grants(inv(**STARTED, i46=3), target)
    assert g.inventory == {46: 5} and g.not_grantable == {}


def test_a_stand_in_pickup_does_not_swallow_a_received_scrap_metal() -> None:
    # Another player's item (the stand-in, 47) picked up raised byte 46; a Scrap Metal received
    # afterwards is still owed.
    target = rc.should_own(START, [47], {0}, [46])
    assert rc.grants(inv(**STARTED, i46=1), target).inventory == {46: 2}


def test_a_data_disc_is_never_put_in_the_inventory() -> None:
    # Counted by the client instead (client/discs.py): received, or the secret boss's own.
    target = rc.should_own(START, TABLE, set(), [44, 44])
    assert target[44] == 0
    g = rc.grants(inv(**STARTED), target)
    assert 44 not in g.inventory and 44 not in g.not_grantable
    g = rc.grants(inv(**STARTED, i44=1), target)
    assert 44 not in g.inventory, "the secret boss's disc is left alone"
