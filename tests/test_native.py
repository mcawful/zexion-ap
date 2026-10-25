"""client/native.py, and the built helper loaded into a stand-in process (never the game).

The stand-in is a sleeping Python process: the helper finds no known build there, so it hooks
nothing, but loading, the pipe and unloading are the same as in the game. Skipped where the
helper has not been built (CI's Python jobs) or off Windows. native/tests/ cover the hook
itself, including unloading under load.
"""

from __future__ import annotations

import subprocess
import sys
import threading
from pathlib import Path

import pytest

import native

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
    import pymem

    process = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
    pm = pymem.Pymem()
    pm.open_process_from_id(process.pid)
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
