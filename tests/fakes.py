"""Stand-ins the tests share: a fake process and game.Game on it, and a seed's plan with a fresh
randomizer save and the loaded section of a save of it (no game, no exe, no real save)."""

from __future__ import annotations

import struct

from client import game, ids, session, state
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


# --- a seed's plan, and a fresh save of its mode ---------------------------------------------

AMOUNTS = (3, 17, 3, 122, 0, 12)  # Standard's, in save order
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
        save[sp.MAP_GRID + y * ids.MAP_SIZE + x] = sp.SAVE_ROOM
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
