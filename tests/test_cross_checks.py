"""What the client and the native helper must agree on, read from the helper's sources.

Each value has one home on either side and a copy on the other: the helper's version (the
client refuses another), the game builds both identify, the addresses the client sends in
``config``, the proxy's off switch, and the game's flags and layouts both read (the goal's and
the scanner's flags, the randomizer section, the save slots), the request names the client
sends and the replies it branches on, the offline seed file's header and keys, the label
separator and the font's punctuation, and the item record count. These read the C++ as text,
so they run without a native build. Also the world's ``world_version``, the same release
number.
"""

from __future__ import annotations

import ast
import importlib
import json
import re
import string

import pytest
from native_source import (
    LINUX_SRC,
    ROOT,
    SRC,
    braced,
    function_body,
    game_requests,
    linux_sources,
    source,
    windows_sources,
    without_comments,
)

from client import (
    aplabels,
    discs,
    game,
    ids,
    linux_helper,
    linux_targets,
    native,
    newgame,
    offline,
    patterns,
    session,
    ticks,
)
from client import save_patch as sp

MANIFEST = ROOT / "worlds" / "zexion" / "archipelago.json"


def test_the_client_knows_the_helpers_size_limits() -> None:
    kept = re.findall(r"constexpr size_t kMaxFile = (\d+) \* 1024;", source("offline_fs.h"))
    assert [int(n) * 1024 for n in kept] == [offline.MAX_FILE]
    pipe = re.findall(r"constexpr DWORD kBufferSize = (\d+) \* 1024;", source("pipe.cpp"))
    assert [int(n) * 1024 for n in pipe] == [offline.MAX_REQUEST]
    assert offline.MAX_FILE < offline.MAX_REQUEST


def test_the_helper_is_the_version_the_client_asks_for() -> None:
    versions = re.findall(r'constexpr char kVersion\[\] = "([^"]+)";', source("dllmain.cpp"))
    assert versions == [native.VERSION]


LINUX_CORE = SRC.parent / "linux" / "src" / "core.h"


def test_the_linux_helper_is_the_release_and_agrees_with_the_client() -> None:
    header = LINUX_CORE.read_text(encoding="utf-8")
    assert re.findall(r'constexpr char kVersion\[\] = "([^"]+)";', header) == [native.VERSION]
    assert re.findall(r'constexpr char kDisabledName\[\] = "([^"]+)";', header) == [
        native.DISABLED_NAME
    ]
    sizes = re.findall(r"constexpr size_t kMaxRequest = (\d+) \* 1024;", header)
    assert [int(n) * 1024 for n in sizes] == [offline.MAX_REQUEST]
    assert linux_helper.MAX_REPLY == offline.MAX_REQUEST
    reads = re.findall(r"constexpr size_t kMaxRead = (\d+) \* 1024;", header)
    assert [int(n) * 1024 for n in reads] == [linux_helper.MAX_READ]
    writes = re.findall(r"constexpr size_t kMaxWrite = (\d+) \* 1024;", header)
    assert [int(n) * 1024 for n in writes] == [linux_helper.MAX_WRITE]
    prefix = re.findall(r'constexpr char kDataPrefix\[\] = "([^"]+)";', header)
    assert [p.encode() for p in prefix] == [linux_helper.DATA_PREFIX]


def test_the_linux_helper_knows_the_linux_builds_the_client_knows() -> None:
    core = (LINUX_CORE.parent / "core.cpp").read_text(encoding="utf-8")
    table = braced(core, "kKnownBuilds[] = {")
    rows = re.findall(r'\{\s*"(\w+)",\s*"([0-9a-f]+)"\s*\}', table)
    assert rows, "no builds parsed from core.cpp"
    assert rows == [(b.name, b.build_id) for b in patterns.KNOWN_LINUX_BUILDS]


