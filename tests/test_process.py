"""client/process.py against this test's own process (Windows only)."""

from __future__ import annotations

import ctypes
import os
import sys
from pathlib import Path

import pytest

pytestmark = pytest.mark.skipif(sys.platform != "win32", reason="the Windows API")

from client import process  # noqa: E402

EXE = Path(sys.executable).name


def test_the_running_process_is_found_by_its_executable() -> None:
    assert process.find_pid(EXE) is not None
    assert process.find_pid("no-such-program-zexion.exe") is None


def test_a_loaded_module_is_found_with_its_size() -> None:
    p = process.Process(EXE, os.getpid())
    try:
        listed = process.modules(p.process_handle)
        base, size = listed["kernel32.dll"]
        assert base == ctypes.windll.kernel32._handle and size > 0  # a module's handle: its base
        get_module = ctypes.WinDLL("kernel32").GetModuleHandleW
        get_module.restype = ctypes.c_void_p
        exe = p.module()  # the executable's own, its name in any case
        assert exe == listed[EXE.lower()] and exe[0] == get_module(None)
        assert p.module("KERNEL32.DLL") == (base, size) and p.module("no-such.dll") is None
    finally:
        p.close_process()


class FakeKernel32:
    """The module listing's calls, failing ``fails`` times first as a busy loader does."""

    def __init__(self, fails: int, listed: dict[int, tuple[str, int, int]]) -> None:
        self.fails, self.listed, self.tries = fails, listed, 0

    def K32EnumProcessModulesEx(self, handle, handles, size, needed, flag) -> int:
        self.tries += 1
        if self.fails:
            self.fails -= 1
            ctypes.set_last_error(299)  # ERROR_PARTIAL_COPY
            return 0
        for i, module in enumerate(self.listed):
            handles[i] = module
        needed._obj.value = len(self.listed) * ctypes.sizeof(ctypes.c_void_p)
        return 1

    def K32GetModuleBaseNameW(self, handle, module, name, length) -> int:
        if self.listed[module][0] is None:
            return 0  # unloaded meanwhile
        name.value = self.listed[module][0]
        return len(name.value)

    def K32GetModuleInformation(self, handle, module, info, size) -> int:
        _, base, image_size = self.listed[module]
        info._obj.lpBaseOfDll, info._obj.SizeOfImage = base, image_size
        return 1


def test_a_busy_loader_is_tried_again_and_a_listing_that_keeps_failing_is_an_error(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(process.time, "sleep", lambda seconds: None)
    listed = {0x10: ("Zexion.exe", 0x7FF600000000, 0x1204000), 0x20: (None, 0, 0)}
    fake = FakeKernel32(fails=3, listed=listed)
    monkeypatch.setattr(process, "kernel32", lambda: fake)
    assert process.modules(1) == {"zexion.exe": (0x7FF600000000, 0x1204000)}
    assert fake.tries == 4
    fake.fails = process.MODULE_TRIES
    with pytest.raises(process.ProcessError, match=r"listing the process's modules.*error 299"):
        process.modules(1)


def test_reads_and_writes_go_through_and_a_bad_address_fails() -> None:
    buffer = ctypes.create_string_buffer(b"zexion--", 8)
    address = ctypes.addressof(buffer)
    p = process.Process(EXE, os.getpid())
    try:
        assert p.read_bytes(address, 8) == b"zexion--"
        p.write_bytes(address, b"AP")
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


def test_access_denied_is_its_own_error() -> None:
    ctypes.set_last_error(process.ERROR_ACCESS_DENIED)  # a game run as administrator
    assert isinstance(process._error("opening Zexion.exe"), process.ProcessAccessDenied)
    ctypes.set_last_error(87)
    assert type(process._error("opening Zexion.exe")) is process.ProcessError
