"""The folders this project must never write to directly, and where to find them.

Four places are protected:

- the game install (any ``steamapps/common/Zexion``, in any Steam library),
- the real save folder (``%APPDATA%/Gallant Leaf/Zexion``; on Linux SDL's, under
  ``~/.local/share``, or Flatpak Steam's: any ``Gallant Leaf/Zexion``),
- Steam's cloud cache for the game (``Steam/userdata/<account>/3392510``),
- the mod's offline files (``%LOCALAPPDATA%/ZexionAP``: the seed list, labels and disk counts
  the helper keeps for play without the client; on Linux the seeds' files and the helper's socket
  token, in ``$XDG_DATA_HOME/ZexionAP``, else ``~/.local/share/ZexionAP``, as the game's saves
  are, so under Flatpak Steam's home too; ``.var/app/com.valvesoftware.Steam/data``, Flatpak's own
  default ``XDG_DATA_HOME``, is covered as well). Only the helper writes them.

The helpers (the game's own process: the Windows helper and the Linux helper) also write their
``ZexionAP-seed-<NAME>.sav`` copies of the seeds a save uses into the real save folder, so Steam
Cloud carries them (native/README.md, "Seed files in the save folder"). The guard never sees that:
it watches Claude's tool calls, and those it blocks as before, these names included. The tools
write the save folder only through ``save_lab.py install`` and ``restore``.

A folder that holds one of them at any depth (Roaming, the home folder and what is above it, ``/``,
a drive's root, Steam's own folders: ``holds_protected``) and a name pattern that may match the
name of a file in one (``Save Game 1.sav``, ``token``, …: ``pattern_may_name_protected``) count
for the hook too: deleting, moving or copying the first, or searching it and acting on what is
found, reaches the folders.

Matching is by pattern rather than by this machine's paths, so the same rules hold on any PC,
but for the home folder and the folders of the environment's variables (``APPDATA``, ``HOME``, …),
which are this machine's.
The write-guard hook (``.claude/hooks/guard_protected.py``) and the tools in this folder share
this one definition. Only ``save_lab.py install`` and ``save_lab.py restore`` write into the
save folder, and only after a verified backup; the Archipelago client never writes it.

``append_log`` is the JSON-lines log that ``grant.py`` and ``helper.py`` keep in ``lab/`` of
each change they make to the running game.
"""

from __future__ import annotations

import contextlib
import datetime as dt
import functools
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
    # On Linux: ``ZexionAP`` after ``.local/share`` (Snap's ``…/common/.local/share`` too),
    # ``XDG_DATA_HOME`` (unexpanded, or not set here) or Flatpak Steam's ``…Steam/data``.
    re.compile(
        r"(?:\.local\W{0,8}share|xdg_data_home|com\.valvesoftware\.steam\W{0,8}data)"
        r"\W{0,8}zexionap(?![\w-]|\.\w)"
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
    (".local", "share", "zexionap"),  # Linux: the data folder SDL and the helper use
    ("xdg_data_home", "zexionap"),  # the variable left unexpanded (``$XDG_DATA_HOME``)
    ("com.valvesoftware.steam", "data", "zexionap"),  # Flatpak Steam's XDG_DATA_HOME
]

# Environment variables that commonly prefix a path to the saves or the Steam library.
_ENV_NAMES = (
    "APPDATA",
    "LOCALAPPDATA",
    "USERPROFILE",
    "HOME",
    "XDG_DATA_HOME",  # Linux: ``$XDG_DATA_HOME/ZexionAP`` is the offline folder
    "TEMP",  # %LOCALAPPDATA%\Temp: ``$TEMP/../ZexionAP`` is the offline folder
    "TMP",
    "PROGRAMFILES(X86)",
    "PROGRAMFILES",
)


class ProtectedPathError(RuntimeError):
    """Raised when a tool is asked to write into a protected folder."""


