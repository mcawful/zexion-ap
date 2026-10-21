"""Find and access Zexion's game state in the running process, by byte pattern.

Nothing here uses a fixed address. On attach, the game's build is identified from its PE
header and refused unless it is in ``patterns.KNOWN_BUILDS``; then every target in
``patterns.TARGETS`` is located by scanning the code section of Zexion.exe as loaded. Each
pattern must match exactly once (overlapping matches count) and all of a target's patterns
must lead to the same address, or ``PatternError`` is raised and nothing is read or written.

Every read and write first checks that the attached process is still running. A failure is
``GameNotRunning`` only when the process has exited; any other failure is a ``GameError``
naming the address, because it means something is wrong with what was located.

Item writes are compare-and-set (``set_item``): the caller says what it expects the byte to
hold, the module re-reads it just before writing and refuses if it differs, and reads the
write back. That cannot see a later reload of the inventory (an area load, a death, loading
a save): the delivery layer must confirm items stuck by other means.

Not handled yet: whether the game is in play, loading, paused or dead. A caller must not
write while it is not in play; there is no check for it here yet.
"""

from __future__ import annotations

import re
import struct
from collections.abc import Iterable
from dataclasses import dataclass

from patterns import KNOWN_BUILDS, TARGETS, Build, Pattern, Target

PROCESS = "Zexion.exe"
MAP_SIZE = 54
COLLECTED = 0x1B  # map cell value once its item is collected
ITEM_IDS = range(0, 51)  # ItemNames.txt: 0 Energy Tank .. 50 Energy Shield
# Item 50's byte may also be what the logic treats as code 50 (docs/re/save-format.md); until
# that is measured, the module does not write it.
WRITABLE_ITEMS = range(0, 50)
LOCATIONS = 154
# The loaded save's randomizer section, as from file offset 0x12814 (docs/re/save-format.md).
RANDO_BLOB_SIZE = 0x908
ITEM_TABLE_AT = 0x26C  # file 0x12A80 - 0x12814


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
    if pattern.kind != "rip":
        # An image-base displacement is only an address if a register holds the image base,
        # which a pattern cannot show; such patterns are refused rather than trusted.
        raise PatternError(f"pattern kind {pattern.kind!r} is not accepted, only 'rip'")
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
        self._check_alive()
        address = self.base + self.rva[target] + offset
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
            raise GameError(f"read-back of {target}+{offset:#x} did not match what was written")

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

    def set_item(self, item: int, expected: int, value: int) -> None:
        """Compare-and-set an item's inventory byte, then read it back.

        Refuses (``GameError``) if the byte does not hold ``expected`` just before the write,
        so a grant cannot overwrite a change the game made since the caller looked. The
        caller decides when writing is safe; see the module docstring.
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

    def pickup_counter(self) -> int:
        """The ITEMS counter: locations collected (16-bit)."""
        return struct.unpack("<H", self._read("pickup_counter", 0, 2))[0]

    def rando_blob(self) -> bytes:
        """The loaded save's randomizer section, as from file offset 0x12814."""
        return self._read("rando_blob", 0, RANDO_BLOB_SIZE)

    def seed(self) -> Seed:
        """The loaded seed's locations and items, checked (``parse_seed``)."""
        return parse_seed(self.rando_blob())
