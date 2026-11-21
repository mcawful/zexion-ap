"""Stand-ins the tests share: a fake process and game.Game on it, a binary of B1's symbols, a
``/proc``, helpers that answer by script and the client's context on Archipelago's stand-ins, and a
seed's plan with a fresh randomizer save and the loaded section of a save of it (no game, no exe,
no real save)."""

from __future__ import annotations

import logging
import os
import struct
import sys
import types
from pathlib import Path
from unittest import mock

from client import elf, game, ids, session, state
from client import save_patch as sp

# --- a fake process and the client's Game on it ---------------------------------------------


class FakeProcess:
    """Memory to read and write as the client reads and writes the game's."""

    def __init__(self, size: int = 0x1000) -> None:
        self.memory = bytearray(size)
        self.writes = 0
        self.fail_write = False
        self.overwrite_with: int | None = None  # the game changes the byte straight after

    def read_bytes(self, address: int, size: int) -> bytes:
        return bytes(self.memory[address : address + size])

    def write_bytes(self, address: int, data: bytes) -> None:
        if self.fail_write:
            raise OSError("access denied")
        self.writes += 1
        self.memory[address : address + len(data)] = data
        if self.overwrite_with is not None:
            self.memory[address] = self.overwrite_with


class FakeGame(game.Game):
    """game.Game on a FakeProcess, alive until the test says otherwise."""

    alive = True

    def _alive(self) -> bool:
        return self.alive


# --- a PE header -----------------------------------------------------------------------------

PE_SECTIONS = [(b".text", 0x1000, 0x2000, 0x400, 0x2000), (b".rdata", 0x3000, 0x80, 0x2400, 0x200)]


def pe_image(
    sections: list[tuple[bytes, int, int, int, int]] = PE_SECTIONS,
    length: int = 0x1000,
    stamp: int = 0x6A3F82DE,
    size_of_image: int = 0x1204000,
) -> bytes:
    """A PE header at 0x80 with the given (name, va, vsize, raw, rsize) sections."""
    data = bytearray(length)
    data[0x3C:0x40] = struct.pack("<I", 0x80)
    data[0x80:0x84] = b"PE\0\0"
    data[0x84:0x8C] = struct.pack("<HHI", 0x8664, len(sections), stamp)
    data[0x94:0x96] = struct.pack("<H", 0xF0)  # optional header size
    data[0x80 + 24 + 56 : 0x80 + 24 + 60] = struct.pack("<I", size_of_image)
    data[0x80 + 24 + 70 : 0x80 + 24 + 72] = struct.pack("<H", 0x0160)
    first = 0x80 + 24 + 0xF0
    for i, (name, va, vsize, raw, rsize) in enumerate(sections):
        at = first + 40 * i
        data[at : at + 8] = name.ljust(8, b"\0")
        data[at + 8 : at + 24] = struct.pack("<IIII", vsize, va, rsize, raw)
    return bytes(data)


# The symbols ``elf_image`` gives by default: (name, address, size, type, defined).
ELF_SYMBOLS = [
    ("gGameState", 0x9E7840, 0x8CCCF8, 1, True),
    ("gCurrentSaveGame", 0x14945A0, 4, 1, True),
    ("_Z10PlaySoundi", 0x412340, 0x80, 2, True),
    ("SDL_GL_SwapWindow", 0, 0, 2, False),
]
ELF_BUILD_ID = bytes.fromhex("4c4416eed9e8a1fa1b373ae3b1badec33510fc90")


