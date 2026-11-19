"""The Linux helper (native/linux/, native/README.md "The Linux helper") from outside the game:
find the running game, check its build, talk to the helper's socket, read the game's memory
through it, and drive the helper's own parts (``LinuxMod``: its ``config``, new games,
``apply``, ``ap_labels``, ``offline_put seed``, ``discs``, ``lang``, ``shield``, ``die``,
``intel on``/``intel off`` and RETURN HOME's ``mission_*`` requests).

The game is found in the host's ``/proc`` by its kernel name or its binary's (``procfs``; not
by command line, which Steam's launch wrappers share) and its build checked by the binary it
runs (``/proc/<pid>/exe``) against ``patterns.KNOWN_LINUX_BUILDS``, build id and hash. The
helper names its socket by its own pid, which under Flatpak's Steam or the Steam Runtime's
container may be the pid inside that namespace: the last entry of ``NSpid`` in
``/proc/<pid>/status``. Any process can bind an abstract socket's name, so the client checks
that the process serving it is the game, and of this user (``SO_PEERCRED``), and that its
``hello`` names the game's build. The client reads the game's memory only through the helper
(``read``): it never opens the game's memory itself.
"""

from __future__ import annotations

import hashlib
import os
import re
import socket
import struct
import threading
from dataclasses import dataclass
from pathlib import Path

from . import elf, native, patterns, procfs

PROC = Path("/proc")
NAMES = ("Zexion",)  # the Linux build's binary (client/procfs.py)
MAX_REPLY = 64 * 1024  # the helper's kMaxRequest
MAX_READ = 32 * 1024  # the helper's kMaxRead: one read's bytes
MAX_WRITE = 16 * 1024  # the helper's kMaxWrite: one write's bytes
DATA_PREFIX = b"data "  # the helper's kDataPrefix
UCRED = struct.Struct("3i")  # pid, uid, gid


class LinuxHelperError(RuntimeError):
    """The game, the helper or its socket is not there or not as expected."""


class GameNotFound(LinuxHelperError):
    """Zexion's Linux build is not running."""


class UnknownLinuxBuild(LinuxHelperError):
    """Zexion runs, but as a binary of a build this client does not know."""


class HelperBusy(LinuxHelperError):
    """The helper is serving another client (one at a time): a connection waits in its queue,
    so the first request goes unanswered."""


class HelperTooOld(LinuxHelperError):
    """The helper in the game takes the client's addresses but not a request this client
    needs: a build of the same version from before it, still preloaded."""


class NoReply(LinuxHelperError):
    """A request went out but no reply came back (within the timeout, or the socket failed or
    closed after the send: ConnectionLost): the helper may have acted on it. Every other
    LinuxHelperError from a request means it did not."""


class ConnectionLost(NoReply):
    """The socket failed or closed after a request went out (not a reply that was late)."""


@dataclass(frozen=True)
class LinuxGame:
    pid: int  # as the host sees it
    inner_pid: int  # as the game sees itself: its socket's name
    build: patterns.LinuxBuild
    image: elf.Image
    started: int = 0  # its start time (start_time), which a new process of the same pid lacks


def _stat_fields(pid: int, proc: Path) -> list[str]:
    """``/proc/<pid>/stat``'s fields from the third on (the state first)."""
    stat = (proc / str(pid) / "stat").read_text(errors="replace")
    # The name (field 2) is in parentheses and may hold spaces: split after its last ")".
    return stat[stat.rindex(")") + 2 :].split()


def start_time(pid: int, proc: Path = PROC) -> int:
    """When process ``pid`` started, in clock ticks since boot (``/proc/<pid>/stat``, field 22):
    with the pid, what tells the game from a later process that reuses its pid."""
    return int(_stat_fields(pid, proc)[19])


def running(pid: int, started: int, proc: Path = PROC) -> bool:
    """Process ``pid`` is the one that started at ``started`` and has not exited: neither a
    later process on its pid nor a zombie (``Z``) or dead one (``X``) waiting to be reaped."""
    try:
        fields = _stat_fields(pid, proc)
        return fields[0] not in ("Z", "X", "x") and int(fields[19]) == started
    except (OSError, ValueError, IndexError):
        return False


