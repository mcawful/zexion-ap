"""client/pe.py against a synthetic image and against the Python running the tests."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import pytest
from fakes import pe_image

from client import pe


def test_the_header_fields_and_sections_are_read() -> None:
    header = pe.parse(pe_image())
    assert (header.machine, header.timestamp) == (0x8664, 0x6A3F82DE)
    assert header.size_of_image == 0x1204000
    assert header.dll_characteristics == 0x0160
    assert header.section(".text") == pe.Section(".text", 0x1000, 0x2000, 0x400, 0x2000)
    assert [s.name for s in header.sections] == [".text", ".rdata"]
    with pytest.raises(pe.PEError, match="no .pdata section"):
        header.section(".pdata")


def test_an_rva_maps_to_its_file_offset() -> None:
    data = bytearray(pe_image(length=0x2600))
    data[0x2410:0x2414] = b"abcd"
    header = pe.parse(bytes(data))
    assert header.file_offset(0x3010, 4) == 0x2410
    assert pe.read_rva(bytes(data), 0x3010, 4) == b"abcd"
    with pytest.raises(pe.PEError, match="in no section"):
        header.file_offset(0x5000)
    with pytest.raises(pe.PEError, match="in no section"):
        header.file_offset(0x31FE, 4)  # runs past the end of .rdata


def test_a_section_table_past_the_bytes_given_is_cut() -> None:
    data = pe_image()
    first = 0x80 + 24 + 0xF0
    assert [s.name for s in pe.parse(data[: first + 40]).sections] == [".text"]


def test_other_bytes_are_refused() -> None:
    for data in (b"", bytes(0x1000), b"MZ" + bytes(0x3E) + struct.pack("<I", 0x80)):
        with pytest.raises(pe.PEError, match="not a PE image"):
            pe.parse(data)


@pytest.mark.skipif(sys.platform != "win32", reason="a Windows executable to read")
def test_the_running_python_executable_parses() -> None:
    data = Path(sys.executable).read_bytes()
    header = pe.parse(data)
    text = header.section(".text")
    assert text.vsize > 0 and header.size_of_image > text.va
    assert header.file_offset(text.va) == text.raw
