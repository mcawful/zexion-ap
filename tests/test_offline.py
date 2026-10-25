"""client/offline.py: what the client sends the helper to dress AP saves without the client."""

from __future__ import annotations

import offline


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
    )
    lines = text.splitlines()
    assert lines[0] == "zexion-ap offline 1"
    assert lines[1] == "table " + bytes(table).hex()
    assert lines[2] == "start 0600bf004901"  # three 16-bit numbers, as the section holds them
    assert lines[3:5] == ["intel 1", "assists free"]
    assert lines[5] == "discs 3 12 " + b"COLLECT 12 OS INSTALL DISKS!".hex()
    assert lines[6] == "unfound 1312,40"
    assert lines[7:] == ["ap_labels", "label 23 14 4150"]


def test_no_disks_and_no_labels() -> None:
    text = offline.seed_text([0] * 154, (0, 0, 0), False, True, None, None)
    assert text.splitlines()[3:] == ["intel 0", "assists lock", "discs -", "unfound -"]


def test_the_cached_config_carries_its_checks() -> None:
    text = offline.config_text("config a=0x10 b=0x20", {"b": bytes(16), "a": b"\x01" * 16})
    assert text.splitlines() == [
        "config a=0x10 b=0x20",
        "check a " + "01" * 16,
        "check b " + "00" * 16,
    ]
    assert "keys_branch" in offline.NOT_CHECKED  # the helper patches it


def test_grid_cells() -> None:
    cells = [(0, 0), (16, 13), (23, 14)]
    assert offline.grid_cells(cells, [2, 1]) == [13 * 54 + 16, 14 * 54 + 23]


def test_the_requests() -> None:
    assert offline.put_request("config", "config a=0x1\n") == "offline_put config\nconfig a=0x1\n"
    assert offline.put_request("seed", "x\n", "AP-0-1") == "offline_put seed AP-0-1\nx\n"
