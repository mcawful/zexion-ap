"""Reading and writing another process's memory with the Windows API alone (ctypes).

What the client needs of a running Zexion: open it by its executable's name, find where its
module is loaded, and read and write bytes. Archipelago's Windows release bundles no
third-party library for this, so the client does it itself, and the RE tools (tools/memscan.py)
and the tests use it too.

``Process`` has the names ``game.Game`` and its tests' fakes use (``read_bytes``,
``write_bytes``, ``process_handle``, ``process_id``, ``close_process``; pymem's, which the client
once used), and ``alive``, which ``game.Game`` checks before every read and write
(``alive``, for a bare handle, is the native helper's loader's too).

"""

from __future__ import annotations

import ctypes
import functools
import sys
from ctypes import wintypes


class ProcessError(OSError):
    """The process could not be found, opened, read or written."""


class ProcessNotFound(ProcessError):
    """No process of that name is running."""


PROCESS_ALL_ACCESS = 0x1F0FFF  # the helper's loader needs threads too
TH32CS_SNAPPROCESS, TH32CS_SNAPMODULE, TH32CS_SNAPMODULE32 = 0x2, 0x8, 0x10
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
ERROR_BAD_LENGTH, ERROR_NO_MORE_FILES = 24, 18
SNAPSHOT_TRIES = 10  # a module snapshot can fail while the process's loader is busy
MAX_PATH = 260
MAX_MODULE_NAME32 = 255
STILL_ACTIVE = 259  # GetExitCodeProcess's code for a process that has not exited


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("th32DefaultHeapID", ctypes.c_size_t),
        ("th32ModuleID", wintypes.DWORD),
        ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD),
        ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", wintypes.DWORD),
        ("szExeFile", ctypes.c_wchar * MAX_PATH),
    ]


class MODULEENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("th32ModuleID", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("GlblcntUsage", wintypes.DWORD),
        ("ProccntUsage", wintypes.DWORD),
        ("modBaseAddr", ctypes.c_void_p),
        ("modBaseSize", wintypes.DWORD),
        ("hModule", wintypes.HMODULE),
        ("szModule", ctypes.c_wchar * (MAX_MODULE_NAME32 + 1)),
        ("szExePath", ctypes.c_wchar * MAX_PATH),
    ]


@functools.cache
def _kernel32():
    if sys.platform != "win32":  # pragma: no cover - the client runs on Windows only
        raise ProcessError("reading another process needs Windows")
    k = ctypes.WinDLL("kernel32", use_last_error=True)
    k.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    k.GetExitCodeProcess.restype = wintypes.BOOL
    k.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    k.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    k.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
    k.Process32FirstW.restype = wintypes.BOOL
    k.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
    k.Process32NextW.restype = wintypes.BOOL
    k.Module32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(MODULEENTRY32W)]
    k.Module32FirstW.restype = wintypes.BOOL
    k.Module32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(MODULEENTRY32W)]
    k.Module32NextW.restype = wintypes.BOOL
    k.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    k.OpenProcess.restype = wintypes.HANDLE
    k.CloseHandle.argtypes = [wintypes.HANDLE]
    k.CloseHandle.restype = wintypes.BOOL
    k.ReadProcessMemory.argtypes = [
        wintypes.HANDLE,
        wintypes.LPCVOID,
        wintypes.LPVOID,
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_size_t),
    ]
    k.ReadProcessMemory.restype = wintypes.BOOL
    k.WriteProcessMemory.argtypes = [
        wintypes.HANDLE,
        wintypes.LPVOID,
        wintypes.LPCVOID,
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_size_t),
    ]
    k.WriteProcessMemory.restype = wintypes.BOOL
    return k


def _error(what: str) -> ProcessError:
    code = ctypes.get_last_error()
    return ProcessError(f"{what} failed: {ctypes.FormatError(code).strip()} (error {code})")


