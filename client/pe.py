"""The few PE header fields the client and the tools read, with the standard library only.

``parse`` reads the start of a PE image, either a file (Zexion.exe or a copy) or the module as
the game has it loaded (its first 4 KiB hold the whole header): the COFF machine and timestamp,
the optional header's SizeOfImage and DllCharacteristics, and the section table. The offsets
are the PE format's own (Microsoft's PE/COFF specification), the same for PE32 and PE32+ up to
DllCharacteristics. A section table that runs past the bytes given is cut where they end, so
the first 4 KiB of a loaded image are enough for the sections that fit in them.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

PE_OFFSET_AT = 0x3C  # e_lfanew in the DOS header
COFF_SIZE = 24  # "PE\0\0" and the COFF file header
SECTION_SIZE = 40


class PEError(ValueError):
    """Not a PE image, or not the part of one asked for."""


@dataclass(frozen=True)
class Section:
    name: str
    va: int  # VirtualAddress, an RVA
    vsize: int  # VirtualSize
    raw: int  # PointerToRawData, the file offset
    rsize: int  # SizeOfRawData


@dataclass(frozen=True)
class Header:
    machine: int
    timestamp: int  # TimeDateStamp, seconds since 1970 (UTC)
    size_of_image: int
    dll_characteristics: int
    sections: tuple[Section, ...]

    def section(self, name: str) -> Section:
        for section in self.sections:
            if section.name == name:
                return section
        raise PEError(f"no {name} section")

    def file_offset(self, rva: int, size: int = 1) -> int:
        """The file offset of ``size`` bytes at ``rva``, which must lie in one section."""
        for s in self.sections:
            if s.va <= rva and rva + size <= s.va + max(s.vsize, s.rsize):
                return s.raw + rva - s.va
        raise PEError(f"RVA {rva:#x} is in no section of the image")


def parse(data: bytes) -> Header:
    """The header of the PE image that ``data`` starts with."""
    if len(data) < PE_OFFSET_AT + 4:
        raise PEError("not a PE image")
    pe = struct.unpack_from("<I", data, PE_OFFSET_AT)[0]
    if data[pe : pe + 4] != b"PE\0\0" or len(data) < pe + COFF_SIZE + 72:
        raise PEError("not a PE image")
    machine, count, timestamp = struct.unpack_from("<HHI", data, pe + 4)
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    size_of_image = struct.unpack_from("<I", data, pe + COFF_SIZE + 56)[0]
    dll_characteristics = struct.unpack_from("<H", data, pe + COFF_SIZE + 70)[0]
    first = pe + COFF_SIZE + optional_size
    sections = []
    for i in range(count):
        at = first + SECTION_SIZE * i
        if at + SECTION_SIZE > len(data):
            break
        name = data[at : at + 8].rstrip(b"\0").decode("latin-1")
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, at + 8)
        sections.append(Section(name, va, vsize, raw, rsize))
    return Header(machine, timestamp, size_of_image, dll_characteristics, tuple(sections))


def read_rva(data: bytes, rva: int, size: int) -> bytes:
    """``size`` bytes at ``rva`` from the file of a PE image."""
    at = parse(data).file_offset(rva, size)
    return data[at : at + size]