# %NAME%, $env:NAME, ${env:NAME}, ${NAME}, $NAME, per variable
_ENV_SPELLINGS = [
    (
        name,
        re.compile(
            rf"%{re.escape(name)}%|\$env:{re.escape(name)}(?!\w)|\$\{{env:{re.escape(name)}\}}"
            rf"|\$\{{{re.escape(name)}\}}|\${re.escape(name)}(?!\w)",
            re.IGNORECASE,
        ),
    )
    for name in _ENV_NAMES
]
# ``~`` as a word's start or alone: the home folder (``~/x``, ``~``, ``"~"``, ``(cd ~)``).
_TILDE = re.compile(r"(^|[\s\"'=])~(?=[/\\\s\"');&|]|$)")


def _expand(text: str) -> str:
    """Expand the env-var spellings of cmd, PowerShell and sh, and a leading ``~``."""
    if not re.search(r"[$%~]", text):
        return text
    for name, spellings in _ENV_SPELLINGS:
        value = os.environ.get(name)
        if value:
            text = spellings.sub(lambda _m, v=value: v, text)
    if "~" in text:
        home = os.path.expanduser("~")
        text = _TILDE.sub(lambda m: m.group(1) + home, text)
    return text


def normalize(text: str, fold: bool = True) -> str:
    """Lowercase (unless ``fold`` is False), forward slashes, env vars expanded, ``\\ ``
    unescaped, repeated slashes merged."""
    text = _expand(text)
    text = text.replace("\\ ", " ").replace("\\", "/")
    text = re.sub(r"/{2,}", "/", text)
    return text.lower() if fold else text


def _by_env(fn):
    """Cache a function of the environment alone for as long as the variables it reads
    (``_ENV_NAMES``, the home folder) stay the same: the guard asks for the offline folders once
    per word of a command."""
    cache: dict[tuple, object] = {}

    @functools.wraps(fn)
    def wrapper():
        key = (*(os.environ.get(name) for name in _ENV_NAMES), os.path.expanduser("~"))
        if key not in cache:
            cache.clear()  # only the current environment is kept
            cache[key] = fn()
        return cache[key]

    return wrapper


def _local_appdata() -> str:
    """``LOCALAPPDATA`` as ``normalize`` reads a path, without a trailing slash. "" when it is
    not set (Linux and macOS), so nothing more is protected there and nothing fails."""
    return normalize(os.environ.get("LOCALAPPDATA") or "").rstrip("/")


@_by_env
def _offline_bases() -> list[str]:
    """The folders this machine's environment puts the offline folder in, read as ``normalize``
    reads a path: ``LOCALAPPDATA`` and, on Linux, ``XDG_DATA_HOME``, each when set."""
    data_home = normalize(os.environ.get("XDG_DATA_HOME") or "").rstrip("/")
    return [base for base in (_local_appdata(), data_home) if base]


@_by_env
def _offline_patterns() -> list[re.Pattern]:
    """The offline folder where this machine's ``LOCALAPPDATA`` or ``XDG_DATA_HOME`` puts it,
    for a value that is not the usual ``AppData/Local`` or ``~/.local/share`` (the fixed
    patterns in ``_PROTECTED`` cover those and the unexpanded variables)."""
    return [
        re.compile(re.escape(base + "/zexionap") + r"(?![\w-]|\.\w)") for base in _offline_bases()
    ]


@_by_env
def _offline_parts() -> list[tuple]:
    """The same folders as path components, for ``word_may_name_protected``."""
    found = []
    for base in _offline_bases():
        parts = _path_parts(base)
        if parts and not any(re.search(r"[*?\[{]", p) for p in parts):
            found.append((*parts, "zexionap"))
    return found


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
    # Without a wildcard or a short name's ``~`` a name must be there as it is: a word that
    # lacks one of a folder's names is not read against it (a long word has many components).
    joined = "/".join(parts)
    exact = re.search(r"[*?\[]", joined) is None and not any(
        _SHORT_NAME.fullmatch(p) for p in parts
    )
    for names in _PROTECTED_PARTS + _offline_parts():
        if exact and any(isinstance(n, str) and n not in joined for n in names):
            continue
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


