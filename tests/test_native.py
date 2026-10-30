"""client/native.py, and the built helper loaded into a stand-in process (never the game).

The stand-in is a sleeping Python process: the helper finds no known build there, so it hooks
nothing, but loading, the pipe and unloading are the same as in the game. Skipped where the
helper has not been built (CI's Python jobs) or off Windows, except where ZEXION_REQUIRE lists
``helper`` (CI's native job): there a missing build fails
test_the_helper_is_built_where_ci_requires_it. native/tests/ cover the hook itself, including
unloading under load.
"""

from __future__ import annotations

import os
import subprocess
import sys
import threading
import types
from dataclasses import dataclass, field
from pathlib import Path

import pytest

from client import native, process, state

WINDOWS = sys.platform == "win32"
BUILT = native.default_dll()
# ZEXION_REQUIRE's entries, as tests/test_hook_runner.py reads them.
REQUIRED = {e.strip() for e in os.environ.get("ZEXION_REQUIRE", "").split(",")} - {""}


@pytest.mark.skipif("helper" not in REQUIRED, reason="ZEXION_REQUIRE does not list helper")
def test_the_helper_is_built_where_ci_requires_it() -> None:
    assert WINDOWS, "the load test runs on Windows only"
    assert BUILT.is_file(), f"no helper at {BUILT}, so the load test skipped"


def test_pipe_name_and_default_dll(monkeypatch: pytest.MonkeyPatch) -> None:
    assert native.pipe_name(1234) == r"\\.\pipe\zexion-ap-1234"
    monkeypatch.delenv("ZEXION_DLL", raising=False)
    assert native.default_dll().parts[-4:] == ("native", "build", "Release", "zexion_ap.dll")
    monkeypatch.setenv("ZEXION_DLL", "elsewhere/x.dll")
    assert native.default_dll() == Path("elsewhere/x.dll")


def test_an_apply_request_puts_each_popup_on_one_line() -> None:
    # The helper reads a request line by line: a player's name with a newline or runs of
    # spaces (any text the server sends) must not break a popup into another line.
    cas = [(17, 0, 1), (0, 3, 4)]
    popups = [(17, "FROM  Bob\nthe\tBold "), (0, ""), (49, " A  hint\n", True), (16, "", True)]
    assert native.batch_text(cas, popups, refresh=True).splitlines() == [
        "apply",
        "cas 17 0 1",
        "cas 0 3 4",
        "refresh",
        "popup 17 FROM Bob the Bold",
        "popup 0",
        "notice 49 A hint",
    ]


def test_load_refuses_a_missing_dll(tmp_path: Path) -> None:
    with pytest.raises(native.HelperError, match="build native"):
        native.load(0, 0, tmp_path / "missing.dll")


@pytest.mark.skipif(not WINDOWS, reason="Windows pipes")
def test_a_pipe_served_by_another_process_is_refused() -> None:
    from multiprocessing.connection import Listener

    fake_pid = 4_000_000_001 % 2**32  # no game has this id; the pipe is served by this process
    listener = Listener(native.pipe_name(fake_pid), family="AF_PIPE")
    accepted = threading.Thread(target=lambda: listener.accept().close(), daemon=True)
    accepted.start()
    try:
        with pytest.raises(native.HelperError, match="not the game"):
            native.Helper(fake_pid)
    finally:
        accepted.join(timeout=2)
        listener.close()


@pytest.fixture
def stand_in():
    from client.process import Process

    process = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
    pm = Process(Path(sys.executable).name, pid=process.pid)
    yield process, pm.process_handle
    process.kill()
    process.wait()
    pm.close_process()


