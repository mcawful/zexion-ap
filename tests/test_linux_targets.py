"""client/linux_targets.py: the client's targets on the Linux build, resolved from a binary's
symbol table, and the same addresses in docs/re/linux.md's Lookup."""

from __future__ import annotations

import re
from pathlib import Path

import pytest
from fakes import b1_like  # tests/test_cross_checks.py and test_game_linux.py take it from here
from native_source import linux_client_keys, linux_config_table

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


def test_the_config_names_every_target_once_in_order() -> None:
    config = linux_targets.config_request(b1_like())
    words = config.split()
    assert words[0] == "config"
    assert [w.split("=")[0] for w in words[1:]] == [t.name for t in linux_targets.TARGETS]
    assert "rando_blob=0x12f6e00" in words and "current_slot=0x14945a0" in words


def test_the_helpers_client_keys_are_the_clients_targets() -> None:
    """native/linux/src/helper.cpp's kClientKeys are exactly the client's targets, each in the
    helper's own table (kConfigKeys, or kClientOnly) at the same symbol and offset as here: what
    a config's agreement check compares."""
    keys, only = linux_client_keys()
    ours = {t.name: (t.symbol, t.offset) for t in linux_targets.TARGETS}
    assert keys == list(ours), "the keys, in the order the client sends them"
    rows = {name: (symbol, offset) for name, _, symbol, offset in linux_config_table()}
    rows.update({name: (symbol, offset) for name, symbol, offset in only})
    for name in keys:
        assert rows[name] == ours[name], name
    assert {name for name, _, _ in only} == set(keys) - {n for n, *_ in linux_config_table()}


def test_every_helper_key_is_a_measured_row_of_the_lookup() -> None:
    """The helper's own addresses (kConfigKeys) and the client-only ones are all in
    docs/re/linux.md's Lookup, and each is measured."""
    doc = (ROOT / "docs" / "re" / "linux.md").read_text("utf-8")
    start = doc.index("## Lookup: targets")
    lookup = doc[start : doc.index("\n## ", start + 1)]
    _, only = linux_client_keys()
    names = [name for name, *_ in linux_config_table()] + [name for name, *_ in only]
    for name in names:
        row = next(r for r in lookup.splitlines() if r.startswith(f"| `{name}` |"))
        assert row.rstrip(" |").rsplit(" | ", 1)[1].startswith("measured"), name
