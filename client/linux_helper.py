"""The Linux helper (native/linux/, native/README.md "The Linux helper") from outside the game:
find the running game, check its build, talk to the helper's socket, read the game's memory
through it, and drive the helper's own parts (``LinuxMod``: its ``config``, new games,
``apply``, ``ap_labels``, ``offline_put seed``, ``discs``, ``hello``'s ``lang``, ``shield``,
``die``, ``intel on``/``intel off``, ``assists lock``/``assists free``, ``client_back``,
``other_save`` and RETURN HOME's ``mission_*`` requests).

The game is found in the host's ``/proc`` by its kernel name or its binary's (``procfs``; not
by command line, which Steam's launch wrappers share) and its build checked by the binary it
runs (``/proc/<pid>/exe``) against ``patterns.KNOWN_LINUX_BUILDS``, build id and hash. The
helper names its socket by its own pid, which under Flatpak's Steam or the Steam Runtime's
container may be the pid inside that namespace: the last entry of ``NSpid`` in
``/proc/<pid>/status``. Any process can bind an abstract socket's name, so the client checks
that the process serving it is the game, and of this user (``SO_PEERCRED``), and that its
``hello`` names the game's build. Any process of the user in the network namespace, a sandboxed
one too, can also connect to it, so the helper serves a connection only after it has sent the
token the helper wrote when the game started, into ``ZexionAP/token`` of the game's data folder
(``find_tokens``: where it looks, and why that reaches the folder under Flatpak's Steam; it
tries each token found, newest first, one connection each); the
client sends it only to the process it has checked. The client reads the game's memory only
through the helper (``read``): it never opens the game's memory itself.
"""

from __future__ import annotations

import hashlib
import os
import re
import socket
import stat
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
TOKEN_FILE = Path("ZexionAP", "token")  # in the game's data folder: the helper's kTokenFile
TOKEN_LENGTH = 64  # the helper's 2 * kTokenBytes: lower-case hex
FLATPAK_STEAM = Path(".var", "app", "com.valvesoftware.Steam")  # its home, on the host


class LinuxHelperError(native.HelperError):
    """The game, the helper or its socket is not there or not as expected. A ``native.HelperError``
    too, so the step's grants (session.give_through_helper) and the poll's ticks (ticks.Link)
    take the two helpers' failures alike."""


class GameNotFound(LinuxHelperError):
    """Zexion's Linux build is not running."""


class UnknownLinuxBuild(LinuxHelperError):
    """Zexion runs, but as a binary of a build this client does not know."""


class HelperBusy(LinuxHelperError):
    """The helper is serving another client (one at a time): a connection waits in its queue,
    so the first request goes unanswered."""


class NoReply(LinuxHelperError, native.ReplyLost):
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


def parse_hello(reply: str) -> dict[str, str]:
    """The helper's ``hello``: its version and each ``key=value`` after it (``build``,
    ``platform``, ``folder``; ``lang`` once the connection has shown the token)."""
    words = reply.split()
    if len(words) < 2 or words[0] != "zexion-ap":
        raise LinuxHelperError(f"an unreadable hello: {' '.join(words)[:80]!r}")
    return {"version": words[1], **dict(w.split("=", 1) for w in words[2:] if "=" in w)}


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
    pids = procfs.namespace_pids((proc / str(pid) / "status").read_text(errors="replace"))
    return int(pids[-1]) if pids else pid


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


def _data_home(environ: dict[str, str]) -> Path | None:
    """The base the helper puts its ``ZexionAP`` folder in, by the game's own rule
    (``offline_folder``): ``$XDG_DATA_HOME``, else ``$HOME/.local/share``, an absolute path only."""
    data = environ.get("XDG_DATA_HOME", "")
    if data.startswith("/"):
        return Path(data)
    home = environ.get("HOME", "")
    return Path(home, ".local", "share") if home.startswith("/") else None


def _game_environment(pid: int, proc: Path) -> dict[str, str]:
    """``/proc/<pid>/environ``'s variables (the environment the game started with); empty if it
    can't be read."""
    try:
        raw = (proc / str(pid) / "environ").read_bytes()
    except OSError:
        return {}
    out: dict[str, str] = {}
    for entry in raw.split(b"\0"):
        key, sep, value = entry.partition(b"=")
        if sep:
            out[key.decode("utf-8", "replace")] = value.decode("utf-8", "replace")
    return out