# Folders above a protected one that no chain of ``_PROTECTED_PARTS`` leads through, by the
# names they end in: Roaming (``Gallant Leaf``'s parent), Flatpak's ``.var/app``, and Steam's own
# folders (the cloud cache and the install are inside them). A Steam folder elsewhere, by its own
# name, is not named here.
_ANCESTOR_TAILS: list[tuple[str, ...]] = [
    ("appdata", "roaming"),
    (".var",),
    (".var", "app"),
    (".steam",),
    (".steam", "steam"),
    (".local", "share", "steam"),
    ("snap", "steam"),
    ("program files (x86)",),
    ("program files (x86)", "steam"),
]
# The variables whose folder, and every folder above it, may hold a protected one: where the
# saves, the offline folder and Steam usually are (``Gallant Leaf`` in Roaming, ``ZexionAP`` in
# Local, ``~/.local/share``), and the home folder.
_ANCESTOR_ENV = ("APPDATA", "LOCALAPPDATA", "USERPROFILE", "HOME", "XDG_DATA_HOME",
                 "PROGRAMFILES(X86)")  # fmt: skip
_DRIVE = re.compile(r"[a-z]:")


def _drive_parts(path: str) -> tuple[list[str], bool]:
    """``path``'s components as ``_path_parts`` reads them, with a drive written as Git Bash,
    WSL or Cygwin write it (``/c/Users``, ``/mnt/c/Users``, ``/cygdrive/c/Users``) and the
    extended-length prefix (``//?/C:/``) as the Windows drive they name, and whether the path
    is absolute (it starts with a separator or a drive)."""
    parts = _path_parts(path)
    absolute = normalize(path).startswith("/")
    if absolute and parts[:1] == ["?"]:
        parts = parts[1:]
    if absolute and len(parts) > 1 and parts[0] in ("mnt", "cygdrive") and len(parts[1]) == 1:
        parts = parts[1:]
    if absolute and parts and re.fullmatch(r"[a-z]", parts[0]):
        parts = [parts[0] + ":", *parts[1:]]
    return parts, absolute or bool(parts and _DRIVE.fullmatch(parts[0]))


@_by_env
def _ancestor_chains() -> list[list[str]]:
    """The folders the environment names in ``_ANCESTOR_ENV``, and the home folder, as the
    components of their paths; each of their first parts is a folder above a protected one too."""
    values = [os.environ.get(name) or "" for name in _ANCESTOR_ENV]
    chains = []
    for value in [*values, os.path.expanduser("~")]:
        parts, absolute = _drive_parts(value) if value else ([], False)
        if absolute and parts and not any(re.search(r"[*?\[{$%~]", p) for p in parts):
            chains.append(parts)
    return chains


def _is_ancestor_token(part: str) -> bool:
    """``~``, ``~user``, or one of ``_ANCESTOR_ENV`` written as a variable (``$HOME``,
    ``%APPDATA%``, ``$env:LOCALAPPDATA``), which the environment here did not expand; a
    wildcard after it too (``%APPDATA%\\*`` in Bash reads ``%APPDATA%*``)."""
    part = part.rstrip("*?")
    return re.fullmatch(r"~[\w.-]*", part) is not None or any(
        spelling.fullmatch(part) for name, spelling in _ENV_SPELLINGS if name in _ANCESTOR_ENV
    )


def holds_protected(path: str) -> bool:
    """True when a folder could hold a protected one at any depth, so that deleting, moving or
    copying it, or searching it and acting on what is found, reaches the saves, the install, the
    cloud cache or the offline files: a folder ``may_be_above_protected`` names, or one of
    ``is_ancestor_folder``."""
    return may_be_above_protected(path) or is_ancestor_folder(path)


