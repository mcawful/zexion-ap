"""client/process.py against this test's own process (Windows only)."""

from __future__ import annotations

import ctypes
import os
import sys
from pathlib import Path

import pytest

pytestmark = pytest.mark.skipif(sys.platform != "win32", reason="the Windows API")

import process  # noqa: E402

EXE = Path(sys.executable).name


def test_the_running_process_is_found_by_its_executable() -> None:
    assert process.find_pid(EXE) is not None
    assert process.find_pid("no-such-program-zexion.exe") is None


def test_a_loaded_module_is_found_with_its_size() -> None:
    found = process.module_base(os.getpid(), "kernel32.dll")
    assert found is not None
    base, size = found
    assert base and size > 0


def test_reads_and_writes_go_through_and_a_bad_address_fails() -> None:
    buffer = ctypes.create_string_buffer(b"zexion--", 8)
    address = ctypes.addressof(buffer)
    p = process.Process(EXE, os.getpid())
    try:
        assert p.read_bytes(address, 8) == b"zexion--"
        p.write_bytes(address, b"AP", 2)
        assert buffer.raw[:8] == b"APxion--"
        assert p.module() is not None
        with pytest.raises(process.ProcessError):
            p.read_bytes(0x10, 4)
    finally:
        p.close_process()


def test_a_missing_process_is_not_found() -> None:
    with pytest.raises(process.ProcessNotFound):
        process.Process("no-such-program-zexion.exe")


def test_a_running_process_is_alive_until_its_handle_is_closed() -> None:
    p = process.Process(EXE, os.getpid())
    try:
        assert p.alive()
    finally:
        p.close_process()
    assert not p.alive()
