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

from client import discs, game, ids, native, newgame, offline, patterns, session
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


def test_the_traps_are_the_same_codes_and_names_in_the_client_and_the_helper() -> None:
    # The client sends a trap's popup and `trap` line by its code (native.batch_text); the
    # helper names it in the popup (native/src/trap.h).
    text = source("trap.h")
    codes = dict(re.findall(r"constexpr int (kDisruption|kDisorient) = (\d+);", text))
    assert (int(codes["kDisruption"]), int(codes["kDisorient"])) == (
        ids.DISRUPTION_TRAP,
        ids.DISORIENT_TRAP,
    )
    for code in ids.TRAPS:
        assert f'"{ids.TRAP_NAMES[code]}"' in text
    assert '{"trap_status", ' in source("dllmain.cpp")


def test_the_client_reads_the_ending_started_as_the_helper_says_it() -> None:
    said = re.findall(r'const char kEndingStarted\[\] = "([^"]+)";', source("mission.cpp"))
    assert said == [discs.ENDING_STARTED]
    assert f'{{"{discs.MISSION_STATUS}", ' in source("dllmain.cpp")


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
    # docs/re/memory.md, "The file select": 30 blocks of 0x9408 (client/game.py, slot_markers).
    slot = layout("slot")
    assert (slot["kCount"], slot["kStride"]) == (game.SLOTS, game.SLOT_STRIDE) == (30, 0x9408)
    assert (slot["kEmpty"], slot["kRandoMarker"]) == (0x935E, 0x9400)