def test_the_traps_are_the_same_codes_and_names_in_the_client_and_the_helper() -> None:
    # The client sends a trap's popup by its code (native.batch_text); the helper names it in
    # the popup (native/src/trap.h).
    text = source("trap.h")
    codes = dict(re.findall(r"constexpr int (kDisruption|kDisorient) = (\d+);", text))
    assert (int(codes["kDisruption"]), int(codes["kDisorient"])) == (
        ids.DISRUPTION_TRAP,
        ids.DISORIENT_TRAP,
    )
    for code in ids.TRAPS:
        assert f'"{ids.TRAP_NAMES[code]}"' in text
    assert '{"trap_status", ' in source("game_requests.cpp")


def test_the_client_reads_the_ending_started_as_the_helper_says_it() -> None:
    said = re.findall(r'const char kEndingStarted\[\] = "([^"]+)";', source("mission.cpp"))
    assert said == [discs.ENDING_STARTED]
    assert f'{{"{discs.MISSION_STATUS}", ' in source("game_requests.cpp")


@pytest.mark.skipif(not MANIFEST.is_file(), reason="worlds/zexion is missing")
def test_the_world_has_the_release_number_of_the_client() -> None:
    world = json.loads(MANIFEST.read_text(encoding="utf-8"))
    assert world["world_version"] == native.VERSION


def test_the_proxy_and_the_client_have_the_same_off_switch() -> None:
    proxy = (SRC.parent / "proxy" / "version.cpp").read_text(encoding="utf-8")
    assert f'L"{native.DISABLED_NAME}"' in proxy


def test_the_helper_knows_the_builds_the_client_knows() -> None:
    table = braced(source("build.cpp"), "kKnownBuilds[] = {")
    rows = re.findall(r'\{\s*"(\w+)",\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+)\s*\}', table)
    assert rows, "no builds parsed from build.cpp"
    assert [(name, int(stamp, 16), int(size, 16)) for name, stamp, size in rows] == [
        (b.name, b.timestamp, b.size_of_image) for b in patterns.KNOWN_BUILDS
    ]


def config_table() -> str:
    """The helper's kConfigKeys entries, comments stripped."""
    table = braced(source("game_calls.cpp"), "constexpr ConfigKey kConfigKeys[] = {")
    return without_comments(table)


def config_keys() -> dict[str, str]:
    """The helper's kConfigKeys, in order: each key's kind (code, data, import_slot)."""
    table = config_table()
    entries = re.findall(r'\b(code|data|import_slot)\(\s*"(\w+)"', table)
    # Every quoted name in the table is some entry's first argument: none was missed.
    first_args = re.findall(r'\(\s*"(\w+)"', table)
    assert [name for _, name in entries] == first_args
    out = {name: kind for kind, name in entries}
    assert len(out) == len(entries), "a key is listed twice"
    return out


def test_the_config_names_the_addresses_the_helper_takes() -> None:
    keys = config_keys()
    assert len(set(native.CONFIG_KEYS)) == len(native.CONFIG_KEYS)
    assert set(native.CONFIG_KEYS) == set(keys)
    assert not patterns.CLIENT_ONLY_TARGETS & set(keys)
    client = {t.name for t in patterns.CLIENT_TARGETS}
    assert set(keys) == client - patterns.CLIENT_ONLY_TARGETS <= client


def test_the_helper_scans_for_the_addresses_it_configures() -> None:
    # The header tools/gen_native_patterns.py writes (tests/test_gen_native_patterns.py checks it
    # is current) names the targets in the client's order; the helper needs each of its keys.
    table = braced(source("findaddr_patterns.h"), "inline constexpr Target kTargets[] = {")
    scanned = re.findall(r'\{"(\w+)", k_\w+, \d+\}', table)
    assert scanned == list(native.CONFIG_KEYS)
    assert set(scanned) == set(config_keys())


