"""The Linux helper (native/linux/) preloaded into the stand-in game the native build makes: it
passes the frame call on, serves only while it is the known build (any program, in the hooktest
build), serves reads of the game's memory itself, never opens that memory to other processes,
and exports nothing but SDL_GL_SwapWindow. And client/linux_helper.py against it.

Skips without a build of native/ on Linux; ``ZEXION_REQUIRE=helper`` makes that a failure.
``ZEXION_NATIVE_BUILD`` points at another build folder than native/build.
"""

from __future__ import annotations

import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

import pytest

from client import elf, linux_helper, native, patterns

ROOT = Path(__file__).resolve().parent.parent
BUILD = Path(os.environ.get("ZEXION_NATIVE_BUILD", ROOT / "native" / "build"))
HELPER = BUILD / "libzexion_ap.so"
HOOKTEST = BUILD / "libzexion_ap_hooktest.so"
GAME = BUILD / "zap_fake_game"
MARKER = b"zexion-ap test marker"

if not sys.platform.startswith("linux"):
    pytest.skip("the Linux helper is Linux's", allow_module_level=True)
if not (HELPER.is_file() and HOOKTEST.is_file() and GAME.is_file()):
    if os.environ.get("ZEXION_REQUIRE") == "helper":
        raise RuntimeError(f"no Linux helper build in {BUILD} (ZEXION_REQUIRE=helper)")
    pytest.skip(f"no Linux helper build in {BUILD}", allow_module_level=True)


def _ptrace_scope() -> int:
    try:
        return int(Path("/proc/sys/kernel/yama/ptrace_scope").read_text())
    except (OSError, ValueError):
        return -1  # no Yama


class Game:
    """The stand-in game, started with ``preload``; its pid and its marker's address."""

    def __init__(self, preload: Path) -> None:
        env = {**os.environ, "LD_PRELOAD": str(preload)}
        self.proc = subprocess.Popen(
            [str(GAME)], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        line = self.proc.stdout.readline()
        match = re.fullmatch(r"marker (0x[0-9a-f]+) block (0x[0-9a-f]+)\n", line)
        assert match, f"the stand-in game said {line!r}"
        self.marker = int(match.group(1), 16)
        self.block = int(match.group(2), 16)  # 40000 bytes, byte i being i % 251
        self.pid = self.proc.pid

    def connect(self, timeout: float = 5.0) -> socket.socket:
        name = b"\0zexion-ap-" + str(self.pid).encode()
        deadline = time.monotonic() + timeout
        while True:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            try:
                sock.connect(name)
                sock.settimeout(timeout)
                return sock
            except OSError:
                sock.close()
                if time.monotonic() > deadline:
                    raise
                time.sleep(0.02)

    def as_found(self) -> linux_helper.LinuxGame:
        """The stand-in as find_game would give it, the hooktest build's "test" as its build."""
        build = patterns.LinuxBuild("test", "", "")
        image = elf.parse(GAME.read_bytes())
        return linux_helper.LinuxGame(self.pid, linux_helper.inner_pid(self.pid), build, image)

    def stop(self) -> tuple[int, str, str]:
        """Its swaps that reached the stand-in SDL, and what it wrote."""
        self.proc.send_signal(signal.SIGTERM)
        out, err = self.proc.communicate(timeout=10)
        match = re.search(r"swaps (\d+)", out)
        return (int(match.group(1)) if match else -1), out, err


@pytest.fixture
def game(request):
    started = Game(request.param)
    yield started
    if started.proc.poll() is None:
        started.proc.kill()
        started.proc.wait()


def ask(sock: socket.socket, request: str) -> str:
    sock.send(request.encode())
    return sock.recv(65536).decode("utf-8", "replace")


def reads_marker(game: Game) -> bool:
    """Whether another process, not the game's ancestor, can read the game's marker itself."""
    code = (
        "import sys\n"
        "try:\n"
        f"    f = open('/proc/{game.pid}/mem', 'rb'); f.seek({game.marker}); "
        "sys.stdout.write(f.read(21).decode())\n"
        "except OSError as e:\n"
        "    sys.stdout.write('denied ' + str(e.errno))\n"
    )
    out = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True).stdout
    assert out == MARKER.decode() or out.startswith("denied "), out
    return out == MARKER.decode()


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_the_helper_serves_counts_frames_and_passes_them_on(game: Game) -> None:
    with game.connect() as sock:
        # folder=1: the stand-in game and the helper are both in the build folder
        assert ask(sock, "hello") == (
            f"zexion-ap {native.VERSION} build=test hooked=1 platform=linux folder=1"
        )
        first = int(ask(sock, "ping").removeprefix("pong frames="))
        time.sleep(0.2)
        later = int(ask(sock, "ping").removeprefix("pong frames="))
        assert later > first
        assert ask(sock, "unload") == "error unknown request"
    swaps, _, err = game.stop()
    assert swaps >= later  # every frame call reached SDL
    assert "serving build test" in err


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_the_helper_reads_the_games_memory_itself(game: Game) -> None:
    with game.connect() as sock:
        sock.send(f"read {game.marker:#x} {len(MARKER)}".encode())
        assert sock.recv(65536) == b"data " + MARKER
        assert ask(sock, "read 0x10 4").startswith("error ")  # not mapped: no fault
        assert ask(sock, "ping").startswith("pong frames=")  # and it still serves


