"""tools/grant.py on a fake process (no game, no game folder)."""

from __future__ import annotations

import json

import pytest

import grant
from client import game
from client.patterns import Build

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
        self.memory = bytearray(0x1000)
        self.writes = 0
        self.fail_write = False
        self.overwrite_with: int | None = None  # the game changes the byte straight after

    def read_bytes(self, address: int, size: int) -> bytes:
        return bytes(self.memory[address : address + size])

    def write_bytes(self, address: int, data: bytes) -> None:
        if self.fail_write:
            raise OSError("access denied")
        self.writes += 1
        self.memory[address : address + len(data)] = data
        if self.overwrite_with is not None:
            self.memory[address] = self.overwrite_with


class FakeGame(game.Game):
    alive = True

    def _alive(self) -> bool:
        return self.alive


INV = 0x400
ENERGY = INV - game.INVENTORY_IN_PLAYER_STATE + game.ENERGY_NOW
RVA = {"inventory": INV, "pickup_counter": 0x980, "save_loaded": 0x900, "menu_state": 0x901}


@pytest.fixture
def fake(monkeypatch, tmp_path):
    monkeypatch.setenv("ZEXION_LAB_DIR", str(tmp_path))
    g = FakeGame(FakeProcess(), 0, dict(RVA), 1, Build("B1", "0" * 64, 0, 0))
    g.pm.memory[INV] = 5  # 5 Energy Tanks
    g.pm.memory[ENERGY : ENERGY + 2] = (80).to_bytes(2, "little")  # alive, a save loaded
    monkeypatch.setattr(game.Game, "attach", classmethod(lambda cls: g))
    monkeypatch.setattr(grant, "load_item_names", lambda: dict(NAMES))
    monkeypatch.setattr(game.time, "sleep", lambda _: None)
    return g, tmp_path / "grants.jsonl"


def entries(log_path) -> list[dict]:
    return [json.loads(line) for line in log_path.read_text().splitlines()]


def events(log_path) -> list[str]:
    return [e["event"] for e in entries(log_path)]


def test_set_writes_and_logs_the_attempt_first(fake) -> None:
    g, log_path = fake
    assert grant.main(["set", "energy tank", "6", "--expect", "5", "--yes"]) == 0
    assert g.pm.memory[INV] == 6
    assert events(log_path) == ["attempt", "written"]


def test_set_refuses_without_yes(fake) -> None:
    g, log_path = fake
    assert grant.main(["set", "0", "6", "--expect", "5"]) == 1
    assert g.pm.writes == 0 and not log_path.exists()


def test_set_refuses_when_the_byte_is_not_as_expected(fake) -> None:
    g, log_path = fake
    assert grant.main(["set", "0", "6", "--expect", "4", "--yes"]) == 1
    assert g.pm.memory[INV] == 5 and g.pm.writes == 0
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
    ticks = iter([lambda: g.pm.memory.__setitem__(INV + 27, 1), None])

    def sleep(_):
        step = next(ticks)
        if step is None:
            raise KeyboardInterrupt
        step()

    monkeypatch.setattr(grant.time, "sleep", sleep)
    assert grant.main(["watch"]) == 0
    out = capsys.readouterr().out
    assert "27 ALPHA KEY: 0 -> 1" in out and "in play" in out
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


@pytest.mark.parametrize(
    ("poke", "state"),
    [
        (lambda m: None, game.IN_PLAY),
        (lambda m: m.__setitem__(0x901, 2), game.MENU),
        (lambda m: m.__setitem__(slice(ENERGY, ENERGY + 2), bytes([0xFF, 0xFF])), game.DEAD),
        (lambda m: m.__setitem__(0x900, 0xFF), game.NO_SAVE),
    ],
)
def test_state_reads_save_menu_and_energy(fake, poke, state) -> None:
    g, _ = fake
    poke(g.pm.memory)
    assert g.state() == state


def set_energy(memory, value: int) -> None:
    memory[ENERGY : ENERGY + 2] = value.to_bytes(2, "little", signed=True)


@pytest.mark.parametrize(
    ("poke", "state"),
    [
        (lambda m: m.__setitem__(0x901, 3), "menu"),
        (lambda m: set_energy(m, -1), "dead"),
        (lambda m: set_energy(m, 0), "dead"),
        (lambda m: m.__setitem__(0x900, 0xFF), "no save loaded"),
    ],
)
def test_set_refuses_outside_play(fake, poke, state) -> None:
    g, log_path = fake
    poke(g.pm.memory)
    assert grant.main(["set", "0", "6", "--expect", "5", "--yes"]) == 1
    assert g.pm.writes == 0 and g.pm.memory[INV] == 5
    assert events(log_path) == ["attempt", "refused"]
    assert f"not in play ({state})" in entries(log_path)[-1]["error"]


def test_set_refuses_while_the_state_is_changing(fake, monkeypatch) -> None:
    g, log_path = fake
    # Between the two reads of settled_state(), a menu opens.
    monkeypatch.setattr(game.time, "sleep", lambda _: g.pm.memory.__setitem__(0x901, 2))
    assert grant.main(["set", "0", "6", "--expect", "5", "--yes"]) == 1
    assert g.pm.writes == 0
    assert "not in play (changing)" in entries(log_path)[-1]["error"]


def test_a_reload_undoing_the_write_is_logged_unconfirmed(fake, monkeypatch) -> None:
    g, log_path = fake
    sleeps = iter([lambda: None, lambda: g.pm.memory.__setitem__(INV, 5)])
    monkeypatch.setattr(game.time, "sleep", lambda _: next(sleeps)())
    assert grant.main(["set", "0", "6", "--expect", "5", "--yes"]) == 2
    assert g.pm.writes == 1
    assert events(log_path) == ["attempt", "unconfirmed"]
    assert entries(log_path)[-1]["now"] == 5


def test_watch_reports_state_changes(fake, monkeypatch, capsys) -> None:
    g, log_path = fake
    ticks = iter([lambda: g.pm.memory.__setitem__(0x901, 2), None])

    def sleep(_):
        step = next(ticks)
        if step is None:
            raise KeyboardInterrupt
        step()

    monkeypatch.setattr(grant.time, "sleep", sleep)
    assert grant.main(["watch"]) == 0
    assert "state: in play -> menu" in capsys.readouterr().out


def test_the_log_creates_a_missing_lab_folder(monkeypatch, tmp_path) -> None:
    lab = tmp_path / "fresh" / "lab"
    monkeypatch.setenv("ZEXION_LAB_DIR", str(lab))
    grant.log({"event": "attempt"})
    assert [e["event"] for e in entries(lab / "grants.jsonl")] == ["attempt"]
