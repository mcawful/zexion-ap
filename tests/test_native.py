"""client/native.py, and the built helper loaded into a stand-in process (never the game).

The stand-in is a sleeping Python process: the helper finds no known build there, so it hooks
nothing, but loading, the pipe and unloading are the same as in the game. Skipped where the
helper has not been built (CI's pytest jobs) or off Windows.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

import native


def test_pipe_name_and_default_dll(monkeypatch: pytest.MonkeyPatch) -> None:
    assert native.pipe_name(1234) == r"\\.\pipe\zexion-ap-1234"
    monkeypatch.delenv("ZEXION_DLL", raising=False)
    assert native.default_dll().parts[-4:] == ("native", "build", "Release", "zexion_ap.dll")
    monkeypatch.setenv("ZEXION_DLL", "elsewhere/x.dll")
    assert native.default_dll() == Path("elsewhere/x.dll")


def test_load_refuses_a_missing_dll(tmp_path: Path) -> None:
    with pytest.raises(native.HelperError, match="build native"):
        native.load(0, tmp_path / "missing.dll")


BUILT = native.default_dll()


@pytest.mark.skipif(sys.platform != "win32" or not BUILT.is_file(), reason="helper not built")
def test_load_talk_and_unload_in_a_stand_in_process() -> None:
    import pymem

    stand_in = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
    try:
        pm = pymem.Pymem()
        pm.open_process_from_id(stand_in.pid)
        handle = pm.process_handle
        native.load(handle, BUILT)
        with pytest.raises(native.HelperError, match="already loaded"):
            native.load(handle, BUILT)
        with native.Helper(stand_in.pid) as helper:
            assert helper.request("hello").endswith("build=unknown hooked=0")
            assert helper.request("ping") == "pong frames=0"
            with pytest.raises(native.HelperError, match="unknown request"):
                helper.request("nonsense")
            assert helper.request("unload") == "bye"
        assert native.wait_unloaded(handle)
        # And it loads again after an unload, as a rebuilt helper would.
        native.load(handle, BUILT)
        with native.Helper(stand_in.pid) as helper:
            assert helper.request("unload") == "bye"
        assert native.wait_unloaded(handle)
        pm.close_process()
    finally:
        stand_in.kill()
