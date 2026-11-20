"""The few ELF fields the tools read from Zexion's Linux build (64-bit little-endian only): the
file type, the machine, the entry point, the sections, the GNU build id, the symbol tables, and
the symbol versions a file needs of each library (the release's check of the Linux helper's glibc:
tools/build_apworld.py).

Zexion's Linux binary is a position-dependent executable (``ET_EXEC``, loaded at its link
addresses) that keeps its symbol table (``docs/re/builds.md``, "B1 on Linux"), so a global's
address is its symbol's value: ``Image.symbol``.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

MAGIC = b"\x7fELF"
ELFCLASS64 = 2
ELFDATA2LSB = 1
ET_EXEC = 2  # a fixed-address executable
ET_DYN = 3  # a shared object, or a position-independent executable
EM_X86_64 = 62
SHT_SYMTAB = 2
SHT_NOTE = 7
SHT_NOBITS = 8
SHT_DYNSYM = 11
SHT_GNU_VERNEED = 0x6FFFFFFE  # .gnu.version_r: the symbol versions needed of each library
SHF_ALLOC = 0x2  # loaded when the program runs
SHF_TLS = 0x400  # per thread: its sh_addr is a template's, overlapping other sections
NT_GNU_BUILD_ID = 3
HEADER_SIZE = 64
SECTION_SIZE = 64
SYMBOL_SIZE = 24
STT_OBJECT = 1
STT_FUNC = 2


class ELFError(ValueError):
    """Not a 64-bit little-endian ELF file, or not the part of one asked for."""


@dataclass(frozen=True)
class Section:
    name: str
    type: int
    flags: int  # sh_flags
    addr: int  # sh_addr: where it is loaded, 0 when it is not
    offset: int  # sh_offset: where it is in the file
    size: int
    link: int  # sh_link: for a symbol table, the index of its string table
    entsize: int


@dataclass(frozen=True)
class Symbol:
    name: str
    value: int  # for an ET_EXEC, the address
    size: int
    type: int  # STT_OBJECT, STT_FUNC, ...
    defined: bool  # st_shndx is not SHN_UNDEF


@dataclass(frozen=True)
class Image:
    type: int  # e_type
    machine: int
    entry: int
    sections: tuple[Section, ...]
    data: bytes

    def section(self, name: str) -> Section:
        for section in self.sections:
            if section.name == name:
                return section
        raise ELFError(f"no {name} section")

    def build_id(self) -> str | None:
        """The GNU build id as hex, or None when the file has none."""
        for section in self.sections:
            if section.type != SHT_NOTE:
                continue
            note = self.data[section.offset : section.offset + section.size]
            at = 0
            while at + 12 <= len(note):
                namesz, descsz, kind = struct.unpack_from("<III", note, at)
                name_end = at + 12 + namesz
                desc_at = at + 12 + _align4(namesz)
                if note[at + 12 : name_end].rstrip(b"\0") == b"GNU" and kind == NT_GNU_BUILD_ID:
                    return note[desc_at : desc_at + descsz].hex()
                at = desc_at + _align4(descsz)
        return None

    def symbols(self) -> list[Symbol]:
        """Every symbol of ``.symtab`` and ``.dynsym``, in that order."""
        found: list[Symbol] = []
        for section in self.sections:
            if section.type not in (SHT_SYMTAB, SHT_DYNSYM) or section.entsize != SYMBOL_SIZE:
                continue
            if section.link >= len(self.sections):
                raise ELFError(f"{section.name} names no string table")
            strings = self.sections[section.link]
            names = self.data[strings.offset : strings.offset + strings.size]
            end = section.offset + section.size - section.size % SYMBOL_SIZE
            for at in range(section.offset, end, SYMBOL_SIZE):
                if at + SYMBOL_SIZE > len(self.data):
                    raise ELFError(f"{section.name} runs past the end of the file")
                name_at, info, _other, shndx, value, size = struct.unpack_from(
                    "<IBBHQQ", self.data, at
                )
                end = names.find(b"\0", name_at)
                name = names[name_at : end if end >= 0 else len(names)].decode("utf-8", "replace")
                found.append(Symbol(name, value, size, info & 0xF, shndx != 0))
        return found

    def symbol(self, name: str) -> Symbol:
        """The one defined symbol called ``name`` (a mangled name for C++). Raises when there is
        none, or more than one with different values."""
        matches = {(s.value, s.size): s for s in self.symbols() if s.name == name and s.defined}
        if not matches:
            raise ELFError(f"no symbol {name}")
        if len(matches) > 1:
            raise ELFError(f"{len(matches)} different symbols named {name}")
        return next(iter(matches.values()))

    def version_needs(self) -> dict[str, set[str]]:
        """The symbol versions the file needs, by the library that defines them
        (``{"libc.so.6": {"GLIBC_2.2.5", ...}}``), from ``.gnu.version_r``: what the dynamic
        loader checks before it maps the file. Empty when the file needs none."""
        needs: dict[str, set[str]] = {}
        for section in self.sections:
            if section.type != SHT_GNU_VERNEED:
                continue
            if section.link >= len(self.sections):
                raise ELFError(f"{section.name} names no string table")
            strings = self.sections[section.link]
            names = self.data[strings.offset : strings.offset + strings.size]
            start, end = section.offset, section.offset + section.size
            at, seen = start, 0
            while seen < 4096:  # a chain longer than any linker writes is a damaged file
                seen += 1
                if at + 16 > min(end, len(self.data)):
                    raise ELFError(f"{section.name} runs past its end")
                _version, count, file_at, aux, following = struct.unpack_from(
                    "<HHIII", self.data, at
                )
                versions = needs.setdefault(_string(names, file_at, section.name), set())
                entry = at + aux
                for _ in range(count):
                    if entry + 16 > min(end, len(self.data)):
                        raise ELFError(f"{section.name} runs past its end")
                    _hash, _flags, _other, name_at, after = struct.unpack_from(
                        "<IHHII", self.data, entry
                    )
                    versions.add(_string(names, name_at, section.name))
                    if not after:
                        break
                    entry += after
                if not following:
                    break
                at += following
        return needs

    def function_at(self, addr: int) -> tuple[Symbol, int] | None:
        """The defined function whose bytes hold ``addr``, and ``addr``'s offset into it; None
        when no function symbol covers it."""
        for sym in self.symbols():
            if sym.type == STT_FUNC and sym.defined and sym.value <= addr < sym.value + sym.size:
                return sym, addr - sym.value
        return None

    def read(self, addr: int, size: int) -> bytes:
        """``size`` bytes at load address ``addr`` from the file. Bytes in ``.bss`` (no file
        data: zero until the game runs) are refused rather than read as zeros."""
        for s in self.sections:
            if not s.flags & SHF_ALLOC or s.flags & SHF_TLS:
                continue
            if s.addr and s.addr <= addr and addr + size <= s.addr + s.size:
                if s.type == SHT_NOBITS:
                    raise ELFError(f"{addr:#x} is in {s.name}, which has no bytes in the file")
                at = s.offset + addr - s.addr
                found = self.data[at : at + size]
                if len(found) != size:
                    raise ELFError(f"{s.name} runs past the end of the file")
                return found
        raise ELFError(f"{addr:#x} is in no loaded section")


def _align4(n: int) -> int:
    return (n + 3) & ~3


def _string(names: bytes, at: int, where: str) -> str:
    """The NUL-ended string at ``at`` in the string table ``names`` (``where``'s)."""
    end = names.find(b"\0", at)
    if at >= len(names) or end < 0:
        raise ELFError(f"{where} names a string past its table")
    return names[at:end].decode("utf-8", "replace")


def parse(data: bytes) -> Image:
    """The ELF image ``data`` holds."""
    if len(data) < HEADER_SIZE or data[:4] != MAGIC:
        raise ELFError("not an ELF file")
    if data[4] != ELFCLASS64 or data[5] != ELFDATA2LSB:
        raise ELFError("not a 64-bit little-endian ELF file")
    kind, machine, _version, entry = struct.unpack_from("<HHIQ", data, 16)
    shoff = struct.unpack_from("<Q", data, 40)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 58)
    if shnum and shentsize != SECTION_SIZE:
        raise ELFError(f"section header size {shentsize}, not {SECTION_SIZE}")
    if shoff + shnum * SECTION_SIZE > len(data):
        raise ELFError("the section table runs past the end of the file")
    raw = []
    for i in range(shnum):
        name_at, stype, flags, addr, offset, size, link, _info, _align, entsize = (
            struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * SECTION_SIZE)
        )
        raw.append((name_at, stype, flags, addr, offset, size, link, entsize))
    names = b""
    if shnum and shstrndx < shnum:
        _, _, _, _, offset, size, _, _ = raw[shstrndx]
        names = data[offset : offset + size]
    sections = []
    for name_at, stype, flags, addr, offset, size, link, entsize in raw:
        end = names.find(b"\0", name_at)
        name = names[name_at : end if end >= 0 else len(names)].decode("ascii", "replace")
        sections.append(Section(name, stype, flags, addr, offset, size, link, entsize))
    return Image(kind, machine, entry, tuple(sections), data)
