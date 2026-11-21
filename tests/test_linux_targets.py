"""client/linux_targets.py: the client's targets on the Linux build, resolved from a binary's
symbol table, and the same addresses in docs/re/linux.md's Lookup."""

from __future__ import annotations

import re
from pathlib import Path

import pytest
from fakes import b1_like  # tests/test_cross_checks.py and test_game_linux.py take it from here

from client import elf, linux_targets

ROOT = Path(__file__).resolve().parent.parent
B1_BINARY = ROOT / "lab" / "linux-b1" / "Zexion"  # a local copy, never committed


def test_each_target_resolves_to_its_b1_address() -> None:
    resolved = linux_targets.resolve(b1_like())
    assert resolved == {t.name: t.b1 for t in linux_targets.TARGETS}


def test_the_player_state_pointer_and_its_place() -> None:
    assert linux_targets.player_state(b1_like()) == (0x12B4578, 0xA04590)


def test_a_missing_symbol_is_refused() -> None:
    with pytest.raises(linux_targets.TargetError, match="current_slot: no symbol gCurrentSaveGame"):
        linux_targets.resolve(b1_like(drop="gCurrentSaveGame"))


def test_an_offset_past_its_symbol_is_refused() -> None:
    with pytest.raises(linux_targets.TargetError, match="item_pool: offset 0x79e990 is past"):
        linux_targets.resolve(b1_like(sizes={"gGameState": 0x100000}))


def test_every_target_is_one_of_the_clients_and_in_the_lookup() -> None:
    from client import patterns

    names = {t.name for t in patterns.TARGETS}
    doc = (ROOT / "docs" / "re" / "linux.md").read_text("utf-8")
    start = doc.index("## Lookup: targets")
    lookup = doc[start : doc.index("\n## ", start + 1)]
    for target in linux_targets.TARGETS:
        assert target.name in names
        rows = [r for r in lookup.splitlines() if r.startswith(f"| `{target.name}` |")]
        assert len(rows) == 1, target.name
        linux_column = rows[0].split(" | ")[2]
        assert f"0x{target.b1:X}" in re.sub(r"`", "", linux_column), target.name


@pytest.mark.skipif(not B1_BINARY.is_file(), reason="no copy of B1's Linux binary in lab/")
def test_the_real_b1_binary_resolves_as_recorded() -> None:
    image = elf.parse(B1_BINARY.read_bytes())
    assert linux_targets.resolve(image) == {t.name: t.b1 for t in linux_targets.TARGETS}
    assert linux_targets.player_state(image) == (0x12B4578, 0xA04590)


def test_a_target_is_writable_only_where_the_lookup_says_measured() -> None:
    doc = (ROOT / "docs" / "re" / "linux.md").read_text("utf-8")
    start = doc.index("## Lookup: targets")
    lookup = doc[start : doc.index("\n## ", start + 1)]
    for target in linux_targets.TARGETS:
        row = next(r for r in lookup.splitlines() if r.startswith(f"| `{target.name}` |"))
        status = row.rstrip(" |").rsplit(" | ", 1)[1]
        # Measured, and nothing of it assumed: only then is it written through.
        measured = status.startswith("measured") and "assumed" not in status
        assert target.measured == measured, (target.name, status)


def test_the_helpers_targets_resolve_to_their_b1_addresses() -> None:
    image = b1_like(extra=True)
    resolved = linux_targets.resolve(image, linux_targets.HELPER_TARGETS)
    assert resolved == {t.name: t.b1 for t in linux_targets.HELPER_TARGETS}


def test_the_helpers_config_names_every_key_once() -> None:
    config = linux_targets.helper_config(b1_like(extra=True))
    words = config.split()
    assert words[0] == "config"
    assert [w.split("=")[0] for w in words[1:]] == list(linux_targets.CONFIG_KEYS)
    assert "placement_fn=0x5abd55" in words and "newgame_fn=0x563363" in words


def test_the_helpers_config_keys_are_the_clients() -> None:
    """native/linux/src/helper.cpp's kConfigKeys (the game's build, not the test build's): the
    same keys in the same order, each at the same symbol and offset as here."""
    helper = (ROOT / "native" / "linux" / "src" / "helper.cpp").read_text("utf-8")
    table = helper.split("#ifndef ZAP_HOOKTEST\nconstexpr ConfigKey kConfigKeys[] = {", 1)[1]
    table = table.split("};", 1)[0]
    rows = re.findall(
        r'\{"(\w+)", &G::\w+, [^,]+,\s*"(\w+)",\s*(0x[0-9A-Fa-f]+|\d+)(?:,\s*\w+)?\}', table
    )
    assert [name for name, _, _ in rows] == list(linux_targets.CONFIG_KEYS)
    ours = {
        t.name: (t.symbol, t.offset) for t in linux_targets.TARGETS + linux_targets.HELPER_TARGETS
    }
    for name, symbol, offset in rows:
        assert ours[name] == (symbol, int(offset, 0)), name


def test_every_helper_target_is_in_the_lookup() -> None:
    doc = (ROOT / "docs" / "re" / "linux.md").read_text("utf-8")
    start = doc.index("## Lookup: targets")
    lookup = doc[start : doc.index("\n## ", start + 1)]
    for target in linux_targets.HELPER_TARGETS:
        row = next(r for r in lookup.splitlines() if r.startswith(f"| `{target.name}` |"))
        assert f"0x{target.b1:X}" in row.replace("`", ""), target.name
        assert row.rstrip(" |").rsplit(" | ", 1)[1].startswith("measured"), target.name
