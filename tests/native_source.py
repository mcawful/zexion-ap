"""The native helpers' C++ sources, read as text by the tests that cross-check them with the client
(test_cross_checks.py) and the helpers' own (test_native*.py).

One place for where the sources are, how a table or a function is cut out of one, and which files
a platform's helper is built from, so a file that moves is fixed here and not in each test.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "native" / "src"
LINUX_SRC = ROOT / "native" / "linux" / "src"


def source(name: str) -> str:
    """A file of native/src, shared or the Windows helper's."""
    return (SRC / name).read_text(encoding="utf-8")


def linux_source(name: str) -> str:
    """A file of native/linux/src, the Linux helper's own."""
    return (LINUX_SRC / name).read_text(encoding="utf-8")


def braced(text: str, opening: str) -> str:
    """What lies between ``opening`` (ending in ``{``) and the ``};`` that closes it."""
    assert text.count(opening) == 1, opening
    return text.split(opening, 1)[1].split("};", 1)[0]


def without_comments(text: str) -> str:
    """``text`` with its ``//`` comments removed."""
    return re.sub(r"//[^\n]*", "", text)


def function_body(text: str, signature: str) -> str:
    """The body of the one function whose definition starts with ``signature``."""
    assert text.count(signature) == 1, signature
    start = text.index("{", text.index(signature) + len(signature))
    depth = 0
    for at in range(start, len(text)):
        depth += {"{": 1, "}": -1}.get(text[at], 0)
        if depth == 0:
            return text[start : at + 1]
    raise AssertionError(f"{signature} is not closed")


def for_platform(text: str, windows: bool) -> str:
    """``text`` without the lines only the other platform builds: the regions of ``#ifdef _WIN32``
    and ``#ifndef _WIN32`` (and their ``#else``) the platform does not take. Other conditionals
    (ZAP_HOOKTEST's, ZAP_TEST_BUILD's) keep both branches, and the directives themselves go."""
    kept: list[str] = []
    stack: list[tuple[bool, bool]] = []  # (a _WIN32 test, the branch now open is taken)
    for line in text.splitlines():
        found = re.match(r"\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b\s*(\S*)", line)
        if found is None:
            if all(taken for _, taken in stack):
                kept.append(line)
            continue
        directive, word = found.groups()
        if directive in ("ifdef", "ifndef", "if"):
            platform = directive != "if" and word == "_WIN32"
            stack.append((platform, windows == (directive == "ifdef") if platform else True))
        elif directive == "elif":
            raise AssertionError(f"an #elif this cannot follow: {line}")
        elif directive == "else":
            platform, taken = stack.pop()
            stack.append((platform, (not taken) if platform else True))
        else:  # endif
            stack.pop()
    assert not stack, "an #if left open"
    return "\n".join(kept)


def windows_sources() -> str:
    """The Windows helper's sources (native/src, not the files only the Linux build compiles:
    ``*_linux.cpp``, ``*_posix.cpp``), as it builds them."""
    paths = sorted(
        path for path in SRC.glob("*.[ch]*") if not path.name.endswith(("_linux.cpp", "_posix.cpp"))
    )
    return "\n".join(for_platform(path.read_text(encoding="utf-8"), True) for path in paths)


def linux_sources() -> str:
    """The Linux helper's own sources and the shared ones of native/src its build compiles
    (native/linux/CMakeLists.txt's ``../src/NAME.cpp``, with their headers), as it builds them
    (not the shared files' Windows branches)."""
    cmake = (ROOT / "native" / "linux" / "CMakeLists.txt").read_text(encoding="utf-8")
    shared = sorted(set(re.findall(r"\.\./src/(\w+)\.cpp", cmake)))
    assert {"game_calls", "offline", "mission"} <= set(shared), shared
    paths = sorted(LINUX_SRC.glob("*.[ch]*"))
    paths += [SRC / f"{name}{ext}" for name in shared for ext in (".cpp", ".h")]
    paths += [
        SRC / "offline_fs.h",
        SRC / "feats_ending.h",
    ]  # headers with no source of the same name
    return "\n".join(
        for_platform(path.read_text(encoding="utf-8"), False) for path in paths if path.exists()
    )


def linux_config_table() -> list[tuple[str, str, str, int]]:
    """The Linux helper's ``kConfigKeys`` (helper.cpp), one row per key: its name, the
    ``GameAddresses`` field, the game's symbol and the offset past it, in order."""
    table = without_comments(
        braced(linux_source("helper.cpp"), "constexpr ConfigKey kConfigKeys[] = {")
    )
    rows = re.findall(
        r'\{"(\w+)", &G::(\w+), [^,]+,\s*"(\w+)",\s*(0x[0-9A-Fa-f]+|\d+)(?:,\s*\w+)?\}', table
    )
    assert rows, "no rows read from kConfigKeys"
    return [(name, field, symbol, int(offset, 0)) for name, field, symbol, offset in rows]


def linux_client_keys() -> tuple[list[str], list[tuple[str, str, int]]]:
    """The keys a client's ``config`` must give (helper.cpp's ``kClientKeys``), in order, and the
    ones of them that are not ``kConfigKeys`` rows (``kClientOnly``: name, symbol, offset)."""
    text = linux_source("helper.cpp")
    keys = re.findall(r'"(\w+)"', braced(text, "constexpr const char* kClientKeys[] = {"))
    only = re.findall(
        r'\{"(\w+)", "(\w+)", (0x[0-9A-Fa-f]+|\d+)\}',
        without_comments(braced(text, "constexpr ClientOnlyKey kClientOnly[] = {")),
    )
    return keys, [(name, symbol, int(offset, 0)) for name, symbol, offset in only]


def game_requests() -> set[str]:
    """The requests the shared table (game_requests.cpp) names, by name: both helpers answer the
    ones that have a handler, and ``config`` and ``offline_put`` are each helper's own."""
    return set(
        re.findall(r'^    \{"([a-z_ ]+)", (?:true|false), ', source("game_requests.cpp"), re.M)
    )
