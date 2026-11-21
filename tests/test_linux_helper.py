"""client/linux_helper.py's finding of the game, against a stand-in ``/proc`` (the connection
and the reads run against the stand-in game in tests/test_native_linux.py)."""

from __future__ import annotations

import hashlib
import os
import re
import socket
import sys
import threading
import types
from pathlib import Path

import pytest
from fakes import ELF_BUILD_ID, elf_image, fake_proc, proc_entry
from native_source import linux_config_table

from client import linux_helper, patterns

STAND_IN = patterns.LinuxBuild("B1", hashlib.sha256(elf_image()).hexdigest(), ELF_BUILD_ID.hex())


@pytest.fixture(autouse=True)
def known(monkeypatch: pytest.MonkeyPatch) -> None:
    """The stand-in binary as the one known Linux build."""
    monkeypatch.setattr(patterns, "KNOWN_LINUX_BUILDS", (STAND_IN,))


def _proc(tmp_path: Path, exe: bytes | None = None, nspid: str = "4242\t2") -> Path:
    """A ``/proc`` with the game (pid 4242, in the namespace pids ``nspid``) running ``exe``."""
    proc = fake_proc(tmp_path)
    proc_entry(proc, "4242", b"Zexion\n", exe=elf_image() if exe is None else exe, nspid=nspid)
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
    proc_entry(proc, "4000", b"reaper\n", wrapper, b"\x7fELF\x01\x01" + bytes(60))
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
    proc_entry(proc, "5000", b"Zexion\n", exe=elf_image())
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


TOKEN = "0123456789abcdef" * 4  # 64 lower-case hex digits, as the helper writes it
linux_only = pytest.mark.skipif(
    not sys.platform.startswith("linux"), reason="the token is read as a Linux process reads it"
)


def _token_at(root: Path, *parts: str, text: str = TOKEN, mode: int = 0o600) -> Path:
    path = root.joinpath(*parts, "ZexionAP", "token")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    path.chmod(mode)
    return path


@linux_only
def test_the_token_is_looked_for_in_the_games_own_data_folder_first(tmp_path: Path) -> None:
    # Under Flatpak's Steam the helper's folder is the sandbox's: seen through /proc/<pid>/root,
    # from the game's own XDG_DATA_HOME; then the host's own folder and Flatpak Steam's.
    proc = _proc(tmp_path)
    (proc / "4242" / "environ").write_bytes(b"HOME=/home/x\0XDG_DATA_HOME=/sandbox/data\0")
    game = linux_helper.find_game(proc)
    home = tmp_path / "home"
    paths = linux_helper.token_paths(game, proc, {"XDG_DATA_HOME": "/host/data"}, home)
    assert paths == [
        proc / "4242" / "root" / "sandbox" / "data" / "ZexionAP" / "token",
        Path("/host/data/ZexionAP/token"),
        home / ".local" / "share" / "ZexionAP" / "token",
        home / ".var/app/com.valvesoftware.Steam/.local/share/ZexionAP/token",
        home / ".var/app/com.valvesoftware.Steam/data/ZexionAP/token",
    ]
    (proc / "4242" / "environ").write_bytes(b"HOME=/home/x\0")  # no XDG_DATA_HOME: HOME's
    inner = linux_helper.token_paths(game, proc, {}, home)[0]
    assert (
        inner == proc / "4242" / "root" / "home" / "x" / ".local" / "share" / "ZexionAP" / "token"
    )
    (proc / "4242" / "environ").write_bytes(b"HOME=relative\0XDG_DATA_HOME=also/relative\0")
    assert len(linux_helper.token_paths(game, proc, {}, home)) == 3  # none of the game's


@linux_only
def test_the_token_is_read_from_the_game_s_view_or_the_hosts(tmp_path: Path) -> None:
    proc = _proc(tmp_path)
    (proc / "4242" / "environ").write_bytes(b"XDG_DATA_HOME=/sandbox/data\0")
    game = linux_helper.find_game(proc)
    home = tmp_path / "home"
    _token_at(home, ".var", "app", "com.valvesoftware.Steam", ".local", "share", text="a" * 64)
    # Flatpak Steam's folder, as the host sees it, when the game's root can't be followed
    assert linux_helper.find_tokens(game, proc, {}, home)[0][1] == "a" * 64
    _token_at(proc / "4242" / "root", "sandbox", "data")  # the game's own view wins
    assert linux_helper.find_tokens(game, proc, {}, home)[0][1] == TOKEN