def inner_pid(pid: int, proc: Path = PROC) -> int:
    """The pid the process ``pid`` has in its own namespace (``NSpid``'s last entry), or
    ``pid`` on a kernel without ``NSpid``."""
    status = (proc / str(pid) / "status").read_text(errors="replace")
    match = re.search(r"^NSpid:\s*(.*)$", status, re.MULTILINE)
    if not match or not match.group(1).split():
        return pid
    return int(match.group(1).split()[-1])


def identify(data: bytes) -> tuple[patterns.LinuxBuild | None, elf.Image]:
    """The known Linux build a binary is, by its GNU build id and its hash, or None."""
    image = elf.parse(data)
    build_id = image.build_id()
    digest = hashlib.sha256(data).hexdigest()
    for build in patterns.KNOWN_LINUX_BUILDS:
        if build.build_id == build_id and build.sha256 == digest:
            return build, image
    return None, image


def find_game(proc: Path = PROC) -> LinuxGame:
    """The running Linux game, a known build; LinuxHelperError otherwise (none running, one of
    a build not known, or two of a known one)."""
    known: list[LinuxGame] = []
    other: list[str] = []
    for pid in procfs.find_pids(NAMES, proc, cmdline=False):
        try:
            data = (proc / str(pid) / "exe").read_bytes()
            build, image = identify(data)
            inner = inner_pid(pid, proc)
            started = start_time(pid, proc)
        except FileNotFoundError:
            continue  # gone
        except (OSError, ValueError, elf.ELFError) as exc:
            other.append(f"pid {pid}: its binary can't be read ({exc})")
            continue
        if build is None:
            other.append(f"pid {pid}: GNU build id {image.build_id() or 'none'}")
        else:
            known.append(LinuxGame(pid, inner, build, image, started))
    if len(known) == 1:
        return known[0]
    if len(known) > 1:
        pids = ", ".join(str(g.pid) for g in known)
        raise LinuxHelperError(f"Zexion runs twice (pids {pids}); close one")
    if other:
        raise UnknownLinuxBuild(
            f"Zexion is a build this client does not know ({'; '.join(other)}); check for an update"
        )
    raise GameNotFound("Zexion (the Linux build) is not running")