@pytest.mark.skipif(_ptrace_scope() < 1, reason="needs Yama's ptrace scope 1 or more")
@pytest.mark.skipif(os.geteuid() == 0, reason="root reads any process")
@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_the_games_memory_is_never_opened_to_other_processes(game: Game) -> None:
    game.connect().close()  # serving
    assert not reads_marker(game)
    with game.connect() as sock:
        assert ask(sock, "ping").startswith("pong frames=")
        assert not reads_marker(game)  # a client connected opens nothing
    assert not reads_marker(game)


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_the_clients_side_reads_the_game_through_the_helper(game: Game) -> None:
    found = game.as_found()
    game.connect().close()  # serving
    with linux_helper.LinuxHelper(found) as helper:
        assert helper.hello()["platform"] == "linux"
        assert helper.frames() >= 0
        assert helper.read(game.marker, len(MARKER)) == MARKER
        rodata = found.image.section(".rodata")
        size = min(64, rodata.size)
        assert helper.read(rodata.addr, size) == found.image.read(rodata.addr, size)
        with pytest.raises(linux_helper.LinuxHelperError, match="reading 4 bytes at 0x10"):
            helper.read(0x10, 4)  # not mapped
        block = helper.read(game.block, 40000)  # in two pieces
        assert block == bytes(i % 251 for i in range(40000))


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_the_frame_calls_are_counted_by_call_site(game: Game) -> None:
    found = game.as_found()
    game.connect().close()
    time.sleep(0.3)
    with linux_helper.LinuxHelper(found) as helper:
        sites, other = helper.callers()
        total = helper.frames()
    named = {}
    for site, count in sites.items():
        function = found.image.function_at(site)
        assert function, f"{site:#x} is in none of the stand-in's functions"
        named[function[0].name] = count
    assert set(named) == {"draw_frame", "draw_editor"} and other == 0
    assert named["draw_frame"] >= 3 * named["draw_editor"] - 3  # one in four frames
    assert abs(sum(named.values()) - total) <= 2  # counted between the two requests


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_a_socket_served_by_another_process_is_not_trusted(game: Game) -> None:
    found = game.as_found()
    squatter = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    squatter.bind(b"\0zexion-ap-999999")  # the name the client would use for a pid of 999999
    squatter.listen(1)
    try:
        impostor = linux_helper.LinuxGame(found.pid, 999999, found.build, found.image)
        with (
            pytest.raises(linux_helper.LinuxHelperError, match="not Zexion"),
            linux_helper.LinuxHelper(impostor),
        ):
            pass
    finally:
        squatter.close()


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_a_helper_of_another_build_is_not_trusted(game: Game) -> None:
    found = game.as_found()
    game.connect().close()
    other = linux_helper.LinuxGame(
        found.pid, found.inner_pid, patterns.LinuxBuild("B1", "", ""), found.image
    )
    with (
        pytest.raises(linux_helper.LinuxHelperError, match="another build"),
        linux_helper.LinuxHelper(other),
    ):
        pass


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_a_second_client_waits_and_is_told_the_helper_is_busy(game: Game) -> None:
    found = game.as_found()
    with game.connect() as first:
        assert ask(first, "ping").startswith("pong")  # being served: the next one waits
        with pytest.raises(linux_helper.HelperBusy), linux_helper.LinuxHelper(found, timeout=0.5):
            pass


