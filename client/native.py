"""Load the native helper (native/, ``zexion_ap.dll``) into the running game and talk to it.

The helper runs on the game's thread once a frame (docs/re/memory.md, "The per-frame call")
and serves ``\\\\.\\pipe\\zexion-ap-<pid>``, one request and one reply per message
(native/README.md). ``attach`` uses a helper already loaded (by the proxy, native/README.md,
"The proxy", or an earlier client) or loads one with ``LoadLibraryW`` from a remote thread,
under a named mutex so two loaders cannot race; ``batch_text`` builds an ``apply``. The pipe
is opened at identification level and its server must be the game's process, so a program
that took the name first is refused.
"""

from __future__ import annotations

import ctypes
import os
import time
from ctypes import wintypes
from pathlib import Path

from . import patterns, process

DLL_NAME = "zexion_ap.dll"
# The player's off switch, beside the proxy in the game's folder (native/proxy/version.cpp
# looks for it in its own folder): while it exists neither the proxy nor the client loads the
# game folder's helper.
DISABLED_NAME = "zexion_ap.disabled"
VERSION = "0.29.0"  # the helper's, in its hello (native/src/dllmain.cpp)
# The addresses the helper needs: every target the client locates (patterns.CLIENT_TARGETS;
# native/src/game_calls.cpp, kConfigKeys; it takes them in any order).
CONFIG_KEYS = tuple(t.name for t in patterns.CLIENT_TARGETS)
APPLY_SECONDS = 3.0  # longer than the 1 s the helper waits for a frame to take a request
ROOT = Path(__file__).resolve().parent.parent
REPLY_SECONDS = 2.0
LOAD_SECONDS = 10.0
UNLOAD_SECONDS = CONNECT_SECONDS = 3.0


class HelperError(RuntimeError):
    """The helper could not be loaded, reached or unloaded."""


class GameExited(HelperError):
    """The game is no longer running."""


class ModFixable(HelperError):
    """The helper is missing or of another version: the message, a clause for the player,
    says what to do about it."""


class ModDisabled(HelperError):
    """The player turned the mod off (DISABLED_NAME in the game's folder): the game folder's
    helper is not loaded. The message is for the player as it is."""


def config_text(rvas: dict[str, int]) -> str:
    missing = [k for k in CONFIG_KEYS if k not in rvas]
    if missing:
        raise HelperError(f"addresses not resolved: {', '.join(missing)}")
    return "config " + " ".join(f"{k}={rvas[k]:#x}" for k in CONFIG_KEYS)


def batch_text(
    cas: list[tuple[int, int, int]],
    popups: list[tuple[int, str] | tuple[int, str, bool]],
    refresh: bool = False,
) -> str:
    """An ``apply`` request: (item, expected, value) writes, done all or none in one frame, and
    popups shown one at a time with the item's sound: (item, suffix) after the item's icon and
    name, or (item, text, True) a notice, the text alone (an Area Map's hint)."""
    lines = ["apply"]
    lines += [f"cas {item} {expected} {value}" for item, expected, value in cas]
    if refresh:
        lines.append("refresh")
    for popup in popups:
        item, text = popup[0], popup[1]
        notice = len(popup) > 2 and bool(popup[2])
        clean = " ".join(text.split())  # one line, single spaces
        if notice:
            if clean:
                lines.append(f"notice {item} {clean}")
        else:
            lines.append(f"popup {item} {clean}".rstrip())
    return "\n".join(lines)


def default_dll() -> Path:
    """The built helper: ``ZEXION_DLL`` if set, else the Release build in native/build/."""
    override = os.environ.get("ZEXION_DLL")
    return Path(override) if override else ROOT / "native" / "build" / "Release" / DLL_NAME


def pipe_name(pid: int) -> str:
    return rf"\\.\pipe\zexion-ap-{pid}"


# --- Windows calls -----------------------------------------------------------------------------

WAIT_OBJECT_0 = 0
WAIT_ABANDONED = 0x80  # the lock's last owner exited holding it: ours now, as with 0
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