def alive(process_handle: int) -> bool:
    """Whether the process behind an open handle is still running."""
    code = wintypes.DWORD()
    ok = _kernel32().GetExitCodeProcess(process_handle, ctypes.byref(code))
    return bool(ok) and code.value == STILL_ACTIVE


def find_pid(name: str) -> int | None:
    """The id of a running process whose executable is ``name`` (case ignored), or None."""
    k = _kernel32()
    snap = k.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snap in (None, INVALID_HANDLE_VALUE):
        raise _error("listing processes")
    try:
        entry = PROCESSENTRY32W()
        entry.dwSize = ctypes.sizeof(entry)
        ok = k.Process32FirstW(snap, ctypes.byref(entry))
        while ok:
            if entry.szExeFile.lower() == name.lower():
                return int(entry.th32ProcessID)
            ok = k.Process32NextW(snap, ctypes.byref(entry))
        if ctypes.get_last_error() != ERROR_NO_MORE_FILES:
            raise _error("listing processes")  # not known to be absent
        return None
    finally:
        k.CloseHandle(snap)


def module_base(pid: int, name: str) -> tuple[int, int] | None:
    """Where module ``name`` (case ignored) is loaded in process ``pid``, and its size."""
    import time

    k = _kernel32()
    for _ in range(SNAPSHOT_TRIES):
        snap = k.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
        if snap not in (None, INVALID_HANDLE_VALUE):
            break
        if ctypes.get_last_error() != ERROR_BAD_LENGTH:
            raise _error("listing the process's modules")
        time.sleep(0.05)  # the loader is busy (the game starting): as Windows advises
    else:
        raise _error("listing the process's modules")
    try:
        entry = MODULEENTRY32W()
        entry.dwSize = ctypes.sizeof(entry)
        ok = k.Module32FirstW(snap, ctypes.byref(entry))
        while ok:
            if entry.szModule.lower() == name.lower():
                return int(entry.modBaseAddr or 0), int(entry.modBaseSize)
            ok = k.Module32NextW(snap, ctypes.byref(entry))
        return None
    finally:
        k.CloseHandle(snap)


class Process:
    """An open handle on a running process, by its executable's name."""

    def __init__(self, name: str, pid: int | None = None):
        self._k = _kernel32()
        pid = find_pid(name) if pid is None else pid
        if pid is None:
            raise ProcessNotFound(f"{name} is not running")
        handle = self._k.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
        if not handle:
            raise _error(f"opening {name} (pid {pid})")
        self.name = name
        self.process_id = pid
        self.process_handle = handle

    def module(self, name: str | None = None) -> tuple[int, int] | None:
        """Where a module is loaded (the executable's own by default), and its size."""
        return module_base(self.process_id, name or self.name)

    def alive(self) -> bool:
        """Whether the process is still running (False once the handle is closed)."""
        return self.process_handle is not None and alive(self.process_handle)

    def read_bytes(self, address: int, size: int) -> bytes:
        buffer = ctypes.create_string_buffer(size)
        done = ctypes.c_size_t(0)
        if not self._k.ReadProcessMemory(
            self.process_handle, ctypes.c_void_p(address), buffer, size, ctypes.byref(done)
        ):
            raise _error(f"reading {size} bytes at {address:#x}")
        return buffer.raw[: done.value]

    def write_bytes(self, address: int, data: bytes, size: int) -> None:
        done = ctypes.c_size_t(0)
        if not self._k.WriteProcessMemory(
            self.process_handle,
            ctypes.c_void_p(address),
            bytes(data[:size]),
            size,
            ctypes.byref(done),
        ):
            raise _error(f"writing {size} bytes at {address:#x}")
        if done.value != size:
            raise ProcessError(f"wrote {done.value} of {size} bytes at {address:#x}")

    def close_process(self) -> None:
        if self.process_handle:
            self._k.CloseHandle(self.process_handle)
            self.process_handle = None