def test_the_helper_says_when_its_addresses_differ_from_the_clients_as_the_client_reads_it() -> (
    None
):
    dllmain = source("dllmain.cpp")
    host = source("host_offline.cpp")
    assert f'"{native.DIFFERS}" + differs' in dllmain
    assert native.DIFFERS.endswith(" ")
    # `hello` gives the scan's outcome and where the addresses came from (native/README.md).
    for field in (" scan=", " addresses="):
        assert f'"{field}' in host, field
    for where in ("scan", "cache", "client"):
        assert f'addresses_ = "{where}";' in host, where
    # The scan's note for optional addresses not found, as hello sends it: spaces made underscores.
    assert '"ok without " + lacking' in host
    one_word = host.split("std::string one_word(std::string text) {", 1)[1].split("}", 1)[0]
    assert "std::replace(text.begin(), text.end(), ' ', '_')" in one_word
    assert "ok without ".replace(" ", "_") == native.SCAN_OK_WITHOUT


def test_the_helper_takes_functions_as_code() -> None:
    keys = config_keys()
    by_name = {t.name: t for t in patterns.TARGETS}
    for name, kind in keys.items():
        # A target named *_fn is a function, so code, except transition_fn, which is data: it
        # holds the address of the routine the game runs next (game_calls.cpp).
        if name.endswith("_fn") and name != "transition_fn":
            assert kind == "code", name
        # A function start (an "at" pattern) is code too.
        if any(p.kind == "at" for p in by_name[name].patterns):
            assert kind == "code", name
    # The code the helper patches, whose bytes the offline cache does not check, is code.
    assert {keys[name] for name in offline.NOT_CHECKED} == {"code"}


def layout(name: str) -> dict[str, int]:
    """The constants of ``namespace name`` in game_layout.h, by name."""
    text = source("game_layout.h")
    opening, closing = f"namespace {name} {{", f"}}  // namespace {name}"
    assert text.count(opening) == 1, opening
    block = text.split(opening, 1)[1].split(closing, 1)[0]
    pairs = re.findall(r"\b(k\w+) = (0x[0-9A-Fa-f]+|\d+)\b", block)
    return {key: int(value, 0) for key, value in pairs}


def test_the_goal_and_scanner_flags_are_the_games_and_the_helpers() -> None:
    # docs/re/memory.md, "The ending: flag 250" (with 211 clear) and "The scanner" (39). The
    # client's tests set flags by these numbers; the helper reads 250 and 39 too.
    assert (session.ENDING_FLAG, session.FINALE_FLAG, session.SCANNED_FLAG) == (250, 211, 39)
    flags = layout("flag")
    assert (flags["kEnding"], flags["kScannerTold"]) == (session.ENDING_FLAG, session.SCANNED_FLAG)
    counts = re.findall(r"\bkFlagCount = (0x[0-9A-Fa-f]+);", source("game_layout.h"))
    assert [int(n, 16) for n in counts] == [game.FLAGS]


def test_the_randomizer_section_is_laid_out_alike() -> None:
    # docs/re/save-format.md: the item table, the start record and the settings, from the
    # section's start; the helper's offsets are the same, as the loaded section is the file's.
    rando = layout("rando")
    assert rando["kItemTable"] == sp.TABLE_AT == 0x26C
    assert rando["kStart"] == sp.START_AT == 0x310
    assert rando["kSettings"] == sp.OPTIONS_AT == 0x87B
    assert (rando["kLocations"], rando["kMaxLocations"]) == (ids.LOCATIONS, ids.MAX_LOCATIONS)
    assert rando["kSecretShuffle"] == ids.SECRET_SHUFFLE
    # The start-with array (the helper leaves those items out of the major-item list).
    assert rando["kStartWith"] == newgame.START_WITH - sp.SECTION == 0x689
    order = braced(source("game_layout.h"), "constexpr int kStartWithOrder[] = {")
    assert tuple(int(n) for n in re.findall(r"\d+", order)) == newgame.START_WITH_ORDER


