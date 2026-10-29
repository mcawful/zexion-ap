"""What the client and the native helper must agree on, read from the helper's sources.

Each value has one home on either side and a copy on the other: the helper's version (the
client refuses another), the game builds both identify, the addresses the client sends in
``config``, the proxy's off switch, and the game's flags and layouts both read (the goal's and
the scanner's flags, the randomizer section, the save slots). These read the C++ as text, so
they run without a native build. Also the world's ``world_version``, the same release number.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

import pytest

from client import game, ids, native, offline, patterns, session
from client import save_patch as sp

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "native" / "src"
MANIFEST = ROOT / "worlds" / "zexion" / "archipelago.json"


def source(name: str) -> str:
    return (SRC / name).read_text(encoding="utf-8")


def braced(text: str, opening: str) -> str:
    """What lies between ``opening`` (ending in ``{``) and the ``};`` that closes it."""
    assert text.count(opening) == 1, opening
    return text.split(opening, 1)[1].split("};", 1)[0]


def test_the_helper_is_the_version_the_client_asks_for() -> None:
    versions = re.findall(r'constexpr char kVersion\[\] = "([^"]+)";', source("dllmain.cpp"))
    assert versions == [native.VERSION]


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
    return re.sub(r"//[^\n]*", "", table)


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
    assert not patterns.TOOL_TARGETS & set(keys)


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


def client_only_keys() -> set[str]:
    """The keys kConfigKeys marks client_only: an offline cache may lack them."""
    pattern = r'\bclient_only\(\s*(?:code|data|import_slot)\(\s*"(\w+)"'
    return set(re.findall(pattern, config_table()))


# Where the client's parts read the client-only members, by file: the definitions there that may
# (a function, or "Class::" for its methods).
# - game_calls.cpp: the grants (apply, refresh_hud), DeathLink (kill), the Energy Shield
#   (run_item_event) and the new game (watch_newgame, start_archipelago, place_seed,
#   ensure_start_table).
# - game_calls.h: panel_handler, for the wrapper ARCHIPELAGO's choice installs.
# - menu.cpp: ARCHIPELAGO's choice, installed only with a new game armed.
# - mission.cpp: RETURN HOME's fade (fade_frames, MissionPrompt::fade), reached only once a
#   prompt is asked for, and the game's routines (routine), which MissionPrompt::watch reads every
#   frame: a null table gives no routine, so nothing runs (offline_tests.cpp drives it null). The
#   ending's check for an unload (MissionPrompt::ending_queued) tests the table for null first.
# - feats.cpp: the Steam stub's test while an AP save loads, set only with current_slot
#   non-null (native/src/feats.h; native/tests/feats_tests.cpp runs it without the key).
CLIENT_READERS = {
    "game_calls.cpp": (
        "Game::apply",
        "Game::refresh_hud",
        "Game::kill",
        "Game::run_item_event",
        "Game::watch_newgame",
        "Game::start_archipelago",
        "Game::place_seed",
        "Game::ensure_start_table",
    ),
    "game_calls.h": ("panel_handler",),
    "menu.cpp": ("ArchipelagoChoice::",),
    "mission.cpp": (
        "fade_frames",
        "routine",
        "MissionPrompt::fade",
        "MissionPrompt::ending_queued",
    ),
    "feats.cpp": ("FeatsGuard::watch", "FeatsGuard::set_switch"),
}
# Where a client's state comes into Game (game_calls.cpp): each refuses until every key is set
# (Game::client_ready), and the readers above run only on what these set. rearm, which puts a
# reloaded helper's state back, takes the armed new game and the asked prompt only with every key.
_REFUSES = r"if \(!client_ready\(\)\) return\b"
CLIENT_GATES = {
    "Game::apply": (_REFUSES,),
    "Game::kill": (_REFUSES,),
    "Game::spawn_shield": (_REFUSES,),
    "Game::arm_newgame": (_REFUSES,),
    "Game::request_mission": (_REFUSES,),
    "Game::rearm": (
        r"if \(s\.newgame_armed && client_ready\(\)\) \{",
        r"if \(client_ready\(\)\) mission_\.resume\(",
    ),
}

_DEFINITION = re.compile(
    r"^(?!//|#|namespace|using|struct|class|enum|template|\}|\s)[^;=]*?((?:\w+::)*~?\w+)\("
)


# In a header, a member function defined in its class: indented, its body opened on its line.
_MEMBER = re.compile(
    r"^\s+(?=\S)(?!(?:return|if|else|for|while|switch|case|do)\b)"
    r"[\w:<>,*&\s~]*?\b(~?\w+)\([^;{}=]*\)[^;{}=]*\{"
)
# A comment, or a string or character literal (kept, so a "//" or "/*" in one is no comment).
_COMMENT_OR_LITERAL = re.compile(
    r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|/\*.*?\*/|//[^\n]*', re.DOTALL
)


def without_comments(text: str) -> str:
    """The source with every comment, // or /* */, blanked to spaces (its line breaks kept)."""

    def blank(m: re.Match[str]) -> str:
        found = m.group(0)
        return found if found[0] in "\"'" else re.sub(r"[^\n]", " ", found)

    return _COMMENT_OR_LITERAL.sub(blank, text)