def is_ancestor_folder(path: str) -> bool:
    """True when a folder is one that no name in a protected folder's own path leads to, but
    that holds one at some depth: Roaming, the home folder (``~``, ``/home/name``,
    ``C:/Users/name``) and what is above it, ``/``, a drive's root, ``/home``, ``C:/Users``,
    Steam's own folders and the folders the environment gives for the variables of
    ``_ANCESTOR_ENV`` (also written unexpanded: ``$HOME``, ``%APPDATA%``) with each one above
    them, each read as ``word_may_name_protected`` reads a path (wildcards, 8.3 short names,
    ``..``). A last name that is only a wildcard (``~/*``, ``$APPDATA/.*``) stands for any
    folder in the one before it, which may be the one. Other folders do not count (a project
    folder under the home folder), and the check is by the folder's own name: what a link in it
    leads to is not followed.
    """
    raw = [p for p in normalize(path).split("/") if p not in ("", ".")]
    if raw and _is_ancestor_token(raw[0]) and all(p == ".." for p in raw[1:]):
        return True  # ``$APPDATA/..``: the variable, then folders up
    parts, absolute = _drive_parts(path)
    if parts and not re.search(r"[a-z0-9_]", _CLASS.sub("?", parts[-1])) and "*" in parts[-1]:
        parts = parts[:-1]
    n = len(parts)
    if n == 0:
        return absolute  # ``/``
    drive = _DRIVE.fullmatch(parts[0]) is not None
    if n == 1 and (drive or (absolute and parts[0] in ("home", "users"))):
        return True
    # A user's home folder: ``/home/name``, ``/Users/name``, ``C:/Users/name``, and ``C:/Users``.
    if absolute and (
        (n == 2 and (parts[0] in ("home", "users") or (drive and parts[1] == "users")))
        or (n == 3 and drive and parts[1] == "users")
    ):
        return True
    if n == 1 and _is_ancestor_token(parts[0]):
        return True
    for tail in _ANCESTOR_TAILS:
        k = len(tail)
        if n >= k and all(
            _part_matches(parts[n - k + j], tail[j], first=False, last=False) for j in range(k)
        ):
            return True
    return absolute and any(
        n <= len(chain)
        and all(_part_matches(parts[j], chain[j], first=False, last=False) for j in range(n))
        for chain in _ancestor_chains()
    )


# Names of the files in the protected folders, as they are on disk: the saves (the game's 30
# slots), its other files, the helpers' copies of the seeds, Steam's, and the offline files (the
# per-build configuration; the socket's token and the folder the seeds' files are in). A name
# pattern that may match one of them counts as naming a protected folder
# (``pattern_may_name_protected``): ``*.sav``, ``Save*``, ``*seed*`` and a bare ``*`` do, ``*.py``
# and ``*foo*`` do not.
_FILE_NAMES = (
    *(f"Save Game {n}.sav" for n in range(1, 31)),
    "Save Game 00.sav",
    "GameUnlocks.sav",
    "PreferencesSynced.sav",
    "PreferencesLocal",
    "Preferences",
    "ZexionAP-seed-12345-0-1.sav",
    "steam_autocloud.vdf",
    "token",
    "seeds",
    "config-B1.txt",
)
# The longest name pattern part read against them: a longer one is read as a file's name.
_MAX_FILE_PART = 200


def _glob_matches(tokens: tuple[str, ...], name: str) -> bool:
    """Whether a name pattern (``tokens`` as ``_wildcard_tokens`` reads it: ``*`` a run, ``?`` one
    character, any other token itself) matches ``name`` whole. The name's positions are kept in a
    bit set, so a long pattern is read in time linear in its length."""
    n = len(name)
    literal: dict[str, int] = {}
    for j, c in enumerate(name):
        literal[c] = literal.get(c, 0) | (1 << j)
    states = 1  # the positions the tokens so far can have reached
    for tok in tokens:
        if tok == "*":  # any run, even none: every position from the first on
            states = ~((states & -states) - 1) & ((1 << (n + 1)) - 1)
        else:
            states = (states & ((1 << n) - 1 if tok == _ANY else literal.get(tok, 0))) << 1
        if not states:
            return False
    return bool(states >> n & 1)


_FILE_NAMES_BY_FIRST: dict[str, list[str]] = {}
for _name in _FILE_NAMES:
    _FILE_NAMES_BY_FIRST.setdefault(_name[0].lower(), []).append(_name)


