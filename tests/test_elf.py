"""client/elf.py against a synthetic ELF file and against the Python running the tests."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest
from fakes import ELF_BUILD_ID, elf_image

from client import elf


def test_the_header_and_sections_are_read() -> None:
    image = elf.parse(elf_image())
    assert (image.type, image.machine, image.entry) == (elf.ET_EXEC, elf.EM_X86_64, 0x40C670)
    assert [s.name for s in image.sections] == [
        "",
        ".tbss",
        ".data",
        ".bss",
        ".note.gnu.build-id",
        ".symtab",
        ".strtab",
        ".shstrtab",
    ]
    assert image.section(".bss").size == 0x1000000
    with pytest.raises(elf.ELFError, match="no .text section"):
        image.section(".text")


def test_the_build_id_is_read_from_its_note() -> None:
    assert elf.parse(elf_image()).build_id() == ELF_BUILD_ID.hex()
    assert elf.parse(elf_image(build_id=None)).build_id() is None


def test_a_symbol_is_found_by_its_exact_name() -> None:
    image = elf.parse(elf_image())
    state = image.symbol("gGameState")
    assert (state.value, state.size, state.type) == (0x9E7840, 0x8CCBF8, elf.STT_OBJECT)
    assert image.symbol("_Z10PlaySoundi").type == elf.STT_FUNC  # C++ by its mangled name
    with pytest.raises(elf.ELFError, match="no symbol PlaySound"):
        image.symbol("PlaySound")


def test_an_import_is_not_a_definition() -> None:
    image = elf.parse(elf_image())
    assert [s.defined for s in image.symbols() if s.name == "SDL_GL_SwapWindow"] == [False]
    with pytest.raises(elf.ELFError, match="no symbol SDL_GL_SwapWindow"):
        image.symbol("SDL_GL_SwapWindow")


def test_two_symbols_of_one_name_at_different_addresses_are_refused() -> None:
    symbols = [("gKeys", 0x12F8980, 1080, 1, True), ("gKeys", 0x12F9000, 1080, 1, True)]
    with pytest.raises(elf.ELFError, match="2 different symbols named gKeys"):
        elf.parse(elf_image(symbols=symbols)).symbol("gKeys")
    same = [("gKeys", 0x12F8980, 1080, 1, True)] * 2  # .symtab and .dynsym agreeing
    assert elf.parse(elf_image(symbols=same)).symbol("gKeys").value == 0x12F8980


def test_bytes_are_read_by_address_but_not_from_bss() -> None:
    image = elf.parse(elf_image(data_bytes=b"zexion data"))
    assert image.read(0x6BA0E0 + 7, 4) == b"data"
    with pytest.raises(elf.ELFError, match="in .bss, which has no bytes"):
        image.read(0x9E7840, 4)
    with pytest.raises(elf.ELFError, match="in no loaded section"):
        image.read(0x100, 4)


def test_thread_local_bss_overlapping_data_does_not_hide_it() -> None:
    image = elf.parse(elf_image(data_bytes=b"zexion data"))
    assert image.section(".tbss").addr == image.section(".data").addr
    assert image.read(0x6BA0E0, 6) == b"zexion"


def test_a_section_cut_short_by_the_file_is_refused() -> None:
    data = elf_image(data_bytes=b"zexion data")
    image = elf.parse(data)
    data_section = image.section(".data")
    cut = elf.Image(image.type, image.machine, image.entry, image.sections,
                    data[: data_section.offset + 3])  # fmt: skip
    with pytest.raises(elf.ELFError, match=".data runs past the end of the file"):
        cut.read(0x6BA0E0, 6)


def test_a_symbol_table_of_an_odd_size_reads_only_whole_entries() -> None:
    image = elf.parse(elf_image())
    table = image.section(".symtab")
    odd = elf.Section(table.name, table.type, table.flags, table.addr, table.offset,
                      table.size + 7, table.link, table.entsize)  # fmt: skip
    sections = tuple(odd if s is table else s for s in image.sections)
    oddly = elf.Image(image.type, image.machine, image.entry, sections, image.data)
    assert len(oddly.symbols()) == len(image.symbols())


@pytest.mark.parametrize(
    ("data", "said"),
    [
        (b"MZ" + bytes(100), "not an ELF file"),
        (b"\x7fELF\x01\x01" + bytes(100), "not a 64-bit little-endian"),
        (elf_image()[:-8], "the section table runs past"),
    ],
    ids=["pe", "32-bit", "cut"],
)
def test_other_files_are_refused(data: bytes, said: str) -> None:
    with pytest.raises(elf.ELFError, match=said):
        elf.parse(data)


@pytest.mark.skipif(not sys.platform.startswith("linux"), reason="Linux runs ELF programs")
def test_the_python_running_the_tests_parses() -> None:
    image = elf.parse(Path(sys.executable).resolve().read_bytes())
    assert image.type in (elf.ET_EXEC, elf.ET_DYN)
    assert image.section(".text").size > 0