def test_the_save_slots_are_laid_out_alike() -> None:
    # docs/re/memory.md, "The file select": 30 blocks of 0x9408 (client/game.py, save_stamp).
    slot = layout("slot")
    assert (slot["kCount"], slot["kStride"]) == (game.SLOTS, game.SLOT_STRIDE) == (30, 0x9408)
    assert (slot["kEmpty"], slot["kRandoMarker"]) == (0x935E, 0x9400)


def says(text: str, reply: str) -> bool:
    """Whether ``text`` gives ``reply``: as a string literal, whole or as its start (a reply may
    go on past it), or, for ``error <why>``, built as ``"error " + why`` from a ``return`` of a
    literal why that starts as the reply's does."""
    if f'"{reply}' in text:
        return True
    why = reply.removeprefix("error ")
    built = reply.startswith("error ") and '"error " + ' in text
    return built and re.search(r'return\s+"' + re.escape(why), text) is not None


def test_every_request_the_client_sends_is_one_the_helper_knows() -> None:
    known = game_requests() | {"hello", "ping", "unload"}  # the table's, and handled before it
    assert {"shield", "config", "apply", "ap_newgame", "intel on"} <= known
    sent = {
        "hello", "unload", "shield", "die", "disarm", "newgame_status", "other_save",
        "client_back", "discs_status", discs.MISSION_STATUS, "ap_labels", "ap_newgame",
        "config", "apply", "offline_put", "discs",
        discs.MISSION_REQUEST.split()[0],
        *ticks.ASSISTS[0], *ticks.INTEL[0],
    }  # fmt: skip
    # The names in the client's sources: a request made there is listed above.
    literal = re.compile(r'(?:helper\.request, |helper\.request\()"([a-z_ ]+)"')
    found: set[str] = set()
    for path in (ROOT / "client").glob("*.py"):
        found |= set(literal.findall(path.read_text(encoding="utf-8")))
    assert found <= sent, f"a request the client sends, not checked here: {found - sent}"

    # Requests with arguments are sent by their first word, plus ones named whole (assists lock).
    def named(request: str) -> bool:
        return request in known or request.split()[0] in known

    assert all(named(request) for request in sent), sorted(r for r in sent if not named(r))
    assert named(native.config_text({k: 0 for k in native.CONFIG_KEYS}))
    assert named(native.batch_text([], []))
    assert named(offline.put_request("config", ""))
    assert named(aplabels.labels_request({}))
    assert named(discs.request(1, 2))


def linux_requests() -> set[str]:
    """The requests the Linux helper answers: its dispatchers, helper.cpp's ``game_request`` and
    core.cpp's ``reply`` (a request one of them dropped is no longer known here), and the auth
    request core.h names (first words: ``config`` for a request that goes on past it)."""
    helper = (LINUX_SRC / "helper.cpp").read_text(encoding="utf-8")
    core = (LINUX_SRC / "core.cpp").read_text(encoding="utf-8")
    text = function_body(helper, "std::optional<std::string> game_request(")
    text += function_body(core, "std::string reply(")
    names = re.findall(r'request(?: == |\.starts_with\()"([a-z_ ]+?)(?:\\n)?"', text)
    auth = re.findall(r'kAuthRequest\[\] = "(\w+) "', (LINUX_SRC / "core.h").read_text("utf-8"))
    # The shared table's, which game_request hands on to (prepare_game_request).
    assert "zap::prepare_game_request(request)" in function_body(
        helper, "std::optional<std::string> game_request("
    )
    return {name.strip() for name in names} | set(auth) | game_requests()