@functools.lru_cache(maxsize=2048)
def _names_a_file(part: str, ignore_case: bool) -> bool:
    """Whether a name pattern, read as written, matches the name of a file in ``_FILE_NAMES``:
    in any case, except the seed copies' name (it starts with ``ZexionAP``, the repo's own name in
    lower case in ``zexion*.py``) unless ``ignore_case``, as ``_CASED`` says for the folders. A
    part over ``_MAX_FILE_PART`` characters is read as one."""
    if len(part) > _MAX_FILE_PART:
        return True
    folded, cased = _wildcard_tokens(part.lower()), _wildcard_tokens(part)
    if not folded:
        return False
    # The names that start as the pattern does, or all when it starts with a wildcard.
    names = _FILE_NAMES if folded[0] in ("*", _ANY) else _FILE_NAMES_BY_FIRST.get(folded[0], ())
    return any(
        _glob_matches(cased, name)
        if name.startswith("ZexionAP") and not ignore_case and "~" not in part
        else _glob_matches(folded, name.lower())
        for name in names
    )


# A bracket expression: ``[abc]``, ``[!a-z]``, ``[]x]``, ``[[:upper:]]``, with at most 64
# members, so a long run of ``[`` is read in linear time. A ``[`` that starts no POSIX class is a
# member of its own (``[[]``), which keeps the members from being read two ways.
_CLASS = re.compile(
    r"\[[!^]?(?:\]|\[:\w{1,16}:\]|[^\]\[]|\[(?!:\w{1,16}:\]))"
    r"(?:\[:\w{1,16}:\]|[^\]\[]|\[(?!:\w{1,16}:\])){0,63}\]"
)


def _class_at(text: str, i: int) -> re.Match | None:
    """The bracket expression that starts at ``text[i]``, if any. A ``[`` with no ``]`` within
    reach (``_CLASS`` is at most 1283 characters long) starts none: a run of ``[`` is then read
    in linear time."""
    if text.find("]", i + 1, i + 1284) < 0:
        return None
    return _CLASS.match(text, i)


def _sub_classes(repl, text: str) -> str:
    """``text`` with ``repl`` applied to each bracket expression in it."""
    return _CLASS.sub(repl, text) if "]" in text else text


# A class of letters and digits only (``[Zz]``) stands for one fixed character.
_PLAIN_CLASS = re.compile(r"\[[A-Za-z0-9]+\]")
# The two protected names that are also the repo's own (``worlds/zexion``, ``zexion*.py``) as they
# are on disk. ``find``'s ``-name``, ``-path`` and ``-regex`` compare case, so they match only
# these; ``-iname`` and PowerShell, which don't, match any case.
_CASED = {"zexion": "Zexion", "zexionap": "ZexionAP"}
# The longest pattern read: a name of that length is no folder's.
_MAX_PATTERN = 1000


def _regex_as_glob(pattern: str, groups: bool = True) -> str:
    """A regular expression read as a path glob, as far as a few common forms go: ``.*`` and
    ``.+`` become ``*``, a lone ``.`` ``?``, an escaped character itself (``\\s``, ``\\w`` and
    ``\\d`` any one character), anchors and alternation ``/`` (so each alternative is read as its
    own name), a negated class ``?``, and a group's parentheses ``/`` too, or with ``groups``
    False nothing (``Z(exionAP)`` is then ``ZexionAP``). ``find``'s default syntax writes them
    ``\\(``, ``\\)`` and ``\\|``. Any other quantifier is left as it is, which then names nothing:
    ``Z.*AP`` is read, ``Zex(ion)?AP`` is not."""
    out, i = [], 0
    paren = "/" if groups else ""
    while i < len(pattern):
        c = pattern[i]
        cls = _class_at(pattern, i) if c == "[" else None
        if cls:
            out.append("?" if cls.group()[1] in "^!" else cls.group())
            i = cls.end()
            continue
        if c == "\\" and i + 1 < len(pattern):
            i += 1
            e = pattern[i]
            out.append(paren if e in "()" else "/" if e == "|" else "?" if e in "sSwWdD" else e)
        elif c == "." and pattern[i + 1 : i + 2] in ("*", "+"):
            out.append("*")
            i += 1
        elif c == ".":
            out.append("?")
        elif c in "()":
            out.append(paren)
        elif c in "^$|":
            out.append("/")
        else:
            out.append(c)
        i += 1
    return "".join(out)