@pytest.mark.skipif(not WINDOWS or not BUILT.is_file(), reason="helper not built")
def test_load_talk_and_unload_in_a_stand_in_process(stand_in) -> None:
    process, handle = stand_in
    native.load(handle, process.pid, BUILT)
    with pytest.raises(native.HelperError, match="already loaded"):
        native.load(handle, process.pid, BUILT)
    with native.Helper(process.pid) as helper:
        assert helper.request("hello").endswith(
            "build=unknown hooked=0 configured=0 feats=off folder=0 offline=- active=0 title=0 "
            "reads=0/-1"
        )
        assert helper.request("ping") == "pong frames=0"
        with pytest.raises(native.ModMismatch, match="doesn.t know the request nonsense"):
            helper.request("nonsense")
        # Hooked to nothing, it has no frames to work in: config and apply are refused.
        rvas = {key: 0x1000 for key in native.CONFIG_KEYS}
        with pytest.raises(native.HelperError, match="not hooked"):
            helper.configure(rvas)
        with pytest.raises(native.HelperError, match="not hooked"):
            helper.apply(native.batch_text([(36, 0, 1)], []))
        assert helper.request("ping") == "pong frames=0"  # error replies keep it in step

        def timed_out(data: bytes, seconds: float) -> bytes:
            raise native.HelperError(f"no reply within {seconds} s")

        helper._exchange = timed_out
        with pytest.raises(native.HelperError, match="no reply"):
            helper.request("ping")
        with pytest.raises(native.HelperError, match="closed after an error"):
            helper.request("ping")  # a timeout closes it for good: a late reply can't pair up
    with native.Helper(process.pid) as helper:
        assert helper.unload() == "bye"
    assert native.wait_unloaded(handle)
    # And it loads again after an unload, as a rebuilt helper would.
    native.load(handle, process.pid, BUILT)
    with native.Helper(process.pid) as helper:
        assert helper.unload() == "bye"
    assert native.wait_unloaded(handle)


@pytest.mark.skipif(not WINDOWS, reason="Windows processes")
def test_a_dead_game_is_not_taken_for_an_unloaded_helper(stand_in) -> None:
    process, handle = stand_in
    process.kill()
    process.wait()
    with pytest.raises(native.GameExited):
        native.is_loaded(handle)


def test_a_listing_that_keeps_failing_is_an_error_unless_the_game_died(monkeypatch) -> None:
    def failing(handle: int) -> dict:
        raise process.ProcessError("listing the process's modules failed (error 299)")

    running = True
    monkeypatch.setattr(process, "alive", lambda handle: running)
    monkeypatch.setattr(process, "modules", lambda handle: {"zexion_ap.dll": (1, 2)})
    assert native.is_loaded(1)
    monkeypatch.setattr(process, "modules", failing)
    with pytest.raises(native.HelperError, match="cannot list the game's modules"):
        native.is_loaded(1)  # not read as no helper
    running = False
    with pytest.raises(native.GameExited):
        native.is_loaded(1)


UNREADABLE = "?"  # a stand-in helper's version whose hello this client can't read


@dataclass
class _Game:
    """A stand-in game for attach: which helper version is loaded, the built file's, and
    whether the helper loaded is the game folder's copy (its hello's ``folder``)."""

    loaded: str | None = None  # the version loaded, None for none; UNREADABLE: a garbled hello
    file_version: str = "0.0.1"  # what the built file loads as
    folder: str = "0"
    build: str = "B1"  # the build the helper knows the game as
    stays: bool = False  # the helper refuses to unload
    loads: int = 0
    unloads: int = 0
    rva: dict[str, int] = field(default_factory=dict)
    pid = 1
    pm = types.SimpleNamespace(process_handle=1)


def stand_in_attach(monkeypatch, **fields) -> _Game:
    """attach's helper calls on a stand-in game with these fields, returned."""
    game = _Game(**fields)

    class FakeHelper:
        def __init__(self, pid: int) -> None:
            pass

        def __enter__(self):
            return self

        def __exit__(self, *exc: object) -> None:
            pass

        def close(self) -> None:
            pass

        def hello(self) -> dict[str, str]:
            if game.loaded == UNREADABLE:
                raise native.UnreadableHello("garbled")
            hooked = "1" if game.build != "unknown" else "0"
            return {
                "version": game.loaded or "",
                "folder": game.folder,
                "build": game.build,
                "hooked": hooked,
            }

        def request(self, text: str, *args: object) -> str:
            assert text == "unload"
            if game.stays:
                raise native.HelperError("a menu is open; not unloading")
            game.unloads += 1
            game.loaded = None
            return "bye"

        def configure(self, rvas: dict[str, int]) -> None:
            pass

    def load(handle, pid, dll, if_absent=False) -> bool:
        game.loads += 1
        game.loaded = game.file_version
        return True

    monkeypatch.setattr(native, "Helper", FakeHelper)
    monkeypatch.setattr(native, "is_loaded", lambda handle: game.loaded is not None)
    monkeypatch.setattr(native, "load", load)
    monkeypatch.setattr(native, "wait_unloaded", lambda handle: True)
    monkeypatch.setattr(native, "_STALE", {})
    return game


