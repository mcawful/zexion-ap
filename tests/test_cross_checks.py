"""What the client and the native helper must agree on, read from the helper's sources.

Each value has one home on either side and a copy on the other: the helper's version (the
client refuses another), the game builds both identify, the addresses the client sends in
``config``, and the proxy's off switch. These read the C++ as text, so they run without a
native build. Also the world's ``world_version``, the same release number.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

import pytest

from client import native, offline, patterns

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


def config_keys() -> dict[str, str]:
    """The helper's kConfigKeys: each key's kind (code, data, import_slot)."""
    table = braced(source("game_calls.cpp"), "constexpr ConfigKey kConfigKeys[] = {")
    table = re.sub(r"//[^\n]*", "", table)
    entries = re.findall(r'\b(code|data|import_slot)\(\s*"(\w+)"', table)
    # Every quoted name in the table is some entry's first argument: none was missed.
    first_args = re.findall(r'\(\s*"(\w+)"', table)
    assert [name for _, name in entries] == first_args
    out = {}
    for kind, name in entries:
        assert name not in out, f"{name} is listed twice"
        out[name] = kind
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
    table = braced(source("game_calls.cpp"), "constexpr ConfigKey kConfigKeys[] = {")
    table = re.sub(r"//[^\n]*", "", table)
    return set(re.findall(r'\bclient_only\(\s*(?:code|data|import_slot)\(\s*"(\w+)"', table))


# Where the client's parts read the client-only members, by file: the functions (as the helper's
# sources define them), or a class's methods ("Name::"), or None for the whole file.
# - game_calls.cpp: the grants (apply, refresh_hud), DeathLink (kill), the Energy Shield
#   (run_item_event) and the new game (watch_newgame, start_archipelago, ensure_start_table).
# - game_calls.h: the game-mode panel's handler, for the wrapper ARCHIPELAGO's choice installs.
# - menu.cpp: ARCHIPELAGO's choice, installed only by Game::watch_menu.
# - mission.cpp: RETURN HOME, which reads them only once a prompt is asked for
#   (Game::request_mission); with none asked, its cancel and restore read nothing of them.
CLIENT_READERS: dict[str, set[str] | None] = {
    "game_calls.cpp": {
        "Game::apply",
        "Game::refresh_hud",
        "Game::kill",
        "Game::run_item_event",
        "Game::watch_newgame",
        "Game::start_archipelago",
        "Game::ensure_start_table",
    },
    "game_calls.h": {"panel_handler"},
    "menu.cpp": {"ArchipelagoChoice::"},
    "mission.cpp": None,
}
# The client's parts' ways in from the frame call and the requests: each refuses or returns
# until every key is set (Game::client_ready). Above, the rest are called only from these.
CLIENT_GATES = {
    "game_calls.cpp": {
        "Game::apply",
        "Game::kill",
        "Game::spawn_shield",
        "Game::watch_newgame",
        "Game::watch_menu",
        "Game::start_archipelago",
        "Game::watch_mission",
        "Game::mission_open",
        "Game::request_mission",
    },
}

_DEFINITION = re.compile(
    r"^(?!//|#|namespace|using|struct|class|enum|template|\}|\s)[^;]*?((?:\w+::)*~?\w+)\("
)
_INLINE = re.compile(r"\b(\w+)\([^()]*\)\s*(?:const\s*)?\{")


def functions(name: str) -> list[tuple[str | None, int, str]]:
    """Each line of a helper source with the function it lies in: a .cpp's column-0 definitions,
    or the inline one a header line itself starts."""
    out: list[tuple[str | None, int, str]] = []
    current = None
    for number, line in enumerate(source(name).splitlines(), 1):
        if name.endswith(".cpp"):
            m = _DEFINITION.match(line)
            if m:
                current = m.group(1)
        else:
            m = _INLINE.search(line)
            current = m.group(1) if m else None
        out.append((current, number, line))
    return out


def allowed(rule: set[str] | None, function: str | None) -> bool:
    if rule is None:
        return True
    return function is not None and any(
        function == r or (r.endswith("::") and function.startswith(r)) for r in rule
    )


def test_the_client_only_keys_are_read_only_by_the_clients_parts() -> None:
    # A key the offline cache may lack is left null there: only parts that run once every key is
    # set may read it, so none of the parts the offline look also uses (the title, the file
    # select, a dressed save, the map, feats) can (native/tests/offline_tests.cpp drives them).
    client_only = client_only_keys()
    assert client_only, "no client_only keys parsed from game_calls.cpp"
    assert client_only <= set(config_keys())
    member = re.compile(r"(?:\.|->)(" + "|".join(sorted(client_only)) + r")\b")
    names = sorted(p.name for p in SRC.iterdir() if p.suffix in (".cpp", ".h"))
    for name in names:
        if name == "addresses.h":
            continue
        for function, number, line in functions(name):
            if member.search(line.split("//", 1)[0]):
                rule = CLIENT_READERS.get(name, set())
                assert allowed(rule, function), f"{name}:{number} ({function}) reads {line.strip()}"


def test_the_clients_parts_wait_for_every_key() -> None:
    bodies: dict[tuple[str, str], str] = {}
    for name in ("game_calls.cpp", "game_calls.h"):
        for function, _, line in functions(name):
            if function is not None:
                key = (name, function)
                bodies[key] = bodies.get(key, "") + line + "\n"
    for name, gates in CLIENT_GATES.items():
        for gate in gates:
            assert "client_ready()" in bodies.get((name, gate), ""), f"{gate} is not gated"
    # The other readers in game_calls.cpp are called only from the readers and the gates.
    readers = CLIENT_READERS["game_calls.cpp"] or set()
    inner = {f.split("::")[-1] for f in readers - CLIENT_GATES["game_calls.cpp"]}
    callers = readers | CLIENT_GATES["game_calls.cpp"]
    call = re.compile(r"\b(" + "|".join(sorted(inner)) + r")\(")
    for function, number, line in functions("game_calls.cpp"):
        m = call.search(line.split("//", 1)[0])
        if m and function != f"Game::{m.group(1)}":
            assert function in callers, f"game_calls.cpp:{number} ({function}) calls {m.group(1)}"
