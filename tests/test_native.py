"""client/native.py, and the built helper loaded into a stand-in process (never the game).

The stand-in is a sleeping Python process: the helper finds no known build there, so it hooks
nothing, but loading, the pipe and unloading are the same as in the game. Skipped where the
helper has not been built (CI's Python jobs) or off Windows. native/tests/ cover the hook
itself, including unloading under load.
"""

from __future__ import annotations

import os
import subprocess
import sys
import threading
from pathlib import Path

import pytest

from client import native, process, state

WINDOWS = sys.platform == "win32"
BUILT = native.default_dll()


def test_pipe_name_and_default_dll(monkeypatch: pytest.MonkeyPatch) -> None:
    assert native.pipe_name(1234) == r"\\.\pipe\zexion-ap-1234"
    monkeypatch.delenv("ZEXION_DLL", raising=False)
    assert native.default_dll().parts[-4:] == ("native", "build", "Release", "zexion_ap.dll")
    monkeypatch.setenv("ZEXION_DLL", "elsewhere/x.dll")
    assert native.default_dll() == Path("elsewhere/x.dll")


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
            "build=unknown hooked=0 configured=0 skip=0 feats=off folder=0 offline=- active=0 "
            "title=0 reads=0/-1"
        )
        assert helper.request("ping") == "pong frames=0"
        with pytest.raises(native.HelperError, match="unknown request"):
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


class _Game:
    """A stand-in game for attach: which helper version is loaded, the built file's, and
    whether the helper loaded is the game folder's copy (its hello's ``folder``)."""

    pid = 1

    class pm:
        process_handle = 1

    def __init__(self) -> None:
        self.loaded: str | None = None  # the version loaded, None for none
        self.file_version = "0.0.1"  # what the built file loads as
        self.folder = "0"
        self.loads = 0
        self.unloads = 0
        self.rva: dict[str, int] = {}


def stand_in_attach(monkeypatch, game: _Game) -> None:
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
            return {"version": game.loaded or "", "folder": game.folder, "hooked": "1"}

        def request(self, text: str, *args: object) -> str:
            assert text == "unload"
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


def test_an_old_game_folder_copy_is_refused_not_reloaded(tmp_path, monkeypatch) -> None:
    # The client from an apworld has no build: the game folder's copy is all there is, so a
    # copy of another version must be refused, not unloaded (FakeHelper takes nothing else)
    # and loaded again.
    (tmp_path / "zexion_ap.dll").write_bytes(b"")
    monkeypatch.setattr(native, "game_folder", lambda handle: tmp_path)
    game = _Game()
    game.loaded, game.folder = "0.0.1", "1"
    stand_in_attach(monkeypatch, game)
    with pytest.raises(native.HelperError, match="copy this release's"):
        native.attach(game, tmp_path / "no-build.dll")
    assert game.unloads == game.loads == 0


def test_a_stale_build_is_refused_not_reloaded_until_it_changes(tmp_path, monkeypatch) -> None:
    # Each retry must not unload and reload the same stale file.
    built = tmp_path / "zexion_ap.dll"
    built.write_bytes(b"")
    game = _Game()
    stand_in_attach(monkeypatch, game)
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
    game = _Game()
    stand_in_attach(monkeypatch, game)
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
    game = _Game()
    game.file_version = native.VERSION
    stand_in_attach(monkeypatch, game)
    with pytest.raises(native.ModDisabled, match="zexion_ap.disabled"):
        native.attach(game, tmp_path / "no-build" / "zexion_ap.dll")
    assert game.loads == 0


def test_a_turned_off_mod_still_loads_the_clients_own_build(tmp_path, monkeypatch) -> None:
    turned_off(tmp_path, monkeypatch)
    built = tmp_path / "build" / "zexion_ap.dll"
    built.parent.mkdir()
    built.write_bytes(b"")
    game = _Game()
    game.file_version = native.VERSION
    stand_in_attach(monkeypatch, game)
    native.attach(game, built)
    assert game.loads == 1 and game.loaded == native.VERSION


def test_a_turned_off_mod_already_loaded_is_used(tmp_path, monkeypatch) -> None:
    turned_off(tmp_path, monkeypatch)
    game = _Game()
    game.loaded = native.VERSION
    stand_in_attach(monkeypatch, game)
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
    assert state.reload_reply(helper.unload()) == (
        True,
        "The Zexion mod was unloaded; loading it again.",
    )

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
    assert state.feats_unguarded(hello["feats"]) == "steam_api64_is_not_loaded"
