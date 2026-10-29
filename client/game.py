"""Find and access Zexion's game state in the running process, by byte pattern.

``attach`` refuses a build not in ``patterns.KNOWN_BUILDS`` and locates each target
(``patterns.CLIENT_TARGETS``; the RE tools ask for all of ``patterns.TARGETS``) by scanning
.text; every pattern must match exactly once and a target's patterns must agree, else
``PatternError`` and nothing is touched. Every read and write first checks the process is
alive (``GameNotRunning`` only once it has exited).

Item writes are compare-and-set and read back. A grant lasts only once the game saves it
(docs/re/memory.md, "Granting each kind of item"); client/session.py gives again what a reload
took. ``state()`` can read IN_PLAY mid-load (the load sets two of its three values before
restoring the inventory), so writers use ``settled_state()`` and re-check after writing
(docs/re/memory.md, "Telling play from menus, the title screen and death").
"""

from __future__ import annotations

import re
import struct
import time
from collections.abc import Iterable, Sequence
from dataclasses import dataclass

from . import ids, pe, process
from .ids import LOCATIONS, MAP_SIZE, SECRET_SHUFFLE, SECTION_SIZE, WRITABLE_ITEMS
from .patterns import CLIENT_TARGETS, KNOWN_BUILDS, TARGETS, Build, Pattern, Target
from .save_patch import OPTIONS_AT, TABLE_AT

SLOTS = 30  # save slots
SLOT_STRIDE = 0x9408  # a slot's first block in the slot array
# The loaded slot's block from +0x9354: a stamp that changes with each save (play time,
# assumed), then the continue point's row and column (docs/re/save-format.md).
SAVE_STAMP_AT, SAVE_STAMP_SIZE = 0x9354, 10
PLAYER_CELL = 0x107A  # in the player state: the game's X, then its Y (this repo's y, then x)

PROCESS = "Zexion.exe"
FLAGS = 0x9C4  # the loaded game's flags, one byte each
# Player state fields, by offset from the player state, which starts 0x108E before the
# inventory (docs/re/memory.md).
INVENTORY_IN_PLAYER_STATE = 0x108E
ENERGY_NOW = 0xCEA  # signed 16-bit; -1 from death until the respawn
# Player-state offsets of the energy, missile and bomb numbers (signed 16-bit), in the order
# supplies() returns them: energy, max energy, missiles, max missiles, bombs, max bombs.
SUPPLY_FIELDS = (0xCEA, 0xCEC, 0xCBC, 0xCC0, 0xCBE, 0xCC2)
# One read covering the supplies (from the first, 0xCBC), the player's cell and the inventory
# (player_state).
PLAYER_SPAN = (min(SUPPLY_FIELDS), INVENTORY_IN_PLAYER_STATE + len(ids.GAME_ITEMS))
NO_SAVE_LOADED = 0xFF  # save_loaded's value on the title and file-select screens
# The player's link to their Energy Shield: pointer, then handle (serial, pool type 2). The
# object repeats the handle at +0x1D8 and its own address at +0x3C8 (docs/re/memory.md,
# "Received Energy Shields").
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


class AccessDenied(GameError):
    """Windows refused to open the game (error 5): Zexion runs as administrator and the client
    does not."""


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


@dataclass(frozen=True)
class PlayerState:
    """The player state's fields a poll uses, from one read (Game.player_state)."""

    inventory: bytes  # one byte per item ID 0-50
    supplies: tuple[int, ...]  # as Game.supplies returns them
    energy: int  # current energy, as Game.energy
    # The player's map cell as the player update keeps it (docs/re/memory.md, "Other players'
    # items"): +0x107A, floor(x / 320), and +0x107B, floor((y - 8) / 224).
    cell: tuple[int, int]


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


def parse_player_state(span: bytes) -> PlayerState:
    """The fields of a read of the player state over PLAYER_SPAN."""
    start = PLAYER_SPAN[0]

    def i16(field: int) -> int:
        return struct.unpack_from("<h", span, field - start)[0]

    inv = INVENTORY_IN_PLAYER_STATE - start
    cell = PLAYER_CELL - start
    return PlayerState(
        inventory=bytes(span[inv : inv + len(ids.GAME_ITEMS)]),
        supplies=tuple(i16(f) for f in SUPPLY_FIELDS),
        energy=i16(ENERGY_NOW),
        cell=(span[cell], span[cell + 1]),
    )


