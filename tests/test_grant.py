"""tools/grant.py on a fake process (no game, no game folder)."""

from __future__ import annotations

import json

import pytest

import game
import grant
from patterns import Build

ITEM_NAMES = "0) ENERGY TANK\r\n\r\nEnergy Tank\r\n\r\n----\r\n27) ALPHA KEY\r\n\r\nAlpha Key\r\n"
NAMES = grant.parse_item_names(ITEM_NAMES)


def test_item_names_come_from_the_headings() -> None:
    assert NAMES == {0: "ENERGY TANK", 27: "ALPHA KEY"}
    assert grant.parse_item_names(ITEM_NAMES.encode("utf-16").decode("utf-16")) == NAMES


def test_items_resolve_by_id_or_by_name() -> None:
    assert grant.resolve_item("27", NAMES) == 27
    assert grant.resolve_item("alpha key", NAMES) == 27
    assert grant.resolve_item("AlphaKey", NAMES) == 27
    with pytest.raises(ValueError, match="no single item"):
        grant.resolve_item("alpha", NAMES)  # a prefix is not a name
    with pytest.raises(ValueError, match="not an item ID"):
        grant.resolve_item("51", NAMES)


def test_changes_lists_each_byte_that_differs() -> None:
    old, new = bytes([5, 3, 0]), bytes([6, 3, 1])
    assert grant.changes(old, new, {0: "ENERGY TANK"}) == ["0 ENERGY TANK: 5 -> 6", "2: 0 -> 1"]


class FakeProcess:
    def __init__(self) -> None:
        self.memory = bytearray(0x100)
        self.writes = 0
        self.fail_write = False
        self.overwrite_with: int | None = None  # the game changes the byte straight after

    def read_bytes(self, address: int, size: int) -> bytes:
        return bytes(self.memory[address : address + size])

    def write_bytes(self, address: int, data: bytes, size: int) -> None:
        if self.fail_write:
            raise OSError("access denied")
        self.writes += 1
        self.memory[address : address + size] = data
        if self.overwrite_with is not None:
            self.memory[address] = self.overwrite_with


class FakeGame(game.Game):
    alive = True

    def _alive(self) -> bool:
        return self.alive


@pytest.fixture
def fake(monkeypatch, tmp_path):
    monkeypatch.setenv("ZEXION_LAB_DIR", str(tmp_path))
    g = FakeGame(
        FakeProcess(),
        0,
        {"inventory": 0x10, "pickup_counter": 0x80},
        1,
        Build("B1", "0" * 64, 0, 0),
    )
    g.pm.memory[0x10] = 5  # 5 Energy Tanks
    monkeypatch.setattr(game.Game, "attach", classmethod(lambda cls: g))
    monkeypatch.setattr(grant, "load_item_names", lambda: dict(NAMES))
    return g, tmp_path / "grants.jsonl"


def entries(log_path) -> list[dict]:
    return [json.loads(line) for line in log_path.read_text().splitlines()]


def events(log_path) -> list[str]:
    return [e["event"] for e in entries(log_path)]


def test_set_writes_and_logs_the_attempt_first(fake) -> None:
    g, log_path = fake
    assert grant.main(["set", "energy tank", "6", "--expect", "5", "--yes"]) == 0
    assert g.pm.memory[0x10] == 6
    assert events(log_path) == ["attempt", "written"]


def test_set_refuses_without_yes(fake) -> None:
    g, log_path = fake
    assert grant.main(["set", "0", "6", "--expect", "5"]) == 1
    assert g.pm.writes == 0 and not log_path.exists()


def test_set_refuses_when_the_byte_is_not_as_expected(fake) -> None:
    g, log_path = fake
    assert grant.main(["set", "0", "6", "--expect", "4", "--yes"]) == 1
    assert g.pm.memory[0x10] == 5 and g.pm.writes == 0
    assert events(log_path) == ["attempt", "refused"]


def test_set_refuses_item_50(fake) -> None:
    g, log_path = fake
    assert grant.main(["set", "50", "1", "--expect", "0", "--yes"]) == 1
    assert g.pm.writes == 0
    assert events(log_path) == ["attempt", "refused"]


def test_no_write_happens_when_the_log_cannot_be_written(fake, monkeypatch) -> None:
    g, _ = fake

    def broken(entry):
        raise OSError("disk full")

    monkeypatch.setattr(grant, "log", broken)
    with pytest.raises(OSError):
        grant.main(["set", "0", "6", "--expect", "5", "--yes"])
    assert g.pm.writes == 0


def test_a_write_the_game_changed_straight_after_is_not_logged_as_refused(fake) -> None:
    g, log_path = fake
    g.pm.overwrite_with = 5  # e.g. a reload put the saved value back
    assert grant.main(["set", "0", "6", "--expect", "5", "--yes"]) == 2
    assert g.pm.writes == 1
    assert events(log_path) == ["attempt", "mismatch"]
    assert entries(log_path)[-1]["read_back"] == 5


def test_a_failed_write_logs_what_the_byte_holds_afterward(fake) -> None:
    g, log_path = fake
    g.pm.fail_write = True
    assert grant.main(["set", "0", "6", "--expect", "5", "--yes"]) == 2
    assert events(log_path) == ["attempt", "unknown"]
    assert entries(log_path)[-1]["observed"] == 5


def test_watch_prints_and_logs_changes(fake, monkeypatch, capsys) -> None:
    g, log_path = fake
    ticks = iter([lambda: g.pm.memory.__setitem__(0x10 + 27, 1), None])

    def sleep(_):
        step = next(ticks)
        if step is None:
            raise KeyboardInterrupt
        step()

    monkeypatch.setattr(grant.time, "sleep", sleep)
    assert grant.main(["watch"]) == 0
    assert "27 ALPHA KEY: 0 -> 1" in capsys.readouterr().out
    assert events(log_path) == ["watch", "change", "watch-ended"]
    assert entries(log_path)[0]["inventory"][0] == 5
    assert entries(log_path)[-1]["reason"] == "stopped"


def test_watch_reports_the_game_exiting(fake, monkeypatch, capsys) -> None:
    g, log_path = fake

    def sleep(_):
        g.alive = False

    monkeypatch.setattr(grant.time, "sleep", sleep)
    assert grant.main(["watch"]) == 1
    assert "has exited" in capsys.readouterr().out
    assert events(log_path) == ["watch", "watch-ended"]


def test_watch_logs_its_end_when_its_time_is_up(fake, monkeypatch) -> None:
    _, log_path = fake
    monkeypatch.setattr(grant.time, "sleep", lambda _: None)
    assert grant.main(["watch", "--seconds", "0.01"]) == 0
    assert events(log_path)[0] == "watch" and events(log_path)[-1] == "watch-ended"
    assert entries(log_path)[-1]["reason"] == "deadline"
