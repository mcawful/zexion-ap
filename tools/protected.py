"""The folders this project must never write to directly, and where to find them.

Four places are protected:

- the game install (any ``steamapps/common/Zexion``, in any Steam library),
- the real save folder (``%APPDATA%/Gallant Leaf/Zexion``; on Linux SDL's, under
  ``~/.local/share``, or Flatpak Steam's: any ``Gallant Leaf/Zexion``),
- Steam's cloud cache for the game (``Steam/userdata/<account>/3392510``),
- the mod's offline files (``%LOCALAPPDATA%/ZexionAP``: the seed list, labels and disk counts
  the helper keeps for play without the client). Only the helper writes them.

Matching is by pattern rather than by this machine's paths, so the same rules hold on any PC.
The write-guard hook (``.claude/hooks/guard_protected.py``) and the tools in this folder share
this one definition. Only ``save_lab.py install`` and ``save_lab.py restore`` write into the
save folder, and only after a verified backup; the Archipelago client never writes it.

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

ROOT = Path(__file__).resolve().parent.parent  # the repo
STEAM_APP_ID = 3392510

_PROTECTED = [
    re.compile(r"steamapps/common/zexion(?![\w-])"),
    re.compile(r"gallant leaf/zexion(?![\w-])"),
    re.compile(rf"(?<![\w-])userdata/\d+/{STEAM_APP_ID}(?![\w-])"),
    # The offline folder: ``ZexionAP`` after ``AppData/Local`` or ``LOCALAPPDATA`` (or
    # ``LocalApplicationData``), with at most a few characters between, so an env var that is
    # not set here, or ``os.environ['LOCALAPPDATA'], 'ZexionAP'``, still counts. A longer name
    # (``ZexionAP-notes``, ``ZexionAP.md``) does not.
    re.compile(
        r"(?:appdata\W{0,8}local|local(?:app(?:lication)?)?data)\W{0,8}zexionap(?![\w-]|\.\w)"
    ),
]
# The same folders as path components, for ``word_may_name_protected``. ``_DIGITS`` stands for
# the Steam account id.
_DIGITS = object()
_PROTECTED_PARTS: list[tuple] = [
    ("steamapps", "common", "zexion"),
    ("gallant leaf", "zexion"),
    ("userdata", _DIGITS, str(STEAM_APP_ID)),
    ("appdata", "local", "zexionap"),
    ("localappdata", "zexionap"),  # the variable left unexpanded (``$env:LOCALAPPDATA``)
]

# Environment variables that commonly prefix a path to the saves or the Steam library.
_ENV_NAMES = (
    "APPDATA",
    "LOCALAPPDATA",
    "USERPROFILE",
    "HOME",
    "TEMP",  # %LOCALAPPDATA%\Temp: ``$TEMP/../ZexionAP`` is the offline folder
    "TMP",
    "PROGRAMFILES(X86)",
    "PROGRAMFILES",
)


class ProtectedPathError(RuntimeError):
    """Raised when a tool is asked to write into a protected folder."""


def _expand(text: str) -> str:
    """Expand the env-var spellings of cmd, PowerShell and sh, and a leading ``~``."""
    for name in _ENV_NAMES:
        value = os.environ.get(name)
        if not value:
            continue
        esc = re.escape(name)
        # %NAME%, $env:NAME, ${env:NAME}, ${NAME}, $NAME
        spellings = rf"%{esc}%|\$env:{esc}(?!\w)|\$\{{env:{esc}\}}|\$\{{{esc}\}}|\${esc}(?!\w)"
        text = re.sub(spellings, lambda _m, v=value: v, text, flags=re.IGNORECASE)
    home = os.path.expanduser("~")
    text = re.sub(r"(^|[\s\"'=])~(?=[/\\])", lambda m: m.group(1) + home, text)
    return text


def normalize(text: str) -> str:
    """Lowercase, forward slashes, env vars expanded, ``\\ `` unescaped, repeated slashes merged."""
    text = _expand(text)
    text = text.replace("\\ ", " ").replace("\\", "/")
    text = re.sub(r"/{2,}", "/", text)
    return text.lower()


def _local_appdata() -> str:
    """``LOCALAPPDATA`` as ``normalize`` reads a path, without a trailing slash. "" when it is
    not set (Linux and macOS), so nothing more is protected there and nothing fails."""
    return normalize(os.environ.get("LOCALAPPDATA") or "").rstrip("/")


def _offline_patterns() -> list[re.Pattern]:
    """The offline folder where this machine's ``LOCALAPPDATA`` puts it, for a value that is
    not the usual ``AppData/Local`` (the fixed pattern in ``_PROTECTED`` covers that and the
    unexpanded variable)."""
    base = _local_appdata()
    if not base:
        return []
    return [re.compile(re.escape(base + "/zexionap") + r"(?![\w-]|\.\w)")]


def _offline_parts() -> list[tuple]:
    """The same folder as path components, for ``word_may_name_protected``."""
    parts = _path_parts(_local_appdata())
    if not parts or any(re.search(r"[*?\[{]", p) for p in parts):
        return []
    return [(*parts, "zexionap")]


def _plain(text: str) -> str:
    text = re.sub(r"[\"'`]", "", text)
    while "/./" in text:
        text = text.replace("/./", "/")
    return re.sub(r"(?<=[^/.\s])[. ]+(?=/)", "", text)


# A folder name and the ``..`` after it: ``a/x/../b`` is ``a/b``, whether or not ``x`` exists
# (Windows and Git Bash fold it away). Not a ``..`` itself.
_UP = re.compile(r"/(?!\.\.(?:/|\s|$))[^/\n]+/\.\.(?=/|\s|$)")


def _fold_up(text: str) -> str:
    for _ in range(32):  # one ``..`` a pass; deeper chains are not worth the time
        folded = _UP.sub("", text)
        if folded == text:
            break
        text = folded
    return text


def _join_run(m: re.Match) -> str:
    run = m.group()
    if "," in run or "/" in run:
        return "/"
    return "" if "+" in run else run


def mentions_protected(text: str) -> bool:
    """True when free text (a shell command, say) names a protected folder anywhere in it.

    It also reads the text with quotes and backticks removed, ``/./`` collapsed, and dots and
    spaces dropped from the end of each folder name (Windows ignores them: ``Gallant Leaf.``),
    so none of those hide the folder; that reading is read again with each name and the ``..``
    after it folded away (``…/Gallant Leaf/"$(echo 1)"/../Zexion``). And it reads it once
    more with the folder names joined from quoted pieces: string prefixes (``r'…'``) are
    dropped, PowerShell's ``-ChildPath`` and ``-AdditionalChildPath`` read as a ``,``, quotes
    that meet (``'Zex' 'ion'``) close up, and then any run of quotes, spaces, parentheses,
    ``$``, ``,``, ``+`` and ``/`` becomes one ``/`` if it holds a ``,`` or ``/``, or nothing if
    it holds a ``+`` but neither; any other run is left as it is. So
    ``os.path.join(os.path.join(…, 'Gallant Leaf'), r'Zexion')``, ``Path(…) / 'Gallant Leaf' /
    'Zexion'``, ``[IO.Path]::Combine(…)``, ``Join-Path … -AdditionalChildPath 'Zexion'``,
    ``'Zex' + 'ion'`` and ``'…/common/' + 'Zexion'`` name the folder too.
    """
    norm = normalize(text)
    joined = re.sub(r"(?<![\w\"'])[rbuf]{1,2}(?=[\"'])", "", norm)
    joined = re.sub(r"\s-(?:additional)?childpath\s", ",", joined)
    joined = re.sub(r"([\"'])\s*\1", "", joined)
    joined = re.sub(r"[\"'\s,+/()$]+", _join_run, joined)  # one pass per run: stays linear
    plain = _plain(norm)
    readings = (norm, plain, _fold_up(plain), _plain(joined))
    patterns = _PROTECTED + _offline_patterns()
    return any(p.search(t) for t in readings for p in patterns)


def word_may_name_protected(word: str) -> bool:
    """True when one shell word, read as a path, could name a protected folder or a file in it.

    Unlike ``mentions_protected`` it reads the path component by component: ``.`` and ``..``
    are resolved, trailing dots and spaces are dropped, a wildcard (``*``, ``?``, ``[...]``, and
    a ``{...}`` brace read as ``*``) may stand for any part of a folder name, and an 8.3 short
    name (``GALLAN~1``) for a long one. It errs toward True: ``Zex*`` counts, though it could
    also match another folder.
    """
    parts = _path_parts(word)
    for names in _PROTECTED_PARTS + _offline_parts():
        last = len(names) - 1
        for start in range(len(parts) - last):
            if all(
                _part_matches(parts[start + k], name, first=k == 0, last=k == last)
                for k, name in enumerate(names)
            ):
                return True
    return False


def may_be_above_protected(path: str) -> bool:
    """True when a folder, by the names it ends in, could hold a protected folder.

    That is a folder ending in ``steamapps``, ``steamapps/common``, ``Gallant Leaf``,
    ``userdata`` or ``userdata/<account id>``, read as ``word_may_name_protected`` reads a path
    (wildcards, 8.3 short names, ``..``), each name matched whole. A relative path from there
    can reach the protected folder without naming it whole. Other names (``steam-achievements``,
    ``fix-userdata``, a user named Steam) do not count, and neither does a last name that is
    only a wildcard (``build/*``) unless a named parent comes before it (``userdata/*``).
    """
    parts = _path_parts(path)
    for names in _PROTECTED_PARTS:
        for k in range(1, len(names)):
            first = parts[len(parts) - k] if len(parts) >= k else ""
            if not re.search(r"[a-z0-9]", re.sub(r"\[[^\]]*\]", "", first)):
                continue  # *, ?, [..]: any folder at all, not a named parent
            if len(parts) >= k and all(
                _part_matches(parts[len(parts) - k + j], names[j], first=False, last=False)
                for j in range(k)
            ):
                return True
    return False


def _path_parts(word: str) -> list[str]:
    """A path's components: normalized, ``.`` and ``..`` resolved, trailing dots and spaces
    dropped, and a ``{...}`` brace read as ``*``."""
    norm = re.sub(r"(?<!\$)\{[^{}/]*\}", "*", normalize(word))
    parts: list[str] = []
    for part in norm.split("/"):
        if part not in (".", ".."):
            part = part.rstrip(". ")
        if part == ".." and parts and parts[-1] != "..":
            parts.pop()
        elif part not in (".", ""):
            parts.append(part)
    return parts


_ANY = "?"  # one wildcard character; "*" is a run of them; anything else is itself


def _wildcard_tokens(part: str) -> list[str]:
    tokens, i = [], 0
    while i < len(part):
        if part[i] == "[" and "]" in part[i + 2 :]:
            i = part.index("]", i + 2)  # a character class: read as any one character
            tokens.append(_ANY)
        else:
            tokens.append(part[i])
        i += 1
    return tokens


def _part_matches(part: str, name: object, first: bool, last: bool) -> bool:
    """Whether path component ``part`` (maybe holding wildcards) could be the folder ``name``.

    ``first``: anything may come before ``name`` in ``part``, as the pattern search allows.
    ``last``: ``part`` may go on after ``name`` with a character that ends a name, like ``.``.
    """
    tokens = _wildcard_tokens(part)
    if name is _DIGITS:
        return bool(tokens) and all(t in "*?" or t.isdigit() for t in tokens)
    assert isinstance(name, str)
    short = re.fullmatch(r"([^~*?\[]{1,6})~\d+", part)
    if short:
        return name.replace(" ", "").startswith(short.group(1))
    n = len(name)
    before, after = -1, n + 1  # states: still before ``name``, and past its end
    states = {before, 0} if first else {0}
    for tok in tokens:
        new: set[int] = set()
        for s in states:
            if s == after:
                new.add(after)
            elif tok == "*":
                new.update(range(max(s, 0), n + 1))
                if s == before:
                    new.add(before)
                if s == n and last:
                    new.add(after)
            elif s == before:
                new.add(before)
                if tok in (_ANY, name[0]):
                    new.add(1)
            elif s < n:
                if tok in (_ANY, name[s]):
                    new.add(s + 1)
            elif last and (tok == _ANY or not re.fullmatch(r"[\w-]", tok)):
                new.add(after)
        states = new
        if not states:
            return False
    return n in states or after in states


def is_protected_path(path: str | os.PathLike[str]) -> bool:
    """True when ``path`` is inside a protected folder, before or after resolving links."""
    raw = os.fspath(path)
    candidates = [raw]
    with contextlib.suppress(OSError, ValueError):
        candidates.append(os.path.realpath(_expand(raw)))
    return any(mentions_protected(c + "/") or word_may_name_protected(c) for c in candidates)


def assert_writable(path: str | os.PathLike[str]) -> Path:
    """Return ``path`` as a Path, or raise if it is inside a protected folder."""
    if is_protected_path(path):
        raise ProtectedPathError(
            f"refusing to write {path}: it is inside a protected folder (the game install, the "
            "saves, Steam's cloud cache, or the mod's offline files). Work on a copy in lab/ "
            "(see /save-lab)."
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


# The game's file in its install: the Windows exe, and the Linux build's binary
# (``docs/re/builds.md``, "B1 on Linux").
GAME_FILES = ("Zexion.exe", "Zexion")
# Flatpak's Steam keeps its home, Steam and the game's saves under this folder of the real home.
FLATPAK_STEAM_HOME = Path(".var", "app", "com.valvesoftware.Steam")


SAVE_FOLDER = ("Gallant Leaf", "Zexion")
# Where Proton keeps a Windows game's %APPDATA%, inside a library (assumed: Proton's layout,
# not seen with this game).
PROTON_APPDATA = ("steamapps", "compatdata", str(STEAM_APP_ID), "pfx", "drive_c", "users",
                  "steamuser", "AppData", "Roaming")  # fmt: skip


def _linux_save_dirs(home: Path, env: dict[str, str], libs: list[Path]) -> list[Path]:
    """Where the game's saves can be on Linux: SDL's preference folder (``$XDG_DATA_HOME``, else
    ``~/.local/share``) for Steam installed natively, Flatpak Steam's home and Snap Steam's
    (assumed: Snap's layout, not seen), and the Windows build's under Proton in each library."""
    data_home = env.get("XDG_DATA_HOME") or str(home / ".local" / "share")
    found = [
        Path(data_home).joinpath(*SAVE_FOLDER),
        home.joinpath(FLATPAK_STEAM_HOME, ".local", "share", *SAVE_FOLDER),
        home.joinpath("snap", "steam", "common", ".local", "share", *SAVE_FOLDER),
    ]
    found += [library.joinpath(*PROTON_APPDATA, *SAVE_FOLDER) for library in libs]
    return found


def _one(found: list[Path], what: str, setting: str) -> Path:
    """The one folder of ``found`` (links and repeats counted once), else an error."""
    unique: list[Path] = []
    for path in found:
        with contextlib.suppress(OSError):
            if path.resolve() not in {u.resolve() for u in unique}:
                unique.append(path)
    if len(unique) == 1:
        return unique[0]
    if not unique:
        raise RuntimeError(f"{what} not found; set {setting} to it")
    raise RuntimeError(
        f"{what} in {len(unique)} places ({', '.join(map(str, unique))}); set {setting} to the "
        "one the game uses"
    )


def save_dir() -> Path:
    """The real save folder. ``ZEXION_SAVE_DIR`` overrides it (tests use this). On Linux it is
    the one of ``_linux_save_dirs`` that exists, and an error when none or several do."""
    override = os.environ.get("ZEXION_SAVE_DIR")
    if override:
        return Path(override)
    if sys.platform == "win32":
        appdata = os.environ.get("APPDATA")
        if not appdata:
            raise RuntimeError("APPDATA is not set; set ZEXION_SAVE_DIR to the save folder")
        return Path(appdata).joinpath(*SAVE_FOLDER)
    candidates = _linux_save_dirs(Path.home(), dict(os.environ), libraries(_steam_roots()))
    return _one([p for p in candidates if p.is_dir()], "Zexion's save folder", "ZEXION_SAVE_DIR")


def _steam_roots() -> list[Path]:
    """Steam's own folders: the registry's on Windows; elsewhere the usual places of a native,
    Flatpak or Snap Steam, those that exist (a link to another counts once)."""
    if sys.platform == "win32":
        import winreg

        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
                value, _ = winreg.QueryValueEx(key, "SteamPath")
        except OSError:
            return []
        return [Path(value)]
    home = Path.home()
    candidates = [
        home / ".steam" / "steam",
        home / ".local" / "share" / "Steam",
        home / FLATPAK_STEAM_HOME / ".local" / "share" / "Steam",
        home / "snap" / "steam" / "common" / ".local" / "share" / "Steam",
    ]
    roots: list[Path] = []
    for candidate in candidates:
        with contextlib.suppress(OSError):
            if candidate.is_dir() and candidate.resolve() not in {r.resolve() for r in roots}:
                roots.append(candidate)
    return roots


def libraries(roots: list[Path]) -> list[Path]:
    """Each root and the libraries its ``libraryfolders.vdf`` lists, in that order."""
    found: list[Path] = []
    for root in roots:
        found.append(root)
        vdf = root / "steamapps" / "libraryfolders.vdf"
        if vdf.is_file():
            text = vdf.read_text(encoding="utf-8", errors="replace")
            for match in re.finditer(r'"path"\s+"([^"]+)"', text):
                found.append(Path(match.group(1).replace("\\\\", "\\")))
    return found


def game_file(install: Path) -> Path:
    """The game's exe or Linux binary in ``install`` (``GAME_FILES``, the first that is there)."""
    for name in GAME_FILES:
        if (install / name).is_file():
            return install / name
    raise RuntimeError(f"no {' or '.join(GAME_FILES)} in {install}")


def game_dir() -> Path:
    """The game install. ``ZEXION_GAME_DIR`` overrides it; otherwise Steam's libraries are tried,
    and two installs (a native and a Flatpak Steam's, say) are an error, not a guess."""
    override = os.environ.get("ZEXION_GAME_DIR")
    if override:
        return Path(override)
    found = []
    for library in libraries(_steam_roots()):
        candidate = library / "steamapps" / "common" / "Zexion"
        if any((candidate / name).is_file() for name in GAME_FILES):
            found.append(candidate)
    return _one(
        found,
        "Zexion's install (the folder with Zexion.exe or, on Linux, the Zexion binary)",
        "ZEXION_GAME_DIR",
    )


def lab_dir() -> Path:
    """Where backups and working copies go. Gitignored. ``ZEXION_LAB_DIR`` overrides it."""
    override = os.environ.get("ZEXION_LAB_DIR")
    path = Path(override) if override else ROOT / "lab"
    return assert_writable(path)
