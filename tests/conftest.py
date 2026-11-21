"""Shared fixtures for the tests."""

from __future__ import annotations

import os
import sys
import tempfile
from pathlib import Path

import pytest

SYNCED_PREFIX = "ZexionAP-seed-"  # the helpers' copies in the save folder (offline.h)


def _real_folders() -> tuple[list[Path], list[Path]]:
    """The real save folders and the helper's real per-user folders, as the operating system names
    them for this user (native/src/offline.cpp: os_save_folders and os_offline_folders: Windows'
    known folders; on Linux the passwd entry's home, under ``.local/share``, Flatpak Steam's
    ``.var/app/com.valvesoftware.Steam/.local/share`` and its ``data``), not through
    ZEXION_SAVE_DIR, ZEXION_LAB_DIR, $HOME or the other variables a test run may point at scratch
    folders; on Linux the $XDG_DATA_HOME this session started with too, if it is absolute and not
    inside the temporary folder."""
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
            [roaming / "Gallant Leaf" / "Zexion"] if roaming else [],
            [local / "ZexionAP"] if local else [],
        )
    import pwd

    try:
        home = Path(pwd.getpwuid(os.getuid()).pw_dir)
    except KeyError:
        return [], []
    bases = [
        home / ".local" / "share",
        home / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share",
        home / ".var" / "app" / "com.valvesoftware.Steam" / "data",
    ]
    data = os.environ.get("XDG_DATA_HOME", "")
    own = data.startswith("/") and not Path(data).is_relative_to(tempfile.gettempdir())
    if own and Path(data) not in bases:
        bases.append(Path(data))
    return [b / "Gallant Leaf" / "Zexion" for b in bases], [b / "ZexionAP" for b in bases]


def _listing(folder: Path, prefix: str = "", recursive: bool = False) -> list[tuple] | str:
    """Every entry of ``folder`` (its files whose names begin with ``prefix``, or all of them,
    under it too if ``recursive``) as (name, size, mtime), sorted; "absent" where the folder is
    not there. A file made and deleted since shows in no listing, but any other change does."""
    if not folder.is_dir():
        return "absent"
    paths = folder.rglob("*") if recursive else folder.iterdir()
    entries = []
    for path in paths:
        if not path.name.startswith(prefix):
            continue
        try:
            stat = path.stat()
        except OSError:
            continue
        entries.append((path.relative_to(folder).as_posix(), stat.st_size, stat.st_mtime_ns))
    return sorted(entries)


def _snapshot() -> dict[str, object]:
    saves, locals_ = _real_folders()
    state: dict[str, object] = {}
    for folder in saves:
        state[f"save {folder}"] = _listing(folder, SYNCED_PREFIX)
    for folder in locals_:
        state[f"offline {folder}"] = _listing(folder, recursive=True)
    return state


@pytest.fixture(scope="session", autouse=True)
def real_folders_are_untouched():
    """A canary: the tests never write the real save folders or the helper's real per-user
    folders. It lists the seed copies in each real save folder and everything in each real
    per-user folder (the seeds, the config files, the Linux token, any ``*.tmp``) with sizes and
    write times, absent folders included, and fails the session if any listing changed or a
    folder appeared (read only). The native tests have the same in CTest
    (native/tests/canary.cpp)."""
    before = _snapshot()
    yield
    after = _snapshot()
    changed = [name for name in before if before[name] != after.get(name)]
    assert not changed, "a test changed a real folder: " + "; ".join(
        f"{name}: {before[name]!r} before, {after.get(name)!r} after" for name in changed
    )
