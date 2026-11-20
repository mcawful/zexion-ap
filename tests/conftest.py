"""Shared fixtures for the tests."""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

from client import ticks

SYNCED_GLOB = "ZexionAP-seed-*.sav"  # the helpers' copies in the save folder (offline.h)


@pytest.fixture(autouse=True)
def no_stored_linux_seed(monkeypatch) -> None:
    """The Linux helper's seed files are the machine's, not a test's: none is read (a test of
    ticks.linux_stored_seed itself keeps the function it took at import)."""
    monkeypatch.setattr(ticks, "linux_stored_seed", lambda mod, name: None)


def _real_folders() -> tuple[Path | None, Path | None]:
    """The real save folder and the helper's real seed folder, as the operating system names
    them for this user (native/src/offline.cpp: os_save_folder and os_offline_folder: Windows'
    known folders; on Linux the passwd entry's home), not through ZEXION_SAVE_DIR, ZEXION_LAB_DIR,
    $HOME or the other variables a test run may point at scratch folders."""
    if sys.platform == "win32":
        import ctypes
        from ctypes import wintypes

        def known(guid: str) -> Path | None:
            ident = ctypes.create_string_buffer(16)
            ctypes.oledll.ole32.CLSIDFromString(guid, ident)
            out = ctypes.c_wchar_p()
            get = ctypes.windll.shell32.SHGetKnownFolderPath
            get.argtypes = [ctypes.c_void_p, wintypes.DWORD, wintypes.HANDLE, ctypes.c_void_p]
            if get(ident, 0, None, ctypes.byref(out)) != 0 or not out.value:
                return None
            path = Path(out.value)
            ctypes.windll.ole32.CoTaskMemFree(out)
            return path

        roaming = known("{3EB685DB-65F9-4CF6-A03A-E3EF65729F3D}")
        local = known("{F1B32785-6FBA-4FCF-9D55-7B8E7F157091}")
        return (
            roaming / "Gallant Leaf" / "Zexion" if roaming else None,
            local / "ZexionAP" / "seeds" if local else None,
        )
    import pwd

    try:
        home = Path(pwd.getpwuid(os.getuid()).pw_dir)
    except KeyError:
        return None, None
    return (
        home / ".local" / "share" / "Gallant Leaf" / "Zexion",
        home / ".local/share/ZexionAP/seeds",
    )


def _snapshot(folder: Path | None, pattern: str) -> tuple[int, int] | None:
    """How many files match ``pattern`` in ``folder`` and the newest write time, or None where
    the folder is not there."""
    if folder is None or not folder.is_dir():
        return None
    stats = [p.stat().st_mtime_ns for p in folder.glob(pattern) if p.is_file()]
    return len(stats), max(stats, default=0)


@pytest.fixture(scope="session", autouse=True)
def real_folders_are_untouched():
    """A canary: the tests never write the real save folder or the helper's real seed folder. It
    records how many seed copies and seed files each holds and the newest write, and fails the
    session if either changed (read only; skipped for a folder that is not there, as in CI).
    The native tests have the same in CTest (native/tests/canary.cpp)."""
    save, seeds = _real_folders()
    before = (_snapshot(save, SYNCED_GLOB), _snapshot(seeds, "*.txt"))
    yield
    after = (_snapshot(save, SYNCED_GLOB), _snapshot(seeds, "*.txt"))
    for name, was, now in (
        ("save folder", before[0], after[0]),
        ("seed folder", before[1], after[1]),
    ):
        if was is not None:
            assert now == was, f"a test wrote the real {name}: {was} before, {now} after"