_kernel32 = process.kernel32  # the one binding (client/process.py), made on first use


def _last_error() -> str:
    return ctypes.FormatError(ctypes.get_last_error()).strip()


def game_folder(process_handle: int) -> Path | None:
    """The folder of the game's executable, where the player puts the mod. Only read: the
    client never writes the game's folder."""
    path = ctypes.create_unicode_buffer(1024)
    if not _kernel32().K32GetModuleFileNameExW(process_handle, None, path, len(path)):
        return None
    return Path(path.value).parent


def mod_disabled(process_handle: int) -> bool:
    """Whether the player has turned the mod off: DISABLED_NAME in the game's folder."""
    folder = game_folder(process_handle)
    return folder is not None and (folder / DISABLED_NAME).exists()


def is_loaded(process_handle: int) -> bool:
    """Whether the helper is in the game's module list (process.modules). A dead game raises
    GameExited, and a listing that keeps failing HelperError, rather than reading as no
    helper."""
    if process.alive(process_handle):
        try:
            return DLL_NAME in process.modules(process_handle)
        except process.ProcessError as exc:
            if process.alive(process_handle):
                raise HelperError(f"cannot list the game's modules: {exc}") from exc
    raise GameExited("the game has exited")


LATE_LOAD_SECONDS = 60.0


def _load_library(process_handle: int, path: Path, seconds: float) -> None:
    data = (str(path) + "\0").encode("utf-16-le")
    remote = _kernel32().VirtualAllocEx(
        process_handle, None, len(data), MEM_COMMIT_RESERVE, PAGE_READWRITE
    )
    if not remote:
        raise HelperError(f"could not allocate in the game: {_last_error()}")
    free_it = True
    try:
        written = ctypes.c_size_t()
        if not _kernel32().WriteProcessMemory(
            process_handle, remote, data, len(data), ctypes.byref(written)
        ):
            raise HelperError(f"could not write the path into the game: {_last_error()}")
        # kernel32 sits at the same address in every process of a session.
        load_library = _kernel32().GetProcAddress(
            _kernel32().GetModuleHandleW("kernel32.dll"), b"LoadLibraryW"
        )
        thread = _kernel32().CreateRemoteThread(
            process_handle, None, 0, load_library, remote, 0, None
        )
        if not thread:
            raise HelperError(f"could not start the loading thread: {_last_error()}")
        try:
            if _kernel32().WaitForSingleObject(thread, int(seconds * 1000)) != WAIT_OBJECT_0:
                # The load may still happen. Hold the caller's lock until it has, so nobody
                # starts a second load meanwhile (that would leave the helper pinned).
                late = _kernel32().WaitForSingleObject(thread, int(LATE_LOAD_SECONDS * 1000))
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
            _kernel32().CloseHandle(thread)
    finally:
        if free_it:
            _kernel32().VirtualFreeEx(process_handle, remote, 0, MEM_RELEASE)


def load(process_handle: int, pid: int, dll: Path, if_absent: bool = False) -> bool:
    """Load the helper into the process: True once loaded. A helper already loaded is refused,
    or with ``if_absent`` left as it is (False): the proxy may have loaded it meanwhile."""
    if not dll.is_file():
        raise HelperError(f"{dll} does not exist; build native/ first (native/README.md)")
    mutex = _kernel32().CreateMutexW(None, False, f"Local\\zexion-ap-load-{pid}")
    if not mutex:
        raise HelperError(f"could not create the load lock: {_last_error()}")
    try:
        if _kernel32().WaitForSingleObject(mutex, 5000) not in (WAIT_OBJECT_0, WAIT_ABANDONED):
            raise HelperError("another loader is busy with this game")
        try:
            if is_loaded(process_handle):
                if if_absent:
                    return False
                raise HelperError("a helper is already loaded; unload it first")
            _load_library(process_handle, dll.resolve(), LOAD_SECONDS)
            if not is_loaded(process_handle):
                raise HelperError(f"loading {dll.name} into the game failed")
            return True
        finally:
            _kernel32().ReleaseMutex(mutex)
    finally:
        _kernel32().CloseHandle(mutex)