def code_lines(name: str) -> list[tuple[str, int, str]]:
    """Each line of a helper source, its comments blanked, with the definition it lies in: the
    last at column 0, or in a header a member function defined in its class ("" before the
    first)."""
    out = []
    current = ""
    for number, line in enumerate(without_comments(source(name)).splitlines(), 1):
        m = _DEFINITION.match(line) or (name.endswith(".h") and _MEMBER.match(line))
        if m:
            current = m.group(1)
        out.append((current, number, line))
    return out


def reads_allowed(name: str, function: str) -> bool:
    return any(
        rule == function or (rule.endswith("::") and function.startswith(rule))
        for rule in CLIENT_READERS.get(name, ())
    )


def test_the_client_only_keys_are_read_only_by_the_clients_parts() -> None:
    # A key the offline cache may lack is left null there: only the client's parts may read it,
    # so none of the parts the offline look also uses (the title, the file select, a dressed
    # save, the map, feats) can (native/tests/offline_tests.cpp drives them with it null).
    client_only = client_only_keys()
    assert client_only, "no client_only keys parsed from game_calls.cpp"
    assert client_only <= set(config_keys())
    member = re.compile(r"(?:\.|->)(" + "|".join(sorted(client_only)) + r")\b")
    for path in sorted(SRC.iterdir()):
        if path.suffix not in (".cpp", ".h") or path.name == "addresses.h":
            continue
        for function, number, code in code_lines(path.name):
            if member.search(code):
                assert reads_allowed(path.name, function), f"{path.name}:{number} ({function})"


def test_the_clients_parts_wait_for_every_key() -> None:
    bodies: dict[str, str] = {}
    for function, _, code in code_lines("game_calls.cpp"):
        bodies[function] = bodies.get(function, "") + code + "\n"
    for gate, forms in CLIENT_GATES.items():
        for form in forms:
            assert re.search(r"^\s*" + form, bodies.get(gate, ""), re.M), f"{gate} is not gated"


def test_the_native_test_lists_the_client_only_keys() -> None:
    # native/tests/offline_tests.cpp configures from a cache without these, in the table's order.
    test = (SRC.parent / "tests" / "offline_tests.cpp").read_text(encoding="utf-8")
    listed = re.findall(r'"(\w+)"', braced(test, "const char* const kClientOnly[] = {"))
    client_only = client_only_keys()
    assert listed == [key for key in config_keys() if key in client_only]


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


def test_the_save_slots_are_laid_out_alike() -> None:
    # docs/re/memory.md, "The file select": 30 blocks of 0x9408 (client/game.py, slot_markers).
    slot = layout("slot")
    assert (slot["kCount"], slot["kStride"]) == (game.SLOTS, game.SLOT_STRIDE) == (30, 0x9408)
    assert (slot["kEmpty"], slot["kRandoMarker"]) == (0x935E, 0x9400)
