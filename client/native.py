"""Load the native helper (native/, ``zexion_ap.dll``) into the running game and talk to it.

The helper runs inside Zexion: it gets a call from the game's main loop, on the game's own
thread (through the game's import of ``SDL_GL_SwapWindow``; docs/re/memory.md, "The per-frame
call"), and serves a local named pipe, ``\\\\.\\pipe\\zexion-ap-<pid>``, one request and one reply
per message: ``hello``, ``ping``, ``config``, ``apply``, ``ap_newgame``, ``disarm``,
``newgame_status`` and ``unload`` (native/README.md).
``attach`` loads the helper if needed and configures it with the addresses the client resolved
by pattern; ``batch_text`` builds an ``apply``: compare-and-set writes to inventory bytes, an
optional HUD refresh, and popups. The AP client (client/session.py) grants through it when it
is attached; tools/helper.py loads and unloads it for tests.

Loading: a remote thread runs ``LoadLibraryW`` on the helper's path, with a timeout, under a
named mutex so two loaders cannot race. Talking: the client opens the pipe at the
identification level only (the server cannot act as the user) and checks that the process
serving it is the game, so another program that took the name first is refused. Loading is a
change to the running game, made only when asked, and the helper hooks nothing in a build it
does not know.
"""

from __future__ import annotations

import ctypes
import os
import sys
import time
from ctypes import wintypes
from pathlib import Path

DLL_NAME = "zexion_ap.dll"
VERSION = "0.5.0"  # the helper's, in its hello (native/src/dllmain.cpp)
# The addresses the helper needs, by client/patterns.py target name (native/src/game_calls.cpp).
CONFIG_KEYS = (
    "inventory",
    "save_loaded",
    "menu_state",
    "item_records",
    "last_message",
    "weapon_strip_gate",
    "sound_system",
    "message_fn",
    "sound_fn",
    "hud_number_fn",
    "hud_layout_fn",
    "energy_hud_fn",
    "weapon_strip_fn",
    "suit_color_fn",
    "rando_blob",
    "rando_marker",
    "transition_fn",
    "transition_timer",
    "current_slot",
    "newgame_fn",
    "current_menu",
    "randomizer_menu",
    "widget_handler_fn",
    "panel_handler_fn",
    "rando_menu_fn",
    "placement_fn",
    "rando_newgame_fn",
    "event_table",
    "event_count",
)
APPLY_SECONDS = 3.0  # the helper waits up to a second for a frame to take a request
ROOT = Path(__file__).resolve().parent.parent
REPLY_SECONDS = 2.0
LOAD_SECONDS = 10.0


class HelperError(RuntimeError):
    """The helper could not be loaded, reached or unloaded."""


class GameExited(HelperError):
    """The game is no longer running."""


def config_text(rvas: dict[str, int]) -> str:
    missing = [k for k in CONFIG_KEYS if k not in rvas]
    if missing:
        raise HelperError(f"addresses not resolved: {', '.join(missing)}")
    return "config " + " ".join(f"{k}={rvas[k]:#x}" for k in CONFIG_KEYS)


def batch_text(
    cas: list[tuple[int, int, int]], popups: list[tuple[int, str]], refresh: bool = False
) -> str:
    """An ``apply`` request: (item, expected, value) writes, done all or none in one frame, and
    (item, suffix) popups shown one at a time, each with its item's icon, name and sound."""
    lines = ["apply"]
    lines += [f"cas {item} {expected} {value}" for item, expected, value in cas]
    if refresh:
        lines.append("refresh")
    for item, suffix in popups:
        clean = " ".join(suffix.split())  # one line, single spaces
        lines.append(f"popup {item} {clean}".rstrip())
    return "\n".join(lines)


def default_dll() -> Path:
    """The built helper: ``ZEXION_DLL`` if set, else the Release build in native/build/."""
    override = os.environ.get("ZEXION_DLL")
    return Path(override) if override else ROOT / "native" / "build" / "Release" / DLL_NAME


def pipe_name(pid: int) -> str:
    return rf"\\.\pipe\zexion-ap-{pid}"