@pytest.mark.parametrize("game", [HELPER], indirect=True)
def test_the_release_helper_does_nothing_in_another_program(game: Game) -> None:
    with pytest.raises(OSError):
        game.connect(timeout=0.3)
    swaps, _, err = game.stop()
    assert swaps > 0  # its SDL_GL_SwapWindow still passes the calls on
    assert "zexion-ap" not in err  # not named Zexion: not a word


def test_a_disabled_file_beside_the_helper_turns_it_off(tmp_path: Path) -> None:
    shutil.copy2(HOOKTEST, tmp_path / HOOKTEST.name)
    (tmp_path / native.DISABLED_NAME).write_text("")
    started = Game(tmp_path / HOOKTEST.name)
    try:
        with pytest.raises(OSError):
            started.connect(timeout=0.3)
        swaps, _, _ = started.stop()
        assert swaps > 0
    finally:
        if started.proc.poll() is None:
            started.proc.kill()
            started.proc.wait()


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_a_request_too_big_drops_the_client(game: Game) -> None:
    with game.connect() as sock:
        sock.send(b"x" * (64 * 1024 + 1))
        assert sock.recv(65536) == b""  # closed
    with game.connect() as sock:
        assert ask(sock, "ping").startswith("pong frames=")


@pytest.mark.parametrize("build", [HELPER, HOOKTEST], ids=["release", "hooktest"])
def test_the_helper_exports_only_its_frame_call(build: Path) -> None:
    image = elf.parse(build.read_bytes())
    dynamic_only = elf.Image(
        image.type,
        image.machine,
        image.entry,
        tuple(s for s in image.sections if s.type != elf.SHT_SYMTAB),  # .dynsym alone
        image.data,
    )
    exported = {
        s.name
        for s in dynamic_only.symbols()
        if s.defined and s.type in (elf.STT_FUNC, elf.STT_OBJECT)
    }
    assert exported == {"SDL_GL_SwapWindow"}  # its C++ runtime stays its own


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_the_game_reads_and_writes_through_the_helper(game: Game) -> None:
    from client import game as client_game

    found = game.as_found()
    found = linux_helper.LinuxGame(
        found.pid, found.inner_pid, found.build, found.image, linux_helper.start_time(game.pid)
    )
    game.connect().close()  # serving
    rodata = found.image.section(".rodata").addr
    pm = linux_helper.LinuxProcess(found)
    try:
        # Targets placed on the stand-in's block (its writable data) and its .rodata.
        g = client_game.Game(pm, 0, {"block": game.block, "rodata": rodata}, game.pid, found.build)
        assert g._read("block", 0, 4) == bytes([0, 1, 2, 3])
        g._write("block", 10, b"zexion")  # written, and read back
        assert g._read("block", 8, 10) == bytes([8, 9]) + b"zexion" + bytes([16, 17])
        big = bytes(range(256)) * 80  # 20480 bytes: two pieces
        g._write("block", 0, big)
        assert g._read("block", 0, len(big)) == big
        before = g._read("rodata", 0, 4)
        with pytest.raises(client_game.WriteStateUnknown, match="not_in_the_game's_writable"):
            g._write("rodata", 0, b"\x00\x00\x00\x00")  # code and constants stay the game's
        assert g._read("rodata", 0, 4) == before
        g.rva["marker"] = game.marker  # initialized data (.data): function pointers live there
        with pytest.raises(client_game.WriteStateUnknown, match="not_in_the_game's_writable"):
            g._write("marker", 0, b"Z")
        assert g._read("marker", 0, len(MARKER)) == MARKER
        pm._helper.__exit__()  # the connection dropped (the helper drops a silent client)
        assert g._read("block", 0, 2) == b"\x00\x01"  # connected again, checks and all
        assert pm.alive()
        game.stop()
        assert not pm.alive()
        with pytest.raises(client_game.GameNotRunning):
            g._read("block", 0, 4)
    finally:
        pm.close_process()