@linux_only
@pytest.mark.parametrize(
    ("text", "mode"),
    [
        (TOKEN, 0o644),
        (TOKEN, 0o640),
        (TOKEN, 0o604),
        (TOKEN[:-1], 0o600),
        (TOKEN.upper(), 0o600),
        (TOKEN + "0", 0o600),
        ("", 0o600),
    ],  # fmt: skip
    ids=[
        "world-readable",
        "group-readable",
        "other-readable",
        "short",
        "upper-case",
        "long",
        "empty",
    ],
)
def test_a_token_file_others_can_read_or_that_is_no_token_is_not_used(
    tmp_path: Path, text: str, mode: int
) -> None:
    proc = _proc(tmp_path)
    game = linux_helper.find_game(proc)
    home = tmp_path / "home"
    _token_at(home, ".local", "share", text=text, mode=mode)
    with pytest.raises(
        linux_helper.LinuxHelperError, match="takes only a client that has its token"
    ):
        linux_helper.find_tokens(game, proc, {}, home)


@linux_only
def test_a_token_that_is_a_link_is_not_followed(tmp_path: Path) -> None:
    proc = _proc(tmp_path)
    game = linux_helper.find_game(proc)
    home = tmp_path / "home"
    real = _token_at(tmp_path, "elsewhere")
    link = home / ".local" / "share" / "ZexionAP" / "token"
    link.parent.mkdir(parents=True)
    link.symlink_to(real)
    with pytest.raises(linux_helper.LinuxHelperError, match="no token|takes only a client"):
        linux_helper.find_tokens(game, proc, {}, home)


@linux_only
def test_no_token_says_where_it_looked_and_what_to_do(tmp_path: Path) -> None:
    proc = _proc(tmp_path)
    game = linux_helper.find_game(proc)
    with pytest.raises(linux_helper.LinuxHelperError) as raised:
        linux_helper.find_tokens(game, proc, {"XDG_DATA_HOME": "/host/data"}, tmp_path / "home")
    message = str(raised.value)
    assert "/host/data/ZexionAP/token" in message and "com.valvesoftware.Steam" in message
    assert "can't read" in message and "no token" in message


def test_the_linux_config_leaves_null_only_what_the_windows_build_alone_reads() -> None:
    # The helper's watch_* calls reach Game's addresses without a check of their own once
    # configured (helper.cpp's frame checks configured() first), so an address the Linux config
    # leaves null must be one nothing reads on Linux. Read by hand, 2026-11-19: these five are read
    # only inside `#ifdef _WIN32` (equip.cpp's malloc and free slots, feats.cpp's patch_ending
    # check, check_steam and hook_steam, translation.cpp's build); the Linux build has the
    # helper's own malloc and free, the export for the Steam call, and its own string.
    root = Path(__file__).resolve().parent.parent
    fields = set(
        re.findall(
            r"BYTE\* (\w+) = nullptr", (root / "native/src/addresses.h").read_text(encoding="utf-8")
        )
    )
    keys = {field for _, field, _, _ in linux_config_table()}
    assert fields - keys == {
        "malloc_slot",
        "free_slot",
        "stats_context",
        "context_init_slot",
        "wstring_new_fn",
    }
    assert keys <= fields


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


def test_a_connection_made_again_that_fails_says_the_request_never_went_out(monkeypatch) -> None:
    # The helper closing during the new connection's own checks is a ConnectionLost of theirs:
    # the caller's request was never sent, so it is no NoReply (a shield not looked for).
    process = object.__new__(linux_helper.LinuxProcess)
    process.game, process._timeout, process._closed = None, 0.2, False
    process._lock, process.connection = threading.Lock(), 1
    process._helper = types.SimpleNamespace(connected=False)

    def dropped(self):
        raise linux_helper.ConnectionLost("the helper closed its socket")

    monkeypatch.setattr(linux_helper.LinuxHelper, "__enter__", dropped)
    with pytest.raises(linux_helper.LinuxHelperError) as raised:
        process.request("shield")
    assert not isinstance(raised.value, linux_helper.NoReply)
    assert process.connection == 1


def test_a_socket_that_cant_be_made_again_says_the_request_never_went_out(monkeypatch) -> None:
    process = object.__new__(linux_helper.LinuxProcess)
    process.game, process._timeout, process._closed = None, 0.2, False
    process._lock, process.connection = threading.Lock(), 1
    process._helper = types.SimpleNamespace(connected=False)

    def no_files(self):
        raise OSError(24, "Too many open files")

    monkeypatch.setattr(linux_helper.LinuxHelper, "__enter__", no_files)
    with pytest.raises(linux_helper.LinuxHelperError, match="connecting to the helper again"):
        process.request("die")