def test_every_request_the_client_sends_the_linux_helper_is_one_it_knows() -> None:
    # As the Windows helper's, for every request made through a LinuxMod, a LinuxProcess or
    # either helper's shared ticks (``mod.request``, ``pm.request``, ``self.request_bytes``...).
    known = linux_requests()
    assert {"config", "apply", "ap_newgame", "lang", "trap_status", "ping", "callers"} <= known
    assert {"auth", "read", "write", "shield", "die", "disarm", "hello", "assists lock"} <= known
    sent = {
        "hello", "ping", "callers", "auth", "read", "write", "trap_status", "lang", "shield",
        "die", "disarm", "newgame_status", "other_save", "client_back", "discs_status",
        discs.MISSION_STATUS, "ap_labels", "ap_newgame", "config", "apply", "offline_put",
        "discs", discs.MISSION_REQUEST.split()[0], *ticks.ASSISTS[0], *ticks.INTEL[0],
    }  # fmt: skip
    literal = re.compile(r'\.request(?:_bytes)?(?:, |\()f?"([a-z_]+(?: [a-z]+)?)')
    found: set[str] = set()
    for path in (ROOT / "client").glob("*.py"):
        found |= set(literal.findall(path.read_text(encoding="utf-8")))
    found -= {"unload"}  # the Windows helper's: the Linux helper stays for the game's run
    assert found <= sent, f"a request the client sends, not checked here: {found - sent}"

    def named(request: str) -> bool:
        return request in known or request.split()[0] in known

    assert all(named(request) for request in sent), sorted(r for r in sent if not named(r))
    assert named(linux_targets.helper_config(linux_helper_image()))
    assert named(native.batch_text([], []))
    assert named(offline.put_request("config", ""))
    assert named(aplabels.labels_request({}))
    assert named(discs.request(1, 2))


def test_the_linux_dispatchers_alone_name_the_requests_it_knows() -> None:
    # Not the pre-auth gate's ``hello`` (helper.cpp's serving loop) or any other comparison in
    # the files: a request dropped from game_request, the shared table or reply is no longer
    # known.
    helper = (LINUX_SRC / "helper.cpp").read_text(encoding="utf-8")
    body = function_body(helper, "std::optional<std::string> game_request(")
    assert "mission_status" in linux_requests() and 'request == "hello"' not in body
    assert 'request == "hello"' in helper  # the gate's own, outside game_request
    assert "hello" in linux_requests()  # from core.cpp's reply
    assert "lang" in linux_requests()


def linux_helper_image():
    from test_linux_targets import b1_like

    return b1_like(extra=True)


# The replies the client branches on, as words the helper's sources have as a string literal (a
# reply may go on past it). The client's own comparisons: client/native.py, state.py, ticks.py,
# shields.py, session.py, linux_helper.py, deathlink.py and discs.py (below, by ``ast``). Both
# helpers give these (the Linux helper builds the shared sources of native/src), except
WINDOWS_ONLY_REPLIES = (  # the Windows helper's alone: its own branches
    "; not unloading", "bye", "ok differs ", "error no LOCALAPPDATA",
)  # fmt: skip
LINUX_ONLY_REPLIES = (  # the Linux helper's alone: native/linux/src, or the shared #else branches
    "lang ", "written ", "data ", "error no folder",
)  # fmt: skip
SHARED_REPLIES = (
    "error too large", "refused not settled", "error unknown request",
    "unknown line ", "missing ", "ok", "applied", "refused ",
    "stored", "unchanged", "labeled", "spawned", "died", "armed", "shown", "unknown", "idle",
    "queued", "on:", "the new game is not this seed", discs.ENDING_STARTED, discs.PUT_BACK,
    "a menu is open", "free", "locked", "intel on", "intel off",
    "discs on", "discs off",
    # An offline seed file's put (ticks.linux_seed_tick): "error " and its reason, or "stored".
    "error cannot ", "error bad ", "error not a seed file",
    "not asked",  # a RETURN HOME prompt dropped before it showed (ticks.return_home_tick)
    "pong frames=",
    "asked", "opening", "yes",  # the statuses of a RETURN HOME prompt still on its way
)  # fmt: skip