class LinuxHelper:
    """A connection to the helper in ``game``: one request a message, one reply a message."""

    def __init__(self, game: LinuxGame, timeout: float = 5.0) -> None:
        self.game = game
        self.timeout = timeout
        self._sock: socket.socket | None = None

    def __enter__(self) -> LinuxHelper:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        sock.settimeout(self.timeout)
        try:
            sock.connect(b"\0zexion-ap-" + str(self.game.inner_pid).encode())
        except OSError as exc:
            sock.close()
            raise LinuxHelperError(
                f"no helper in Zexion (pid {self.game.pid}): {exc}. Is libzexion_ap.so in the "
                "game's launch options (LD_PRELOAD)?"
            ) from exc
        self._sock = sock
        try:
            self._check_server()
        except BaseException:
            self.__exit__()
            raise
        return self

    def _check_server(self) -> None:
        """The socket's server is the game, of this user, and says it is the game's build."""
        assert self._sock is not None
        raw = self._sock.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, UCRED.size)
        pid, uid, _gid = UCRED.unpack(raw)
        if pid != self.game.pid or uid != os.getuid():
            raise LinuxHelperError(
                f"the socket zexion-ap-{self.game.inner_pid} is served by pid {pid} (user {uid}),"
                f" not Zexion (pid {self.game.pid}): not talking to it"
            )
        try:
            hello = self.hello()
        except ConnectionLost:
            raise  # dropped (another user's, say), not waiting behind another client
        except NoReply as exc:  # its first request unanswered: queued behind another client
            raise HelperBusy(
                f"the helper in Zexion (pid {self.game.pid}) is serving another client"
            ) from exc
        if hello.get("build") != self.game.build.name or hello.get("platform") != "linux":
            raise LinuxHelperError(f"the helper says it is in another build: {hello}")
        from .native import VERSION  # here: native imports nothing of this module's

        if hello.get("version") != VERSION:
            raise LinuxHelperError(
                f"the mod in Zexion is version {hello.get('version')}, this client {VERSION}: "
                "copy this client's libzexion_ap.so into the game's folder"
            )

    def __exit__(self, *_exc: object) -> None:
        if self._sock is not None:
            self._sock.close()
            self._sock = None

    @property
    def connected(self) -> bool:
        return self._sock is not None

    def request_bytes(self, data: bytes) -> bytes:
        if self._sock is None:
            raise LinuxHelperError("not connected")
        # A request with no reply closes the connection: its reply could still come, and would
        # then be read as the next request's.
        try:
            self._sock.send(data)
        except OSError as exc:  # a timeout too: one message, sent whole or not at all
            self.__exit__()
            raise LinuxHelperError(f"the helper's socket failed: {exc}") from exc
        try:
            reply = self._sock.recv(MAX_REPLY)
        except TimeoutError as exc:
            self.__exit__()
            raise NoReply(f"the helper did not answer within {self.timeout:g} s") from exc
        except OSError as exc:
            self.__exit__()
            raise ConnectionLost(f"the helper's socket failed: {exc}") from exc
        if not reply:
            self.__exit__()
            raise ConnectionLost("the helper closed its socket")
        return reply

    def request(self, text: str) -> str:
        return self.request_bytes(text.encode()).decode("utf-8", "replace")

    def hello(self) -> dict[str, str]:
        words = self.request("hello").split()
        if len(words) < 2 or words[0] != "zexion-ap":
            raise LinuxHelperError(f"an unreadable hello: {' '.join(words)[:80]!r}")
        return {"version": words[1], **dict(w.split("=", 1) for w in words[2:] if "=" in w)}

    def frames(self) -> int:
        reply = self.request("ping")
        count = reply.removeprefix("pong frames=")
        if count == reply or not count.isdigit():
            raise LinuxHelperError(f"unexpected reply to ping: {reply!r}")
        return int(count)

    def callers(self) -> tuple[dict[int, int], int]:
        """The frame calls by call site: each return address with its count, and the calls
        from sites the helper had no room left for."""
        reply = self.request("callers")
        words = reply.split()
        if not words or words[0] != "callers":
            raise LinuxHelperError(f"unexpected reply to callers: {reply!r}")
        sites: dict[int, int] = {}
        other = 0
        try:
            for word in words[1:]:
                key, count = word.split("=", 1)
                if key == "other":
                    other = int(count)
                else:
                    sites[int(key, 16)] = int(count)
        except ValueError as exc:
            raise LinuxHelperError(f"unexpected reply to callers: {reply!r}") from exc
        return sites, other

    def write(self, address: int, data: bytes) -> None:
        """Write ``data`` into the game's memory at ``address``, by the helper, in pieces of at
        most MAX_WRITE; LinuxHelperError when a piece is refused (the helper writes only the
        game binary's writable data). A failure after the first piece leaves those before it
        written: the caller reads back."""
        for at in range(0, len(data), MAX_WRITE):
            piece = data[at : at + MAX_WRITE]
            reply = self.request(f"write {address + at:#x} {piece.hex()}")
            if reply != f"written {len(piece)}":
                raise LinuxHelperError(f"writing {len(piece)} bytes at {address + at:#x}: {reply}")

    def read(self, address: int, size: int) -> bytes:
        """``size`` bytes of the game's memory at ``address``, read by the helper in pieces of
        at most MAX_READ; LinuxHelperError when they can't all be read."""
        if size < 0 or address < 0:
            raise ValueError("a read needs an address and a size of 0 or more")
        out = bytearray()
        while len(out) < size:
            at = address + len(out)
            piece = min(MAX_READ, size - len(out))
            reply = self.request_bytes(f"read {at:#x} {piece}".encode())
            if not reply.startswith(DATA_PREFIX) or len(reply) != len(DATA_PREFIX) + piece:
                said = reply[:120].decode("utf-8", "replace")
                raise LinuxHelperError(f"reading {piece} bytes at {at:#x}: {said}")
            out += reply[len(DATA_PREFIX) :]
        return bytes(out)