NEWGAME_SYMBOLS = {
    "rando_blob": "zap_section",
    "randomizer_menu": "zap_menu",
    "transition_fn": "zap_transition",
    "transition_timer": "zap_timer",
    "newgame_fn": "zap_new_game",
    "placement_fn": "zap_placement",
    "rando_marker": "zap_marker",
    "current_slot": "zap_slot",
    "start_count": "zap_start_count",
    "start_cells": "zap_start_cells",
    "start_flags": "zap_start_flags",
}
PLAN = (
    "ap_newgame\nidentify 0x658 4142434445464748\nmenu 0x88 0300\nexpect 0x26C 5A\nwrite 0x26C 2A"
)


def newgame_addresses() -> dict[str, int]:
    image = elf.parse(GAME.read_bytes())
    return {key: image.symbol(name).value for key, name in NEWGAME_SYMBOLS.items()}


def wait_for(sock: socket.socket, request: str, wanted, seconds: float = 3.0) -> str:
    deadline = time.monotonic() + seconds
    while True:
        reply = ask(sock, request)
        if wanted(reply) or time.monotonic() > deadline:
            return reply
        time.sleep(0.02)


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_a_new_game_queued_with_the_seeds_text_gets_the_seed(game: Game) -> None:
    at = newgame_addresses()
    with game.connect() as sock:
        assert ask(sock, PLAN) == "refused not configured"
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        assert ask(sock, PLAN) == "armed"
        assert ask(sock, "newgame_status") == "armed"
        # The player's START GAME, with the seed's text typed in: the section, then the queue.
        assert ask(sock, f"write {at['rando_blob'] + 0x658:#x} {b'ABCDEFGH'.hex()}").startswith(
            "written"
        )
        assert ask(sock, f"write {at['rando_blob'] + 0x26C:#x} 11") == "written 1"
        assert ask(sock, f"write {at['current_slot']:#x} 04000000") == "written 4"
        assert ask(sock, f"write {at['rando_marker']:#x} 0100") == "written 2"
        assert ask(sock, f"write {at['transition_timer']:#x} 28000000") == "written 4"
        newgame = at["newgame_fn"].to_bytes(8, "little").hex()
        assert ask(sock, f"write {at['transition_fn']:#x} {newgame}") == "written 8"
        status = wait_for(sock, "newgame_status", lambda r: r != "armed")
        assert status == "applied slot 5"
        sock.send(f"read {at['rando_blob'] + 0x26C:#x} 1".encode())
        assert sock.recv(64) == b"data \x2a"  # the AP table written over the placement


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_a_config_outside_the_games_data_or_code_is_refused(game: Game) -> None:
    at = newgame_addresses()
    with game.connect() as sock:
        bad = dict(at, rando_blob=game.marker)  # .data, not .bss
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in bad.items())
        assert ask(sock, config) == "error rando_blob is not in the game's data"
        bad = dict(at, placement_fn=at["rando_blob"])  # data, not code
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in bad.items())
        assert ask(sock, config) == "error placement_fn is not in the game's code"
        missing = {k: v for k, v in at.items() if k != "start_flags"}
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in missing.items())
        assert ask(sock, config) == "error missing start_flags"
        assert ask(sock, PLAN) == "refused not configured"