def test_the_replies_the_client_branches_on_are_ones_the_helper_gives() -> None:
    text = windows_sources()
    for reply in SHARED_REPLIES + WINDOWS_ONLY_REPLIES:
        assert says(text, reply), reply
    for reply in LINUX_ONLY_REPLIES:
        assert not says(text, reply), f"{reply!r} is the Windows helper's too: move it"
    # A new game's refusals are "refused " and the reason (native/src/newgame.cpp, refuse).
    assert 'status_ = "refused " + why;' in source("newgame.cpp")


def test_the_replies_the_client_branches_on_are_ones_the_linux_helper_gives() -> None:
    text = linux_sources()
    for reply in SHARED_REPLIES + LINUX_ONLY_REPLIES:
        assert says(text, reply), reply
    for reply in WINDOWS_ONLY_REPLIES:
        assert not says(text, reply), f"{reply!r} is the Linux helper's too: move it"
    assert 'status_ = "refused " + why;' in source("newgame.cpp")  # shared with the Windows one
    assert 'return "lang " + name;' in (LINUX_SRC / "helper.cpp").read_text(encoding="utf-8")


# What the client compares a reply with that is no protocol word (a name, a count, a said line).
NOT_REPLIES = {
    "told.discs_status",
    "self.said",
    "f'written {len(piece)}'",
    "len(DATA_PREFIX) + piece",
}


def reply_word_nodes(
    name: str, module: object, node: ast.AST, line: int
) -> list[tuple[str, int, str]]:
    """The words ``node`` (a comparison's other side) names in client module ``name``: a string,
    bytes or a tuple of them, or a constant of ``module`` (``DATA_PREFIX``, ``discs.PUT_BACK``).
    Anything else must be listed in NOT_REPLIES."""
    if isinstance(node, (ast.Tuple, ast.List, ast.Set)):
        return [word for elt in node.elts for word in reply_word_nodes(name, module, elt, line)]
    if isinstance(node, ast.Constant):
        value = node.value
    elif isinstance(node, ast.Name):
        value = getattr(module, node.id, None)
    elif isinstance(node, ast.Attribute) and isinstance(node.value, ast.Name):
        value = getattr(getattr(module, node.value.id, None), node.attr, None)
    else:
        value = None
    if isinstance(value, bytes):
        value = value.decode("ascii")
    if isinstance(value, str):
        return [(name, line, value)]
    assert ast.unparse(node) in NOT_REPLIES, (name, line, ast.unparse(node))
    return []


def reply_words() -> list[tuple[str, int, str]]:
    """Every word the client's modules compare a ``reply``, ``status`` or ``why`` with (``==``,
    ``in``, ``startswith`` and its kind, tuples of words too), constants resolved from their
    module: (module, line, word). A comparison with something else is listed in NOT_REPLIES."""
    names = {"reply", "status", "why"}
    found: list[tuple[str, int, str]] = []
    modules = (
        "native", "state", "ticks", "shields", "session", "linux_helper", "deathlink", "discs",
    )  # fmt: skip
    for name in modules:
        module = importlib.import_module(f"client.{name}")
        tree = ast.parse((ROOT / "client" / f"{name}.py").read_text(encoding="utf-8"))
        for node in ast.walk(tree):
            if isinstance(node, ast.Compare) and any(
                isinstance(n, ast.Name) and n.id in names for n in ast.walk(node.left)
            ):
                if all(isinstance(op, (ast.Eq, ast.NotEq, ast.In, ast.NotIn)) for op in node.ops):
                    for other in node.comparators:
                        found += reply_word_nodes(name, module, other, node.lineno)
            elif (
                isinstance(node, ast.Call)
                and isinstance(node.func, ast.Attribute)
                and node.func.attr in ("startswith", "endswith", "removeprefix")
                and any(
                    isinstance(n, ast.Name) and n.id in names for n in ast.walk(node.func.value)
                )
                and node.args
            ):
                found += reply_word_nodes(name, module, node.args[0], node.lineno)
    return found