# --- Windows calls -----------------------------------------------------------------------------

STILL_ACTIVE = 259
WAIT_OBJECT_0 = 0
INVALID_HANDLE = ctypes.c_void_p(-1).value
GENERIC_READ_WRITE = 0x80000000 | 0x40000000
OPEN_EXISTING = 3
SECURITY_SQOS_PRESENT = 0x00100000
SECURITY_IDENTIFICATION = 0x00010000
PIPE_READMODE_MESSAGE = 2
ERROR_MORE_DATA = 234
ERROR_PIPE_BUSY = 231
MEM_COMMIT_RESERVE = 0x1000 | 0x2000
MEM_RELEASE = 0x8000
PAGE_READWRITE = 0x04


def _kernel32():
    k = ctypes.WinDLL("kernel32", use_last_error=True)
    h, b, d, p, s = wintypes.HANDLE, wintypes.BOOL, wintypes.DWORD, ctypes.c_void_p, ctypes.c_size_t
    sig = {
        "GetExitCodeProcess": (b, [h, ctypes.POINTER(d)]),
        "VirtualAllocEx": (p, [h, p, s, d, d]),
        "VirtualFreeEx": (b, [h, p, s, d]),
        "WriteProcessMemory": (b, [h, p, p, s, ctypes.POINTER(s)]),
        "GetModuleHandleW": (h, [wintypes.LPCWSTR]),
        "GetProcAddress": (p, [h, ctypes.c_char_p]),
        "CreateRemoteThread": (h, [h, p, s, p, p, d, p]),
        "WaitForSingleObject": (d, [h, d]),
        "CloseHandle": (b, [h]),
        "CreateMutexW": (h, [p, b, wintypes.LPCWSTR]),
        "ReleaseMutex": (b, [h]),
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


def _psapi():
    k = ctypes.WinDLL("psapi", use_last_error=True)
    h, d = wintypes.HANDLE, wintypes.DWORD
    k.EnumProcessModulesEx.restype = wintypes.BOOL
    k.EnumProcessModulesEx.argtypes = [h, ctypes.POINTER(wintypes.HMODULE), d, ctypes.POINTER(d), d]
    k.GetModuleBaseNameW.restype = d
    k.GetModuleBaseNameW.argtypes = [h, wintypes.HMODULE, wintypes.LPWSTR, d]
    return k


_K = _kernel32() if sys.platform == "win32" else None
_PSAPI = _psapi() if sys.platform == "win32" else None
LIST_MODULES_ALL = 3


def _last_error() -> str:
    return ctypes.FormatError(ctypes.get_last_error()).strip()


def alive(process_handle: int) -> bool:
    code = wintypes.DWORD()
    return bool(_K.GetExitCodeProcess(process_handle, ctypes.byref(code))) and (
        code.value == STILL_ACTIVE
    )


def module_names(process_handle: int, attempts: int = 5) -> list[str]:
    """The game's loaded modules, lower-cased. Listing them can fail for a moment while the
    game loads or unloads one; a listing that keeps failing raises rather than reading as
    empty, and a dead game raises GameExited."""
    for _ in range(attempts):
        if not alive(process_handle):
            raise GameExited("the game has exited")
        handles = (wintypes.HMODULE * 1024)()
        needed = wintypes.DWORD()
        if _PSAPI.EnumProcessModulesEx(
            process_handle, handles, ctypes.sizeof(handles), ctypes.byref(needed), LIST_MODULES_ALL
        ):
            count = min(needed.value // ctypes.sizeof(wintypes.HMODULE), len(handles))
            names = []
            name = ctypes.create_unicode_buffer(260)
            for module in handles[:count]:
                if _PSAPI.GetModuleBaseNameW(process_handle, module, name, len(name)):
                    names.append(name.value.lower())
            return names
        time.sleep(0.02)
    raise HelperError(f"cannot list the game's modules: {_last_error()}")


def is_loaded(process_handle: int) -> bool:
    """Whether the helper is in the game's module list (GameExited for a dead game)."""
    return DLL_NAME in module_names(process_handle)


LATE_LOAD_SECONDS = 60.0


def _load_library(process_handle: int, path: Path, seconds: float) -> None:
    data = (str(path) + "\0").encode("utf-16-le")
    remote = _K.VirtualAllocEx(process_handle, None, len(data), MEM_COMMIT_RESERVE, PAGE_READWRITE)
    if not remote:
        raise HelperError(f"could not allocate in the game: {_last_error()}")
    free_it = True
    try:
        written = ctypes.c_size_t()
        if not _K.WriteProcessMemory(
            process_handle, remote, data, len(data), ctypes.byref(written)
        ):
            raise HelperError(f"could not write the path into the game: {_last_error()}")
        # kernel32 sits at the same address in every process of a session.
        load_library = _K.GetProcAddress(_K.GetModuleHandleW("kernel32.dll"), b"LoadLibraryW")
        thread = _K.CreateRemoteThread(process_handle, None, 0, load_library, remote, 0, None)
        if not thread:
            raise HelperError(f"could not start the loading thread: {_last_error()}")
        try:
            if _K.WaitForSingleObject(thread, int(seconds * 1000)) != WAIT_OBJECT_0:
                # The load may still happen. Hold the caller's lock until it has, so nobody
                # starts a second load meanwhile (that would leave the helper pinned).
                late = _K.WaitForSingleObject(thread, int(LATE_LOAD_SECONDS * 1000))
                free_it = late == WAIT_OBJECT_0  # a stuck thread may still read the path
                raise HelperError(
                    f"loading did not finish within {seconds} s"
                    + (
                        "; it finished later, check `helper.py status`"
                        if free_it
                        else "; it is stuck, restart the game before trying again"
                    )
                )
        finally:
            _K.CloseHandle(thread)
    finally:
        if free_it:
            _K.VirtualFreeEx(process_handle, remote, 0, MEM_RELEASE)


def load(process_handle: int, pid: int, dll: Path, seconds: float = LOAD_SECONDS) -> None:
    """Load the helper into the process. Refuses if a helper is already loaded."""
    if not dll.is_file():
        raise HelperError(f"{dll} does not exist; build native/ first (native/README.md)")
    mutex = _K.CreateMutexW(None, False, f"Local\\zexion-ap-load-{pid}")
    if not mutex:
        raise HelperError(f"could not create the load lock: {_last_error()}")
    try:
        if _K.WaitForSingleObject(mutex, 5000) not in (WAIT_OBJECT_0, 0x80):  # 0x80: abandoned
            raise HelperError("another loader is busy with this game")
        try:
            if is_loaded(process_handle):
                raise HelperError("a helper is already loaded; unload it first")
            _load_library(process_handle, dll.resolve(), seconds)
            if not is_loaded(process_handle):
                raise HelperError(f"loading {dll.name} into the game failed")
        finally:
            _K.ReleaseMutex(mutex)
    finally:
        _K.CloseHandle(mutex)


def wait_unloaded(process_handle: int, seconds: float = 3.0) -> bool:
    """True once the helper has left the module list; GameExited if the game died instead."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if not is_loaded(process_handle):
            return True
        time.sleep(0.05)
    return False


class Helper:
    """A connection to the helper's pipe in process ``pid``. Any error closes it for good."""

    def __init__(self, pid: int, connect_seconds: float = 3.0):
        name = pipe_name(pid)
        deadline = time.monotonic() + connect_seconds
        while True:
            handle = _K.CreateFileW(
                name,
                GENERIC_READ_WRITE,
                0,
                None,
                OPEN_EXISTING,
                SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
                None,
            )
            if handle not in (None, INVALID_HANDLE):
                break
            if time.monotonic() > deadline:
                busy = ctypes.get_last_error() == ERROR_PIPE_BUSY
                raise HelperError(
                    f"the helper's pipe is in use by another client ({name})"
                    if busy
                    else f"no helper answers at {name}"
                )
            time.sleep(0.05)
        self._handle = handle
        try:
            server = wintypes.ULONG()
            if not _K.GetNamedPipeServerProcessId(handle, ctypes.byref(server)):
                raise HelperError(f"cannot tell who serves {name}: {_last_error()}")
            if server.value != pid:
                raise HelperError(f"{name} is served by process {server.value}, not the game")
            mode = wintypes.DWORD(PIPE_READMODE_MESSAGE)
            if not _K.SetNamedPipeHandleState(handle, ctypes.byref(mode), None, None):
                raise HelperError(f"cannot set message mode: {_last_error()}")
        except BaseException:
            self.close()
            raise

    def request(self, text: str, seconds: float = REPLY_SECONDS) -> str:
        if self._handle is None:
            raise HelperError("this connection was closed after an error")
        try:
            reply = self._exchange(text.encode("utf-8"), seconds).decode("utf-8")
        except BaseException:
            self.close()  # a late reply must never answer the next request
            raise
        if reply.startswith("error "):
            raise HelperError(reply[len("error ") :])
        return reply

    def _exchange(self, data: bytes, seconds: float) -> bytes:
        done = wintypes.DWORD()
        if not _K.WriteFile(self._handle, data, len(data), ctypes.byref(done), None):
            raise HelperError(f"the helper's pipe closed: {_last_error()}")
        deadline = time.monotonic() + seconds
        available = wintypes.DWORD()
        while True:
            if not _K.PeekNamedPipe(self._handle, None, 0, None, ctypes.byref(available), None):
                raise HelperError(f"the helper's pipe closed: {_last_error()}")
            if available.value:
                break
            if time.monotonic() > deadline:
                raise HelperError(f"no reply within {seconds} s")
            time.sleep(0.005)
        reply = b""
        buffer = ctypes.create_string_buffer(65536)
        while True:
            ok = _K.ReadFile(self._handle, buffer, len(buffer), ctypes.byref(done), None)
            reply += buffer.raw[: done.value]
            if ok:
                return reply
            if ctypes.get_last_error() != ERROR_MORE_DATA:
                raise HelperError(f"the helper's pipe closed: {_last_error()}")

    def hello(self) -> dict[str, str]:
        """The helper's hello as fields: version, build, hooked, configured, skip."""
        words = self.request("hello").split()
        if len(words) < 2 or words[0] != "zexion-ap":
            raise HelperError(f"unexpected hello: {' '.join(words)!r}")
        fields = dict(w.split("=", 1) for w in words[2:] if "=" in w)
        return {"version": words[1], **fields}

    def configure(self, rvas: dict[str, int]) -> None:
        reply = self.request(config_text(rvas), APPLY_SECONDS)
        if reply != "ok":
            raise HelperError(f"configuring failed: {reply}")

    def apply(self, text: str) -> str:
        """ "applied", or "refused <why>" (nothing was written); HelperError otherwise."""
        reply = self.request(text, APPLY_SECONDS)
        if reply != "applied" and not reply.startswith("refused "):
            raise HelperError(f"unexpected reply to apply: {reply!r}")
        return reply

    def close(self) -> None:
        if self._handle is not None:
            _K.CloseHandle(self._handle)
            self._handle = None

    def __enter__(self) -> Helper:
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()


def attach(g, dll: Path) -> Helper:
    """The helper in game ``g``, loaded if it is not, and configured. ``g`` is an attached
    ``game.Game``. A helper of another version is unloaded and the built one loaded instead."""
    handle = g.pm.process_handle
    if is_loaded(handle):
        with Helper(g.pid) as old:
            stale = old.hello()["version"] != VERSION
            if stale and (old.request("unload") != "bye" or not wait_unloaded(handle)):
                raise HelperError("could not replace a helper of another version")
    if not is_loaded(handle):
        load(handle, g.pid, dll)
    helper = Helper(g.pid)
    try:
        hello = helper.hello()
        if hello.get("hooked") != "1":
            raise HelperError(f"the helper did not hook the game: {hello}")
        helper.configure(dict(g.rva))
    except BaseException:
        helper.close()
        raise
    return helper