def _braces(pattern: str, limit: int = 32) -> list[str]:
    """The pattern with its first ``{a,b}`` brace expansion read out, as Bash does, repeated for
    the others: ``{ZexionAP,}`` is ``ZexionAP`` and ``''``. At most ``limit`` readings."""
    found = [pattern]
    for _ in range(4):
        grown = []
        for text in found:
            m = re.search(r"\{([^{}]*,[^{}]*)\}", text)
            if m:
                grown += [text[: m.start()] + a + text[m.end() :] for a in m.group(1).split(",")]
            else:
                grown.append(text)
        found = grown[:limit]
    return found


def _literal_len(part: str) -> int:
    """How many characters of a name pattern are fixed: not a wildcard, and not a class but a
    plain one of letters and digits (``[Zz]``), which counts as one."""
    part = _sub_classes(lambda m: "x" if _PLAIN_CLASS.fullmatch(m.group()) else "", part)
    return len(re.sub(r"[*?]", "", part))


def pattern_may_name_protected(pattern: str, kind: str = "name", ignore_case: bool = False) -> bool:
    """True when a name pattern, such as a ``find`` predicate's (``-name``, ``-path``,
    ``-regex``), could match the name of a protected folder or of one above it: the last name
    of each protected folder (``Zexion``, ``ZexionAP``, the app id) or a folder that may hold
    one (``may_be_above_protected``: ``Gallant Leaf``, ``steamapps``, ``userdata``, …), each
    component of a path pattern on its own, with wildcards and 8.3 short names read as
    ``word_may_name_protected`` reads them. ``kind`` is ``"name"`` (``-name``), ``"path"``
    (``-path``), ``"regex"`` (``-regex``, read as ``_regex_as_glob`` reads it) or ``"ps"``
    (PowerShell's, where a backslash separates folders and case is ignored). A backslash
    escapes the character after it, as in ``find``.

    ``Zexion`` and ``ZexionAP`` are also the repo's own names in lower case (``worlds/zexion``,
    ``zexion*.py``): a predicate that compares case matches them only as written (``_CASED``),
    unless ``ignore_case`` (``-iname``) says it does not. A component with fewer than three
    fixed characters is no name: ``*`` or ``Z*`` stands for too many others to tell
    (``_literal_len``). A folder above a protected one counts only as the pattern's last name
    (``*/Gallant Leaf/*``, not ``*/AppData/Local/Temp/x``). In a path or a regex a ``*`` (a
    ``.``, a ``?``) may match a ``/`` too, so the pieces around one are read as names also. A
    pattern over 1000 characters is read as a name, whatever it is. The match is by name alone,
    so ``-name ZexionAP`` counts wherever the search starts.

    A pattern that may match the name of a file in the protected folders counts too
    (``_FILE_NAMES``, ``_names_a_file``): ``*.sav``, ``Save*``, ``*seed*``, ``Pref*``, ``token``
    and a bare ``*``, but not ``*.py`` or ``*foo*``. Of a ``path`` or a ``regex`` only the last
    name is read so, and it needs a fixed character (``./build/*`` names no file).
    """
    if len(pattern) > _MAX_PATTERN:
        return True
    if kind != "regex" and word_may_name_protected(pattern):
        return True
    ignore_case = ignore_case or kind == "ps"
    texts = (
        [_regex_as_glob(pattern), _regex_as_glob(pattern, groups=False)]
        if kind == "regex"
        else [pattern]
        if kind == "ps"
        else [re.sub(r"\\(.)", r"\1", p) for p in _braces(pattern)]
    )
    readings = []
    for text in texts:
        text = _sub_classes(lambda m: "?" if "/" in m.group() else m.group(), text)
        readings.append(text)
        if kind != "name":
            readings += [text.replace("*", "/"), text.replace("?", "/")]
    leaves = [names[-1] for names in _PROTECTED_PARTS + _offline_parts()]
    for reading in readings:
        names = _path_parts(reading, fold=False)
        if kind in ("path", "regex"):  # the file's name is the last; ``./build/*`` names none
            names = [p for p in names[-1:] if _literal_len(p) >= 1]
        if any(_names_a_file(p, ignore_case) for p in names):
            return True
        cased = [p for p in _path_parts(reading, fold=False) if _literal_len(p) >= 3]
        parts = [p.lower() for p in cased]
        for i, part in enumerate(parts):
            # A folder above a protected one counts only as the last name: ``*/AppData/Local/
            # Temp/x`` is below it, not it.
            if i == len(parts) - 1 and may_be_above_protected(part):
                return True
            for name in leaves:
                exact = _CASED.get(name) if not ignore_case and "~" not in part else None
                if (
                    _part_matches(cased[i], exact, first=False, last=False)
                    if exact
                    else _part_matches(part, name, first=False, last=False)
                ):
                    return True
    return False