def test_an_old_game_folder_copy_is_refused_not_reloaded(tmp_path, monkeypatch) -> None:
    # The client from an apworld has no build: the game folder's copy is all there is, so a
    # copy of another version must be refused, not unloaded (FakeHelper takes nothing else)
    # and loaded again.
    (tmp_path / "zexion_ap.dll").write_bytes(b"")
    monkeypatch.setattr(native, "game_folder", lambda handle: tmp_path)
    game = stand_in_attach(monkeypatch, loaded="0.0.1", folder="1")
    with pytest.raises(native.HelperError, match="from this client's release"):
        native.attach(game, tmp_path / "no-build.dll")
    assert game.unloads == game.loads == 0


def test_a_stale_build_is_refused_not_reloaded_until_it_changes(tmp_path, monkeypatch) -> None:
    # Each retry must not unload and reload the same stale file.
    built = tmp_path / "zexion_ap.dll"
    built.write_bytes(b"")
    game = stand_in_attach(monkeypatch)
    with pytest.raises(native.ModFixable, match="rebuild native"):
        native.attach(game, built)
    assert game.loads == 1 and game.loaded == "0.0.1"
    for _ in range(2):
        with pytest.raises(native.HelperError, match="rebuild native"):
            native.attach(game, built)
    assert game.loads == 1 and game.unloads == 0, "refused without unloading or loading"
    # Rebuilt: the old helper is put back by the new one.
    game.file_version = native.VERSION
    later = built.stat().st_mtime_ns + 1_000_000_000
    os.utime(built, ns=(later, later))
    native.attach(game, built)
    assert game.unloads == 1 and game.loads == 2 and game.loaded == native.VERSION


def test_a_stale_build_is_not_loaded_into_a_game_without_a_helper(tmp_path, monkeypatch) -> None:
    built = tmp_path / "zexion_ap.dll"
    built.write_bytes(b"")
    game = stand_in_attach(monkeypatch)
    with pytest.raises(native.HelperError, match="rebuild native"):
        native.attach(game, built)
    game.loaded = None  # the game restarted
    with pytest.raises(native.HelperError, match="rebuild native"):
        native.attach(game, built)
    assert game.loads == 1


def test_the_off_switch_is_read_from_the_games_folder(tmp_path, monkeypatch) -> None:
    monkeypatch.setattr(native, "game_folder", lambda handle: tmp_path)
    assert not native.mod_disabled(1)
    (tmp_path / "zexion_ap.disabled").write_bytes(b"")
    assert native.mod_disabled(1)
    monkeypatch.setattr(native, "game_folder", lambda handle: None)
    assert not native.mod_disabled(1), "no folder read: not turned off"


def turned_off(tmp_path: Path, monkeypatch) -> Path:
    """A game folder holding the helper and zexion_ap.disabled; its helper's path."""
    folder_copy = tmp_path / "zexion_ap.dll"
    folder_copy.write_bytes(b"")
    (tmp_path / "zexion_ap.disabled").write_bytes(b"")
    monkeypatch.setattr(native, "game_folder", lambda handle: tmp_path)
    return folder_copy


def test_a_turned_off_mod_is_not_loaded_from_the_games_folder(tmp_path, monkeypatch) -> None:
    turned_off(tmp_path, monkeypatch)
    game = stand_in_attach(monkeypatch, file_version=native.VERSION)
    with pytest.raises(native.ModDisabled, match="zexion_ap.disabled"):
        native.attach(game, tmp_path / "no-build" / "zexion_ap.dll")
    assert game.loads == 0


def test_a_turned_off_mod_still_loads_the_clients_own_build(tmp_path, monkeypatch) -> None:
    turned_off(tmp_path, monkeypatch)
    built = tmp_path / "build" / "zexion_ap.dll"
    built.parent.mkdir()
    built.write_bytes(b"")
    game = stand_in_attach(monkeypatch, file_version=native.VERSION)
    native.attach(game, built)
    assert game.loads == 1 and game.loaded == native.VERSION