class LinuxProcess:
    """The game's memory through its helper, with what ``game.Game`` needs of a process:
    ``read_bytes``, ``write_bytes``, ``alive`` and ``close_process``. It holds the helper's one
    client connection, one request at a time (``game.Game`` is called from worker threads), and
    connects again, with the server's checks, once a connection has closed (the helper drops a
    client silent for 30 s; a request with no reply closes it): a read or write then fails only
    if that fails too."""

    def __init__(self, game: LinuxGame, proc: Path = PROC, timeout: float = 5.0) -> None:
        self.game = game
        self.process_id = game.pid
        self._proc = proc
        self._timeout = timeout
        self._lock = threading.Lock()
        self._closed = False
        self._helper = LinuxHelper(game, timeout).__enter__()
        # Counts the connections made: the helper drops a client's plan and labels when its
        # connection closes, so what was told on an earlier one is told again (LinuxMod; the
        # seed's text too, which the helper keeps: told again, it replaces the same record).
        self.connection = 1

    def _connection(self) -> LinuxHelper:
        if self._closed:
            raise LinuxHelperError("closed")
        if not self._helper.connected:
            try:
                self._helper = LinuxHelper(self.game, self._timeout).__enter__()
            except (NoReply, OSError) as exc:
                # Its checks' requests, not the caller's: that one never went out, so not a
                # NoReply (which says the helper may have acted on it); nor a raw OSError (the
                # socket not made: no file descriptors left, say), which callers don't map.
                raise LinuxHelperError(f"connecting to the helper again failed: {exc}") from exc
            self.connection += 1
        return self._helper

    def read_bytes(self, address: int, size: int) -> bytes:
        with self._lock:
            return self._connection().read(address, size)

    def write_bytes(self, address: int, data: bytes) -> None:
        with self._lock:
            self._connection().write(address, data)

    @property
    def connected(self) -> bool:
        """Whether its connection to the helper is open (one closed is made again by the next
        read, write or request)."""
        return self._helper.connected

    def request(self, text: str) -> str:
        """A request to the helper on this connection (one at a time with the reads and
        writes)."""
        with self._lock:
            return self._connection().request(text)

    def alive(self) -> bool:
        """The game's pid still names the process attached to, and it has not exited."""
        return running(self.game.pid, self.game.started, self._proc)

    def close_process(self) -> None:
        with self._lock:
            self._closed = True
            self._helper.__exit__()


class LinuxMod:
    """The parts of the Linux helper the client drives (native/README.md, "The Linux helper"),
    over ``pm``, the game's connection, configured when made with their addresses from the
    game's binary (linux_targets.helper_config): new games (``request``, for the client's
    newgame_tick), grants with their popups, traps among them (``apply``, for
    session.give_through_helper), other players' items' labels (``request``, for
    ticks.linux_labels_tick), the seed's own traps (``request``, for ticks.linux_seed_tick),
    received Energy Shields (``request``, for ticks.shields_tick) and DeathLink's deaths
    (``request``, for ticks.deathlink_tick), among others. LinuxHelperError when the helper
    refuses the addresses. ``told``: what the client's ticks told it (ticks.labels_tick,
    linux_seed_tick, linux_discs_tick, linux_intel_tick and the finds' and RETURN HOME's), as
    the Windows helper's ctx.told. It holds for one connection of ``pm`` only (``current``): the
    helper drops the plan and the labels when it closes (it keeps the seed's text, which is told
    again all the same and replaces its own record: Game::merge_offline)."""

    def __init__(self, pm: LinuxProcess) -> None:
        from . import linux_targets, state  # here: neither imports this module

        self.pm = pm
        self.told = state.HelperState()
        # No offline look on Linux, so no CLIENT CONNECTED to ask for first (client_back_tick):
        # the finds' follow-ups and the Landing Pod's prompt need not wait for it.
        self.told.client_back_sent = True
        reply = pm.request(linux_targets.helper_config(pm.game.image))
        if reply != "ok":
            raise LinuxHelperError(f"the helper did not take the game's addresses: {reply}")
        # A helper of this version built before traps were ported refuses their popups, which
        # would hold every grant back for good (session.give_through_helper retries a refusal).
        reply = pm.request("trap_status")
        if not reply.startswith("queued "):
            raise HelperTooOld(f"the helper does not know trap_status: {reply}")
        self.connection = pm.connection

    def current(self) -> bool:
        """Still the connection it was configured on, and open: one closed (a request with no
        reply) took the helper's plan and labels with it, though the next request opens another."""
        return self.pm.connection == self.connection and self.pm.connected

    def request(self, text: str) -> str:
        return self.pm.request(text)

    def apply(self, text: str) -> str:
        """As native.Helper.apply: "applied", or "refused <why>" (nothing was written);
        native.ReplyLost if the request went out with no reply (it may have been applied), and
        native.HelperError otherwise."""
        try:
            reply = self.pm.request(text)
        except NoReply as exc:
            raise native.ReplyLost(f"the Linux helper: {exc}") from exc
        except LinuxHelperError as exc:
            raise native.HelperError(f"the Linux helper: {exc}") from exc
        if reply != "applied" and not reply.startswith("refused "):
            raise native.HelperError(f"unexpected reply to apply: {reply!r}")
        return reply
