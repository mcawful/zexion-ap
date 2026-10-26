"""Reading and writing another process's memory with ctypes alone: Archipelago's Windows
release bundles no library for it. ``kernel32()`` is the one binding of kernel32's functions,
client/native.py's included.
"""

from __future__ import annotations

import ctypes
import functools
import sys
import time
from ctypes import wintypes


class ProcessError(OSError):
    """The process could not be found, opened, read or written."""


class ProcessNotFound(ProcessError):
    """No process of that name is running."""


class ProcessAccessDenied(ProcessError):
    """Windows refused access (error 5): opening a process that runs as administrator from one
    that does not, say."""


PROCESS_ALL_ACCESS = 0x1F0FFF  # the helper's loader needs threads too
TH32CS_SNAPPROCESS = 0x2
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
ERROR_ACCESS_DENIED, ERROR_NO_MORE_FILES = 5, 18
LIST_MODULES_ALL = 3  # EnumProcessModulesEx: 32-bit and 64-bit modules
MODULE_TRIES = 10  # listing the modules can fail for a moment while the process's loader is busy
MAX_PATH = 260
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


class MODULEINFO(ctypes.Structure):
    _fields_ = [
        ("lpBaseOfDll", ctypes.c_void_p),
        ("SizeOfImage", wintypes.DWORD),
        ("EntryPoint", ctypes.c_void_p),
    ]


@functools.cache
def kernel32():
    """kernel32, with the argument and result types of every function the client calls."""
    if sys.platform != "win32":  # pragma: no cover - the client runs on Windows only
        raise ProcessError("reading another process needs Windows")
    k = ctypes.WinDLL("kernel32", use_last_error=True)
    h, b, d, p, s = wintypes.HANDLE, wintypes.BOOL, wintypes.DWORD, ctypes.c_void_p, ctypes.c_size_t
    m, text = wintypes.HMODULE, wintypes.LPWSTR
    sig = {
        # Processes, their modules (psapi's functions, which kernel32 exports as K32*) and their
        # memory.
        "GetExitCodeProcess": (b, [h, ctypes.POINTER(d)]),
        "CreateToolhelp32Snapshot": (h, [d, d]),
        "Process32FirstW": (b, [h, ctypes.POINTER(PROCESSENTRY32W)]),
        "Process32NextW": (b, [h, ctypes.POINTER(PROCESSENTRY32W)]),
        "K32EnumProcessModulesEx": (b, [h, ctypes.POINTER(m), d, ctypes.POINTER(d), d]),
        "K32GetModuleBaseNameW": (d, [h, m, text, d]),
        "K32GetModuleFileNameExW": (d, [h, m, text, d]),
        "K32GetModuleInformation": (b, [h, m, ctypes.POINTER(MODULEINFO), d]),
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


def modules(process_handle: int) -> dict[str, tuple[int, int]]:
    """The process's loaded modules: each one's name, lower-cased, with where it is loaded and
    its size ((0, 0) if that could not be read). Listing them can fail for a moment while the
    process's loader is busy (it is starting, or loading or unloading a module): tried again,
    then ProcessError, never an empty listing."""
    k = kernel32()
    handles = (wintypes.HMODULE * 1024)()
    listed = wintypes.DWORD()  # the bytes of handles listed
    for _ in range(MODULE_TRIES):
        if k.K32EnumProcessModulesEx(
            process_handle, handles, ctypes.sizeof(handles), ctypes.byref(listed), LIST_MODULES_ALL
        ):
            break
        time.sleep(0.05)
    else:
        raise _error("listing the process's modules")
    out = {}
    name, info = ctypes.create_unicode_buffer(MAX_PATH), MODULEINFO()
    for module in handles[: min(listed.value // ctypes.sizeof(wintypes.HMODULE), len(handles))]:
        if not k.K32GetModuleBaseNameW(process_handle, module, name, len(name)):
            continue  # unloaded meanwhile
        ok = k.K32GetModuleInformation(
            process_handle, module, ctypes.byref(info), ctypes.sizeof(info)
        )
        base, size = (info.lpBaseOfDll or 0, info.SizeOfImage) if ok else (0, 0)
        out[name.value.lower()] = (int(base), int(size))
    return out


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
        """Where a module is loaded (the executable's own by default, case ignored), and its
        size; None if it is not loaded."""
        return modules(self.process_handle).get((name or self.name).lower())

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
