"""Load the native helper (native/, ``zexion_ap.dll``) into the running game and talk to it.

The helper runs inside Zexion: it gets a call once per frame on the game's own thread (through
the game's import of ``SDL_GL_SwapWindow``) and serves a local named pipe,
``\\\\.\\pipe\\zexion-ap-<pid>``, one request and one reply per message. Phase 1 understands
``hello``, ``ping`` and ``unload``; see native/README.md.

Loading it is a change to the running game, made only when asked (tools/helper.py), and the
helper hooks nothing in a build it does not know.
"""

from __future__ import annotations

import os
import time
from pathlib import Path

DLL_NAME = "zexion_ap.dll"
ROOT = Path(__file__).resolve().parent.parent
REPLY_SECONDS = 2.0


class HelperError(RuntimeError):
    """The helper could not be loaded, reached or unloaded."""


def default_dll() -> Path:
    """The built helper: ``ZEXION_DLL`` if set, else the Release build in native/build/."""
    override = os.environ.get("ZEXION_DLL")
    return Path(override) if override else ROOT / "native" / "build" / "Release" / DLL_NAME


def pipe_name(pid: int) -> str:
    return rf"\\.\pipe\zexion-ap-{pid}"


def is_loaded(process_handle: int) -> bool:
    import pymem.process

    return pymem.process.module_from_name(process_handle, DLL_NAME) is not None


def load(process_handle: int, dll: Path) -> None:
    """Load the helper into the process. Refuses if a helper is already loaded."""
    import pymem.process

    if not dll.is_file():
        raise HelperError(f"{dll} does not exist; build native/ first (native/README.md)")
    if is_loaded(process_handle):
        raise HelperError("a helper is already loaded; unload it first")
    pymem.process.inject_dll_from_path(process_handle, str(dll.resolve()))
    # pymem's return value comes from this process, not the game's: look in the game instead.
    if not is_loaded(process_handle):
        raise HelperError(f"loading {dll.name} into the game failed")


def wait_unloaded(process_handle: int, seconds: float = 3.0) -> bool:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if not is_loaded(process_handle):
            return True
        time.sleep(0.05)
    return False


class Helper:
    """A connection to the helper's pipe."""

    def __init__(self, pid: int, connect_seconds: float = 3.0):
        import _winapi
        from multiprocessing.connection import Client

        name = pipe_name(pid)
        deadline = time.monotonic() + connect_seconds
        while True:
            try:
                _winapi.WaitNamedPipe(name, 100)
                break
            except OSError as exc:
                if time.monotonic() > deadline:
                    raise HelperError(f"no helper answers at {name}") from exc
                time.sleep(0.05)
        self._conn = Client(name, family="AF_PIPE")

    def request(self, text: str, seconds: float = REPLY_SECONDS) -> str:
        self._conn.send_bytes(text.encode("utf-8"))
        if not self._conn.poll(seconds):
            raise HelperError(f"no reply to {text!r} within {seconds} s")
        reply = self._conn.recv_bytes().decode("utf-8")
        if reply.startswith("error "):
            raise HelperError(reply[len("error ") :])
        return reply

    def close(self) -> None:
        self._conn.close()

    def __enter__(self) -> Helper:
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()
