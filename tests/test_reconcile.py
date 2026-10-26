"""client/reconcile.py on synthetic inventories (no game)."""

from __future__ import annotations

from collections import Counter

import ids
import reconcile as rc

CELLS = [(25, 25), (29, 24), (3, 40), (50, 1)]
TABLE = [3, 0, 46, 27]  # Machine Gun, Energy Tank, Scrap (a remote item), Alpha Key
START = {0: 3, 1: 3, 4: 1}


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
    have = inv(i0=3, i1=3, i4=1)
    target = rc.should_own(START, TABLE, set(), [17, 0])
    g = rc.grants(have, target)
    assert g.inventory == {17: 1, 0: 4}
    for item, value in g.inventory.items():
        have[item] = value
    assert rc.grants(have, target).inventory == {}


def test_a_native_pickup_is_not_given_again() -> None:
    # The game handed out location 1's Energy Tank: inventory and map agree.
    target = rc.should_own(START, TABLE, {1}, [])
    assert rc.grants(inv(i0=4, i1=3, i4=1), target).inventory == {}


def test_a_received_item_lost_to_a_death_is_given_again() -> None:
    target = rc.should_own(START, TABLE, set(), [18])
    assert rc.grants(inv(i0=3, i1=3, i4=1), target).inventory == {18: 1}


def test_a_surplus_is_left_alone() -> None:
    target = rc.should_own(START, TABLE, set(), [])
    assert rc.grants(inv(i0=9, i1=3, i4=1, i22=1), target).inventory == {}


def test_progressive_weapons_fill_the_weapon_then_the_upgrade() -> None:
    target = rc.should_own(START, TABLE, set(), [5])
    assert rc.grants(inv(i0=3, i1=3, i4=1), target).inventory == {5: 1}
    target = rc.should_own(START, TABLE, set(), [5, 5])
    assert rc.grants(inv(i0=3, i1=3, i4=1, i5=1), target).inventory == {15: 1}
    # The starting Force Blaster plus a received one is the Pentashot (14).
    target = rc.should_own(START, TABLE, set(), [4])
    assert rc.grants(inv(i0=3, i1=3, i4=1), target).inventory == {14: 1}


def test_a_native_weapon_pickup_that_became_the_upgrade_counts() -> None:
    # Location 0 holds the Machine Gun; the player already had it from a remote copy, so the
    # game gave the Pulse Wave (9). Nothing is owed.
    target = rc.should_own(START, TABLE, {0}, [3])
    assert rc.grants(inv(i0=3, i1=3, i3=1, i4=1, i9=1), target).inventory == {}


def test_a_third_copy_of_a_weapon_is_reported() -> None:
    target = rc.should_own(START, TABLE, set(), [3, 3, 3])
    g = rc.grants(inv(i0=3, i1=3, i4=1), target)
    assert g.inventory == {3: 1, 9: 1}
    assert g.not_grantable == {3: 1}


def test_items_that_cannot_be_written_are_reported() -> None:
    target = rc.should_own(START, TABLE, set(), [31])
    g = rc.grants(inv(i0=3, i1=3, i4=1), target)
    assert g.inventory == {} and g.not_grantable == {31: 1}


def test_a_received_energy_shield_is_given_as_scrap_metal() -> None:
    target = rc.should_own(START, TABLE, set(), [50, 50])
    g = rc.grants(inv(i0=3, i1=3, i4=1), target)
    assert g.inventory == {46: 2} and g.not_grantable == {}


def test_a_spawned_shield_counts_once_seen_up_in_play() -> None:
    assert rc.shield_outcome(in_play=True, up=True, left_play=False, overdue=False) == rc.COUNT
    # overdue does not matter once it is up
    assert rc.shield_outcome(in_play=True, up=True, left_play=False, overdue=True) == rc.COUNT


def test_a_spawned_shield_not_seen_waits_then_is_spawned_again() -> None:
    assert rc.shield_outcome(in_play=True, up=False, left_play=False, overdue=False) == rc.WAIT
    assert rc.shield_outcome(in_play=True, up=False, left_play=False, overdue=True) == rc.AGAIN


def test_a_paused_game_waits_for_play_however_long_it_takes() -> None:
    for up in (False, True):
        for overdue in (False, True):
            assert rc.shield_outcome(False, up, left_play=False, overdue=overdue) == rc.WAIT


def test_a_death_or_unload_since_the_spawn_means_spawning_again() -> None:
    # a shield up afterwards may be the reloaded save's own, not the one spawned
    for in_play in (False, True):
        for up in (False, True):
            assert rc.shield_outcome(in_play, up, left_play=True, overdue=False) == rc.AGAIN


def test_shields_owed_are_the_received_ones_not_yet_given() -> None:
    assert rc.shields_owed([50, 3, 50, 50], 1) == 2
    assert rc.shields_owed([50], 3) == 0
    assert rc.shields_owed([], 0) == 0


def test_should_own_counts_start_and_upgrade_codes_under_the_weapon() -> None:
    assert rc.should_own({9: 1}, TABLE, set(), [3]) == Counter({3: 2})


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


def test_a_weapon_byte_of_two_still_owes_the_upgrade() -> None:
    # If a second pickup in play raised the weapon byte instead of setting the upgrade.
    target = rc.should_own(START, TABLE, {0}, [3])
    assert rc.grants(inv(i0=3, i1=3, i3=2, i4=1), target).inventory == {9: 1}


def test_scrap_pickups_count_as_scrap_metal() -> None:
    # Three scrap pickups in play raised byte 46 to 3; two received scrap items are owed on top.
    target = rc.should_own(START, [47, 49, 48, 27], {0, 1, 2}, [47, 49])
    g = rc.grants(inv(i0=3, i1=3, i4=1, i46=3), target)
    assert g.inventory == {46: 5} and g.not_grantable == {}


def test_a_stand_in_pickup_does_not_swallow_a_received_scrap_metal() -> None:
    # Another player's item (the stand-in, 47) picked up raised byte 46; a Scrap Metal received
    # afterwards is still owed.
    target = rc.should_own(START, [47], {0}, [46])
    assert rc.grants(inv(i0=3, i1=3, i4=1, i46=1), target).inventory == {46: 2}


def test_a_data_disc_is_never_put_in_the_inventory() -> None:
    # Counted by the client instead (client/discs.py): received, or the secret boss's own.
    target = rc.should_own(START, TABLE, set(), [44, 44])
    assert target[44] == 0
    g = rc.grants(inv(i0=3, i1=3, i4=1), target)
    assert 44 not in g.inventory and 44 not in g.not_grantable
    g = rc.grants(inv(i0=3, i1=3, i4=1, i44=1), target)
    assert 44 not in g.inventory, "the secret boss's disc is left alone"