def token_paths(
    game: LinuxGame,
    proc: Path = PROC,
    environ: dict[str, str] | None = None,
    home: Path | None = None,
) -> list[Path]:
    """Where the helper's token may be, most likely first, as this client can see it.

    The helper writes it into the data folder its own process has, which under Flatpak's Steam
    is the sandbox's, not the host's: so first the game's own view, ``/proc/<pid>/root`` (a
    process of the same user may follow it, as it reads ``/proc/<pid>/exe``) and the game's own
    ``XDG_DATA_HOME`` or ``HOME`` from its environment; then, as the host sees them, this
    client's own data folder and Flatpak Steam's, whose sandbox home is
    ``~/.var/app/com.valvesoftware.Steam``."""
    environ = dict(os.environ) if environ is None else environ
    home = Path.home() if home is None else home
    found: list[Path] = []
    inner = _data_home(_game_environment(game.pid, proc))
    if inner is not None:
        found.append(proc / str(game.pid) / "root" / str(inner).lstrip("/") / TOKEN_FILE)
    own = _data_home(environ)
    if own is not None:
        found.append(own / TOKEN_FILE)
    found.append(home / ".local" / "share" / TOKEN_FILE)
    found.append(home / FLATPAK_STEAM / ".local" / "share" / TOKEN_FILE)
    found.append(home / FLATPAK_STEAM / "data" / TOKEN_FILE)
    return list(dict.fromkeys(found))


def _read_token(path: Path) -> tuple[str, float] | None:
    """The token in the file at ``path`` and when the file was written, or None if it is not a
    plain file only its user can read, holding one (never through a link)."""
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
    except OSError:
        return None
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_mode & 0o077:
            return None
        text = os.read(fd, 4 * TOKEN_LENGTH).decode("ascii", "replace").strip()
    except OSError:
        return None
    finally:
        os.close(fd)
    if not re.fullmatch(f"[0-9a-f]{{{TOKEN_LENGTH}}}", text):
        return None
    return text, info.st_mtime


def find_tokens(
    game: LinuxGame,
    proc: Path = PROC,
    environ: dict[str, str] | None = None,
    home: Path | None = None,
) -> list[tuple[Path, str]]:
    """Every token in the places ``token_paths`` names, with its file, the most recently written
    first (the helper writes its own when the game starts, so a stale one is older; files of
    one time keep ``token_paths``' order), none twice. LinuxHelperError, naming the places looked,
    when none of them holds one."""
    paths = token_paths(game, proc, environ, home)
    found: list[tuple[float, Path, str]] = []
    for path in paths:
        read = _read_token(path)
        if read is not None and all(read[0] != other for _, _, other in found):
            found.append((read[1], path, read[0]))
    if found:
        found.sort(key=lambda entry: -entry[0])  # stable
        return [(path, token) for _, path, token in found]
    where = "; ".join(str(p) for p in paths)
    raise LinuxHelperError(
        f"the helper in Zexion (pid {game.pid}) takes only a client that has its token, and "
        f"none is in a place this client can read (looked: {where}). The helper writes it, "
        "readable by its user alone, into ZexionAP/token of the game's data folder "
        "($XDG_DATA_HOME, else ~/.local/share, as the game sees them) when the game starts. "
        "Either that folder is one this client can't read (another user, a sandbox that hides "
        "it), or the helper had nowhere to write it: the game's stderr then has a 'no token' "
        "line"
    )