def _path_parts(word: str, fold: bool = True) -> list[str]:
    """A path's components: normalized (``fold`` False keeps the case), ``.`` and ``..``
    resolved, trailing dots and spaces dropped, and a ``{...}`` brace read as ``*``."""
    norm = re.sub(r"(?<!\$)\{[^{}/]*\}", "*", normalize(word, fold))
    parts: list[str] = []
    for part in norm.split("/"):
        if part not in (".", ".."):
            part = part.rstrip(". ")
        if part == ".." and parts and parts[-1] != "..":
            parts.pop()
        elif part not in (".", ""):
            parts.append(part)
    return parts


_SHORT_NAME = re.compile(r"([^~*?\[]{1,6})~\d+")  # an 8.3 short name: ``GALLAN~1``
_ANY = "?"  # one wildcard character; "*" is a run of them; anything else is itself


@functools.lru_cache(maxsize=1024)  # a part is read against every name, in every reading
def _wildcard_tokens(part: str) -> tuple[str, ...]:
    tokens, i = [], 0
    while i < len(part):
        cls = _class_at(part, i) if part[i] == "[" else None
        if cls:  # a character class: read as any one character
            tokens.append(_ANY)
            i = cls.end()
        else:
            tokens.append(part[i])
            i += 1
    return tuple(tokens)


def _part_matches(part: str, name: object, first: bool, last: bool) -> bool:
    """Whether path component ``part`` (maybe holding wildcards) could be the folder ``name``.

    ``first``: anything may come before ``name`` in ``part``, as the pattern search allows.
    ``last``: ``part`` may go on after ``name`` with a character that ends a name, like ``.``.
    """
    tokens = _wildcard_tokens(part)
    if name is _DIGITS:
        return bool(tokens) and all(t in "*?" or t.isdigit() for t in tokens)
    assert isinstance(name, str)
    short = _SHORT_NAME.fullmatch(part)
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


# A UNC path: two separators and a server. Not the extended-length and device forms of a local
# path (``//?/C:/x``, ``//./C:/x``), which ``//?/UNC/server/share`` makes a network path again.
_UNC = re.compile(r"[\\/]{2}(?:(?![?.][\\/])|[?.][\\/]UNC[\\/])", re.IGNORECASE)


def _on_windows() -> bool:
    return sys.platform == "win32"


def is_network_path(path: str) -> bool:
    """True, on Windows, for a UNC path (``//server/share``, written with backslashes as well,
    or ``//?/UNC/server/share``). Windows resolves one over the network, seconds for each
    server, so nothing asks the file system about it. A local path with the extended-length or
    device prefix (``//?/C:/x``, ``//./C:/x``) is not one, and neither is ``//x`` on Linux and
    macOS."""
    return _on_windows() and _UNC.match(path) is not None


def is_protected_path(path: str | os.PathLike[str]) -> bool:
    """True when ``path`` is inside a protected folder, before or after resolving links. Not
    resolved (judged by its text alone) is a UNC path (``is_network_path``), where resolving
    asks the network."""
    raw = os.fspath(path)
    candidates = [raw]
    expanded = _expand(raw)
    if not is_network_path(expanded):
        with contextlib.suppress(OSError, ValueError):
            candidates.append(os.path.realpath(expanded))
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