def identify_build(header: bytes, known: Iterable[Build] = KNOWN_BUILDS) -> Build:
    """The known build with the PE image's TimeDateStamp and SizeOfImage."""
    try:
        h = pe.parse(header)
    except pe.PEError as exc:
        raise GameError(str(exc)) from exc
    stamp, size = h.timestamp, h.size_of_image
    for build in known:
        if build.timestamp == stamp and build.size_of_image == size:
            return build
    raise UnknownBuild(
        f"Zexion.exe build with timestamp {stamp:#x} and image size {size:#x} is not one "
        "docs/re/ has been checked on; refusing to touch it"
    )


def code_section(header: bytes) -> tuple[int, int]:
    """(RVA, size) of .text, from the first 4 KiB of a PE image."""
    try:
        text = pe.parse(header).section(".text")
    except pe.PEError as exc:
        raise GameError(str(exc)) from exc
    return text.va, text.vsize


@dataclass(frozen=True)
class Seed:
    """The loaded seed, from its randomizer section."""

    locations: tuple[tuple[int, int], ...]  # (x, y) per location, in table order
    items: bytes  # item ID per location


def parse_seed(blob: bytes) -> Seed:
    """Check the randomizer section looks right before anything uses it.

    154 distinct (x, y) cells on the map, sorted by y then x, then 4 zero bytes, then 154 item
    IDs from 0 to 50; with Secret Shuffle (setting t37 set) 155 cells, filling those 4 bytes,
    and 155 item IDs (docs/re/randocells.md, "The pool"). Anything else means the section is not
    where it was located, or no randomizer save is loaded.
    """
    if len(blob) != SECTION_SIZE:
        raise GameError(f"randomizer section is {len(blob)} bytes, expected {SECTION_SIZE}")
    n = LOCATIONS + bool(blob[OPTIONS_AT + SECRET_SHUFFLE])
    cells = tuple(struct.unpack_from("<HH", blob, 4 * i) for i in range(n))
    if any(not (0 <= x < MAP_SIZE and 0 <= y < MAP_SIZE) for x, y in cells):
        raise GameError("randomizer section: a location cell is off the map")
    if len(set(cells)) != n or list(cells) != sorted(cells, key=lambda c: (c[1], c[0])):
        raise GameError("randomizer section: location cells are not distinct and sorted")
    if any(blob[4 * n : TABLE_AT]):
        raise GameError("randomizer section: no zero gap after the location list")
    items = bytes(blob[TABLE_AT : TABLE_AT + n])
    if any(i not in ids.GAME_ITEMS for i in items):
        raise GameError("randomizer section: an item ID is out of range")
    return Seed(cells, items)


# --- the running game ---------------------------------------------------------------------


