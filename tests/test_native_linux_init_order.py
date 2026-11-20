"""The Linux helper starts its serving thread from the library's constructor
(native/linux/src/helper.cpp, ``start``), which runs in the library's ``.init_array`` ahead of
every dynamic initializer of default priority, in the helper's own file and in the shared sources
(``zap_shared``) alike. A global the thread reads that is built dynamically after that, with a
C++ library whose constructors are not constexpr, would be built afresh over what the thread holds.

What this covers, exactly (it reads the sources; nothing here needs the build or Linux):

* ``helper.cpp`` and ``game_mem_linux.cpp``, every namespace-scope object in them (a declaration
  starting in column 0 that is not a function or a type): it is constant by its type (``constexpr``,
  ``constinit``, ``std::atomic``, a scalar, a pointer, an array of those, or one of the few plain
  structs listed below), ``Kept`` (built in place, never destroyed), or has ``init_priority(101)``,
  which builds it before the constructor runs, whatever the compiler's library does. Any other
  type, a user's or the library's, fails until it is one of these, and so does a scalar whose
  initializer is a call (``= getenv(...)``), which runs at load time. Not seen by this scan: a
  type declared together with its object (``struct { ... } g;``).
* ``feats.cpp``'s stand-in holder, which the Steam export answers with from any thread, must be
  ``constinit``.

Not covered: the other shared sources' globals (``feats.cpp``'s ``g_replaced``, ``g_hd`` and
``g_steam``, ``offline.cpp``, and the rest). They are read on the game's thread or by the
export, not by the serving thread before its first frame (reviewed by reading, 2026-11-19), which
this test does not check: a change that has the thread read one is for its author to add here.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

FILES = [ROOT / "native/linux/src/helper.cpp", ROOT / "native/src/game_mem_linux.cpp"]

PRIORITY = re.compile(r"__attribute__\(\(init_priority\((\d+)\)\)\)")

# A type a global may have with no priority: constant-initialized in every C++20 library.
CONSTANT = re.compile(
    r"^(?:thread_local\s+|static\s+|inline\s+|extern\s+)*"
    r"(?:constexpr\b|constinit\b|const\s+(?:char|int|bool)\b"
    r"|std::atomic(?:_flag)?\b"
    r"|(?:const\s+)?(?:bool|char|int|long|unsigned|float|double|size_t|ssize_t|uint\d+_t"
    r"|int\d+_t|uintptr_t|intptr_t|DWORD|BYTE|Swap)\b"
    r"|zap::linux_helper::SymbolSpan\b)"
)


def declarations(text: str) -> list[str]:
    """Each statement that starts in column 0 (outside a function: these files' namespaces are
    not indented), to its ``;`` or ``{``, with its indented continuation lines."""
    out = []
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if not line or line[0] in " \t/#}" or line.startswith("namespace"):
            continue
        statement = line
        j = i
        while ";" not in statement and "{" not in statement and j + 1 < len(lines):
            if not lines[j + 1].startswith((" ", "\t")):
                break
            j += 1
            statement += " " + lines[j].strip()
        out.append(statement)
    return out


def objects(text: str) -> list[tuple[str, str, str]]:
    """The declarations that are objects, as (statement, type, name): no parameter list before
    the declarator ends, and not a type or an alias."""
    found = []
    for statement in declarations(text):
        bare = PRIORITY.sub("", statement).strip()
        end = min((bare.find(c) for c in ";{=" if c in bare), default=len(bare))
        head = re.sub(r"\balignas\([^)]*\)", "", bare[:end]).strip()
        plain = head
        while re.search(r"<[^<>]*>", plain):  # std::function<void()>: the parentheses are a type's
            plain = re.sub(r"<[^<>]*>", "", plain)
        if "(" in plain or re.match(r"(template|using|struct|class|enum|typedef)\b", head):
            continue
        match = re.match(r"(.*?)\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)*$", head)
        if not match:
            continue
        found.append((statement, match.group(1).strip(), match.group(2)))
    return found


def test_every_global_is_constant_kept_or_built_before_the_helpers_constructor() -> None:
    checked = 0
    for path in FILES:
        for statement, kind, name in objects(path.read_text(encoding="utf-8")):
            checked += 1
            if CONSTANT.match(kind):
                # Constant by its type, unless a call initializes it (`= getenv(...)`): that runs
                # at load time, in an initializer of default priority.
                after = PRIORITY.sub("", statement).split(";")[0]
                initializer = after[after.index("=") :] if "=" in after else ""
                constant_init = re.match(
                    r"(?:thread_local\s+|static\s+)*(?:constexpr|constinit)\b", kind
                )
                assert constant_init or "(" not in initializer or PRIORITY.search(statement), (
                    f"{path.name}: {name} is initialized by a call, at load time: "
                    f"{statement.strip()}"
                )
                continue
            if kind.startswith("Kept<") or PRIORITY.search(statement):
                priority = PRIORITY.search(statement)
                assert priority and priority.group(1) == "101", (
                    f"{path.name}: {name} is built at default priority, after the serving "
                    f"thread starts: {statement.strip()}"
                )
                continue
            raise AssertionError(
                f"{path.name}: {name} ({kind}) is not constant-initialized by its type, Kept or "
                f"init_priority(101): {statement.strip()}"
            )
    assert checked >= 40  # the scan sees the files' objects, not nothing


def test_the_scan_finds_the_globals_the_serving_thread_reads() -> None:
    names = set()
    for path in FILES:
        for statement, _kind, name in objects(path.read_text(encoding="utf-8")):
            if PRIORITY.search(statement):
                names.add(name)
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
    } <= names


def test_the_steam_exports_stand_in_is_built_at_load_time() -> None:
    # The export may be called by a constructor of another library, before the helper's own runs:
    # its holder is never zero then (a constinit object is a compile error if it is not constant).
    source = (ROOT / "native/src/feats.cpp").read_text(encoding="utf-8")
    assert re.search(r"^constinit StandIn g_stand_in;", source, re.MULTILINE)
