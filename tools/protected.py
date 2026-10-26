"""The folders this project must never write to directly, and where to find them.

Three places are protected:

- the game install (any ``steamapps/common/Zexion``, in any Steam library),
- the real save folder (``%APPDATA%/Gallant Leaf/Zexion``),
- Steam's cloud cache for the game (``Steam/userdata/<account>/3392510``).

Matching is by pattern rather than by this machine's paths, so the same rules hold on any PC.
The write-guard hook (``.claude/hooks/guard_protected.py``) and the tools in this folder share
this one definition. Of the tools, only ``save_lab.py install`` and ``save_lab.py restore``
write into the save folder, and only after a verified backup. The one other writer is the
Archipelago client's ``/prepare`` (``client/prepare.py``), which keeps its own checks (Zexion
closed, a verified backup, an atomic replace) because the client does not ship this module.

``append_log`` is the JSON-lines log that ``grant.py`` and ``helper.py`` keep in ``lab/`` of
each change they make to the running game.
"""

from __future__ import annotations

import contextlib
import datetime as dt
import json
import os
import re
import sys
from pathlib import Path

STEAM_APP_ID = 3392510

_PROTECTED = [
    re.compile(r"steamapps/common/zexion(?![\w-])"),
    re.compile(r"gallant leaf/zexion(?![\w-])"),
    re.compile(rf"/userdata/\d+/{STEAM_APP_ID}(?![\w-])"),
]

# Environment variables that commonly prefix a path to the saves or the Steam library.
_ENV_NAMES = ("APPDATA", "LOCALAPPDATA", "USERPROFILE", "HOME", "PROGRAMFILES(X86)", "PROGRAMFILES")


class ProtectedPathError(RuntimeError):
    """Raised when a tool is asked to write into a protected folder."""


def _expand(text: str) -> str:
    """Expand the env-var spellings of cmd, PowerShell and sh, and a leading ``~``."""
    for name in _ENV_NAMES:
        value = os.environ.get(name)
        if not value:
            continue
        esc = re.escape(name)
        for pattern in (
            rf"%{esc}%",
            rf"\$env:{esc}(?!\w)",
            rf"\$\{{env:{esc}\}}",
            rf"\$\{{{esc}\}}",
            rf"\${esc}(?!\w)",
        ):
            text = re.sub(pattern, lambda _m, v=value: v, text, flags=re.IGNORECASE)
    home = os.path.expanduser("~")
    text = re.sub(r"(^|[\s\"'=])~(?=[/\\])", lambda m: m.group(1) + home, text)
    return text


def normalize(text: str) -> str:
    """Lowercase, forward slashes, env vars expanded, ``\\ `` unescaped, repeated slashes merged."""
    text = _expand(text)
    text = text.replace("\\ ", " ").replace("\\", "/")
    text = re.sub(r"/{2,}", "/", text)
    return text.lower()


def mentions_protected(text: str) -> bool:
    """True when free text (a shell command, say) names a protected folder anywhere in it."""
    norm = normalize(text)
    return any(p.search(norm) for p in _PROTECTED)


def is_protected_path(path: str | os.PathLike[str]) -> bool:
    """True when ``path`` is inside a protected folder, before or after resolving links."""
    raw = os.fspath(path)
    candidates = [raw]
    with contextlib.suppress(OSError, ValueError):
        candidates.append(os.path.realpath(_expand(raw)))
    return any(mentions_protected(c + "/") for c in candidates)


def assert_writable(path: str | os.PathLike[str]) -> Path:
    """Return ``path`` as a Path, or raise if it is inside a protected folder."""
    if is_protected_path(path):
        raise ProtectedPathError(
            f"refusing to write {path}: it is inside the game install or the real save folder. "
            "Work on a copy in lab/ (see /save-lab)."
        )
    return Path(path)


def append_log(path: str | os.PathLike[str], entry: dict) -> None:
    """Append ``entry``, stamped with the time, as one JSON line to ``path``.

    It raises rather than skip the line (a protected path, a full disk), so a tool that logs
    before it changes the game stops when the log cannot be written.
    """
    path = assert_writable(path)
    entry = {"time": dt.datetime.now().isoformat(timespec="milliseconds"), **entry}
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as f:
        f.write(json.dumps(entry) + "\n")


def save_dir() -> Path:
    """The real save folder. ``ZEXION_SAVE_DIR`` overrides it (tests use this)."""
    override = os.environ.get("ZEXION_SAVE_DIR")
    if override:
        return Path(override)
    appdata = os.environ.get("APPDATA")
    if not appdata:
        raise RuntimeError("APPDATA is not set; set ZEXION_SAVE_DIR to the save folder")
    return Path(appdata) / "Gallant Leaf" / "Zexion"


def _steam_root() -> Path | None:
    if sys.platform != "win32":
        return None
    import winreg

    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
            value, _ = winreg.QueryValueEx(key, "SteamPath")
    except OSError:
        return None
    return Path(value)


def game_dir() -> Path:
    """The game install. ``ZEXION_GAME_DIR`` overrides it; otherwise Steam's libraries are tried."""
    override = os.environ.get("ZEXION_GAME_DIR")
    if override:
        return Path(override)
    root = _steam_root()
    libraries: list[Path] = []
    if root is not None:
        libraries.append(root)
        vdf = root / "steamapps" / "libraryfolders.vdf"
        if vdf.is_file():
            text = vdf.read_text(encoding="utf-8", errors="replace")
            for match in re.finditer(r'"path"\s+"([^"]+)"', text):
                libraries.append(Path(match.group(1).replace("\\\\", "\\")))
    for library in libraries:
        candidate = library / "steamapps" / "common" / "Zexion"
        if (candidate / "Zexion.exe").is_file():
            return candidate
    raise RuntimeError(
        "Zexion install not found; set ZEXION_GAME_DIR to the folder with Zexion.exe"
    )


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def lab_dir() -> Path:
    """Where backups and working copies go. Gitignored. ``ZEXION_LAB_DIR`` overrides it."""
    override = os.environ.get("ZEXION_LAB_DIR")
    path = Path(override) if override else repo_root() / "lab"
    return assert_writable(path)
