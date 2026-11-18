"""Where the client's targets are in Zexion's Linux build: each a symbol of the binary's own
symbol table and an offset from it, as docs/re/linux.md ("Lookup: targets") records them.

The Linux binary is a fixed-address executable that keeps its symbol table, so a target's
address is its symbol's value plus the offset, read from the binary the game runs once its
build is known (CLAUDE.md, "Rules"). ``b1`` is what each resolved to on B1's binary, for
checking only (tests/test_linux_targets.py): code never uses it as an address.

``TARGETS`` are the ones ``game.Game`` reads and writes itself; ``HELPER_TARGETS`` the rest of
those the Linux helper's ``config`` takes (CONFIG_KEYS, helper_config). The Windows helper's
other targets have Linux counterparts too (docs/re/linux.md), for parts the Linux helper does
not run yet.
"""

from __future__ import annotations

from dataclasses import dataclass

from . import elf


@dataclass(frozen=True)
class LinuxTarget:
    name: str  # the target's name in client/patterns.py
    symbol: str
    offset: int
    b1: int  # the address on B1's binary: for checks only
    # docs/re/linux.md's status for it: False while only assumed, and game.Game then writes
    # nothing through it.
    measured: bool = True


# The player state is a fixed object inside gGameState (gMainCharacterSpriteP pointed there in
# the game, at the title screen and in play), the inventory 0x108E into it.
PLAYER_STATE = ("gGameState", 0x1CD50)
PLAYER_POINTER = "gMainCharacterSpriteP"

TARGETS = (
    LinuxTarget("map_grid", "gGameState", 0x14FFD, 0x9FC83D),
    LinuxTarget("cell_state", "gGameState", 0x15B61, 0x9FD3A1),
    LinuxTarget("inventory", "gGameState", 0x1CD50 + 0x108E, 0xA0561E),
    LinuxTarget("pickup_counter", "gGameState", 0xAC8, 0x9E8308),
    LinuxTarget("rando_blob", "gCurrentRando", 0, 0x12F6E00),
    LinuxTarget("slot_array", "gSaveGames", 0, 0x14945C0, measured=False),
    LinuxTarget("menu_state", "gCursorMode", 0, 0x12F9901),
    LinuxTarget("save_loaded", "gGameState", 0xACA, 0x9E830A),
    LinuxTarget("rando_marker", "gGameState", 0x13738, 0x9FAF78),
    LinuxTarget("last_message", "gGameState", 0x13350, 0x9FAB90),
    LinuxTarget("transition_timer", "gExitDelay", 0, 0x12F570C),
    LinuxTarget("flags", "gGameState", 0xED, 0x9E792D),
    LinuxTarget("item_pool", "gGameState", 0x79E990, 0x11861D0),
    LinuxTarget("current_slot", "gCurrentSaveGame", 0, 0x14945A0),
)