def test_disconnect_lets_go_and_the_next_request_connects_again(monkeypatch) -> None:
    closed = []
    helper = types.SimpleNamespace(connected=True, __exit__=lambda *_: closed.append(1))
    helper.__exit__ = lambda *_: (closed.append(1), setattr(helper, "connected", False))
    process = object.__new__(linux_helper.LinuxProcess)
    process.game, process._timeout, process._closed = None, 0.2, False
    process._lock, process.connection, process._helper = threading.Lock(), 1, helper
    process.disconnect()
    assert closed == [1] and not process.connected
    process.disconnect()  # already closed: nothing
    assert closed == [1]
    process._closed = True
    helper.connected = True
    process.disconnect()  # the process is closed for good: left to close_process
    assert closed == [1]


@linux_only
def test_the_newest_token_is_tried_first_and_none_twice(tmp_path: Path) -> None:
    proc = _proc(tmp_path)
    game = linux_helper.find_game(proc)
    home = tmp_path / "home"
    old = _token_at(home, ".local", "share", text="a" * 64)
    os.utime(old, (1_000_000_000, 1_000_000_000))
    flatpak = home / ".var/app/com.valvesoftware.Steam"
    _token_at(flatpak, ".local", "share", text="b" * 64)
    _token_at(flatpak, "data", text="b" * 64)  # the same token again: once
    found = linux_helper.find_tokens(game, proc, {}, home)
    assert [token for _, token in found] == ["b" * 64, "a" * 64]


def test_the_ranges_the_helper_lets_a_client_write_are_the_ones_the_client_writes() -> None:
    # native/linux/src/core.cpp, client_write_ranges: offsets from gGameState, by hand from
    # client/game.py's writes (set_item, set_supplies, set_item_collected, set_map_collected).
    from client import game as client_game
    from client import ids, linux_targets

    root = Path(__file__).resolve().parent.parent
    source = (root / "native/linux/src/core.cpp").read_text(encoding="utf-8")

    def constant(name: str) -> int:
        match = re.search(rf"{name} = ([0-9a-fA-Fx* ]+?)[,;]", source)
        assert match, name
        return eval(match.group(1))  # noqa: S307 (a number or a product of two, from our own source)

    targets = {t.name: t for t in linux_targets.TARGETS}
    state = linux_targets.PLAYER_STATE[1]
    assert constant("kPlayerState") == state
    assert constant("kInventoryAt") == client_game.INVENTORY_IN_PLAYER_STATE
    assert targets["inventory"].offset == state + constant("kInventoryAt")
    assert constant("kWritableItems") == len(ids.WRITABLE_ITEMS)
    fields = re.search(r"kSupplyFields\[\] = \{([^}]*)\}", source)
    assert fields
    assert sorted(int(f, 16) for f in fields.group(1).split(",")) == sorted(
        client_game.SUPPLY_FIELDS
    )
    assert constant("kCellStates") == targets["cell_state"].offset
    assert constant("kCellStride") == client_game.CELL_STRIDE
    assert client_game.CELL_ITEM < client_game.CELL_STRIDE
    assert constant("kMapGrid") == targets["map_grid"].offset
    assert constant("kMapCells") == client_game.MAP_SIZE * client_game.MAP_SIZE


def test_the_token_file_the_helper_took_is_remembered(monkeypatch, tmp_path: Path) -> None:
    # ticks.linux_stored_seed reads the seed files beside it, not beside the newest token found.
    stale, live = tmp_path / "stale" / "token", tmp_path / "live" / "token"
    monkeypatch.setattr(linux_helper, "find_tokens", lambda game: [(stale, "a"), (live, "b")])
    helper = linux_helper.LinuxHelper(types.SimpleNamespace(pid=1))
    monkeypatch.setattr(helper, "_connect", lambda: None)
    monkeypatch.setattr(helper, "__exit__", lambda *args: None)
    monkeypatch.setattr(helper, "request", lambda text: "ok" if text == "auth b" else "refused")
    assert helper.token_path is None
    assert helper.__enter__() is helper
    assert helper.token_path == live
    process = object.__new__(linux_helper.LinuxProcess)
    process._helper = helper
    assert process.token_path == live
