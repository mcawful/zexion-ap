"""Find and access Zexion's game state in the running process, by byte pattern.

Nothing here uses a fixed address. On attach, the game's build is identified from its PE
header and refused unless it is in ``patterns.KNOWN_BUILDS``; then every target in
``patterns.TARGETS`` is located by scanning the code section of Zexion.exe as loaded. Each
pattern must match exactly once (overlapping matches count) and all of a target's patterns
must lead to the same address, or ``PatternError`` is raised and nothing is read or written.

Every read and write first checks that the attached process is still running. A failure is
``GameNotRunning`` only when the process has exited; any other failure is a ``GameError``
naming the address, because it means something is wrong with what was located.

Item writes are compare-and-set (``set_item``, and ``set_supplies`` for the energy, missile
and bomb numbers): the caller says what it expects to be there, the module re-reads it just
before writing and refuses if it differs, and reads the write back (``ReadBackMismatch`` if the
game changed it straight away). That cannot see a
later reload of the inventory: a grant lasts only once the game has saved it, and a death
returns to the last save (measured; docs/re/memory.md, "Granting each kind of item"), so the
delivery layer (client/session.py) gives again whatever a reload took away.

``state()`` says whether the game is in play: a save is loaded, no menu is open, and the
player is alive (docs/re/memory.md, "Telling play from menus, the title screen and death").
It reads three values one after another, so the state can change between them and right after.
Worse, loading a save (which is also how the game respawns after a death) sets two of them
before it copies the inventory back from the save, so one ``state()`` can read IN_PLAY in the
middle of a load, and a write made then reads back fine and is overwritten a moment later.
``settled_state()`` reads the state twice, ``SETTLE_SECONDS`` apart, and only agrees on
IN_PLAY when both do; a caller writes only then, and afterwards waits as long again and checks
that the state is still IN_PLAY and the byte still holds what it wrote. Even that only says
the write was not undone at once: the save is the final word. Room transitions and saving kept
the in-play values in a recording of the bytes; cutscenes and endings are untested.
"""

from __future__ import annotations

import re
import struct
import time
from collections.abc import Iterable, Sequence
from dataclasses import dataclass

from patterns import KNOWN_BUILDS, TARGETS, Build, Pattern, Target

SLOTS = 30  # save slots
SLOT_STRIDE = 0x9408  # a slot's first block in the slot array
SLOT_COPY = 0x908  # a slot's copy of its randomizer section

PROCESS = "Zexion.exe"
MAP_SIZE = 54
COLLECTED = 0x1B  # map cell value once its item is collected
FLAGS = 0x9C4  # the loaded game's flags, one byte each
ITEM_IDS = range(0, 51)  # ItemNames.txt: 0 Energy Tank .. 50 Energy Shield
# A collected Energy Shield (item 50) left its byte at 0 in a live game: its pickup counts as
# Scrap Metal (docs/re/memory.md, "Other players' items"), so the module never writes byte 50.
WRITABLE_ITEMS = range(0, 50)
LOCATIONS = 154
# The loaded save's randomizer section, as from file offset 0x12814 (docs/re/save-format.md).
RANDO_BLOB_SIZE = 0x908
ITEM_TABLE_AT = 0x26C  # file 0x12A80 - 0x12814
# Player state fields, by offset from the player state, which starts 0x108E before the
# inventory (docs/re/memory.md).
INVENTORY_IN_PLAYER_STATE = 0x108E
ENERGY_NOW = 0xCEA  # signed 16-bit; -1 from death until the respawn
# Player-state offsets of the energy, missile and bomb numbers (signed 16-bit), in the order
# supplies() returns them: energy, max energy, missiles, max missiles, bombs, max bombs.
SUPPLY_FIELDS = (0xCEA, 0xCEC, 0xCBC, 0xCC0, 0xCBE, 0xCC2)
SUPPLY_SPAN = (0xCBC, 0xCEE)  # one read covering all six
NO_SAVE_LOADED = 0xFF  # save_loaded's value on the title and file-select screens
# The player's link to their Energy Shield (docs/re/memory.md, "Received Energy Shields"): a
# pointer to the shield object, then that object's handle (a 32-bit serial number, then its
# pool's type, 2). The pointer is 0 while no shield is up. The object holds the same handle
# at +0x1D8, with serial 0 once its slot is free, and its own address at +0x3C8.
SHIELD_LINK = 0xFE8
SHIELD_POOL_TYPE = 2
OBJECT_HANDLE = 0x1D8
OBJECT_SELF = 0x3C8