def elf_image(
    symbols: list[tuple[str, int, int, int, bool]] = ELF_SYMBOLS,
    build_id: bytes | None = ELF_BUILD_ID,
    kind: int = 2,
    data_bytes: bytes = b"zexion data",
) -> bytes:
    """A 64-bit little-endian ELF file with ``.tbss`` (thread-local, 0x10 bytes at 0x6BA0E0, as
    a linker overlaps it with what follows), ``.data`` (``data_bytes`` at 0x6BA0E0), ``.bss``
    (0x9E0000, 0x1000000 bytes), a GNU build-id note, and ``symbols`` in ``.symtab``."""
    shstr = b"\0.tbss\0.data\0.bss\0.note.gnu.build-id\0.symtab\0.strtab\0.shstrtab\0"

    def name(n: str) -> int:
        return shstr.index(b"\0" + n.encode() + b"\0") + 1

    note = b""
    if build_id is not None:
        note = struct.pack("<III", 4, len(build_id), 3) + b"GNU\0" + build_id
    strtab = bytearray(b"\0")
    symtab = bytearray(24)  # the null symbol
    for sym, value, size, stype, defined in symbols:
        at = len(strtab)
        strtab += sym.encode() + b"\0"
        symtab += struct.pack("<IBBHQQ", at, 0x10 | stype, 0, 1 if defined else 0, value, size)
    body = bytearray(64)
    blobs = {}
    for key, blob in (("data", data_bytes), ("note", note), ("sym", symtab)):
        blobs[key] = len(body)
        body += blob
        body += b"\0" * (-len(body) % 8)
    for key, blob in (("str", bytes(strtab)), ("shstr", shstr)):
        blobs[key] = len(body)
        body += blob
    body += b"\0" * (-len(body) % 8)
    shoff = len(body)
    alloc, write, tls = 0x2, 0x1, 0x400
    sections = [
        (0, 0, 0, 0, 0, 0, 0, 0),
        (name(".tbss"), 8, alloc | write | tls, 0x6BA0E0, blobs["data"], 0x10, 0, 0),
        (name(".data"), 1, alloc | write, 0x6BA0E0, blobs["data"], len(data_bytes), 0, 0),
        (name(".bss"), 8, alloc | write, 0x9E0000, blobs["data"], 0x1000000, 0, 0),
        (name(".note.gnu.build-id"), 7, alloc, 0x400200, blobs["note"], len(note), 0, 0),
        (name(".symtab"), 2, 0, 0, blobs["sym"], len(symtab), 6, 24),
        (name(".strtab"), 3, 0, 0, blobs["str"], len(strtab), 0, 0),
        (name(".shstrtab"), 3, 0, 0, blobs["shstr"], len(shstr), 0, 0),
    ]
    for sname, stype, flags, addr, offset, size, link, entsize in sections:
        body += struct.pack(
            "<IIQQQQIIQQ", sname, stype, flags, addr, offset, size, link, 0, 8, entsize
        )
    body[0:16] = b"\x7fELF\x02\x01\x01" + b"\0" * 9
    struct.pack_into("<HHIQQQ", body, 16, kind, 62, 1, 0x40C670, 0, shoff)
    struct.pack_into("<IHHHHHH", body, 48, 0, 64, 56, 0, 64, len(sections), 7)
    return bytes(body)