def test_the_replies_the_clients_comparisons_name_are_all_checked_here() -> None:
    ours = set(SHARED_REPLIES + WINDOWS_ONLY_REPLIES + LINUX_ONLY_REPLIES)
    ours |= {"unknown request", "refused", "queued ", "error "}  # a prefix, or the form built
    words = reply_words()
    for name, line, word in words:
        assert word in ours, (name, line, word)
    seen = {word for _, _, word in words}
    assert len(words) > 50, "the scan must find what it is there to check"
    # The forms a line-by-line pattern missed: a tuple of words, and a constant (client/discs.py's
    # and native.py's, linux_helper.py's bytes).
    assert {"asked", "opening", "yes", discs.PUT_BACK, native.DIFFERS, "data "} <= seen


def test_the_offline_seed_file_has_the_header_and_keys_the_helper_reads() -> None:
    reader = source("offline.cpp").split("std::string parse_offline_seed", 1)[1]
    reader = reader.split("bool offline_matches", 1)[0]
    header = re.findall(r'line != "(zexion-ap offline \d+)"', reader)
    keys = set(re.findall(r'key == "(\w+)"', reader))
    assert len(header) == 1 and keys
    text = offline.seed_text(
        [1, 2], (3, 4, 5), True, True, (1, 2, "Collect 2 OS Install Disks!", [7]),
        aplabels.labels_request({(1, 2): "@0"}), [(8, 101)],
    )  # fmt: skip
    lines = text.splitlines()
    assert lines[0] == header[0]
    assert {line.split()[0] for line in lines[1:]} <= keys | {"ap_labels", "label"}
    # The labels go last: the helper reads everything after "ap_labels" as that request.
    assert lines.index("ap_labels") == len(lines) - 2
    assert 'line == "ap_labels"' in reader
    without = offline.seed_text([1], (0, 0, 0), False, False, None, None)
    assert {line.split()[0] for line in without.splitlines()[1:]} <= keys


def test_the_helper_writes_a_seed_file_as_the_client_does() -> None:
    # The helper writes a seed back out (format_offline_seed) when it makes two copies one: its
    # lines, in its order, are the client's (offline.seed_text).
    writer = source("offline.cpp").split("std::string format_offline_seed", 1)[1]
    writer = writer.split("std::string synced_file_text", 1)[0]
    in_order = re.findall(
        r'"(zexion-ap offline 1|table|start|intel|assists|discs|unfound|traps)\b', writer
    )
    text = offline.seed_text(
        [1, 2], (3, 4, 5), True, True, (1, 2, "Collect 2 OS Install Disks!", [7]),
        aplabels.labels_request({(1, 2): "@0"}), [(8, 101)],
    )  # fmt: skip
    client = [line.split()[0] for line in text.splitlines()]
    client = [k if k != "zexion-ap" else "zexion-ap offline 1" for k in client]
    first = list(dict.fromkeys(in_order))  # "discs" and "unfound" are written on two branches
    assert first == [k for k in client if k in first]
    assert client[-2] == "ap_labels"  # which the writer appends as it holds it
    assert "seed.labels" in writer


def test_the_seed_copy_in_the_save_folder_has_the_name_steam_cloud_syncs() -> None:
    # Steam's Auto-Cloud rule for the game syncs `*.sav` in the save folder
    # (docs/re/save-format.md, "Steam Auto-Cloud syncs only *.sav and *.png in the save folder
    # itself"), and the helpers write only under their own prefix, never a name the game uses.
    header = (SRC / "offline.h").read_text(encoding="utf-8")
    prefix = re.findall(r'kSyncedPrefix = "([^"]*)";', header)
    suffix = re.findall(r'kSyncedSuffix = "([^"]*)";', header)
    assert prefix == ["ZexionAP-seed-"] and suffix == [".sav"]
    game_names = ("Save Game", "GameUnlocks", "Preferences", "steam_autocloud")
    assert not any(prefix[0].startswith(name) for name in game_names)
    # The one write into the save folder goes through write_synced_copy, under that name.
    code = source("offline.cpp")
    assert code.count("write_whole(present,") == 1
    assert "const std::wstring present = join(save, synced_file_name(base));" in code
    # The Linux build lists the same names, so a file one writes is read by the other.
    assert code.count("kSyncedPrefix") >= 3
    claude = (ROOT / "CLAUDE.md").read_text(encoding="utf-8")
    assert f"`{prefix[0]}<NAME>{suffix[0]}`" in claude


