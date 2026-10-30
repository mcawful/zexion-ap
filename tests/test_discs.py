"""client/discs.py: the Data Disc hunt's count and popups (no game, no server)."""

from __future__ import annotations

import struct

import fakes
import pytest

from client import discs, ids

SLOT = {
    "goal": "os_install_disks",
    "discs_required": 3,
    "discs_placed": 5,
    "discs_start": 0,
    "disc_locations": [7, 40],
}


def test_the_core_goal_has_no_hunt() -> None:
    assert discs.from_slot_data({"goal": "core"}) is None


def test_a_goal_the_client_does_not_know_is_refused() -> None:
    with pytest.raises(KeyError):
        discs.from_slot_data({})
    with pytest.raises(ValueError, match="'boss'"):
        discs.from_slot_data({"goal": "boss"})
    with pytest.raises(KeyError):
        discs.from_slot_data({k: v for k, v in SLOT.items() if k != "disc_locations"})


def test_the_hunt_comes_from_slot_data() -> None:
    hunt = discs.from_slot_data(SLOT)
    assert hunt == discs.DiscHunt(3, 5, 0, frozenset({7, 40}))


def test_owned_counts_received_own_checked_and_starting_discs() -> None:
    hunt = discs.DiscHunt(3, 5, 1, frozenset({7, 40}))
    assert discs.owned(hunt, [0, 44, 1], []) == 2
    assert discs.owned(hunt, [0, 44, 1], [3, 7]) == 3, "own disc at location 7"
    assert discs.owned(hunt, [], [3, 9]) == 1, "other checked locations are not discs"


def test_before_the_scanner_the_popups_give_the_count_alone() -> None:
    hunt = discs.from_slot_data(SLOT)
    assert discs.note(2) == "OS INSTALL DISK 2"
    assert discs.received_notes(hunt, [44], [], told=False) == {0: "OS INSTALL DISK 1"}


def test_received_discs_count_up_in_their_popups() -> None:
    hunt = discs.from_slot_data(SLOT)
    notes = discs.received_notes(hunt, [44, 0, 44, 44], [40])
    assert notes == {
        0: "OS INSTALL DISK 2 OF 3",
        2: "OS INSTALL DISK 3 OF 3",
        3: "OS INSTALL DISK 4 OF 3",
    }


def test_the_helper_request_carries_the_count_and_the_line_in_hex() -> None:
    assert discs.request(2, 3) == "discs 2 3 " + b"COLLECT 3 OS INSTALL DISKS!".hex() + " 1"
    assert discs.request(2, 3, told=False).endswith(" 0")
    assert discs.OFF == "discs"


def test_seed_slots_are_randomizer_saves_holding_this_seed() -> None:
    plan = fakes.plan(location_items=(46,) * 154, start_items={}, start=(6, 191, 329))
    ours = bytearray(ids.SECTION_SIZE)
    ours[0x26C : 0x26C + 154] = bytes(plan.location_items)
    ours[0x310:0x316] = bytes.fromhex("0600bf004901")
    other = bytearray(ids.SECTION_SIZE)
    copies = bytes(ours) + bytes(ours) + bytes(ours) + bytes(other) + bytes(ids.SECTION_SIZE) * 26
    markers = [(1, True), (0, True), (1, False), (1, True)] + [(0, False)] * 26
    # Slot 1 ours; 2 a normal save over a stale copy; 3 empty; 4 another seed.
    assert discs.seed_slots(markers, copies, plan) == [1]


def test_the_slots_request() -> None:
    assert discs.slots_request(3, 15, [4, 5]) == "disk_slots 3 15 4,5"
    assert discs.slots_request(3, 15, []) == "disk_slots 3 15 -"
    assert discs.SLOTS_OFF == "disk_slots"


def test_the_scanner_line_fits_one_line_of_the_game_text() -> None:
    assert discs.scan_line(15) == "COLLECT 15 OS INSTALL DISKS!"
    assert discs.scan_line(1) == "COLLECT 1 OS INSTALL DISK!"
    assert len(discs.scan_line(41)) <= 31


def stamp(time: int, column: int, row: int, slot: int = 3) -> bytes:
    """A save stamp as game.Game.save_stamp reads it: the slot, then the block from +0x9354."""
    return struct.pack("<iHHHHH", slot, time, 0, 0, row, column)


def test_saved_at_home_compares_the_continue_point_with_the_landing_pod() -> None:
    # The live26 save, read 2026-10-25: a save at the pod (191, 329); one at (151, 464) elsewhere.
    assert discs.saved_at_home(stamp(39567, 191, 329))
    assert not discs.saved_at_home(stamp(36589, 151, 464))
    assert not discs.saved_at_home(b"")


def test_home_is_the_landing_pod_whichever_room_the_seed_starts_in() -> None:
    # A seed starting in the save room at (23, 22) (tiles 333, 449): a save there is not home;
    # its start record is not read at all.
    assert discs.HOME == (191, 329)
    assert not discs.saved_at_home(stamp(100, 333, 449))
    assert discs.saved_at_home(stamp(200, 191, 329))


def test_the_home_cell_is_the_pods_row_and_column_in_cells() -> None:
    # (191, 329): cell (13, 16) in this repo's (x, y), which the player state holds as 16, 13.
    assert discs.HOME_CELL == (16, 13)


def test_the_mission_request_gives_the_helper_home() -> None:
    assert discs.MISSION_REQUEST == "mission_prompt 191 329"


def test_return_home_is_offered_after_a_save_at_the_pod_only() -> None:
    home = discs.ReturnHome()
    home.see(stamp(100, 191, 329), True, True, True)
    assert not home.offer  # loaded at the pod: not a save
    home.see(stamp(100, 191, 329), True, True, True)
    assert not home.offer  # nothing new
    home.see(stamp(200, 151, 464), False, True, False)
    assert not home.offer  # saved elsewhere
    home.see(stamp(300, 191, 329), True, False, True)
    assert not home.offer  # saved at the pod without enough disks
    home.see(stamp(400, 191, 329), True, True, True)
    assert home.offer  # saved at the pod with enough
    home.offer = False  # the prompt queued (NO plays on)
    home.see(stamp(400, 191, 329), True, True, True)
    assert not home.offer
    home.see(stamp(500, 191, 329), True, True, True)
    assert home.offer  # the next save there offers it again


def test_a_save_away_from_the_pods_cell_offers_nothing() -> None:
    home = discs.ReturnHome()
    home.see(stamp(100, 191, 329), True, True, False)
    # The game's own save in the escape keeps the pod as the continue point.
    home.see(stamp(200, 191, 329), True, True, False)
    assert not home.offer
    home.see(stamp(300, 191, 329), True, True, True)
    assert home.offer
    home.see(stamp(300, 191, 329), True, True, False)
    assert not home.offer  # left the cell before the prompt came: it lapses


def test_another_slot_is_not_a_save() -> None:
    home = discs.ReturnHome()
    home.see(stamp(100, 191, 329, slot=3), True, True, True)
    home.see(stamp(900, 191, 329, slot=5), True, True, True)
    assert not home.offer  # another save of the seed loaded at its pod
    home.see(stamp(950, 191, 329, slot=5), True, True, True)
    assert home.offer


def test_the_pod_note_waits_for_the_received_items_popups() -> None:
    assert not discs.popups_handed(None, 3)  # not known yet
    assert not discs.popups_handed(2, 3)  # the third received disk's popup not handed yet
    assert discs.popups_handed(3, 3)