@dataclass
class Game:
    """An attached Zexion process with the targets it was attached with located (the
    client's, patterns.CLIENT_TARGETS, by default)."""

    pm: object
    base: int
    rva: dict[str, int]
    pid: int
    build: Build

    @classmethod
    def attach(cls, targets: Iterable[Target] = CLIENT_TARGETS) -> Game:
        """Attach to the running game and locate ``targets`` (the client's by default)."""
        try:
            pm = process.Process(PROCESS)
        except process.ProcessNotFound as exc:
            raise GameNotRunning(f"{PROCESS} is not running") from exc
        except process.ProcessAccessDenied as exc:
            raise AccessDenied(f"opening {PROCESS} failed: {exc}") from exc
        except process.ProcessError as exc:
            raise GameError(f"opening {PROCESS} failed: {exc}") from exc
        try:
            module = pm.module()
            if module is None or not module[0]:
                raise GameError(f"{PROCESS} is running but its module was not found")
            base = module[0]
            header = pm.read_bytes(base, 0x1000)
            build = identify_build(header)
            text_rva, text_size = code_section(header)
            code = pm.read_bytes(base + text_rva, text_size)
            rva = locate_all(code, text_rva, targets)
            return cls(pm, base, rva, pm.process_id, build)
        except GameError:
            pm.close_process()
            raise
        except Exception as exc:
            pm.close_process()
            raise GameError(f"attaching to {PROCESS} failed: {exc}") from exc

    def _alive(self) -> bool:
        return self.pm.alive()

    def _check_alive(self) -> None:
        if not self._alive():
            raise GameNotRunning(f"{PROCESS} (pid {self.pid}) has exited")

    def _failure(self, what: str, address: int, exc: Exception) -> GameError:
        if not self._alive():
            return GameNotRunning(f"{PROCESS} (pid {self.pid}) exited during {what}")
        return GameError(f"{what} at {address:#x} failed in a running game: {exc}")

    def code_checks(self, keys: Iterable[str], size: int) -> dict[str, bytes]:
        """The first ``size`` bytes of each located target among ``keys`` that lies in the
        game's code (.text), as the game holds them: what the helper compares before it uses
        cached addresses (client/offline.py)."""
        start, length = code_section(self._read_pointer(self.base, 0x1000))
        out = {}
        for key in keys:
            rva = self.rva.get(key)
            if rva is not None and start <= rva < start + length:
                out[key] = self._read_pointer(self.base + rva, size)
        return out

    # Every access goes through _read_pointer or _write, which check liveness first.
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
            self.pm.write_bytes(address, data)
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

    def map_grid(self) -> bytes:
        return self._read("map_grid", 0, MAP_SIZE * MAP_SIZE)

    def inventory(self) -> bytes:
        """One byte per item ID 0-50."""
        return self._read("inventory", 0, len(ids.GAME_ITEMS))

    def flag_bytes(self, first: int, count: int) -> bytes:
        """``count`` of the loaded game's flags from ``first``, in one read (0 to 0x9C3;
        docs/re/memory.md, "Keys, the gates and the key markers" and "The ending")."""
        if first < 0 or count <= 0 or first + count > FLAGS:
            raise ValueError(
                f"flags {first} to {first + count - 1} are outside the 0x{FLAGS:X} flags"
            )
        return self._read("flags", first, count)

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
        return self.player_state().supplies

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
        return self.player_state().energy

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

    def state(self, energy: int | None = None) -> str:
        """NO_SAVE, MENU, DEAD or IN_PLAY. Only IN_PLAY is a state to write items in.
        ``energy`` is the current energy if the caller has just read it (player_state)."""
        if self._read("save_loaded", 0, 1)[0] == NO_SAVE_LOADED:
            return NO_SAVE
        if self._read("menu_state", 0, 1)[0] != 0:
            return MENU
        if (self.energy() if energy is None else energy) <= 0:
            return DEAD
        return IN_PLAY

    def player_state(self) -> PlayerState:
        """The inventory, supplies, energy and the player's cell, read at once."""
        start, end = PLAYER_SPAN
        return parse_player_state(
            self._read("inventory", start - INVENTORY_IN_PLAYER_STATE, end - start)
        )

    def settled_state(self, gap: float = SETTLE_SECONDS) -> str:
        """``state()`` read twice, ``gap`` seconds apart; CHANGING if the two differ."""
        first = self.state()
        time.sleep(gap)
        second = self.state()
        return second if second == first else CHANGING

    def pickup_counter(self) -> int:
        """The ITEMS counter (16-bit): pickups, except items 27-31 and 38-45. Only the RE tools
        locate it (``patterns.TOOL_TARGETS``).

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

    def save_stamp(self) -> bytes:
        """The loaded slot (``current_slot``, 0-based, 4 bytes) and its block's bytes from
        SAVE_STAMP_AT: new after every save, its continue point the save point last used."""
        raw = self._read("current_slot", 0, 4)
        slot = struct.unpack("<i", raw)[0]
        if not 0 <= slot < SLOTS:
            raise GameError(f"the current slot reads {slot}")
        return raw + self._read("slot_array", slot * SLOT_STRIDE + SAVE_STAMP_AT, SAVE_STAMP_SIZE)

    def rando_slot_copies(self) -> bytes:
        """The game's copy of each slot's randomizer section (30 of SECTION_SIZE bytes, in
        the loaded section's layout; docs/re/memory.md, "The file select")."""
        return self._read("rando_slots", 0, SLOTS * SECTION_SIZE)

    def rando_blob(self) -> bytes:
        """The loaded save's randomizer section, as from file offset 0x12814."""
        return self._read("rando_blob", 0, SECTION_SIZE)

    def seed(self) -> Seed:
        """The loaded seed's locations and items, checked (``parse_seed``)."""
        return parse_seed(self.rando_blob())