# What state() returns.
NO_SAVE = "no save loaded"
MENU = "menu"
DEAD = "dead"
IN_PLAY = "in play"
CHANGING = "changing"  # settled_state(): two reads disagreed
SETTLE_SECONDS = 0.1  # several frames


class GameError(RuntimeError):
    """Base class for everything this module refuses to do."""


class GameNotRunning(GameError):
    """The attached process has exited (or was never found)."""


class PatternError(GameError):
    """A pattern matched zero or several times, or a target's patterns disagreed."""


class UnknownBuild(GameError):
    """The running Zexion.exe is not a build docs/re/ has been checked on."""


class WriteStateUnknown(GameError):
    """A write failed part-way: the byte may or may not have changed. Re-read before acting."""


class ReadBackMismatch(GameError):
    """The write was made, but reading it back gave something else: the game changed it
    straight after (a reload, a clamp), or the write did not take. ``read_back`` holds what
    was read."""

    def __init__(self, message: str, read_back: bytes):
        super().__init__(message)
        self.read_back = read_back


# --- pure helpers (tested without the game) ----------------------------------------------


def compile_pattern(text: str) -> re.Pattern[bytes]:
    parts = []
    for tok in text.split():
        parts.append(b"." if tok == "??" else re.escape(bytes([int(tok, 16)])))
    # A lookahead finds every start position, so overlapping matches are all counted.
    return re.compile(b"(?=" + b"".join(parts) + b")", re.DOTALL)


def find_unique(code: bytes, pattern: Pattern) -> int:
    """Offset of the only match of ``pattern`` in ``code``."""
    hits = [m.start() for m in compile_pattern(pattern.text).finditer(code)]
    if len(hits) != 1:
        raise PatternError(f"pattern matched {len(hits)} times, expected once: {pattern.text}")
    return hits[0]


def resolve(code: bytes, code_rva: int, pattern: Pattern) -> int:
    """The RVA ``pattern`` leads to, given ``code`` loaded at ``code_rva``."""
    if pattern.kind == "at":
        return code_rva + find_unique(code, pattern) + pattern.addend
    if pattern.kind != "rip":
        # An image-base displacement is only an address if a register holds the image base,
        # which a pattern cannot show; such patterns are refused rather than trusted.
        raise PatternError(f"pattern kind {pattern.kind!r} is not accepted, only 'rip' or 'at'")
    at = find_unique(code, pattern)
    field = struct.unpack_from("<i", code, at + pattern.disp_at)[0]
    return code_rva + at + pattern.rip_end + field + pattern.addend


def locate_all(code: bytes, code_rva: int, targets: Iterable[Target] = TARGETS) -> dict[str, int]:
    """RVA of every target; every pattern of a target must agree."""
    out = {}
    for target in targets:
        found = {resolve(code, code_rva, p) for p in target.patterns}
        if len(found) != 1:
            listed = ", ".join(f"{v:#x}" for v in sorted(found))
            raise PatternError(f"{target.name}: patterns disagree ({listed})")
        out[target.name] = found.pop()
    return out


def pe_header_fields(header: bytes) -> tuple[int, int]:
    """(TimeDateStamp, SizeOfImage) from the start of a PE image."""
    pe = struct.unpack_from("<I", header, 0x3C)[0]
    if header[pe : pe + 4] != b"PE\0\0":
        raise GameError("not a PE image")
    stamp = struct.unpack_from("<I", header, pe + 8)[0]
    size_of_image = struct.unpack_from("<I", header, pe + 24 + 56)[0]
    return stamp, size_of_image


def identify_build(header: bytes, known: Iterable[Build] = KNOWN_BUILDS) -> Build:
    stamp, size = pe_header_fields(header)
    for build in known:
        if build.timestamp == stamp and build.size_of_image == size:
            return build
    raise UnknownBuild(
        f"Zexion.exe build with timestamp {stamp:#x} and image size {size:#x} is not one "
        "docs/re/ has been checked on; refusing to touch it"
    )