class LinuxHelper:
    """A connection to the helper in ``game``: one request a message, one reply a message."""

    def __init__(self, game: LinuxGame, timeout: float = 5.0) -> None:
        self.game = game
        self.timeout = timeout
        self._sock: socket.socket | None = None
        # The token file the helper took (``auth``), once connected: its folder is the helper's
        # data folder, as this client sees it (ticks.linux_stored_seed reads the seed files there).
        self.token_path: Path | None = None

    def __enter__(self) -> LinuxHelper:
        """Connect, check the server, and show it a token. The tokens found (``find_tokens``) are
        tried newest first on a connection of their own each: a helper drops a connection that
        shows a wrong one, and a stale file found first (a token left by an earlier run, in another
        data folder) must not keep the client from the live one."""
        tokens: list[tuple[Path, str]] | None = None
        refused: list[Path] = []
        while True:
            self._connect()
            try:
                if tokens is None:
                    tokens = find_tokens(self.game)
                path, token = tokens[len(refused)]
                reply = self.request(f"auth {token}")
                if reply == "ok":
                    self.token_path = path
                    return self
                refused.append(path)
                if len(refused) == len(tokens):
                    raise LinuxHelperError(
                        f"the helper in Zexion (pid {self.game.pid}) refused every token this "
                        f"client found: {', '.join(str(p) for p in refused)}. Each is a file from "
                        "another run of the game or another game's data folder; the helper "
                        "writes its own token into ZexionAP/token of the data folder the game "
                        "sees ($XDG_DATA_HOME, else ~/.local/share, inside Flatpak's Steam "
                        "the sandbox's), which this client can't read: is the game's folder "
                        "hidden from it (another user, a sandbox)?"
                    )
                self.__exit__()  # the helper closed this one: a new connection for the next
            except BaseException:
                self.__exit__()
                raise

    def _connect(self) -> None:
        """The socket to the helper, made and checked (``_check_server``)."""
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

    def _check_server(self) -> None:
        """The socket's server is the game, of this user, and says it is the game's build and this
        client's version (the token comes after, to that server alone)."""
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
        return parse_hello(self.request("hello"))

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

    @property
    def token_path(self) -> Path | None:
        """The token file the helper took on the latest connection made: the helper's data
        folder holds it, so its ``seeds`` folder is beside it."""
        return self._helper.token_path

    def request(self, text: str) -> str:
        """A request to the helper on this connection (one at a time with the reads and
        writes)."""
        with self._lock:
            return self._connection().request(text)

    def alive(self) -> bool:
        """The game's pid still names the process attached to, and it has not exited."""
        return running(self.game.pid, self.game.started, self._proc)

    def disconnect(self) -> None:
        """Let go of the helper for now: close the connection (the helper puts back what a client
        that sent ``config`` set, as when it goes), if one is open. Not ``close_process``: the
        next read, write or request connects again, as after a dropped connection. Nothing if it
        is already closed, or the process is."""
        with self._lock:
            if not self._closed and self._helper.connected:
                self._helper.__exit__()

    def close_process(self) -> None:
        with self._lock:
            self._closed = True
            self._helper.__exit__()


class LinuxMod:
    """The parts of the Linux helper the client drives (native/README.md, "The Linux helper"),
    over ``pm``, the game's connection, configured when made (``config``: the client's targets'
    addresses from the game's binary, which the helper checks against its own,
    linux_targets.config_request): new games (``request``, for the client's
    newgame_tick), grants with their popups, traps among them (``apply``, for
    session.give_through_helper), other players' items' labels (``request``, for
    ticks.labels_tick), the seed's own traps (``request``, for ticks.linux_seed_tick),
    received Energy Shields (``request``, for ticks.shields_tick) and DeathLink's deaths
    (``request``, for ticks.deathlink_tick), among others. LinuxHelperError when the helper
    refuses the addresses. ``told``: what the client's ticks told it (the ticks of
    zexion_client.poll, through ticks.helper_link; it starts with nothing told, CLIENT CONNECTED
    asked as of the Windows helper), as the Windows helper's ctx.told. It holds for one
    connection of ``pm`` only (``current``): the helper drops the plan and the labels when it
    closes (it keeps the seed's text, which is told again all the same and replaces its own
    record: Game::merge_offline)."""

    def __init__(self, pm: LinuxProcess) -> None:
        from . import linux_targets, state  # here: neither imports this module

        self.pm = pm
        self.told = state.HelperState()
        reply = pm.request(linux_targets.config_request(pm.game.image))
        if reply != "ok":
            raise LinuxHelperError(f"the helper did not take the game's addresses: {reply}")
        self.connection = pm.connection

    def current(self) -> bool:
        """Still the connection it was configured on, and open: one closed (a request with no
        reply) took the helper's plan and labels with it, though the next request opens another."""
        return self.pm.connection == self.connection and self.pm.connected

    def request(self, text: str) -> str:
        return self.pm.request(text)

    def hello(self) -> dict[str, str]:
        """As native.Helper.hello: the helper's version and fields, with ``lang`` (the language
        the game shows) as the Windows helper's."""
        return parse_hello(self.pm.request("hello"))

    def apply(self, text: str) -> str:
        """As native.Helper.apply: "applied", or "refused <why>" (nothing was written);
        NoReply (a native.ReplyLost) if the request went out with no reply (it may have been
        applied), and another LinuxHelperError (a native.HelperError) otherwise."""
        reply = self.pm.request(text)
        if reply != "applied" and not reply.startswith("refused "):
            raise LinuxHelperError(f"unexpected reply to apply: {reply!r}")
        return reply
