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


# The helper's new games' symbols too (linux_targets.HELPER_TARGETS): (name, address, size, type).
B1_HELPER_SYMBOLS = [
    ("gRandoPreferences", 0x12F6CE0, 0x114, 1),
    ("gExitActionP", 0x12F5710, 8, 1),
    ("_Z16StartNewGameExitv", 0x563363, 0x200, 2),
    ("_Z13generateRandov", 0x5ABD55, 0x200, 2),
    ("rStartingLocationCount", 0x9E0C40, 2, 1),
    ("rStartingLocations", 0x9E0B40, 0x100, 1),
    ("rStartingLocationCage", 0x9E0C60, 0x40, 1),
    ("gLastGameMenuP", 0x12F5700, 8, 1),
    ("_Z14RandomizerMenuv", 0x6021B1, 0x100, 2),
    ("_Z18StandardMenuActionP12GameMenuItemi", 0x58B8CB, 0x100, 2),
    ("_Z16FrameWrapOrArrowP12GameMenuItemi", 0x56ED11, 0x100, 2),
    ("_Z9forceSeedv", 0x5AF50F, 0x100, 2),
    ("gCurrentTranslation", 0x12B4840, 0x40150, 1),
    ("gPowerUpDefs", 0x16DA0B8, 8, 1),
    ("gPreferences", 0x12F5A00, 0xE70, 1),
    ("gGameSoundManager", 0x9E6F60, 0x8C8, 1),
    ("gTranslationGameMessages", 0x12F49A0, 0x8E0, 1),
    ("_Z17createGameMessagePKciiiiii", 0x631C5B, 0x100, 2),
    ("_Z13PlayGameSoundP16GameSoundManagersffffss", 0x4D0398, 0x100, 2),
    ("_Z19updateNumberDisplayP13NumberDisplaydb", 0x4A2E9F, 0x100, 2),
    ("_Z29updateNumberDisplayVisibilityP12PlayerStruct", 0x51773F, 0x100, 2),
    ("_Z15updateEnergyBarP12PlayerStructb", 0x518CB2, 0x100, 2),
    ("_Z19updateWeaponDisplayP12PlayerStruct", 0x4F0231, 0x100, 2),
    ("_Z16setSpritePalettehs", 0x4953EE, 0x100, 2),
    ("gSpriteWorldP", 0x9E6F50, 8, 1),
    ("_Z17playerCollideProcP10CollideRecS0_P6dPointc", 0x64086D, 0x401B, 2),
    ("gSpriteDrawProcs", 0x12F7E40, 0x208, 1),
    ("gMasterSpritesP", 0x14937E8, 8, 1),
    ("gAreaEvents", 0x16DA010, 8, 1),
    ("gAreaEventCount", 0x16DA008, 2, 1),
    ("gAreaEventProcs", 0x12F87A0, 0x180, 1),
    ("gEditorMode", 0x12B456E, 1, 1),
    ("_Z10weaponCoolP12PlayerStruct", 0x5074F6, 0x1293, 2),
    ("gMoveProcs", 0x12F79A0, 0x4A0, 1),
    ("_Z15PowerUpMoveProcP9SpriteRec", 0x53D3E4, 0xDFA, 2),
    ("_Z17alterSpriteStringP9SpriteRecPKcii", 0x566E1F, 0xDC, 2),
    ("gSmallTextCount", 0x16DA0D8, 2, 1),
    ("gPowerUpDefCount", 0x16DA0AA, 2, 1),
    ("_Z28createTranslationGameMessagePKwiiiiiissP9SpriteRec", 0x637EFD, 0x62E, 2),
    (
        "_Z12generateTextffNSt7__cxx1112basic_stringIwSt11char_traitsIwESaIwEEE"
        "P8GameFontP12SpriteStringb",
        0x54DA05,
        0x33C5,
        2,
    ),
    ("gSmallTexts", 0x16DA0E0, 8, 1),
    ("_Z13EquipmentMenuv", 0x5E32FB, 0x40C3, 2),
    ("_Z14EquipmentFrameP12GameMenuItemi", 0x572842, 0x2D66, 2),
    ("_Z16moveGameMenuItemP9SpriteReciib", 0x56BF2E, 0x169, 2),
    ("_Z16moveSpriteStringP9SpriteRecii", 0x568526, 0x15E, 2),
    (
        "_Z18createGameMenuItemPKciP8GameMenuPFvP12GameMenuItemiEPFvvEPvS9_iP5sRect",
        0x56BD34,
        0x1FA,
        2,
    ),
    ("_Z20SWChangeMasterSpriteP9SpriteRecS0_", 0x4CD979, 0x27D, 2),
    ("_Z11addGameMenuP8GameMenu", 0x56CC09, 0x7E, 2),
    ("_Z9PauseGamev", 0x6189E2, 0xC2B, 2),
    ("_Z22refreshPauseFromOptionP14GameMenuScreen", 0x618777, 0x26B, 2),
]


def b1_like(drop: str = "", sizes: dict[str, int] | None = None, extra: bool = False) -> elf.Image:
    """A binary with B1's symbols the targets name (and with ``extra`` the helper's too)."""
    sizes = sizes or {}
    symbols = [
        (name, value, sizes.get(name, size), 1, True)
        for name, value, size in B1_SYMBOLS
        if name != drop
    ]
    if extra:
        symbols += [(n, v, s, t, True) for n, v, s, t in B1_HELPER_SYMBOLS if n != drop]
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
