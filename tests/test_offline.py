"""client/offline.py: what the client sends the helper to dress AP saves without the client."""

from __future__ import annotations

import pytest

from client import offline


def test_the_seed_file_name() -> None:
    assert offline.seed_file_name("AP_123/..\\x", 0, 2) == "AP_123____x-0-2"
    assert offline.seed_file_name("", 1, 1) == "seed-1-1"


def test_a_seed_file_in_the_helpers_format() -> None:
    table = list(range(154))
    text = offline.seed_text(
        table,
        (6, 191, 329),
        intel=True,
        lock_assists=False,
        discs=(3, 12, "COLLECT 12 OS INSTALL DISKS!", [1312, 40]),
        labels="ap_labels\nlabel 23 14 4150",
        traps=[(1375, 103), (40, 102)],
    )
    lines = text.splitlines()
    assert lines[0] == "zexion-ap offline 1"
    assert lines[1] == "table " + bytes(table).hex()
    assert lines[2] == "start 0600bf004901"  # three 16-bit numbers, as the section holds them
    assert lines[3:5] == ["intel 1", "assists free"]
    assert lines[5] == "discs 3 12 " + b"COLLECT 12 OS INSTALL DISKS!".hex()
    assert lines[6] == "unfound 1312,40"
    assert lines[7] == "traps 40:102,1375:103"  # this player's own traps: cell and item
    assert lines[8:] == ["ap_labels", "label 23 14 4150"]


def test_no_disks_and_no_labels() -> None:
    text = offline.seed_text([0] * 154, (0, 0, 0), False, True, None, None)
    assert text.splitlines()[3:] == [
        "intel 0",
        "assists lock",
        "discs -",
        "unfound -",
        "traps -",
    ]


def test_the_cached_config_carries_its_checks() -> None:
    text = offline.config_text("config a=0x10 b=0x20", {"b": bytes(16), "a": b"\x01" * 16})
    assert text.splitlines() == [
        "config a=0x10 b=0x20",
        "check a " + "01" * 16,
        "check b " + "00" * 16,
    ]
    assert "keys_branch" in offline.NOT_CHECKED  # the helper patches it
    assert "ending_unlocks" in offline.NOT_CHECKED  # and this, in AP saves
    assert "seen_marker_code" in offline.NOT_CHECKED  # and the on-screen marker
    assert "text_cut_code" in offline.NOT_CHECKED  # and the text window's cut


def test_grid_cells() -> None:
    cells = [(0, 0), (16, 13), (23, 14)]
    assert offline.grid_cells(cells, [2, 1]) == [13 * 54 + 16, 14 * 54 + 23]


def test_the_requests() -> None:
    assert offline.put_request("config", "config a=0x1\n") == "offline_put config\nconfig a=0x1\n"
    assert offline.put_request("seed", "x\n", "AP-0-1") == "offline_put seed AP-0-1\nx\n"


# --- a poorer text never replaces a richer file ---------------------------------------------------


def seed(*, discs=None, labels=None, traps=None, table=None) -> str:
    return offline.seed_text(
        table if table is not None else list(range(154)),
        (6, 191, 329),
        intel=False,
        lock_assists=True,
        discs=discs,
        labels=labels,
        traps=traps or [],
    )


WHOLE = seed(
    discs=(3, 12, "COLLECT 12 OS INSTALL DISKS!", [1312, 40]),
    labels="ap_labels\nlabel 23 14 4150\nlabel 4 5 41",
    traps=[(40, 102)],
)
POOR = seed(traps=[(40, 102), (7, 103)])


def test_the_stored_labels_and_disk_counts_stay_in_a_poorer_text() -> None:
    merged = offline.keep_richer(POOR, WHOLE)
    lines = merged.splitlines()
    whole = WHOLE.splitlines()
    assert lines[:3] == whole[:3]  # the seed's table, start
    assert lines[-3:] == ["ap_labels", "label 23 14 4150", "label 4 5 41"]
    assert whole[5] in lines and whole[6] in lines  # discs, unfound
    assert "traps 7:103,40:102" in lines, "this run's own traps are the poorer text's"
    assert merged.endswith("\n")


def test_a_text_with_its_own_labels_and_disks_is_left_as_it_is() -> None:
    mine = seed(discs=(1, 12, "X", [5]), labels="ap_labels\nlabel 1 1 41")
    assert offline.keep_richer(mine, WHOLE) == mine


@pytest.mark.parametrize("stored", [None, "", "junk", "zexion-ap offline 1\ntraps -\n"])
def test_nothing_is_taken_from_a_file_that_is_no_seed_of_this_one(stored) -> None:
    assert offline.keep_richer(POOR, stored) == POOR


def test_nothing_is_taken_from_the_file_of_another_table() -> None:
    other = seed(table=[1] * 154, labels="ap_labels\nlabel 1 1 41")
    assert offline.keep_richer(POOR, other) == POOR


def test_a_stored_file_without_labels_adds_none() -> None:
    plain = seed(traps=[(40, 102)])
    assert offline.keep_richer(POOR, plain) == POOR


def test_assist_unlock_is_never_taken_back_by_a_poorer_text() -> None:
    # Before this connection's items are listed the slot data's lock stands, with no unlock seen.
    free = WHOLE.replace("assists lock", "assists free")
    assert "assists free" in free.splitlines()
    merged = offline.keep_richer(POOR, free)
    assert "assists free" in merged.splitlines() and "assists lock" not in merged.splitlines()
    assert offline.keep_richer(POOR, WHOLE).count("assists lock") == 1  # locked in both