def code_section(header: bytes) -> tuple[int, int]:
    """(RVA, size) of .text, from the first 4 KiB of a PE image."""
    pe = struct.unpack_from("<I", header, 0x3C)[0]
    if header[pe : pe + 4] != b"PE\0\0":
        raise GameError("not a PE image")
    nsec = struct.unpack_from("<H", header, pe + 6)[0]
    optsz = struct.unpack_from("<H", header, pe + 20)[0]
    for i in range(nsec):
        o = pe + 24 + optsz + 40 * i
        if o + 40 > len(header):
            break
        if header[o : o + 8].rstrip(b"\0") == b".text":
            vsize, va = struct.unpack_from("<II", header, o + 8)
            return va, vsize
    raise GameError("no .text section")


@dataclass(frozen=True)
class Seed:
    """The loaded seed, from its randomizer section."""

    locations: tuple[tuple[int, int], ...]  # (x, y) per location, in table order
    items: bytes  # item ID per location


def parse_seed(blob: bytes) -> Seed:
    """Check the randomizer section looks right before anything uses it.

    154 distinct (x, y) cells on the map, sorted by y then x, then 4 zero bytes, then 154 item
    IDs from 0 to 50. Anything else means the section is not where it was located, or no
    randomizer save is loaded.
    """
    if len(blob) != RANDO_BLOB_SIZE:
        raise GameError(f"randomizer section is {len(blob)} bytes, expected {RANDO_BLOB_SIZE}")
    cells = tuple(struct.unpack_from("<HH", blob, 4 * i) for i in range(LOCATIONS))
    if any(not (0 <= x < MAP_SIZE and 0 <= y < MAP_SIZE) for x, y in cells):
        raise GameError("randomizer section: a location cell is off the map")
    if len(set(cells)) != LOCATIONS or list(cells) != sorted(cells, key=lambda c: (c[1], c[0])):
        raise GameError("randomizer section: location cells are not distinct and sorted")
    if any(blob[4 * LOCATIONS : ITEM_TABLE_AT]):
        raise GameError("randomizer section: no zero gap after the location list")
    items = bytes(blob[ITEM_TABLE_AT : ITEM_TABLE_AT + LOCATIONS])
    if any(i not in ITEM_IDS for i in items):
        raise GameError("randomizer section: an item ID is out of range")
    return Seed(cells, items)


# --- the running game ---------------------------------------------------------------------


def game_running() -> bool:
    """Whether a Zexion process is running (without attaching to it)."""
    try:
        import pymem
        import pymem.exception
    except ImportError as exc:  # pragma: no cover - only on machines without pymem
        raise GameError("pymem is not installed (Windows only)") from exc
    try:
        pm = pymem.Pymem(PROCESS)
    except pymem.exception.ProcessNotFound:
        return False
    pm.close_process()
    return True