def verneed_image(needs: list[tuple[str, list[str]]], cut: int = 0) -> bytes:
    """A 64-bit ELF file with only ``.dynstr`` and ``.gnu.version_r``, which needs ``needs``'
    versions of each library in turn; ``cut`` bytes taken off the end of ``.gnu.version_r``."""
    dynstr = bytearray(b"\0")

    def string(text: str) -> int:
        at = len(dynstr)
        dynstr.extend(text.encode() + b"\0")
        return at

    verneed = bytearray()
    for i, (library, versions) in enumerate(needs):
        last = i == len(needs) - 1
        size = 16 + 16 * len(versions)
        verneed += struct.pack("<HHIII", 1, len(versions), string(library), 16, 0 if last else size)
        for j, version in enumerate(versions):
            after = 0 if j == len(versions) - 1 else 16
            verneed += struct.pack("<IHHII", 0, 0, j + 2, string(version), after)
    verneed = verneed[: len(verneed) - cut] if cut else verneed
    shstr = b"\0.dynstr\0.gnu.version_r\0.shstrtab\0"
    body = bytearray(64)
    at_dynstr = len(body)
    body += dynstr
    at_verneed = len(body)
    body += verneed
    at_shstr = len(body)
    body += shstr
    body += b"\0" * (-len(body) % 8)
    shoff = len(body)
    sections = [
        (0, 0, 0, 0, 0),
        (1, 3, at_dynstr, len(dynstr), 0),  # .dynstr
        (9, 0x6FFFFFFE, at_verneed, len(verneed), 1),  # .gnu.version_r
        (24, 3, at_shstr, len(shstr), 0),  # .shstrtab
    ]
    for name, kind, offset, size, link in sections:
        body += struct.pack("<IIQQQQIIQQ", name, kind, 0, 0, offset, size, link, 0, 8, 0)
    body[0:16] = b"\x7fELF\x02\x01\x01" + b"\0" * 9
    struct.pack_into("<HHIQQQ", body, 16, 3, 62, 1, 0, 0, shoff)
    struct.pack_into("<IHHHHHH", body, 48, 0, 64, 56, 0, 64, len(sections), 3)
    return bytes(body)


# --- a binary of B1's symbols ------------------------------------------------------------------

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


# The rest of B1's symbols the Linux helper's own table (native/linux/src/helper.cpp, kConfigKeys)
# names: (name, address, size, type). With B1_SYMBOLS they give each row's address on B1, which
# docs/re/linux.md's Lookup records (tests/test_linux_targets.py).
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
    ("gExitActions", 0x12F5720, 0x80, 1),
    ("_Z17PlayerDestructionP9SpriteRecc", 0x51C15C, 0x11BF, 2),
    ("_Z10AssistMenuv", 0x6081F1, 0xFBE, 2),
    ("gStatsSpriteLayerP", 0x12B46C0, 0x8, 1),
    ("_Z15newSpriteStringPKcii", 0x5588CD, 0x39D, 2),
    ("_Z15addSpriteStringP9SpriteRec", 0x569651, 0xBD, 2),
    ("gSavedRandos", 0x16BF7C0, 0x10EF0, 1),
    ("_Z14EndingCutscenev", 0x55AB11, 0x7D2, 2),
    ("gLargeTextCount", 0x16DA0DA, 0x2, 1),
    ("_Z20SaveGamePlayerCursorP12GameMenuItemi", 0x576BB1, 0xA6D, 2),
    ("_Z18SaveGameMenuActionP12GameMenuItemi", 0x59B85E, 0x146A, 2),
    ("_Z18CopyGameMenuActionP12GameMenuItemi", 0x5B1B09, 0x17EF, 2),
    ("_Z19ClearGameMenuActionP12GameMenuItemi", 0x5B0A65, 0x10A4, 2),
    ("_Z28setGameMessageStringClippingP9SpriteRecii", 0x568FB6, 0x69B, 2),
]


def b1_values() -> dict[str, int]:
    """Every B1 symbol the helper's table or the client's targets name, by name: its address."""
    return {n: v for n, v, *_ in B1_SYMBOLS + B1_HELPER_SYMBOLS}


def b1_like(drop: str = "", sizes: dict[str, int] | None = None) -> elf.Image:
    """A binary with B1's symbols the targets name."""
    sizes = sizes or {}
    symbols = [
        (name, value, sizes.get(name, size), 1, True)
        for name, value, size in B1_SYMBOLS
        if name != drop
    ]
    return elf.parse(elf_image(symbols=symbols))


# --- a seed's plan, and a fresh save of its mode ---------------------------------------------

