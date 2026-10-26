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

from client import native

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
            "build=unknown hooked=0 configured=0 skip=0 folder=0 offline=- active=0 title=0 "
            "reads=0/-1"
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
        assert helper.request("unload") == "bye"
    assert native.wait_unloaded(handle)
    # And it loads again after an unload, as a rebuilt helper would.
    native.load(handle, process.pid, BUILT)
    with native.Helper(process.pid) as helper:
        assert helper.request("unload") == "bye"
    assert native.wait_unloaded(handle)


@pytest.mark.skipif(not WINDOWS, reason="Windows processes")
def test_a_dead_game_is_not_taken_for_an_unloaded_helper(stand_in) -> None:
    process, handle = stand_in
    process.kill()
    process.wait()
    with pytest.raises(native.GameExited):
        native.is_loaded(handle)


class _OldHelper:
    """A helper of another version, loaded from the game's folder."""

    requests: list[str] = []

    def __init__(self, pid: int) -> None:
        pass

    def __enter__(self):
        return self

    def __exit__(self, *exc: object) -> None:
        pass

    def hello(self) -> dict[str, str]:
        return {"version": "0.0.1", "folder": "1"}

    def request(self, text: str, *args: object) -> str:
        _OldHelper.requests.append(text)
        return "bye"


def test_an_old_game_folder_copy_is_refused_not_reloaded(tmp_path, monkeypatch) -> None:
    # The client from an apworld has no build: the game folder's copy is all there is, so a
    # copy of another version must be refused, not unloaded and loaded again.
    folder_copy = tmp_path / "zexion_ap.dll"
    folder_copy.write_bytes(b"")
    monkeypatch.setattr(native, "game_folder_dll", lambda handle: folder_copy)
    monkeypatch.setattr(native, "is_loaded", lambda handle: True)
    monkeypatch.setattr(native, "Helper", _OldHelper)
    _OldHelper.requests = []

    class Game:
        pid = 1

        class pm:
            process_handle = 1

    with pytest.raises(native.HelperError, match="copy this release's"):
        native.attach(Game(), tmp_path / "no-build.dll")
    assert "unload" not in _OldHelper.requests


class _Game:
    """A stand-in game for attach: which helper version is loaded, and the built file's."""

    pid = 1

    class pm:
        process_handle = 1

    def __init__(self) -> None:
        self.loaded: str | None = None  # the version loaded, None for none
        self.file_version = "0.0.1"  # what the built file loads as
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
            return {"version": game.loaded or "", "folder": "0", "hooked": "1"}

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


def test_a_stale_build_is_refused_not_reloaded_until_it_changes(tmp_path, monkeypatch) -> None:
    # Before, every retry unloaded the helper the stale build had loaded, loaded the same file
    # again, and failed the version check after loading.
    built = tmp_path / "zexion_ap.dll"
    built.write_bytes(b"")
    game = _Game()
    stand_in_attach(monkeypatch, game)
    with pytest.raises(native.HelperError, match="rebuild native"):
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


def test_the_popups_waiting_are_read_from_the_helpers_reply(monkeypatch) -> None:
    helper = object.__new__(native.Helper)
    replies = iter(["pending 3", "pending 0", "pending", "pending -1", "refused no frame"])
    monkeypatch.setattr(helper, "request", lambda text, *args: next(replies), raising=False)
    assert helper.popups_pending() == 3
    assert helper.popups_pending() == 0
    for _ in range(3):
        with pytest.raises(native.HelperError, match="unexpected reply to popups"):
            helper.popups_pending()
