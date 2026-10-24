"""client/discs.py: the Data Disc hunt's count and popups (no game, no server)."""

from __future__ import annotations

import discs

SLOT = {
    "goal": "os_install_disks",
    "discs_required": 3,
    "discs_placed": 5,
    "discs_start": 0,
    "disc_locations": [7, 40],
}


def test_the_core_goal_has_no_hunt() -> None:
    assert discs.from_slot_data({"goal": "core"}) is None
    assert discs.from_slot_data({}) is None  # a world from before the option


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


def test_the_scanner_line_fits_one_line_of_the_game_text() -> None:
    assert discs.scan_line(15) == "COLLECT 15 OS INSTALL DISKS!"
    assert discs.scan_line(1) == "COLLECT 1 OS INSTALL DISK!"
    assert len(discs.scan_line(41)) <= 31
