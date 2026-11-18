"""client/linux_helper.py's finding of the game, against a stand-in ``/proc`` (the connection
and the reads run against the stand-in game in tests/test_native_linux.py)."""

from __future__ import annotations

import hashlib
import socket
import sys
from pathlib import Path

import pytest
from fakes import ELF_BUILD_ID, elf_image

from client import linux_helper, patterns

STAND_IN = patterns.LinuxBuild("B1", hashlib.sha256(elf_image()).hexdigest(), ELF_BUILD_ID.hex())


@pytest.fixture(autouse=True)
def known(monkeypatch: pytest.MonkeyPatch) -> None:
    """The stand-in binary as the one known Linux build."""
    monkeypatch.setattr(patterns, "KNOWN_LINUX_BUILDS", (STAND_IN,))


def _process(proc: Path, pid: str, comm: bytes, exe: bytes | None, cmdline: bytes = b"",
             nspid: str = "") -> None:  # fmt: skip
    game = proc / pid
    game.mkdir()
    (game / "comm").write_bytes(comm)
    (game / "cmdline").write_bytes(cmdline)
    (game / "status").write_text(f"Name:\t{comm.decode().strip()}\nNSpid:\t{nspid or pid}\n")
    # /proc/<pid>/stat: the pid, the name in parentheses, then fields 3 on; 22 is the start time.
    rest = " ".join(["S"] + ["0"] * 18 + [str(int(pid) * 10)] + ["0"] * 30)
    (game / "stat").write_text(f"{pid} ({comm.decode().strip()}) {rest}\n")
    if exe is not None:
        (game / "exe").write_bytes(exe)  # a link to the binary in the real /proc


def _proc(tmp_path: Path, exe: bytes | None = None, nspid: str = "4242\t2") -> Path:
    proc = tmp_path / "proc"
    (proc / "self").mkdir(parents=True)
    (proc / "self" / "status").write_text("NSpid:\t100\n")
    _process(proc, "4242", b"Zexion\n", elf_image() if exe is None else exe, nspid=nspid)
    return proc


@pytest.mark.parametrize(("nspid", "inner"), [("4242\t2", 2), ("4242", 4242), ("", 4242)])
def test_the_game_is_found_with_its_pid_inside_its_namespace(
    tmp_path: Path, nspid: str, inner: int
) -> None:
    proc = _proc(tmp_path, nspid=nspid)
    if not nspid:
        (proc / "4242" / "status").write_text("Name:\tZexion\n")  # a kernel without NSpid
    game = linux_helper.find_game(proc)
    assert (game.pid, game.inner_pid) == (4242, inner)
    assert game.build == STAND_IN
    assert game.image.build_id() == ELF_BUILD_ID.hex()


def test_steams_launch_wrappers_are_not_the_game(tmp_path: Path) -> None:
    proc = _proc(tmp_path)
    wrapper = b"reaper\0SteamLaunch\0AppId=3392510\0--\0/games/steamapps/common/Zexion/Zexion\0"
    _process(proc, "4000", b"reaper\n", b"\x7fELF\x01\x01" + bytes(60), wrapper)
    assert linux_helper.find_game(proc).pid == 4242


def test_another_build_is_refused(tmp_path: Path) -> None:
    proc = _proc(tmp_path, exe=elf_image(build_id=bytes(20)))
    with pytest.raises(linux_helper.LinuxHelperError, match="a build this client does not know"):
        linux_helper.find_game(proc)


def test_a_binary_edited_after_linking_is_not_the_known_build(tmp_path: Path) -> None:
    edited = elf_image(data_bytes=b"zexion DATA")  # the same build id, another hash
    proc = _proc(tmp_path, exe=edited)
    with pytest.raises(linux_helper.LinuxHelperError, match="does not know"):
        linux_helper.find_game(proc)


def test_two_games_of_the_known_build_are_refused(tmp_path: Path) -> None:
    proc = _proc(tmp_path)
    _process(proc, "5000", b"Zexion\n", elf_image())
    with pytest.raises(linux_helper.LinuxHelperError, match="runs twice"):
        linux_helper.find_game(proc)


