"""game.Game.attach_linux with stand-ins for the game finder and the helper's connection: the
targets from the binary's symbols, and the checks made before it attaches."""

from __future__ import annotations

import struct

import pytest
from test_linux_targets import b1_like

from client import game, linux_helper, linux_targets, patterns

B1 = patterns.KNOWN_LINUX_BUILDS[0]


class FakeLinuxProcess:
    """The helper's connection: memory by address (unset bytes read 0), and whether it was
    closed. ``memory[address] = data`` sets bytes from there on."""

    instances: list[FakeLinuxProcess] = []

    class _Memory(dict):
        def __setitem__(self, address: int, data: bytes) -> None:
            for i, byte in enumerate(data):
                super().__setitem__(address + i, byte)

    def __init__(self, found: linux_helper.LinuxGame) -> None:
        self.process_id = found.pid
        self.memory = FakeLinuxProcess._Memory()
        self.closed = False
        FakeLinuxProcess.instances.append(self)

    def read_bytes(self, address: int, size: int) -> bytes:
        return bytes(self.memory.get(address + i, 0) for i in range(size))

    def write_bytes(self, address: int, data: bytes) -> None:
        self.memory[address] = data

    def alive(self) -> bool:
        return True

    def close_process(self) -> None:
        self.closed = True


@pytest.fixture
def linux(monkeypatch: pytest.MonkeyPatch):
    """find_game finds B1 at pid 4242; the helper connection is a FakeLinuxProcess. Returns a
    setter for the player-state pointer's value."""
    FakeLinuxProcess.instances = []
    found = linux_helper.LinuxGame(4242, 2, B1, b1_like(), 99)
    monkeypatch.setattr(linux_helper, "find_game", lambda: found)
    pointer = {"value": 0xA04590}

    def connect(found_game):
        fake = FakeLinuxProcess(found_game)
        fake.memory[0x12B4578] = struct.pack("<Q", pointer["value"])
        return fake

    monkeypatch.setattr(linux_helper, "LinuxProcess", connect)
    return pointer


def test_it_attaches_with_the_targets_from_the_symbols(linux) -> None:
    g = game.Game.attach_linux()
    assert (g.platform, g.base, g.pid, g.build) == ("linux", 0, 4242, B1)
    assert g.rva == {t.name: t.b1 for t in linux_targets.TARGETS}
    g.pm.memory[0xA0561E] = bytes(range(64))  # the inventory
    assert g.inventory()[:3] == bytes([0, 1, 2])


def test_a_player_state_not_yet_set_refuses_for_now(linux) -> None:
    linux["value"] = 0  # early in start-up, before the game sets it: a later attach may do
    with pytest.raises(game.GameError, match="not set up its player state yet"):
        game.Game.attach_linux()
    assert FakeLinuxProcess.instances[-1].closed


def test_play_is_not_reported_once_the_player_state_moves(linux) -> None:
    g = game.Game.attach_linux()
    g.pm.memory[0x9E830A] = b"\x00"  # save_loaded: a save
    g.pm.memory[0x12F9901] = b"\x00"  # menu_state: none open
    g.pm.memory[0xA0561E - 0x108E + 0xCEA] = struct.pack("<h", 99)  # energy 99
    assert g.state() == game.IN_PLAY
    g.pm.memory[0x12B4578] = struct.pack("<Q", 0xA05000)
    with pytest.raises(game.GameError, match="not using it"):
        g.state()


def test_nothing_is_written_through_a_target_only_assumed(linux) -> None:
    g = game.Game.attach_linux()
    assert g.unwritable == {t.name for t in linux_targets.TARGETS if not t.measured}
    assert "slot_array" in g.unwritable
    with pytest.raises(game.GameError, match="not yet measured"):
        g._write("slot_array", 0, b"\x00")
    assert 0x14945C0 not in g.pm.memory  # nothing written


def test_a_player_state_elsewhere_refuses_to_attach(linux) -> None:
    linux["value"] = 0xA05000
    with pytest.raises(game.GameError, match="player state is at 0xa05000, not 0xa04590"):
        game.Game.attach_linux()
    assert FakeLinuxProcess.instances[-1].closed


@pytest.mark.parametrize(
    ("error", "raised"),
    [
        (linux_helper.GameNotFound("not running"), game.GameNotRunning),
        (linux_helper.UnknownLinuxBuild("does not know"), game.UnknownBuild),
        (linux_helper.LinuxHelperError("twice"), game.GameError),
    ],
    ids=["not-running", "unknown-build", "other"],
)
def test_finding_errors_become_game_errors(monkeypatch, error, raised) -> None:
    def fail():
        raise error

    monkeypatch.setattr(linux_helper, "find_game", fail)
    with pytest.raises(raised):
        game.Game.attach_linux()


def test_a_binary_lacking_a_target_refuses_before_connecting(monkeypatch) -> None:
    found = linux_helper.LinuxGame(4242, 2, B1, b1_like(drop="gExitDelay"), 99)
    monkeypatch.setattr(linux_helper, "find_game", lambda: found)
    monkeypatch.setattr(linux_helper, "LinuxProcess", lambda f: pytest.fail("connected"))
    with pytest.raises(game.GameError, match="lacks a target"):
        game.Game.attach_linux()


def test_no_helper_is_a_game_error(monkeypatch) -> None:
    found = linux_helper.LinuxGame(4242, 2, B1, b1_like(), 99)
    monkeypatch.setattr(linux_helper, "find_game", lambda: found)

    def no_helper(f):
        raise linux_helper.LinuxHelperError("no helper in Zexion")

    monkeypatch.setattr(linux_helper, "LinuxProcess", no_helper)
    with pytest.raises(game.GameError, match="no helper"):
        game.Game.attach_linux()


def test_code_checks_are_for_the_windows_helper(linux) -> None:
    with pytest.raises(game.GameError, match="Windows build's helper"):
        game.Game.attach_linux().code_checks(["message_fn"], 16)


def test_the_clients_attach_is_the_linux_one_on_linux(monkeypatch) -> None:
    monkeypatch.setattr(game.sys, "platform", "linux")
    monkeypatch.setattr(game.Game, "attach_linux", classmethod(lambda cls: "linux game"))
    assert game.Game.attach() == "linux game"


def test_the_windows_helper_is_not_tried_with_the_linux_build(linux, tmp_path) -> None:
    from client import native

    g = game.Game.attach_linux()
    with pytest.raises(native.LinuxPartial, match="Linux version") as caught:
        native.attach(g, tmp_path / "zexion_ap.dll")
    assert isinstance(caught.value, native.ModFixable) and not caught.value.old_gone
    from client import state

    line = state.helper_failed(caught.value)
    assert line.startswith("On Linux the Zexion mod starts new games of this seed, shows your")
    assert "popups" in line and "traps" in line and "without popups" not in line
