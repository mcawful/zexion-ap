"""client/linux_targets.py: the client's targets on the Linux build, resolved from a binary's
symbol table, and the same addresses in docs/re/linux.md's Lookup."""

from __future__ import annotations

import re
from pathlib import Path

import pytest
from fakes import elf_image

from client import elf, linux_targets

ROOT = Path(__file__).resolve().parent.parent
B1_BINARY = ROOT / "lab" / "linux-b1" / "Zexion"  # a local copy, never committed

# B1's symbols the targets name: (name, address, size).
B1_SYMBOLS = [
    ("gGameState", 0x9E7840, 0x8CCCF8),
    ("gCurrentRando", 0x12F6E00, 0x908),
    ("gSaveGames", 0x14945C0, 30 * 0x9408),
    ("gCursorMode", 0x12F9901, 1),
    ("gExitDelay", 0x12F570C, 4),
    ("gCurrentSaveGame", 0x14945A0, 4),
    ("gMainCharacterSpriteP", 0x12B4578, 8),
]


def b1_like(drop: str = "", sizes: dict[str, int] | None = None) -> elf.Image:
    sizes = sizes or {}
    symbols = [
        (name, value, sizes.get(name, size), 1, True)
        for name, value, size in B1_SYMBOLS
        if name != drop
    ]
    return elf.parse(elf_image(symbols=symbols))


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