def test_a_turned_off_mod_already_loaded_is_used(tmp_path, monkeypatch) -> None:
    turned_off(tmp_path, monkeypatch)
    game = stand_in_attach(monkeypatch, loaded=native.VERSION)
    native.attach(game, tmp_path / "no-build" / "zexion_ap.dll")
    assert game.loads == 0 and game.unloads == 0


def piped_helper(monkeypatch, exchange) -> tuple[native.Helper, list[int]]:
    """A Helper on a stand-in pipe: ``exchange`` answers each request; the handles closed are
    listed."""
    closed: list[int] = []

    class Kernel32:
        def CloseHandle(self, handle: int) -> bool:
            closed.append(handle)
            return True

    monkeypatch.setattr(native, "_kernel32", Kernel32)
    helper = native.Helper.__new__(native.Helper)
    helper._handle = 1234
    helper._exchange = exchange
    return helper, closed


@pytest.mark.parametrize(
    ("why", "said"),
    [
        ("a menu is open", "with a menu open: close it and try again"),
        ("the ending's patch is still the helper's", "(the ending's patch is still the helper's)"),
    ],
)
def test_reload_mod_refused_leaves_the_mod_loaded(monkeypatch, why: str, said: str) -> None:
    # What /reload_mod does (zexion_client.poll): the unload request, then reload_reply.
    refusal = f"error {why}; not unloading".encode()
    helper, closed = piped_helper(monkeypatch, lambda data, seconds: refusal)
    unloaded, line = state.reload_reply(helper.unload())
    assert not unloaded and said in line, "a refusal, not a failed request"
    assert closed == [] and helper._handle == 1234, "the pipe stays open: the helper works on"


def test_reload_mod_loads_again_after_bye_or_a_failed_request(monkeypatch) -> None:
    helper, closed = piped_helper(monkeypatch, lambda data, seconds: b"bye")
    assert helper.unload() == "bye"

    def broken(data: bytes, seconds: float) -> bytes:
        raise native.HelperError("the helper's pipe closed: 109")

    helper, closed = piped_helper(monkeypatch, broken)
    unloaded, line = state.reload_reply(helper.unload())
    assert unloaded and "didn't answer" in line and closed == [1234]
    # Any other error reply: what the helper did is not known, so it is dropped too.
    helper, closed = piped_helper(monkeypatch, lambda data, seconds: b"error unknown request")
    assert helper.unload() is None and closed == [1234]


def test_hello_keeps_the_feats_guards_reason_as_one_field(monkeypatch) -> None:
    reply = b"zexion-ap 0.30.0 build=B1 hooked=1 feats=on:steam_api64_is_not_loaded folder=1"
    helper, _ = piped_helper(monkeypatch, lambda data, seconds: reply)
    hello = helper.hello()
    assert hello["feats"] == "on:steam_api64_is_not_loaded" and hello["folder"] == "1"
    _, detail = state.FeatsCheck().seen(hello["feats"], True, 0.0, state.SayOnce())
    assert detail == "not all in place: steam_api64_is_not_loaded"


# --- a helper that doesn't match the client ----------------------------------------------------


@pytest.mark.parametrize(
    ("version", "own_build", "said"),
    [
        ("0.29.0", False, ["the mod in Zexion's folder is version 0.29.0", "client is version"]),
        (
            "9.0.0",
            False,
            ["version 9.0.0", "newer than the client", "release 9.0.0's zexion.apworld"],
        ),
        ("", False, ["another version than this client's", "too old or too new"]),
        ("0.29.0", True, ["the mod built at", "version 0.29.0", "rebuild native/"]),
    ],
)
def test_another_versions_message_names_both_and_what_to_do(version, own_build, said) -> None:
    exc = native._wrong_version(Path("zexion_ap.dll"), version, own_build)
    line = state.helper_failed(exc)
    assert isinstance(exc, native.ModMismatch)
    assert line.startswith("The Zexion mod and this client don't match: ")
    assert native.VERSION in line and all(s in line for s in said)
    assert "zexion_ap.dll and version.dll" in line