# The Linux helper's own targets beyond those, for the parts of the Windows helper's Game it runs
# (its ``config``: native/linux/src/helper.cpp): new games, the ARCHIPELAGO choice, grants and
# their popups. The rest of CONFIG_KEYS is in TARGETS. Functions by their mangled names.
HELPER_TARGETS = (
    LinuxTarget("randomizer_menu", "gRandoPreferences", 0, 0x12F6CE0),
    LinuxTarget("transition_fn", "gExitActionP", 0, 0x12F5710),
    LinuxTarget("newgame_fn", "_Z16StartNewGameExitv", 0, 0x563363),
    LinuxTarget("placement_fn", "_Z13generateRandov", 0, 0x5ABD55),
    LinuxTarget("start_count", "rStartingLocationCount", 0, 0x9E0C40),
    LinuxTarget("start_cells", "rStartingLocations", 0, 0x9E0B40),
    LinuxTarget("start_flags", "rStartingLocationCage", 0, 0x9E0C60),
    # The ARCHIPELAGO choice's (native/src/choice.h).
    LinuxTarget("current_menu", "gLastGameMenuP", 0, 0x12F5700),
    LinuxTarget("rando_menu_fn", "_Z14RandomizerMenuv", 0, 0x6021B1),
    LinuxTarget("widget_handler_fn", "_Z18StandardMenuActionP12GameMenuItemi", 0, 0x58B8CB),
    LinuxTarget("panel_handler_fn", "_Z16FrameWrapOrArrowP12GameMenuItemi", 0, 0x56ED11),
    LinuxTarget("rando_newgame_fn", "_Z9forceSeedv", 0, 0x5AF50F),
    LinuxTarget("tr_flag", "gCurrentTranslation", 0x91, 0x12B48D1),
    # Grants and their popups (native/src/game_calls.h, Game::apply).
    LinuxTarget("item_records", "gPowerUpDefs", 0, 0x16DA0B8),
    LinuxTarget("weapon_strip_gate", "gPreferences", 0x1DD, 0x12F5BDD),
    LinuxTarget("sound_system", "gGameSoundManager", 0, 0x9E6F60),
    LinuxTarget("message_fn", "_Z17createGameMessagePKciiiiii", 0, 0x631C5B),
    LinuxTarget("sound_fn", "_Z13PlayGameSoundP16GameSoundManagersffffss", 0, 0x4D0398),
    LinuxTarget("hud_number_fn", "_Z19updateNumberDisplayP13NumberDisplaydb", 0, 0x4A2E9F),
    LinuxTarget("hud_layout_fn", "_Z29updateNumberDisplayVisibilityP12PlayerStruct", 0, 0x51773F),
    LinuxTarget("energy_hud_fn", "_Z15updateEnergyBarP12PlayerStructb", 0, 0x518CB2),
    LinuxTarget("weapon_strip_fn", "_Z19updateWeaponDisplayP12PlayerStruct", 0, 0x4F0231),
    LinuxTarget("suit_color_fn", "_Z16setSpritePalettehs", 0, 0x4953EE),
    LinuxTarget("hd_messages", "gTranslationGameMessages", 0, 0x12F49A0),
    LinuxTarget("message_slots", "gGameState", 0x8C6B48, 0x12AE388),
)
CONFIG_KEYS = (
    "rando_blob", "randomizer_menu", "transition_fn", "transition_timer", "newgame_fn",
    "placement_fn", "rando_marker", "current_slot", "start_count", "start_cells", "start_flags",
    "current_menu", "rando_menu_fn", "widget_handler_fn", "panel_handler_fn", "rando_newgame_fn",
    "tr_flag", "inventory", "save_loaded", "menu_state", "item_records", "last_message",
    "weapon_strip_gate", "sound_system", "message_fn", "sound_fn", "hud_number_fn",
    "hud_layout_fn", "energy_hud_fn", "weapon_strip_fn", "suit_color_fn", "hd_messages",
    "message_slots",
)  # fmt: skip


class TargetError(ValueError):
    """A target's symbol is missing from the binary, or its offset runs past the symbol."""


def resolve(image: elf.Image, targets: tuple[LinuxTarget, ...] = TARGETS) -> dict[str, int]:
    """Each target's address in ``image``, a known Linux build's binary. A symbol the binary
    lacks, or a target outside its symbol's size, is a TargetError."""
    out = {}
    for target in targets:
        try:
            symbol = image.symbol(target.symbol)
        except elf.ELFError as exc:
            raise TargetError(f"{target.name}: {exc}") from exc
        if symbol.size and target.offset >= symbol.size:
            raise TargetError(
                f"{target.name}: offset {target.offset:#x} is past {target.symbol}'s "
                f"{symbol.size:#x} bytes"
            )
        out[target.name] = symbol.value + target.offset
    return out


def player_state(image: elf.Image) -> tuple[int, int]:
    """Where the player state's pointer is, and the address it should hold."""
    holder, offset = PLAYER_STATE
    return image.symbol(PLAYER_POINTER).value, image.symbol(holder).value + offset


def helper_config(image: elf.Image) -> str:
    """The Linux helper's ``config`` request: CONFIG_KEYS' addresses in ``image``."""
    found = resolve(image, TARGETS + HELPER_TARGETS)
    return "config " + " ".join(f"{key}={found[key]:#x}" for key in CONFIG_KEYS)