AMOUNTS = (3, 17, 3, 122, 0, 12)  # Standard's, in save order
SAVE_ROOM = 0x22  # the value a fresh save's start cell holds
CELLS = [(i % 54, i // 54) for i in range(sp.LOCATIONS)]  # distinct, sorted by y then x


def fresh(
    start_cell: tuple[int, int] | None = (13, 16), cells: list = CELLS, secret: bool = False
) -> bytearray:
    """A fresh randomizer save as far as save_patch.check_fresh and the new game's writes
    (client/newgame.py) look: the mode's amounts, a few settings, one revealed start cell, a
    nonzero item table; ``cells`` as its location list, and with ``secret`` Secret Shuffle on."""
    save = bytearray(sp.SAVE_SIZE)
    for i, (x, y) in enumerate(cells):
        struct.pack_into("<HH", save, sp.SECTION + 4 * i, x, y)
    save[sp.OPTIONS + sp.SECRET_SHUFFLE] = secret
    save[sp.RANDO_MARKER] = 1
    struct.pack_into("<6h", save, sp.AMOUNTS, *AMOUNTS)
    save[sp.OPTIONS + 10] = 1  # Guns First
    save[sp.OPTIONS + 42] = 2  # Safety Normal
    save[sp.OPTIONS + 72] = 1  # Gates Vanilla
    save[sp.ITEM_TABLE : sp.ITEM_TABLE + sp.LOCATIONS] = bytes([7] * sp.LOCATIONS)
    if start_cell is not None:
        x, y = start_cell
        save[sp.MAP_GRID + y * ids.MAP_SIZE + x] = SAVE_ROOM
    return save


ITEMS = [46] * sp.LOCATIONS
ITEMS[3], ITEMS[9], ITEMS[12] = 47, 48, 44  # a stand-in, a progression stand-in, a disk


def slot_data(**changes) -> dict:
    """A seed's slot data as the world sends it, every key the client reads, the options off."""
    slot = {
        "location_items": ITEMS,
        "start_items": {"0": 3},
        "start_index": 5,
        "start_column": 326,
        "start_row": 449,
        "second_start": None,
        "amounts": list(AMOUNTS),
        "options": {"10": 1, "42": 2},
        "locations": [list(c) for c in CELLS],
        "kept_locations": [i for i in range(sp.LOCATIONS) if i != 9],
        "mode": "Standard",
        "menu": {
            "seed_text": "0123456789ABCDEF",
            "start": [0] * 27,
            "pool": [1] * 27,
            "settings": [1] + [0] * 31 + [2] + [0] * 31,  # as the options
            "second_target": [0, 20, 509],
        },
        "hints": [],
        "allow_assists": True,
        "intel_chip": False,
        "death_link": False,
        "goal": "core",
        "discs_required": 0,
        "discs_placed": 0,
        "discs_start": 0,
        "disc_locations": [],
    }
    slot.update(changes)
    return slot


def connection(**changes) -> state.Connection:
    """A connection to the seed of ``slot_data(**changes)`` as slot 1, its items listed."""
    conn = state.Connection.from_slot_data(slot_data(**changes), team=0, slot=1)
    conn.items_listed, conn.awaiting_backlog = True, False
    return conn


def seed_state(conn: state.Connection) -> state.SeedState:
    """The SeedState the client keeps for ``conn``'s seed (zexion_client's connected)."""
    return state.SeedState(("S", conn.team, conn.slot), session.Session(conn.plan, conn.intel_chip))


def menu_setup(**changes) -> sp.MenuSetup:
    """A menu setup of plan()'s mode: a mode with one start."""
    settings = bytearray(64)
    settings[10 - 10], settings[42 - 10], settings[72 - 10] = 1, 2, 1  # as plan()'s options
    fields = dict(
        seed_text="0123456789ABCDEF",
        start=bytes([0] * 4 + [1] + [0] * 22),
        pool=bytes([1] * 4 + [0] + [1] * 22),
        settings=bytes(settings),
        second_target=(0, 20, 509),
    )
    fields.update(changes)
    return sp.MenuSetup(**fields)


def plan(**changes) -> sp.SeedPlan:
    fields = dict(
        location_items=tuple(i % 46 for i in range(sp.LOCATIONS)),
        start_items={0: 2, 1: 3, 36: 1, 3: 1},
        start=(5, 23 * 14 + 4, 22 * 20 + 9),
        second_start=None,
        amounts=AMOUNTS,
        options={10: 1, 42: 2, 72: 1},
        menu=menu_setup(),
        cells=tuple(CELLS),
    )
    fields.update(changes)
    return sp.SeedPlan(**fields)


# A Secret Shuffle seed's 155 locations: SECRET_LIST in the game's list order (grid order), and
# SECRET_CELLS by location index, as the world lists them, its 155th (index 154) last: the cell
# at list position 100.
SECRET_LIST = [(i % 54, i // 54) for i in range(sp.LOCATIONS + 1)]
SECRET_CELLS = SECRET_LIST[:100] + SECRET_LIST[101:] + [SECRET_LIST[100]]


def secret_plan(**changes) -> sp.SeedPlan:
    """plan() with Secret Shuffle on: 155 locations, listed by location index."""
    settings = bytearray(menu_setup().settings)
    settings[sp.SECRET_SHUFFLE - 10] = 1
    fields = dict(
        location_items=tuple(i % 46 for i in range(sp.LOCATIONS + 1)),
        options={10: 1, 42: 2, 72: 1, sp.SECRET_SHUFFLE: 1},
        cells=tuple(SECRET_CELLS),
        menu=menu_setup(settings=bytes(settings)),
    )
    fields.update(changes)
    return plan(**fields)


def section(p: sp.SeedPlan) -> bytearray:
    """The loaded section of a save of ``p``: its cells in the game's list order, Secret
    Shuffle's byte, the item table in the game's order and the start record."""
    section = bytearray(ids.SECTION_SIZE)
    for i, at in enumerate(p.order):
        struct.pack_into("<HH", section, 4 * i, *p.cells[at])
    section[sp.OPTIONS_AT + sp.SECRET_SHUFFLE] = p.list_size > sp.LOCATIONS
    section[sp.TABLE_AT : sp.TABLE_AT + len(p.table)] = p.table
    struct.pack_into("<3H", section, sp.START_AT, *p.start)
    return section


class Showing:
    """A running game whose state() is ``shown``; nothing else is read."""

    def __init__(self, shown: str) -> None:
        self.shown = shown

    def state(self) -> str:
        return self.shown


class Running(Showing):
    """A running game, its process alive (game_alive)."""

    def __init__(self, shown: str) -> None:
        super().__init__(shown)
        self.pm = types.SimpleNamespace(alive=lambda: True)


# --- a stand-in /proc -----------------------------------------------------------------------


def fake_proc(
    tmp_path: Path, processes: dict[str, bytes] | None = None, nspid: str = "100"
) -> Path:
    """A ``/proc`` of ``processes`` (id to comm), with ``self`` in a namespace of ``nspid`` (the
    ``NSpid`` line of its status)."""
    proc = tmp_path / "proc"
    proc.mkdir()
    (proc / "self").mkdir()  # not a process id
    (proc / "self" / "comm").write_bytes(b"Zexion\n")
    (proc / "self" / "status").write_text(f"Name:\tpython\nNSpid:\t{nspid}\n")
    for pid, comm in (processes or {}).items():
        proc_entry(proc, pid, comm)
    return proc


def proc_entry(
    proc: Path,
    pid: str,
    comm: bytes,
    cmdline: bytes = b"",
    exe: bytes | str | None = None,
    nspid: str = "",
) -> None:
    """Process ``pid`` in the ``/proc`` ``proc``: its name, command line, ``NSpid`` (its own id
    when not given), start time (the id times 10) and ``exe``: a binary's bytes (the file itself
    stands for the link, as a read follows it) or a link's target."""
    entry = proc / pid
    entry.mkdir()
    (entry / "comm").write_bytes(comm)
    (entry / "cmdline").write_bytes(cmdline)
    (entry / "status").write_text(f"Name:\t{comm.decode().strip()}\nNSpid:\t{nspid or pid}\n")
    # /proc/<pid>/stat: the pid, the name in parentheses, then fields 3 on; 22 is the start time.
    rest = " ".join(["S"] + ["0"] * 18 + [str(int(pid) * 10)] + ["0"] * 30)
    (entry / "stat").write_text(f"{pid} ({comm.decode().strip()}) {rest}\n")
    if isinstance(exe, bytes):
        (entry / "exe").write_bytes(exe)
    elif exe:
        (entry / "exe").symlink_to(exe)


# --- helpers that answer by script ----------------------------------------------------------


class Answering:
    """A helper that answers by script and keeps what it was asked. ``replies`` maps the first word
    of a request to its reply: a string, an exception to raise, or a function of the request's
    text; any other request is answered with ``default``. ``apply`` (a batch of popups) is its
    own word, "applied" unless scripted. It serves as the Windows helper (native.Helper's
    ``request``, ``close``) and as a ``linux_helper.LinuxMod`` or its process, as far as the ticks
    reach them."""

    loaded_here = False  # native.Helper's: already in the game when attached

    def __init__(self, replies: dict | None = None, default: str = "none") -> None:
        self.replies = dict(replies or {})
        self.default = default
        self.requests: list[str] = []
        self.closed = 0
        self.hello_fields: dict[str, str] = {}
        self.hellos = 0

    def _answer(self, text: str, word: str, default: str) -> str:
        self.requests.append(text)
        reply = self.replies.get(word, default)
        if isinstance(reply, BaseException):
            raise reply
        return reply(text) if callable(reply) else reply

    def request(self, text: str, seconds: float = 2.0) -> str:
        words = text.split(None, 1)
        return self._answer(text, words[0] if words else "", self.default)

    def apply(self, text: str) -> str:
        return self._answer(text, "apply", "applied")

    def hello(self) -> dict[str, str]:
        """The Windows helper's hello: its version and ``hello_fields`` (``lang``, ``feats``)."""
        self.hellos += 1
        return {"version": "1.0.0", **self.hello_fields}

    def firsts(self) -> list[str]:
        """Each request's first word."""
        return [(r.split(None, 1) or [""])[0] for r in self.requests]

    def notices(self) -> list[str]:
        """The notice lines of every batch applied (or requested)."""
        lines = [line for r in self.requests for line in r.splitlines()]
        return [line for line in lines if line.startswith("notice")]

    def close(self) -> None:
        self.closed += 1


class LinuxPm(Answering):
    """The game's Linux helper connection (linux_helper.LinuxProcess) on B1's symbols: answers by
    ``replies`` (``config`` "ok" unless scripted, any other request an unknown one), counts the
    connections made and let go. ``token_path``: where the helper's token was found."""

    def __init__(self, replies: dict | None = None) -> None:
        super().__init__({"config": "ok", **(replies or {})}, default="error unknown request")
        self.game = types.SimpleNamespace(image=b1_like())
        self.connection = 1  # the connections made
        self.connected = True
        self.disconnects = 0
        self.token_path: Path | None = None

    @property
    def sent(self) -> list[str]:
        return self.requests

    def disconnect(self) -> None:  # closed, made again on use
        self.connected = False
        self.disconnects += 1

    def alive(self) -> bool:
        return True


def said(caplog) -> list[str]:
    """The lines the client's window got (the "Client" logger) among what ``caplog`` caught."""
    return [r.getMessage() for r in caplog.records if r.name == "Client"]


# --- the client's context on Archipelago's stand-ins ----------------------------------------


class CommonContext:
    """What ZexionContext and the ticks read of CommonClient's context."""

    def __init__(self, server_address: str | None, password: str | None) -> None:
        self.items_received: list = []
        self.locations_checked: set[int] = set()
        self.checked_locations: set[int] = set()  # the server's
        self.stored_data: dict = {}
        self.stored_data_notification_keys: set[str] = set()
        self.gets: list[list[str]] = []  # the keys of each Get sent, as CommonClient sends them
        self.player_names: dict[int, str] = {1: "Me", 2: "Bob"}
        self.slot_info: dict = {}
        self.locations_info: dict = {}  # what the server said each location holds
        self.slot, self.team = 1, 0
        self.server_seed_name = self.seed_name = None
        self.finished_game = False
        self.deaths_sent: list[str] = []

    def set_notify(self, *keys: str) -> None:
        if new := [k for k in keys if k not in self.stored_data_notification_keys]:
            self.stored_data_notification_keys.update(new)
            self.gets.append(new)

    async def disconnect(self, allow_autoreconnect: bool = False) -> None:
        pass

    async def update_death_link(self, on: bool) -> None:
        pass

    async def send_msgs(self, msgs: list[dict]) -> None:
        pass

    async def send_death(self, text: str = "") -> None:
        self.deaths_sent.append(text)


def _import_zexion_client() -> types.ModuleType:
    """client/zexion_client.py, imported with Archipelago's modules stood in (it is not installed
    here). Only the stand-ins are put back as they were (the client's modules stay the ones the
    other tests use), and this copy of zexion_client is not left for another test to import."""
    common = types.ModuleType("CommonClient")
    common.CommonContext = CommonContext
    common.ClientCommandProcessor = object
    common.logger = logging.getLogger("Client")
    common.get_base_parser = common.handle_url_arg = common.server_loop = None
    common.gui_enabled = False
    net = types.ModuleType("NetUtils")
    net.ClientStatus = None
    stand_ins = {"CommonClient": common, "NetUtils": net, "Utils": types.ModuleType("Utils")}
    saved = {name: sys.modules.get(name) for name in stand_ins}
    sys.modules.update(stand_ins)
    try:
        with mock.patch.dict(os.environ, ZEXION_AP_DIR=os.devnull):  # not a folder: not on the path
            from client import zexion_client
    finally:
        sys.modules.pop("client.zexion_client", None)
        vars(sys.modules["client"]).pop("zexion_client", None)  # the package's name for it too
        for name, module in saved.items():
            if module is None:
                del sys.modules[name]
            else:
                sys.modules[name] = module
    return zexion_client


zc = _import_zexion_client()


def context(conn: state.Connection, g, dropped: bool = False) -> zc.ZexionContext:
    """The client's context connected as ``conn`` to its seed, with ``g`` the attached game (a
    stand-in, or None). ``dropped``: the connection is down (no slot), as reset_server_state
    leaves it."""
    ctx = zc.ZexionContext(None, None)
    ctx.conn, ctx.seed_state, ctx.game_conn = conn, seed_state(conn), g
    if dropped:
        ctx.slot = None  # no connection steps (away_tick)
    return ctx


def stepping(monkeypatch, ctx: zc.ZexionContext, result: session.Step | None = None) -> list[dict]:
    """The session's step replaced by one that records the keywords of each call (``helper``,
    ``give``, ``followups``...) and returns ``result``, or, with none, ends the poll there as a
    game that closed does (GameNotRunning). The records are the list returned."""
    calls: list[dict] = []

    def step(g, received, **kwargs):
        calls.append(kwargs)
        if result is None:
            raise game.GameNotRunning("Zexion is not running")
        return result

    monkeypatch.setattr(ctx.seed_state.session, "step", step)
    return calls


# --- a scripted time.sleep, for the tools' polling loops ---------------------------------------


def scripted_sleep(*steps):
    """A ``time.sleep`` that runs the next step at each call, then stops the loop as Ctrl+C
    does (KeyboardInterrupt) once the steps run out."""
    remaining = iter(steps)

    def sleep(_seconds: float) -> None:
        step = next(remaining, None)
        if step is None:
            raise KeyboardInterrupt
        step()

    return sleep