def test_the_helper_never_opens_the_games_save_files() -> None:
    # The game opens a save to write with no share mode: a read open by the helper, on its pipe
    # thread, could fail the game's save. Which seeds a save uses comes from the slots in the game's
    # memory (Game::saved_seeds, published from the frame call), never from the files.
    code = source("offline.cpp")
    assert "Save Game" not in code.replace("`Save Game N.sav`", "")
    assert "read_prefix" not in code and "saved_seeds(save" not in code
    host = source("host_offline.cpp")
    assert "game_.saved_seeds()" in host and "saved_.publish(" in host
    assert "g_host.snapshot_saves()" in source("dllmain.cpp")
    linux = (ROOT / "native" / "linux" / "src" / "helper.cpp").read_text(encoding="utf-8")
    assert "g_game->saved_seeds()" in linux and "g_saved->publish(" in linux
    # Both helpers write a copy from a thread of their own, never the frame call.
    assert "copy_thread" in host and "copy_loop" in linux


def test_the_test_builds_read_their_folders_only_through_the_guard() -> None:
    # A test run must never write the real save folder or per-user folder (CLAUDE.md, "Rules"):
    # the test builds take them through restrict_to_test_folder and name their save folder only in
    # ZAP_TEST_SAVE_DIR, never ZEXION_SAVE_DIR (which the repo's tools define as the real one).
    windows = source("dllmain.cpp")
    assert "zap::restrict_to_test_folder(zap::offline_folder())" in windows
    # Chosen at compile time by the target, so the release DLL has no test-folder rule.
    assert "restrict_to_test_folder" not in source("host_offline.cpp")
    assert 'L"ZAP_TEST_SAVE_DIR"' in windows and 'L"ZEXION_SAVE_DIR"' not in windows
    linux = (ROOT / "native" / "linux" / "src" / "helper.cpp").read_text(encoding="utf-8")
    assert "zap::restrict_to_test_folder(zap::offline_folder())" in linux
    assert '"ZAP_TEST_SAVE_DIR"' in linux and 'getenv("ZEXION_SAVE_DIR")' not in linux
    # And the production build in no known build takes no seed.
    assert 'return "error not the game";' in source("dllmain.cpp")


def test_no_name_the_client_puts_in_a_label_has_the_helpers_separator() -> None:
    # "@N NAME/PICKUP": the helper ends NAME at the first "/" (native/src/requests.cpp,
    # parse_labels), which the client never sends in one.
    assert "NAME ends at the first '/', which the client never sends" in source("requests.cpp")
    names = [name for _, name in aplabels.ZEXION_LOOKS.values()]
    names += [discs.NAME, *ids.TRAP_NAMES.values()]
    for name in names:
        assert "/" not in aplabels.own_label(1, name).split(" ", 1)[-1], name
    for code, look in ((ids.DISRUPTION_TRAP, 3), (ids.DISORIENT_TRAP, 40)):
        first = aplabels.trap_label(look, code).split("\n")[0]
        assert first.count("/") == 1, first


def test_the_labels_font_is_the_helpers() -> None:
    punctuation = re.findall(r'kFontPunctuation\[\] = "([^"]*)";', source("text.h"))
    assert len(punctuation) == 1
    letters = set(string.ascii_uppercase + string.digits)
    assert letters | set(punctuation[0]) == aplabels.FONT


def test_the_game_has_as_many_item_records_as_the_client_counts() -> None:
    assert layout("record")["kCount"] == len(ids.GAME_ITEMS) == 51