def wait_unloaded(process_handle: int) -> bool:
    """True once the helper has left the module list; GameExited if the game died instead."""
    deadline = time.monotonic() + UNLOAD_SECONDS
    while time.monotonic() < deadline:
        if not is_loaded(process_handle):
            return True
        time.sleep(0.05)
    return False


class Helper:
    """A connection to the helper's pipe in process ``pid``. Any error closes it for good."""

    def __init__(self, pid: int):
        name = pipe_name(pid)
        deadline = time.monotonic() + CONNECT_SECONDS
        while True:
            handle = _kernel32().CreateFileW(
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
            if not _kernel32().GetNamedPipeServerProcessId(handle, ctypes.byref(server)):
                raise HelperError(f"cannot tell who serves {name}: {_last_error()}")
            if server.value != pid:
                raise HelperError(f"{name} is served by process {server.value}, not the game")
            mode = wintypes.DWORD(PIPE_READMODE_MESSAGE)
            if not _kernel32().SetNamedPipeHandleState(handle, ctypes.byref(mode), None, None):
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
        if not _kernel32().WriteFile(self._handle, data, len(data), ctypes.byref(done), None):
            raise HelperError(f"the helper's pipe closed: {_last_error()}")
        deadline = time.monotonic() + seconds
        available = wintypes.DWORD()
        while True:
            if not _kernel32().PeekNamedPipe(
                self._handle, None, 0, None, ctypes.byref(available), None
            ):
                raise HelperError(f"the helper's pipe closed: {_last_error()}")
            if available.value:
                break
            if time.monotonic() > deadline:
                raise HelperError(f"no reply within {seconds} s")
            time.sleep(0.005)
        reply = b""
        buffer = ctypes.create_string_buffer(65536)
        while True:
            ok = _kernel32().ReadFile(self._handle, buffer, len(buffer), ctypes.byref(done), None)
            reply += buffer.raw[: done.value]
            if ok:
                return reply
            if ctypes.get_last_error() != ERROR_MORE_DATA:
                raise HelperError(f"the helper's pipe closed: {_last_error()}")

    def hello(self) -> dict[str, str]:
        """The helper's hello as fields: version, build, hooked, configured, skip, folder,
        offline, active, title, reads and, after a failed read, read_error (native/README.md)."""
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

    def popups_pending(self) -> int | None:
        """How many popups of ``apply`` batches the helper holds that it has not shown yet
        (``popups``, replying ``pending N``); None when no frame took the request (a load, a
        stall: asked again later); HelperError for any other reply."""
        reply = self.request("popups")
        if reply.startswith("refused "):
            return None
        words = reply.split()
        if len(words) != 2 or words[0] != "pending" or not words[1].isdigit():
            raise HelperError(f"unexpected reply to popups: {reply!r}")
        return int(words[1])

    def close(self) -> None:
        if self._handle is not None:
            _kernel32().CloseHandle(self._handle)
            self._handle = None

    def __enter__(self) -> Helper:
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()


# Helper files that loaded as another version than VERSION, by path: (mtime, version). Not
# loaded again until the file changes.
_STALE: dict[str, tuple[int, str]] = {}


def _modified(dll: Path) -> int | None:
    try:
        return dll.stat().st_mtime_ns
    except OSError:
        return None


def _wrong_version(dll: Path, version: str, own_build: bool) -> ModFixable:
    """What to do about a helper file of another version: rebuild it, or update the game's
    folder."""
    if own_build:
        return ModFixable(
            f"the helper built at {dll} is version {version}, and this client needs {VERSION}: "
            "rebuild native/ (native/README.md), or copy this release's zexion_ap.dll and "
            "version.dll into Zexion's folder"
        )
    return ModFixable(
        f"the copy in Zexion's folder is version {version} and this client needs {VERSION}; "
        "with Zexion closed, copy this release's zexion_ap.dll and version.dll there"
    )


def attach(g, dll: Path, notes: list[str] | None = None) -> Helper:
    """The helper in game ``g``, loaded if absent, and configured. A loaded helper of another
    version is replaced by the built ``dll`` if there is one; a game-folder copy with no build
    to replace it, or a ``dll`` known to load as another version (_STALE), is refused; with
    DISABLED_NAME in the game's folder, no build and none loaded, ModDisabled. ``notes``
    collects what to tell the player."""
    handle = g.pm.process_handle
    own_build = dll.is_file()  # this client's own build (not from the Launcher's apworld)
    built = own_build
    # DISABLED_NAME turns off the game folder's copy only, as the proxy does: a build of this
    # client's own (native/build, ZEXION_DLL) still loads, and a helper already loaded is used.
    disabled = not own_build and mod_disabled(handle)
    if not built and not disabled:
        # No build: the game folder's copy, as the proxy loads it (after an unload, say), if
        # the player put one there.
        folder = game_folder(handle)
        if folder is not None and (folder / DLL_NAME).is_file():
            dll = folder / DLL_NAME
        built = dll.is_file()
    stale = _STALE.get(str(dll))
    if stale is not None and stale[0] != _modified(dll):
        stale = None  # rebuilt or replaced since: tried again
    if is_loaded(handle):
        with Helper(g.pid) as old:
            hello = old.hello()
            version = hello.get("version", "")
            in_folder = hello.get("folder") == "1"
            if version != VERSION:
                # The game folder's copy can only be replaced by a build of this client's own:
                # loading that same copy again would give the same version (each retry).
                if in_folder and not own_build:
                    raise _wrong_version(dll, version, own_build=False)
                if stale is not None:  # the same file again: refused, the helper left alone
                    raise _wrong_version(dll, stale[1], own_build)
                if old.request("unload") != "bye" or not wait_unloaded(handle):
                    raise HelperError("could not replace a helper of another version")
                if in_folder and notes is not None:
                    notes.append(
                        f"The helper in the game's folder is version {version}; the built "
                        f"{VERSION} is used instead (update the game folder's copy)."
                    )
            elif in_folder and notes is not None:
                notes.append("The helper was loaded with the game (the proxy in its folder).")
    if not is_loaded(handle):
        if disabled:
            raise ModDisabled(
                f"The mod is turned off ({DISABLED_NAME} is in Zexion's folder): items arrive "
                "without popups, and new games of this seed can't be started. Delete that file "
                "to turn the mod back on."
            )
        if not built:
            if not ROOT.is_dir():  # the client in an apworld: the player installs the helper
                raise ModFixable(
                    "it isn't installed; with Zexion closed, copy this release's "
                    "zexion_ap.dll and version.dll into Zexion's folder"
                )
            raise ModFixable(f"none is loaded in the game, and there is no build at {dll}")
        if stale is not None:
            raise _wrong_version(dll, stale[1], own_build)
        loaded_here = load(handle, g.pid, dll, if_absent=True)
    else:
        loaded_here = False
    helper = Helper(g.pid)
    try:
        hello = helper.hello()
        version = hello.get("version", "")
        if version != VERSION:
            if loaded_here:  # this file is that version: not loaded again until it changes
                _STALE[str(dll)] = (_modified(dll), version)
                raise _wrong_version(dll, version, own_build)
            # The proxy loaded its copy after the version check above.
            if own_build:
                raise HelperError(
                    f"the proxy loaded the game folder's helper {version}, not {VERSION}, "
                    "meanwhile; trying again puts the built one in its place"
                )
            raise _wrong_version(dll, version, own_build=False)
        if hello.get("hooked") != "1":
            raise HelperError(f"the helper did not hook the game: {hello}")
        helper.configure(dict(g.rva))
    except BaseException:
        helper.close()
        raise
    return helper
