"""The Linux helper starts its serving thread from the library's constructor
(native/linux/src/helper.cpp, ``start``), a function in the library's ``.init_array`` of default
priority: it runs ahead of the initializers with a priority (``init_priority(101)``) and, among
those of default priority, in link order, so a dynamic initializer of a shared source
(``zap_shared``: ``offline.cpp`` was one, found by the review before 2.0.0) can run after it, in
the thread's way. A
global the thread reads that is built dynamically after that would be built afresh over what the
thread holds, or walked before it is built.

What this covers, exactly:

* The sources (nothing here needs the build or Linux): ``helper.cpp``, ``game_mem_linux.cpp`` and
  ``offline.cpp``, every namespace-scope object in them (a declaration starting in column 0 that
  is not a function or a type): it is constant by its type (``constexpr``, ``constinit``,
  ``std::atomic``, ``std::mutex``, a scalar, a pointer, an array of those, or one of the few plain
  structs listed below), ``Kept`` (built in place, never destroyed) with ``init_priority(101)``,
  which builds it before the constructor runs, whatever the compiler's library does. Any other
  type, a user's or the library's, fails until it is one of these, and so does a scalar whose
  initializer is a call (``= getenv(...)``, ``{call()}``) or names another ``g_`` global, which
  runs at load time. A global of ``offline.cpp``, which builds on MSVC too and so has no
  ``init_priority``, is not allowed to be a ``Kept``: its one that needs a constructor, the pending
  seeds, is a ``Kept`` in a function-local static (built on first use, whichever thread asks first;
  checked below). Not seen by this scan: a type declared together with its object
  (``struct { ... } g;``), a direct-initialized object (``T g(call());``, read as a function), and
  an initializer naming a non-constexpr variable that is not a ``g_`` global.
* ``feats_linux.cpp``'s stand-in holder, which the Steam export answers with from any thread, must be
  ``constinit``.
* With a Linux build (``native/build``, or ``ZEXION_NATIVE_BUILD``), the order itself, in each
  helper's own ``.init_array``: after the constructor come only the C++ runtime's own initializers
  (``*.cc``), none of this project's (``*.cpp``). That covers every shared source, not only the
  three read above, and the file that ships (CI runs it on the Steam Runtime build too). The
  runtime's own initializers after the constructor (``_GLOBAL__sub_I_*.cc`` of the statically
  linked libstdc++: locale instantiations, ``system_error``, ``ios_errcat``, ``eh_alloc``) are
  accepted, not examined: the locale and error-category ones are lazily built or constant when
  first used, and ``eh_alloc``'s emergency pool is read only when ``malloc`` fails for an exception.
  That is reasoning from the library's design, not a proof (2026-11-21, GCC 13 and 14 builds); the
  serving thread starts in the constructor because a client must be able to attach before the
  game's first frame, and moving it to the first ``SDL_GL_SwapWindow`` was not tried in the game.

Not covered: the other shared sources' globals (``feats.cpp``'s ``g_replaced``, ``g_hd`` and
``g_steam``, and the rest) by the source scan: the build check sees that none has a dynamic
initializer (they are constant by their types), which it keeps true.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent

OFFLINE = ROOT / "native/src/offline.cpp"
# offline.cpp builds on MSVC too: no init_priority there, so no global of it is a Kept.
FILES = [ROOT / "native/linux/src/helper.cpp", ROOT / "native/src/game_mem_linux.cpp", OFFLINE]

PRIORITY = re.compile(r"__attribute__\(\(init_priority\((\d+)\)\)\)")

# A type a global may have with no priority: constant-initialized in every C++20 library.
CONSTANT = re.compile(
    r"^(?:thread_local\s+|static\s+|inline\s+|extern\s+)*"
    r"(?:constexpr\b|constinit\b|const\s+(?:char|int|bool)\b"
    r"|std::atomic(?:_flag)?\b|std::mutex\b"  # a mutex's constructor is constexpr
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


def objects(text: str) -> list[tuple[str, str, str, str]]:
    """The declarations that are objects, as (statement, type, name, initializer): no parameter
    list before the declarator ends, and not a type or an alias. The initializer is what follows
    the name, from its ``=`` or ``{`` to the ``;``."""
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
        found.append((statement, match.group(1).strip(), match.group(2), bare[end:].split(";")[0]))
    return found


def test_every_global_is_constant_kept_or_built_before_the_helpers_constructor() -> None:
    checked = 0
    for path in FILES:
        for statement, kind, name, initializer in objects(path.read_text(encoding="utf-8")):
            checked += 1
            if CONSTANT.match(kind):
                # Constant by its type, unless its initializer is a call (`= getenv(...)`,
                # `{call()}`) or names another global (`= g_other + 1`): that runs at load time,
                # in an initializer of default priority.
                constant_init = re.match(
                    r"(?:thread_local\s+|static\s+)*(?:constexpr|constinit)\b", kind
                )
                dynamic = "(" in initializer or re.search(r"\bg_\w+", initializer)
                assert constant_init or not dynamic or PRIORITY.search(statement), (
                    f"{path.name}: {name} is initialized by a call or another global, at load "
                    f"time: {statement.strip()}"
                )
                continue
            if path == OFFLINE:
                raise AssertionError(
                    f"{path.name}: {name} ({kind}) is a global with a constructor in a shared "
                    "source: built after the helper's constructor started the serving thread "
                    f"(the 2.0.0 review's start-up race); hold it in a function-local Kept: "
                    f"{statement.strip()}"
                )
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
        for statement, _kind, name, _init in objects(path.read_text(encoding="utf-8")):
            if PRIORITY.search(statement):
                names.add(name)
    assert {
        "g_client_writes",
        "g_mailbox",
        "g_game",
        "g_inbox",
        "g_offline_folder",
        "g_save_folder",
        "g_saved",
        "g_token",
        "g_loads",
        "g_code",
        "g_spans_once",
        "g_map_mutex",
        "g_map",
    } <= names


def test_the_steam_exports_stand_in_is_built_at_load_time() -> None:
    # The export may be called by a constructor of another library, before the helper's own runs:
    # its holder is never zero then (a constinit object is a compile error if it is not constant).
    source = (ROOT / "native/src/feats_linux.cpp").read_text(encoding="utf-8")
    assert re.search(r"^constinit StandIn g_stand_in;", source, re.MULTILINE)


def test_the_pending_seeds_are_built_on_first_use_and_never_destroyed() -> None:
    # Round 7's LH1: `std::map g_pending;` at namespace scope in offline.cpp was built by that
    # file's own initializer, after the helper's constructor had started the serving thread, which
    # walks it in its first join_saves; and destroyed by exit() under the threads.
    source = OFFLINE.read_text(encoding="utf-8")
    assert "g_pending" not in source
    assert re.search(
        r"PendingSeeds& pending_seeds\(\) \{\s+static Kept<PendingSeeds> pending;"
        r"\s+return \*pending;",
        source,
    )
    assert source.count("pending_seeds()") >= 4  # every use goes through it


def _init_array(library: Path) -> list[str]:
    """The names of the initializers of ``library``'s ``.init_array``, in the order they run."""
    dump = subprocess.run(
        ["readelf", "-x", ".init_array", str(library)], capture_output=True, text=True, check=True
    ).stdout
    raw = b""
    for line in dump.splitlines():
        match = re.match(r"\s+0x[0-9a-f]+((?: [0-9a-f]{1,8}){1,4})", line)
        if match:
            raw += bytes.fromhex("".join(match.group(1).split()))
    entries = [int.from_bytes(raw[i : i + 8], "little") for i in range(0, len(raw) - 7, 8)]
    names: dict[int, str] = {}
    listing = subprocess.run(["nm", str(library)], capture_output=True, text=True, check=True)
    for line in listing.stdout.splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3 and parts[1] in "tTwW":
            names.setdefault(int(parts[0], 16), parts[2])
    return [names.get(entry, hex(entry)) for entry in entries]


BUILD = Path(os.environ.get("ZEXION_NATIVE_BUILD", ROOT / "native" / "build"))


@pytest.mark.parametrize("name", ["libzexion_ap.so", "libzexion_ap_hooktest.so"])
def test_no_initializer_of_ours_runs_after_the_helpers_constructor(name: str) -> None:
    library = BUILD / name
    if not sys.platform.startswith("linux") or shutil.which("readelf") is None:
        pytest.skip("reads an ELF library with readelf, on Linux")
    if not library.is_file() or shutil.which("nm") is None:
        if os.environ.get("ZEXION_REQUIRE") == "helper":
            raise RuntimeError(f"no {name} in {BUILD} (ZEXION_REQUIRE=helper)")
        pytest.skip(f"no {name} in {BUILD}")
    order = _init_array(library)
    starts = [i for i, entry in enumerate(order) if re.search(r"5startEv$", entry)]
    assert len(starts) == 1, f"the helper's constructor is not in {name}'s .init_array: {order}"
    after = order[starts[0] + 1 :]
    # What comes after is the C++ runtime's own (libstdc++, linked in): `_GLOBAL__sub_I_*.cc`.
    # Anything of ours (`_GLOBAL__sub_I_offline.cpp`) is built when the serving thread may already
    # be reading what it builds.
    ours = [entry for entry in after if not entry.endswith(".cc")]
    assert not ours, f"{name}: built after the helper's constructor started its threads: {ours}"
    # And the ones with a priority are before it, as the sources say (helper.cpp and
    # game_mem_linux.cpp).
    before = order[: starts[0]]
    assert any(".00101_" in entry and "helper.cpp" in entry for entry in before), before
    assert any(".00101_" in entry and "game_mem_linux.cpp" in entry for entry in before), before
