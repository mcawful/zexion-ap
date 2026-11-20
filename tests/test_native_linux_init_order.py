"""The Linux helper starts its serving thread from the library's constructor
(native/linux/src/helper.cpp, ``start``), which runs in the library's ``.init_array`` ahead of
every dynamic initializer of default priority, in the helper's own file and in the shared sources
(``zap_shared``) alike. A global the thread reads that is built dynamically after that, with a
C++ library whose constructors are not constexpr, would be built afresh over what the thread holds.

So a namespace-scope object of a type with a constructor, in the sources the Linux helper links
that the serving thread reaches, is either ``Kept`` (which is built in place, and never destroyed)
or has ``init_priority(101)``, which builds it before the constructor runs, whatever the
compiler's library does. This reads the sources: nothing here needs the build or Linux.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# The files with namespace-scope objects the serving thread reads: the helper's, and the memory
# layer's Linux part that its `read` and `write` reach.
FILES = [ROOT / "native/linux/src/helper.cpp", ROOT / "native/src/game_mem_linux.cpp"]

# Types whose constructor may not be constant: the standard library's containers, strings,
# synchronization objects, and the helper's own wrapper for a global it never destroys.
NON_TRIVIAL = re.compile(
    r"\b(std::(?:vector|string|wstring|map|set|unordered_map|unordered_set|deque|list|function|"
    r"optional|mutex|once_flag|shared_ptr|unique_ptr)|Kept)\b"
)
PRIORITY = re.compile(r"__attribute__\(\(init_priority\((\d+)\)\)\)")


def declarations(text: str) -> list[str]:
    """Each statement that starts in column 0 (outside a function, as these files' namespaces are
    not indented), up to the ``;``, ``{`` or ``(`` that ends its declarator."""
    out = []
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if not line or line[0] in " \t/#}" or line.startswith("namespace"):
            continue
        statement = line
        j = i
        # A declaration may continue on indented lines (the attribute's line, a long type).
        while ";" not in statement and "{" not in statement and j + 1 < len(lines):
            if not lines[j + 1].startswith((" ", "\t")):
                break
            j += 1
            statement += " " + lines[j].strip()
        out.append(statement)
    return out


def objects(text: str) -> list[str]:
    """The declarations that are objects (no parameter list before the declarator ends)."""
    found = []
    for statement in declarations(text):
        bare = PRIORITY.sub("", statement)
        end = min((bare.find(c) for c in ";{=" if c in bare), default=len(bare))
        head = bare[:end]
        if "(" in head or head.lstrip().startswith(("template", "using", "struct", "class")):
            continue  # a function, a type
        if re.match(r"\s*(?:static |inline |extern |const )*(?:constexpr)\b", head):
            continue  # constant by definition
        found.append(statement)
    return found


def test_every_global_with_a_constructor_is_built_before_the_helpers_constructor() -> None:
    checked = 0
    for path in FILES:
        for statement in objects(path.read_text(encoding="utf-8")):
            if not NON_TRIVIAL.search(statement.split(";")[0].split("=")[0].split("{")[0]):
                continue
            checked += 1
            priority = PRIORITY.search(statement)
            assert priority and priority.group(1) == "101", (
                f"{path.name}: a global built at default priority would be built after the "
                f"serving thread starts: {statement.strip()}"
            )
    assert checked >= 12  # helper.cpp's Kept globals and mutex, the memory layer's vectors


def test_the_scan_finds_the_globals_it_checks() -> None:
    # The scan sees what it should (it would pass vacuously if it matched nothing).
    names = []
    for path in FILES:
        for statement in objects(path.read_text(encoding="utf-8")):
            if NON_TRIVIAL.search(statement.split(";")[0].split("=")[0].split("{")[0]):
                names.append(re.findall(r"\b(g_\w+)", statement.split(";")[0])[-1])
    assert {
        "g_client_writes",
        "g_mailbox",
        "g_game",
        "g_offline_files",
        "g_put_seeds",
        "g_offline_folder",
        "g_token",
        "g_offline_mutex",
        "g_loads",
        "g_code",
        "g_spans_once",
        "g_map_mutex",
        "g_map",
    } <= set(names)