@dataclass
class Game:
    """An attached Zexion process with every target located."""

    pm: object
    base: int
    rva: dict[str, int]
    pid: int
    build: Build

    @classmethod
    def attach(cls) -> Game:
        try:
            import pymem
            import pymem.process
        except ImportError as exc:  # pragma: no cover - only on machines without pymem
            raise GameError("pymem is not installed (Windows only)") from exc
        try:
            pm = pymem.Pymem(PROCESS)
        except Exception as exc:
            raise GameNotRunning(f"{PROCESS} is not running") from exc
        try:
            module = pymem.process.module_from_name(pm.process_handle, PROCESS)
            if module is None:
                raise GameError(f"{PROCESS} is running but its module was not found")
            base = module.lpBaseOfDll
            header = pm.read_bytes(base, 0x1000)
            build = identify_build(header)
            text_rva, text_size = code_section(header)
            code = pm.read_bytes(base + text_rva, text_size)
            rva = locate_all(code, text_rva)
            return cls(pm, base, rva, pm.process_id, build)
        except GameError:
            pm.close_process()
            raise
        except Exception as exc:
            pm.close_process()
            raise GameError(f"attaching to {PROCESS} failed: {exc}") from exc

    def _alive(self) -> bool:
        import ctypes

        still_active = 259
        code = ctypes.c_ulong()
        ok = ctypes.windll.kernel32.GetExitCodeProcess(self.pm.process_handle, ctypes.byref(code))
        return bool(ok) and code.value == still_active

    def _check_alive(self) -> None:
        if not self._alive():
            raise GameNotRunning(f"{PROCESS} (pid {self.pid}) has exited")

    def _failure(self, what: str, address: int, exc: Exception) -> GameError:
        if not self._alive():
            return GameNotRunning(f"{PROCESS} (pid {self.pid}) exited during {what}")
        return GameError(f"{what} at {address:#x} failed in a running game: {exc}")

    # Every access goes through these two, so the liveness check cannot be skipped.
    def _read(self, target: str, offset: int, size: int) -> bytes:
        return self._read_pointer(self.base + self.rva[target] + offset, size)

    def _read_pointer(self, address: int, size: int) -> bytes:
        """Read at an address: a located target's, or a pointer the game holds."""
        self._check_alive()
        try:
            data = self.pm.read_bytes(address, size)
        except Exception as exc:
            raise self._failure("read", address, exc) from exc
        if len(data) != size:
            raise GameError(f"short read at {address:#x}: {len(data)} of {size} bytes")
        return data

    def _write(self, target: str, offset: int, data: bytes) -> None:
        self._check_alive()
        address = self.base + self.rva[target] + offset
        try:
            self.pm.write_bytes(address, data, len(data))
        except Exception as exc:
            raise WriteStateUnknown(f"write at {address:#x} failed part-way: {exc}") from exc
        try:
            back = self.pm.read_bytes(address, len(data))
        except Exception as exc:
            raise WriteStateUnknown(f"write at {address:#x} could not be read back: {exc}") from exc
        if back != data:
            raise ReadBackMismatch(
                f"read-back of {target}+{offset:#x} gave {back.hex()}, not the {data.hex()} "
                "written",
                back,
            )

    # --- state ------------------------------------------------------------------------

    def map_cell(self, x: int, y: int) -> int:
        if not (0 <= x < MAP_SIZE and 0 <= y < MAP_SIZE):
            raise ValueError(f"cell ({x}, {y}) is off the map")
        return self._read("map_grid", y * MAP_SIZE + x, 1)[0]

    def map_grid(self) -> bytes:
        return self._read("map_grid", 0, MAP_SIZE * MAP_SIZE)

    def inventory(self) -> bytes:
        """One byte per item ID 0-50."""
        return self._read("inventory", 0, len(ITEM_IDS))

    def flag(self, number: int) -> int:
        """One of the loaded game's flags (0 to 0x9C3; docs/re/memory.md, "Keys, the gates and
        the key markers" and "The ending")."""
        if not 0 <= number < FLAGS:
            raise ValueError(f"flag {number} is outside the 0x{FLAGS:X} flags")
        return self._read("flags", number, 1)[0]

    def set_item(self, item: int, expected: int, value: int) -> None:
        """Compare-and-set an item's inventory byte, then read it back.

        Refuses (``GameError``) if the byte does not hold ``expected`` just before the write,
        so a grant cannot overwrite a change the game made since the caller looked. The
        caller decides when writing is safe; see the module docstring. For Energy Tanks,
        Missile Containers and Mega Bombs the byte alone does not change the energy, missile
        or bomb numbers the game uses (docs/re/memory.md, "Granting each kind of item").
        """
        if item not in WRITABLE_ITEMS:
            raise ValueError(f"item {item} is not an item this module writes")
        for v in (expected, value):
            if not 0 <= v <= 0xFF:
                raise ValueError(f"value {v} does not fit a byte")
        current = self._read("inventory", item, 1)[0]
        if current != expected:
            raise GameError(
                f"item {item} holds {current}, not the expected {expected}; not written"
            )
        self._write("inventory", item, bytes([value]))

    def supplies(self) -> tuple[int, ...]:
        """Energy, max energy, missiles, max missiles, bombs, max bombs, read at once."""
        start, end = SUPPLY_SPAN
        span = self._read("inventory", start - INVENTORY_IN_PLAYER_STATE, end - start)
        return tuple(struct.unpack_from("<h", span, f - start)[0] for f in SUPPLY_FIELDS)

    def set_supplies(self, expected: Sequence[int], new: Sequence[int]) -> None:
        """Compare-and-set the six numbers ``supplies()`` returns, writing only those that
        change, each read back. Refuses (``GameError``) if any differs from ``expected`` just
        before the write, as when the player fired or took damage since the caller looked."""
        if len(expected) != len(SUPPLY_FIELDS) or len(new) != len(SUPPLY_FIELDS):
            raise ValueError("supplies are six numbers")
        if any(not -0x8000 <= v <= 0x7FFF for v in new):
            raise ValueError(f"supplies {tuple(new)} do not fit 16 bits")
        current = self.supplies()
        if current != tuple(expected):
            raise GameError(f"supplies are {current}, not the expected {tuple(expected)}")
        for field, old, value in zip(SUPPLY_FIELDS, current, new, strict=True):
            if value != old:
                offset = field - INVENTORY_IN_PLAYER_STATE
                self._write("inventory", offset, struct.pack("<h", value))

    def energy(self) -> int:
        """Current energy (signed); -1 from the moment of death until the respawn (measured),
        so 0 or below counts as dead (0 itself is an assumption)."""
        offset = ENERGY_NOW - INVENTORY_IN_PLAYER_STATE
        return struct.unpack("<h", self._read("inventory", offset, 2))[0]

    def shield_up(self) -> bool:
        """Whether the player has an Energy Shield up: the player's link to it is set, and the
        object it points to holds the same live handle and its own address. A link whose
        object does not match counts as no shield."""
        link = self._read("inventory", SHIELD_LINK - INVENTORY_IN_PLAYER_STATE, 16)
        pointer = struct.unpack_from("<Q", link, 0)[0]
        serial, pool = struct.unpack_from("<IH", link, 8)
        if pointer == 0 or serial == 0 or pool != SHIELD_POOL_TYPE:
            return False
        handle = self._read_pointer(pointer + OBJECT_HANDLE, 8)
        own = struct.unpack("<Q", self._read_pointer(pointer + OBJECT_SELF, 8))[0]
        return handle == link[8:16] and own == pointer

    def state(self) -> str:
        """NO_SAVE, MENU, DEAD or IN_PLAY. Only IN_PLAY is a state to write items in."""
        if self._read("save_loaded", 0, 1)[0] == NO_SAVE_LOADED:
            return NO_SAVE
        if self._read("menu_state", 0, 1)[0] != 0:
            return MENU
        if self.energy() <= 0:
            return DEAD
        return IN_PLAY

    def settled_state(self, gap: float = SETTLE_SECONDS) -> str:
        """``state()`` read twice, ``gap`` seconds apart; CHANGING if the two differ."""
        first = self.state()
        time.sleep(gap)
        second = self.state()
        return second if second == first else CHANGING

    def pickup_counter(self) -> int:
        """The ITEMS counter (16-bit): pickups, except items 27-31 and 38-45.

        Not a per-location signal; the map grid is (docs/re/memory.md).
        """
        return struct.unpack("<H", self._read("pickup_counter", 0, 2))[0]

    def rando_marker(self) -> int:
        """1 when the loaded save is a randomizer save, 0 for a normal one. Loading a normal
        save leaves the previous randomizer section in memory (measured), so ``rando_blob()``
        means something only while this reads 1."""
        return struct.unpack("<H", self._read("rando_marker", 0, 2))[0]

    def slot_markers(self) -> list[tuple[int, bool]]:
        """Each of the 30 save slots' randomizer marker (block +0x9400, 1 for a randomizer
        save) and whether it holds a save (block +0x935E not 0xFF), from the slot array."""
        out = []
        for n in range(SLOTS):
            marker = struct.unpack("<H", self._read("slot_array", n * SLOT_STRIDE + 0x9400, 2))[0]
            used = self._read("slot_array", n * SLOT_STRIDE + 0x935E, 1)[0] != 0xFF
            out.append((marker, used))
        return out

    def rando_slot_copies(self) -> bytes:
        """The game's copy of each slot's randomizer section (30 of SLOT_COPY bytes, in the
        loaded section's layout; docs/re/memory.md, "The file select")."""
        return self._read("rando_slots", 0, SLOTS * SLOT_COPY)

    def rando_blob(self) -> bytes:
        """The loaded save's randomizer section, as from file offset 0x12814."""
        return self._read("rando_blob", 0, RANDO_BLOB_SIZE)

    def seed(self) -> Seed:
        """The loaded seed's locations and items, checked (``parse_seed``)."""
        return parse_seed(self.rando_blob())
