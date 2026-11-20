"""Where the client's targets are in Zexion's Linux build: each a symbol of the binary's own
symbol table and an offset from it, as docs/re/linux.md ("Lookup: targets") records them.

The Linux binary is a fixed-address executable that keeps its symbol table, so a target's
address is its symbol's value plus the offset, read from the binary the game runs once its
build is known (CLAUDE.md, "Rules"). ``b1`` is what each resolved to on B1's binary, for
checking only (tests/test_linux_targets.py): code never uses it as an address.

``TARGETS`` are the ones ``game.Game`` reads and writes itself; ``HELPER_TARGETS`` the rest of
those the Linux helper's ``config`` takes (CONFIG_KEYS, helper_config). The Windows helper's
other targets that have Linux counterparts (docs/re/linux.md) but no key here are those the Linux
helper reads from its own symbols, or needs none of on Linux.
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
    LinuxTarget("slot_array", "gSaveGames", 0, 0x14945C0),
    LinuxTarget("menu_state", "gCursorMode", 0, 0x12F9901),
    LinuxTarget("save_loaded", "gGameState", 0xACA, 0x9E830A),
    LinuxTarget("rando_marker", "gGameState", 0x13738, 0x9FAF78),
    LinuxTarget("last_message", "gGameState", 0x13350, 0x9FAB90),
    LinuxTarget("transition_timer", "gExitDelay", 0, 0x12F570C),
    LinuxTarget("flags", "gGameState", 0xED, 0x9E792D),
    LinuxTarget("item_pool", "gGameState", 0x79E990, 0x11861D0),
    LinuxTarget("current_slot", "gCurrentSaveGame", 0, 0x14945A0),
)


# The Linux helper's own targets beyond those, for the parts of the Windows helper's Game it
# runs (its ``config``: native/linux/src/helper.cpp): new games, the ARCHIPELAGO choice, grants
# and their popups, other players' items (the map's too), traps, the translations, the OS Install
# Disks' count, RETURN HOME, DeathLink's deaths, the ASSIST lock, the file select's logo, counts
# and ITEMS row (the major-item list), the title's lines, the Feats guard and the text window's
# top cut. The rest of
# CONFIG_KEYS is in TARGETS. Functions by their mangled names.
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
    # The translations (native/src/translation.h): the object, its tables, the counts, the
    # message's and the label's builders.
    LinuxTarget("translation", "gCurrentTranslation", 0, 0x12B4840),
    LinuxTarget("small_text", "gCurrentTranslation", 0x30, 0x12B4870),
    LinuxTarget("small_offsets", "gCurrentTranslation", 0x58, 0x12B4898),
    LinuxTarget("item_names", "gCurrentTranslation", 0x48, 0x12B4888),
    LinuxTarget("item_name_offsets", "gCurrentTranslation", 0x78, 0x12B48B8),
    LinuxTarget("small_count", "gSmallTextCount", 0, 0x16DA0D8),
    LinuxTarget("item_name_count", "gPowerUpDefCount", 0, 0x16DA0AA),
    LinuxTarget(
        "hd_text_fn", "_Z28createTranslationGameMessagePKwiiiiiissP9SpriteRec", 0, 0x637EFD
    ),
    LinuxTarget(
        "hd_build_fn",
        "_Z12generateTextffNSt7__cxx1112basic_stringIwSt11char_traitsIwESaIwEEE"
        "P8GameFontP12SpriteStringb",
        0,
        0x54DA05,
    ),
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
    # Other players' items (native/src/game_calls.h, Game::watch_ap_items). The popup's width is
    # an immediate inside the pickup's code.
    LinuxTarget("sprite_texture", "gSpriteWorldP", 0, 0x9E6F50),
    LinuxTarget(
        "popup_width_code", "_Z17playerCollideProcP10CollideRecS0_P6dPointc", 0x251E, 0x642D8B
    ),
    LinuxTarget("palette_table", "gSpriteDrawProcs", 0, 0x12F7E40),
    LinuxTarget("frame_clock", "gGameState", 0xB0, 0x9E78F0),
    LinuxTarget("sprite_templates", "gMasterSpritesP", 0, 0x14937E8),
    LinuxTarget("object_pool", "gGameState", 0x388DA0, 0xD705E0),
    LinuxTarget("event_table", "gAreaEvents", 0, 0x16DA010),
    LinuxTarget("event_count", "gAreaEventCount", 0, 0x16DA008),
    LinuxTarget("event_handlers", "gAreaEventProcs", 0, 0x12F87A0),
    # Traps (native/src/trap.h): AILMENTS, its two tests the trap's patches skip, the halt byte.
    LinuxTarget("ailments", "gGameState", 0x13724, 0x9FAF64),
    LinuxTarget("ailments_hit_code", "_Z10weaponCoolP12PlayerStruct", 0xAD5, 0x507FCB),
    LinuxTarget("ailments_wave_code", "_Z10weaponCoolP12PlayerStruct", 0xB65, 0x50805B),
    LinuxTarget("halt_flag", "gEditorMode", 0, 0x12B456E),
    # The map (native/src/mapmarks.h): the item update's tests of the ID the seen-marker patch
    # spans, the map's move procedure's slot, the function that sets a text's glyphs.
    LinuxTarget("seen_marker_code", "_Z15PowerUpMoveProcP9SpriteRec", 0x788, 0x53DB6C),
    LinuxTarget("map_handler", "gMoveProcs", 0x3C0, 0x12F7D60),
    LinuxTarget("text_set_fn", "_Z17alterSpriteStringP9SpriteRecPKcii", 0, 0x566E1F),
    # The OS Install Disks' count (native/src/equip.h, pause.h, scan.h): the Text table, the
    # equipment screen's builder and the functions its disk widget is made with, the pause
    # screen's KEYS branch and its way back from OPTION MENU.
    LinuxTarget("text_table", "gSmallTexts", 0, 0x16DA0E0),
    LinuxTarget("equipment_fn", "_Z13EquipmentMenuv", 0, 0x5E32FB),
    LinuxTarget("equip_panel_fn", "_Z14EquipmentFrameP12GameMenuItemi", 0, 0x572842),
    LinuxTarget("widget_move_fn", "_Z16moveGameMenuItemP9SpriteReciib", 0, 0x56BF2E),
    LinuxTarget("text_place_fn", "_Z16moveSpriteStringP9SpriteRecii", 0, 0x568526),
    LinuxTarget(
        "widget_new_fn",
        "_Z18createGameMenuItemPKciP8GameMenuPFvP12GameMenuItemiEPFvvEPvS9_iP5sRect",
        0,
        0x56BD34,
    ),
    LinuxTarget("sprite_sheet_fn", "_Z20SWChangeMasterSpriteP9SpriteRecS0_", 0, 0x4CD979),
    LinuxTarget("panel_link_fn", "_Z11addGameMenuP8GameMenu", 0, 0x56CC09),
    LinuxTarget("keys_branch", "_Z9PauseGamev", 0x9C, 0x618A7E),
    LinuxTarget("options_back_fn", "_Z22refreshPauseFromOptionP14GameMenuScreen", 0, 0x618777),
    # RETURN HOME WITH THE DISKS? (native/src/mission.h): the game's fade and its routines.
    LinuxTarget("fade", "gGameState", 0x13448, 0x9FAC88),
    LinuxTarget("fade_level", "gGameState", 0x138A3, 0x9FB0E3),
    LinuxTarget("routine_table", "gExitActions", 0, 0x12F5720),
    # DeathLink's received deaths (Game::kill): the player's death.
    LinuxTarget("death_fn", "_Z17PlayerDestructionP9SpriteRecc", 0, 0x51C15C),
    # The ASSIST lock (native/src/menu.h): ASSIST's own callback.
    LinuxTarget("assist_menu_fn", "_Z10AssistMenuv", 0, 0x6081F1),
    # The title screen's lines (native/src/title.h): the title's layer and the text functions.
    LinuxTarget("draw_layer", "gStatsSpriteLayerP", 0, 0x12B46C0),
    LinuxTarget("text_new_fn", "_Z15newSpriteStringPKcii", 0, 0x5588CD),
    LinuxTarget("text_link_fn", "_Z15addSpriteStringP9SpriteRec", 0, 0x569651),
    # The Feats guard's load test (native/src/feats.h): the slots' randomizer sections.
    LinuxTarget("rando_slots", "gSavedRandos", 0, 0x16BF7C0),
    # And its ending's patch: the first achievement call in EndingCutscene, and its join.
    LinuxTarget("ending_unlocks", "_Z14EndingCutscenev", 0x3B, 0x55AB4C),
    LinuxTarget("ending_join", "_Z14EndingCutscenev", 0x39C, 0x55AEAD),
    # And the Feats of Natu screen's note in a translation: LargeText's count.
    LinuxTarget("large_count", "gLargeTextCount", 0, 0x16DA0DA),
    # And the file select's AP logo and disk counts: the save panel's and the entries' handlers.
    LinuxTarget("file_panel_fn", "_Z20SaveGamePlayerCursorP12GameMenuItemi", 0, 0x576BB1),
    LinuxTarget("file_entry_fn", "_Z18SaveGameMenuActionP12GameMenuItemi", 0, 0x59B85E),
    LinuxTarget("copy_entry_fn", "_Z18CopyGameMenuActionP12GameMenuItemi", 0, 0x5B1B09),
    LinuxTarget("delete_entry_fn", "_Z19ClearGameMenuActionP12GameMenuItemi", 0, 0x5B0A65),
    # And the major-item list, the file select's ITEMS row, with its length.
    LinuxTarget("pickup_list", "gGameState", 0xAE0, 0x9E8320),
    # And the text window's top cut, patched so tall popups' lines are cut right.
    LinuxTarget("text_cut_code", "_Z28setGameMessageStringClippingP9SpriteRecii", 0x661, 0x569617),
)
CONFIG_KEYS = (
    "rando_blob", "randomizer_menu", "transition_fn", "transition_timer", "newgame_fn",
    "placement_fn", "rando_marker", "current_slot", "start_count", "start_cells", "start_flags",
    "current_menu", "rando_menu_fn", "widget_handler_fn", "panel_handler_fn", "rando_newgame_fn",
    "tr_flag", "translation", "small_text", "small_offsets", "item_names", "item_name_offsets",
    "small_count", "item_name_count", "hd_text_fn", "hd_build_fn", "inventory", "save_loaded",
    "menu_state", "item_records", "last_message",
    "weapon_strip_gate", "sound_system", "message_fn", "sound_fn", "hud_number_fn",
    "hud_layout_fn", "energy_hud_fn", "weapon_strip_fn", "suit_color_fn", "hd_messages",
    "message_slots", "sprite_texture", "popup_width_code", "item_pool", "palette_table",
    "frame_clock", "sprite_templates", "object_pool", "flags", "map_grid", "event_table",
    "event_count", "event_handlers", "ailments", "ailments_hit_code", "ailments_wave_code",
    "halt_flag", "seen_marker_code", "map_handler", "text_set_fn", "text_table", "equipment_fn",
    "equip_panel_fn", "widget_move_fn", "text_place_fn", "widget_new_fn", "sprite_sheet_fn",
    "panel_link_fn", "keys_branch", "options_back_fn", "fade", "fade_level", "routine_table",
    "death_fn", "assist_menu_fn", "draw_layer", "text_new_fn", "text_link_fn", "slot_array",
    "rando_slots", "ending_unlocks", "ending_join", "large_count", "file_panel_fn",
    "file_entry_fn", "copy_entry_fn", "delete_entry_fn", "pickup_list", "text_cut_code",
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