def test_a_binary_that_cant_be_read_is_said(tmp_path: Path) -> None:
    proc = _proc(tmp_path)
    (proc / "4242" / "exe").unlink()
    (proc / "4242" / "exe").mkdir()  # reading it fails, but not as "gone"
    with pytest.raises(linux_helper.LinuxHelperError, match="can't be read"):
        linux_helper.find_game(proc)


def test_no_game_is_said(tmp_path: Path) -> None:
    proc = _proc(tmp_path)
    (proc / "4242" / "comm").write_bytes(b"bash\n")
    with pytest.raises(linux_helper.LinuxHelperError, match="not running"):
        linux_helper.find_game(proc)


def test_using_the_helper_without_a_connection_is_refused() -> None:
    game = linux_helper.LinuxGame(1, 1, STAND_IN, None)
    with pytest.raises(linux_helper.LinuxHelperError, match="not connected"):
        linux_helper.LinuxHelper(game).read(0x1000, 4)


def test_the_start_time_is_read_past_a_name_with_spaces_and_parentheses(tmp_path: Path) -> None:
    proc = tmp_path / "proc"
    (proc / "7").mkdir(parents=True)
    rest = " ".join(["S"] + ["0"] * 18 + ["123456"] + ["0"] * 30)
    (proc / "7" / "stat").write_text(f"7 (a (b) c) {rest}\n")
    assert linux_helper.start_time(7, proc) == 123456


def test_the_game_found_carries_its_start_time(tmp_path: Path) -> None:
    assert linux_helper.find_game(_proc(tmp_path)).started == 42420


def _stat(proc: Path, pid: int, state: str, started: int) -> None:
    (proc / str(pid)).mkdir(parents=True, exist_ok=True)
    rest = " ".join([state] + ["0"] * 18 + [str(started)] + ["0"] * 30)
    (proc / str(pid) / "stat").write_text(f"{pid} (Zexion) {rest}\n")


@pytest.mark.parametrize(
    ("state", "started", "alive"),
    [("S", 500, True), ("R", 500, True), ("Z", 500, False), ("X", 500, False), ("S", 501, False)],
    ids=["sleeping", "running", "zombie", "dead", "pid-reused"],
)
def test_the_game_counts_as_running_only_as_itself(tmp_path, state, started, alive) -> None:
    proc = tmp_path / "proc"
    _stat(proc, 9, state, started)
    assert linux_helper.running(9, 500, proc) is alive
    assert linux_helper.running(10, 500, proc) is False  # no such process


class Socket:
    """A connection whose send fails (``sent`` False), or whose peer closes after the send."""

    def __init__(self, sent: bool) -> None:
        self.sent = sent

    def send(self, data: bytes) -> int:
        if not self.sent:
            raise BrokenPipeError("broken pipe")
        return len(data)

    def recv(self, size: int) -> bytes:
        return b""  # closed

    def close(self) -> None:
        pass


@pytest.mark.parametrize("sent", [False, True])
def test_a_socket_that_fails_is_told_by_whether_the_request_went_out(sent: bool) -> None:
    # After the send the helper may have acted on it (NoReply: ConnectionLost, not a late reply,
    # so not a busy helper); before it, it did not.
    helper = linux_helper.LinuxHelper(linux_helper.LinuxGame(1, 1, STAND_IN, None), 0.2)
    helper._sock = Socket(sent)
    with pytest.raises(linux_helper.LinuxHelperError) as failed:
        helper.request("ping")
    assert isinstance(failed.value, linux_helper.ConnectionLost) is sent
    assert isinstance(failed.value, linux_helper.NoReply) is sent
    assert not helper.connected


@pytest.mark.skipif(
    not sys.platform.startswith("linux"), reason="SOCK_SEQPACKET unix sockets are Linux's"
)
def test_a_request_with_no_reply_closes_the_connection() -> None:
    """Its reply could still come, and would be read as the next request's."""
    client, silent = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    try:
        helper = linux_helper.LinuxHelper(linux_helper.LinuxGame(1, 1, STAND_IN, None), 0.2)
        client.settimeout(0.2)
        helper._sock = client
        with pytest.raises(linux_helper.NoReply):
            helper.request("ping")
        assert not helper.connected
        with pytest.raises(BrokenPipeError):  # the late reply finds the connection closed
            silent.send(b"pong frames=1")
        with pytest.raises(linux_helper.LinuxHelperError, match="not connected"):
            helper.request("ping")
    finally:
        silent.close()
        client.close()
