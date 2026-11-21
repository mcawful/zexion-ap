"""Where the client's targets are in Zexion's Linux build: each a symbol of the binary's own
symbol table and an offset from it, as docs/re/linux.md ("Lookup: targets") records them.

The Linux binary is a fixed-address executable that keeps its symbol table, so a target's
address is its symbol's value plus the offset, read from the binary the game runs once its
build is known (CLAUDE.md, "Rules"). ``b1`` is what each resolved to on B1's binary, for
checking only (tests/test_linux_targets.py): code never uses it as an address.

``TARGETS`` are the ones ``game.Game`` reads and writes itself, and the ones the Linux helper's
``config`` carries (config_request): the helper's own parts run on addresses it reads from its own
symbols (native/linux/src/helper.cpp, ``kConfigKeys``: docs/re/linux.md, "Lookup: targets"), and a
``config`` only checks that the client's agree.
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


def config_request(image: elf.Image) -> str:
    """The Linux helper's ``config`` request: ``TARGETS``' addresses in ``image``. The helper
    activates its own addresses (native/linux/src/helper.cpp, ``check_client_config``) and refuses
    unless these are exactly the ones its own symbols give, for every target and no other."""
    found = resolve(image)
    return "config " + " ".join(f"{target.name}={found[target.name]:#x}" for target in TARGETS)
