"""Reading and writing another process's memory with ctypes alone: Archipelago's Windows
release bundles no library for it. ``kernel32()`` is the one binding of kernel32's functions,
client/native.py's included.
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


class ProcessAccessDenied(ProcessError):
    """Windows refused access (error 5): opening a process that runs as administrator from one
    that does not, say."""


PROCESS_ALL_ACCESS = 0x1F0FFF  # the helper's loader needs threads too
TH32CS_SNAPPROCESS, TH32CS_SNAPMODULE, TH32CS_SNAPMODULE32 = 0x2, 0x8, 0x10
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
ERROR_ACCESS_DENIED, ERROR_BAD_LENGTH, ERROR_NO_MORE_FILES = 5, 24, 18
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
def kernel32():
    """kernel32, with the argument and result types of every function the client calls."""
    if sys.platform != "win32":  # pragma: no cover - the client runs on Windows only
        raise ProcessError("reading another process needs Windows")
    k = ctypes.WinDLL("kernel32", use_last_error=True)
    h, b, d, p, s = wintypes.HANDLE, wintypes.BOOL, wintypes.DWORD, ctypes.c_void_p, ctypes.c_size_t
    sig = {
        # Processes, their modules and their memory.
        "GetExitCodeProcess": (b, [h, ctypes.POINTER(d)]),
        "CreateToolhelp32Snapshot": (h, [d, d]),
        "Process32FirstW": (b, [h, ctypes.POINTER(PROCESSENTRY32W)]),
        "Process32NextW": (b, [h, ctypes.POINTER(PROCESSENTRY32W)]),
        "Module32FirstW": (b, [h, ctypes.POINTER(MODULEENTRY32W)]),
        "Module32NextW": (b, [h, ctypes.POINTER(MODULEENTRY32W)]),
        "OpenProcess": (h, [d, b, d]),
        "CloseHandle": (b, [h]),
        "ReadProcessMemory": (b, [h, wintypes.LPCVOID, wintypes.LPVOID, s, ctypes.POINTER(s)]),
        "WriteProcessMemory": (b, [h, wintypes.LPVOID, wintypes.LPCVOID, s, ctypes.POINTER(s)]),
        # The helper's loader (client/native.py).
        "VirtualAllocEx": (p, [h, p, s, d, d]),
        "VirtualFreeEx": (b, [h, p, s, d]),
        "GetModuleHandleW": (h, [wintypes.LPCWSTR]),
        "GetProcAddress": (p, [h, ctypes.c_char_p]),
        "CreateRemoteThread": (h, [h, p, s, p, p, d, p]),
        "WaitForSingleObject": (d, [h, d]),
        "CreateMutexW": (h, [p, b, wintypes.LPCWSTR]),
        "ReleaseMutex": (b, [h]),
        # The helper's pipe (client/native.py).
        "CreateFileW": (h, [wintypes.LPCWSTR, d, d, p, d, d, h]),
        "SetNamedPipeHandleState": (b, [h, ctypes.POINTER(d), p, p]),
        "GetNamedPipeServerProcessId": (b, [h, ctypes.POINTER(wintypes.ULONG)]),
        "WriteFile": (b, [h, p, d, ctypes.POINTER(d), p]),
        "ReadFile": (b, [h, p, d, ctypes.POINTER(d), p]),
        "PeekNamedPipe": (b, [h, p, d, p, ctypes.POINTER(d), p]),
    }
    for name, (restype, argtypes) in sig.items():
        fn = getattr(k, name)
        fn.restype, fn.argtypes = restype, argtypes
    return k


def _error(what: str) -> ProcessError:
    code = ctypes.get_last_error()
    kind = ProcessAccessDenied if code == ERROR_ACCESS_DENIED else ProcessError
    return kind(f"{what} failed: {ctypes.FormatError(code).strip()} (error {code})")


def alive(process_handle: int) -> bool:
    """Whether the process behind an open handle is still running."""
    code = wintypes.DWORD()
    ok = kernel32().GetExitCodeProcess(process_handle, ctypes.byref(code))
    return bool(ok) and code.value == STILL_ACTIVE


def find_pid(name: str) -> int | None:
    """The id of a running process whose executable is ``name`` (case ignored), or None."""
    k = kernel32()
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

    k = kernel32()
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
        self._k = kernel32()
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

    def write_bytes(self, address: int, data: bytes) -> None:
        size = len(data)
        done = ctypes.c_size_t(0)
        if not self._k.WriteProcessMemory(
            self.process_handle, ctypes.c_void_p(address), bytes(data), size, ctypes.byref(done)
        ):
            raise _error(f"writing {size} bytes at {address:#x}")
        if done.value != size:
            raise ProcessError(f"wrote {done.value} of {size} bytes at {address:#x}")

    def close_process(self) -> None:
        if self.process_handle:
            self._k.CloseHandle(self.process_handle)
            self.process_handle = None