@pytest.mark.parametrize(
    "reply",
    [b"zexion-ap build=B1 hooked=1", b"zexion-ap", b"pong frames=3", b"", b"error unknown request"],
)
def test_a_hello_this_client_cant_read_is_another_version(monkeypatch, reply: bytes) -> None:
    helper, _ = piped_helper(monkeypatch, lambda data, seconds: reply)
    with pytest.raises(native.UnreadableHello, match="another version") as caught:
        helper.hello()
    assert "don't match" in state.helper_failed(caught.value)


def test_an_unreadable_mod_in_the_game_is_refused_without_a_build(tmp_path, monkeypatch) -> None:
    # Where it came from is not known, so it is not unloaded for the game folder's copy, likely
    # the very same file.
    (tmp_path / "zexion_ap.dll").write_bytes(b"")
    monkeypatch.setattr(native, "game_folder", lambda handle: tmp_path)
    game = stand_in_attach(monkeypatch, loaded=UNREADABLE)
    with pytest.raises(native.ModMismatch, match="the mod loaded in Zexion is another version"):
        native.attach(game, tmp_path / "no-build.dll")
    assert game.unloads == game.loads == 0


def test_an_unreadable_mod_in_the_game_is_replaced_by_the_build(tmp_path, monkeypatch) -> None:
    built = tmp_path / "zexion_ap.dll"
    built.write_bytes(b"")
    game = stand_in_attach(monkeypatch, loaded=UNREADABLE, file_version=native.VERSION)
    native.attach(game, built)
    assert game.unloads == 1 and game.loads == 1 and game.loaded == native.VERSION


def test_a_build_that_loads_unreadable_is_not_loaded_again(tmp_path, monkeypatch) -> None:
    built = tmp_path / "zexion_ap.dll"
    built.write_bytes(b"")
    game = stand_in_attach(monkeypatch, file_version=UNREADABLE)
    for _ in range(2):
        with pytest.raises(native.ModMismatch, match="rebuild native"):
            native.attach(game, built)
    assert game.loads == 1


def test_a_mod_of_another_version_that_wont_unload_says_they_dont_match(
    tmp_path, monkeypatch
) -> None:
    built = tmp_path / "zexion_ap.dll"
    built.write_bytes(b"")
    game = stand_in_attach(monkeypatch, loaded="0.29.0", stays=True)
    with pytest.raises(native.ModMismatch, match="version 0.29.0, this client") as caught:
        native.attach(game, built)
    assert "restart Zexion" in str(caught.value) and game.loads == 0


def test_a_mod_that_doesnt_know_the_games_build_is_another_build(tmp_path, monkeypatch) -> None:
    built = tmp_path / "zexion_ap.dll"
    built.write_bytes(b"")
    game = stand_in_attach(monkeypatch, file_version=native.VERSION, build="unknown")
    with pytest.raises(native.ModMismatch, match="different builds"):
        native.attach(game, built)


@pytest.mark.parametrize(
    ("request_text", "reply", "said"),
    [
        ("disk_slots 1 2 3", b"error unknown request", "doesn't know the request disk_slots"),
        ("ap_newgame\nfoo 1", b"error unknown line foo 1", "a line of ap_newgame (foo)"),
    ],
)
def test_a_request_the_mod_doesnt_know_is_another_build(
    monkeypatch, request_text: str, reply: bytes, said: str
) -> None:
    helper, _ = piped_helper(monkeypatch, lambda data, seconds: reply)
    with pytest.raises(native.ModMismatch, match="different builds") as caught:
        helper.request(request_text)
    assert said in str(caught.value) and native.VERSION in str(caught.value)


def test_an_address_the_mod_needs_and_the_client_lacks_is_another_build(monkeypatch) -> None:
    rvas = dict.fromkeys(native.CONFIG_KEYS, 0x1000)
    helper, _ = piped_helper(monkeypatch, lambda data, seconds: b"error missing fade")
    with pytest.raises(native.ModMismatch, match="needs the address fade"):
        helper.configure(rvas)
    # Any other refusal of the addresses is no mismatch: said plainly as before.
    helper, _ = piped_helper(monkeypatch, lambda data, seconds: b"error inventory not writable")
    with pytest.raises(native.HelperError) as caught:
        helper.configure(rvas)
    assert not isinstance(caught.value, native.ModMismatch)
