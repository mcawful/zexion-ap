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
import struct
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
    """The stand-in game, started with ``preload``; its pid and its marker's address.
    ``data_home`` is its XDG_DATA_HOME: the helper's seed files go in its ``ZexionAP``, never the
    real one."""

    def __init__(self, preload: Path, data_home: Path, **env_more: str) -> None:
        self.offline = data_home / "ZexionAP"
        env = {**os.environ, "LD_PRELOAD": str(preload), "XDG_DATA_HOME": str(data_home)}
        env.update(env_more)
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
def game(request, tmp_path):
    started = Game(request.param, tmp_path / "data")
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
    started = Game(tmp_path / HOOKTEST.name, tmp_path / "data")
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


# The config's keys (native/linux/src/helper.cpp, kConfigKeys' test build): the stand-in game's
# symbol for each, and the offset past it.
CONFIG_SYMBOLS = {
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
    "current_menu": "zap_menu_top",
    "rando_menu_fn": "zap_rando_menu",
    "widget_handler_fn": "zap_widget_handler",
    "panel_handler_fn": "zap_panel_handler",
    "rando_newgame_fn": "zap_force_seed",
    "tr_flag": ("zap_translation", 0x91),
    "translation": "zap_translation",
    "small_text": ("zap_translation", 0x30),
    "small_offsets": ("zap_translation", 0x58),
    "item_names": ("zap_translation", 0x48),
    "item_name_offsets": ("zap_translation", 0x78),
    "small_count": "zap_small_count",
    "item_name_count": "zap_item_name_count",
    "hd_text_fn": "zap_hd_text",
    "hd_build_fn": "zap_build_text",
    "inventory": ("zap_player", 0x108E),
    "save_loaded": "zap_save_loaded",
    "menu_state": "zap_cursor",
    "item_records": "zap_records",
    "last_message": "zap_last_message",
    "weapon_strip_gate": "zap_strip_gate",
    "sound_system": "zap_sounds",
    "message_fn": "zap_message",
    "sound_fn": "zap_sound",
    "hud_number_fn": "zap_hud_number",
    "hud_layout_fn": "zap_hud_layout",
    "energy_hud_fn": "zap_energy_hud",
    "weapon_strip_fn": "zap_weapon_strip",
    "suit_color_fn": "zap_suit_color",
    "hd_messages": "zap_hd_messages",
    "message_slots": "zap_message_slots",
    "sprite_texture": "zap_sprite_world",
    "popup_width_code": "zap_pickup",
    "item_pool": "zap_item_pool",
    "palette_table": "zap_palettes",
    "frame_clock": "zap_frame_clock",
    "sprite_templates": "zap_templates",
    "object_pool": "zap_object_pool",
    "flags": "zap_flags",
    "map_grid": "zap_map_grid",
    "event_table": "zap_events",
    "event_count": "zap_event_count",
    "event_handlers": "zap_event_procs",
    "ailments": "zap_ailments",
    "ailments_hit_code": "zap_weapon_cool",
    "ailments_wave_code": ("zap_weapon_cool", 0x35),
    "halt_flag": "zap_halt",
    "seen_marker_code": "zap_power_up",
    "map_handler": ("zap_move_procs", 0x3C0),
    "text_set_fn": "zap_text_set",
    "text_table": "zap_text_table",
    "equipment_fn": "zap_equipment_menu",
    "equip_panel_fn": "zap_equipment_frame",
    "widget_move_fn": "zap_widget_move",
    "text_place_fn": "zap_text_place",
    "widget_new_fn": "zap_widget_new",
    "sprite_sheet_fn": "zap_sprite_sheet",
    "panel_link_fn": "zap_panel_link",
    "keys_branch": "zap_pause_game",
    "options_back_fn": "zap_options_back",
    "fade": "zap_fade",
    "fade_level": "zap_fade_level",
    "routine_table": "zap_routines",
    "death_fn": "zap_death",
    "assist_menu_fn": "zap_assist_menu",
    "draw_layer": "zap_draw_layer",
    "text_new_fn": "zap_text_new",
    "text_link_fn": "zap_text_link",
}
PLAN = (
    "ap_newgame\nidentify 0x658 4142434445464748\nmenu 0x88 0300\nexpect 0x26C 5A\nwrite 0x26C 2A"
)


def config_addresses() -> dict[str, int]:
    image = elf.parse(GAME.read_bytes())
    found = {}
    for key, where in CONFIG_SYMBOLS.items():
        name, offset = where if isinstance(where, tuple) else (where, 0)
        found[key] = image.symbol(name).value + offset
    return found


def stand_in(name: str) -> int:
    """The stand-in game's symbol ``name``."""
    return elf.parse(GAME.read_bytes()).symbol(name).value


def read_at(sock: socket.socket, address: int, size: int) -> bytes:
    sock.send(f"read {address:#x} {size}".encode())
    reply = sock.recv(64 + size)
    assert reply.startswith(b"data "), reply
    return reply[len(b"data ") :]


def wait_for(sock: socket.socket, request: str, wanted, seconds: float = 3.0) -> str:
    deadline = time.monotonic() + seconds
    while True:
        reply = ask(sock, request)
        if wanted(reply) or time.monotonic() > deadline:
            return reply
        time.sleep(0.02)


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_a_new_game_queued_with_the_seeds_text_gets_the_seed(game: Game) -> None:
    at = config_addresses()
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
    at = config_addresses()
    with game.connect() as sock:
        bad = dict(at, rando_blob=game.marker)  # not the game's own symbol for it
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in bad.items())
        assert ask(sock, config) == "error rando_blob is not where the game's symbols put it"
        bad = dict(at, placement_fn=at["rando_blob"])
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in bad.items())
        assert ask(sock, config) == "error placement_fn is not where the game's symbols put it"
        bad = dict(at, start_cells=at["start_cells"] + 4)  # inside .bss, off by a room
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in bad.items())
        assert ask(sock, config) == "error start_cells is not where the game's symbols put it"
        missing = {k: v for k, v in at.items() if k != "start_flags"}
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in missing.items())
        assert ask(sock, config) == "error missing start_flags"
        assert ask(sock, PLAN) == "refused not configured"


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_a_client_that_goes_takes_its_plan_with_it(game: Game) -> None:
    at = config_addresses()
    with game.connect() as sock:
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        assert ask(sock, PLAN) == "armed"
    with game.connect() as sock:  # as the Windows helper does: no plan kept for a client gone
        assert wait_for(sock, "newgame_status", lambda r: r != "armed") == "idle"


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_a_helper_of_another_version_is_not_used(game: Game, monkeypatch) -> None:
    from client import native as client_native

    monkeypatch.setattr(client_native, "VERSION", "9.9.9")
    found = game.as_found()
    game.connect().close()
    with (
        pytest.raises(linux_helper.LinuxHelperError, match="version 1.2.0, this client 9.9.9"),
        linux_helper.LinuxHelper(found),
    ):
        pass


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_an_apply_gives_the_items_and_shows_their_popup(game: Game) -> None:
    at = config_addresses()
    batch = "apply\ncas 3 0 1\npopup 3 from Tester"
    with game.connect() as sock:
        assert ask(sock, batch) == "refused not configured"
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        assert ask(sock, "apply\ncas 3 1 2") == "refused item 3 holds 0"  # nothing written
        assert read_at(sock, at["inventory"] + 3, 1) == b"\x00"
        assert ask(sock, batch) == "applied"
        assert read_at(sock, at["inventory"] + 3, 1) == b"\x01"
        # The popup at a later frame: the record's name, the suffix, the record's sound.
        count = stand_in("zap_shown_count")
        wait_for(sock, f"read {count:#x} 4", lambda r: r != "data " + "\x00" * 4)
        assert read_at(sock, count, 4) == (1).to_bytes(4, "little")
        shown = read_at(sock, stand_in("zap_shown"), 64).split(b"\x00")[0]
        assert b"MACHINE GUN" in shown and b"FROM TESTER" in shown
        assert read_at(sock, stand_in("zap_sound_id"), 4) == (7).to_bytes(4, "little")
        assert read_at(sock, stand_in("zap_hud_calls"), 4) == (2).to_bytes(4, "little")


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_an_apply_is_refused_out_of_play(game: Game) -> None:
    at = config_addresses()
    with game.connect() as sock:
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        assert ask(sock, f"write {at['menu_state']:#x} 01") == "written 1"  # a menu up
        assert ask(sock, "apply\ncas 3 0 1") == "refused not in play"
        assert read_at(sock, at["inventory"] + 3, 1) == b"\x00"


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_in_a_translation_a_popup_in_pixel_text_is_not_left_hidden(game: Game) -> None:
    # A popup that cannot go into the translation's entries (here the run is not as traced:
    # entry 66 not empty) shows the pixel text. A translated message (the game's own pickup, say)
    # left its HD object marked: the game would hide the next message in that slot by it
    # (docs/re/memory.md, "Translated messages"), so the mark is cleared.
    at = config_addresses()
    has_text = at["hd_messages"] + 0x22C
    with game.connect() as sock:
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        entry_66 = stand_in("zap_small") + 4 * 200
        assert ask(sock, f"write {entry_66:#x} 58000000") == "written 4"  # an X there
        assert ask(sock, f"write {at['tr_flag']:#x} 01") == "written 1"
        assert ask(sock, f"write {has_text:#x} 01") == "written 1"
        assert ask(sock, "apply\npopup 3 from Tester") == "applied"
        count = stand_in("zap_shown_count")
        wait_for(sock, f"read {count:#x} 4", lambda r: r != "data " + "\x00" * 4)
        assert read_at(sock, count, 4) == (1).to_bytes(4, "little")
        # The mark cleared, in the frame the popup is made (read after the game's call made it).
        assert wait_for(sock, f"read {has_text:#x} 1", lambda r: r == "data \x00") == "data \x00"


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_labels_name_the_stand_ins_and_size_the_popup_until_the_client_goes(game: Game) -> None:
    from client import aplabels

    at = config_addresses()
    energy = stand_in("zap_record_table") + 47 * 100 + 0x0A  # record 47's name
    width = stand_in("zap_pickup") + 1  # the immediate of its mov esi, 24
    labels = aplabels.labels_request({(3, 2): "Hookshot\nfor Alice"})  # the player's: row 2
    with game.connect() as sock:
        assert ask(sock, labels) == "refused not configured"
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        assert ask(sock, labels) == "labeled 1"
        wait_for(sock, f"read {energy:#x} 12", lambda r: "SCRAP ENERGY" not in r)
        uploads = int.from_bytes(read_at(sock, stand_in("zap_gl_uploads"), 4), "little")
        assert uploads > 0, "the logos painted into the texture first"
        name = read_at(sock, energy, 50).split(b"\x00")[0]
        assert b"HOOKSHOT" in name and b"FOR ALICE" in name
        # The game's code, patched in place (mprotect): as wide as the label's longest line.
        wait_for(sock, f"read {width:#x} 4", lambda r: r != "data \x18\x00\x00\x00")
        patched = int.from_bytes(read_at(sock, width, 4), "little")
        assert 1 <= patched < 24
    with game.connect() as sock:  # the client went: the stand-ins are the game's again
        wait_for(sock, f"read {width:#x} 4", lambda r: r == "data \x18\x00\x00\x00")
        assert read_at(sock, width, 4) == (24).to_bytes(4, "little")
        assert read_at(sock, energy, 13) == b"SCRAP ENERGY\x00"


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_with_no_logos_painted_the_stand_ins_stay_the_games(game: Game) -> None:
    # The painting is not proven on Linux: restyled into a texture without the logos, the
    # stand-ins would draw their empty frames, so they stay as the game has them.
    from client import aplabels

    at = config_addresses()
    energy = stand_in("zap_record_table") + 47 * 100 + 0x0A
    with game.connect() as sock:
        assert ask(sock, f"write {stand_in('zap_no_texture'):#x} 01") == "written 1"
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        labels = aplabels.labels_request({(3, 2): "Hookshot\nfor Alice"})
        assert ask(sock, labels) == "labeled 1"
        frames = int(ask(sock, "ping").split("frames=")[1])
        wait_for(sock, "ping", lambda r: int(r.split("frames=")[1]) > frames + 20)
        assert read_at(sock, energy, 13) == b"SCRAP ENERGY\x00"
        width = stand_in("zap_pickup") + 1
        assert read_at(sock, width, 4) == (24).to_bytes(4, "little")


DISRUPTION_TIMER = 0xDC2  # the player's (native/src/trap.h)
ENERGY = 0xCEA  # the player's current energy (native/src/game_calls.cpp, kEnergy)
HIT_JUMPS = (0x0B, 0x33)  # the Linux build's two jne's in the hit patch (trap.h)


def player_word(sock: socket.socket, at: dict[str, int], offset: int) -> int:
    return int.from_bytes(read_at(sock, at["inventory"] - 0x108E + offset, 2), "little")


def hit_patch_bytes(sock: socket.socket, at: dict[str, int]) -> list[int]:
    code = read_at(sock, at["ailments_hit_code"], 0x35)
    return [code[i] for i in HIT_JUMPS]


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_a_received_trap_goes_off_as_its_popup_shows(game: Game) -> None:
    at = config_addresses()
    with game.connect() as sock:
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        assert ask(sock, "apply\npopup 102 from Tester") == "applied"  # a Disruption Trap
        assert wait_for(sock, "trap_status", lambda r: "set 102" in r).endswith("set 102")
        assert player_word(sock, at, DISRUPTION_TIMER) == 660
        shown = read_at(sock, stand_in("zap_shown"), 64).split(b"\x00")[0]
        assert b"DISRUPTION TRAP" in shown and b"FROM TESTER" in shown
        assert read_at(sock, stand_in("zap_sound_id"), 4) == (26).to_bytes(4, "little")
        # AILMENTS left out while it runs: the stand-in's weaponCool tests patched (75 to EB),
        assert hit_patch_bytes(sock, at) == [0xEB, 0xEB]
        # and put back once its timer has run out.
        timer = at["inventory"] - 0x108E + DISRUPTION_TIMER
        assert ask(sock, f"write {timer:#x} 0000") == "written 2"
        wait_for(sock, f"read {at['ailments_hit_code'] + 0x0B:#x} 1", lambda r: r == "data u")
        assert hit_patch_bytes(sock, at) == [0x75, 0x75]


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_this_worlds_own_trap_goes_off_at_its_pickup(game: Game) -> None:
    from client import offline

    at = config_addresses()
    cell = 100
    seed = offline.seed_text([0] * 154, (0, 0, 0), False, False, None, None, [(cell, 103)])
    put = offline.put_request("seed", seed, offline.seed_file_name("S", 0, 1))
    with game.connect() as sock:
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        assert ask(sock, put) == "stored"
        assert ask(sock, offline.put_request("config", "x")) == "error not on the Linux build"
        # The seed's save in play (its section's table and start are the stand-in's zeros).
        assert ask(sock, f"write {at['rando_marker']:#x} 0100") == "written 2"
        frames = int(ask(sock, "ping").split("frames=")[1])
        wait_for(sock, "ping", lambda r: int(r.split("frames=")[1]) > frames + 5)
        assert ask(sock, f"write {at['map_grid'] + cell:#x} 1b") == "written 1"  # picked up
        assert wait_for(sock, "trap_status", lambda r: "set 103" in r).endswith("set 103")
        assert player_word(sock, at, 0xDC4) == 660  # the Disorient Trap's timer
        assert read_at(sock, stand_in("zap_sound_id"), 4) == (82).to_bytes(4, "little")


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_an_own_disk_picked_up_with_no_client_shows_its_count(game: Game) -> None:
    # Game::watch_own_disks: the seed's own disks' cells watched with a client or none; a pickup
    # with one queues nothing (the client's follow-up shows it), one after the client has gone
    # queues the count, the file's plus each own disk the map shows collected.
    from client import discs, offline

    at = config_addresses()
    own = (100, 101)
    seed = offline.seed_text(
        [0] * 154, (0, 0, 0), False, False, (1, 3, discs.scan_line(3), list(own)), None
    )
    put = offline.put_request("seed", seed, offline.seed_file_name("S", 0, 1))
    shown = stand_in("zap_shown_count")

    def settle(sock: socket.socket) -> None:
        frames = int(ask(sock, "ping").split("frames=")[1])
        wait_for(sock, "ping", lambda r: int(r.split("frames=")[1]) > frames + 5)

    with game.connect() as sock:
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        assert ask(sock, put) == "stored"
        assert ask(sock, f"write {at['rando_marker']:#x} 0100") == "written 2"  # the seed's save
        settle(sock)
        before = read_at(sock, shown, 4)
        assert ask(sock, f"write {at['map_grid'] + own[0]:#x} 1b") == "written 1"  # with a client
        time.sleep(1.0)
        assert read_at(sock, shown, 4) == before
    with game.connect() as sock:  # the client gone: the helper's to count
        settle(sock)
        assert ask(sock, f"write {at['map_grid'] + own[1]:#x} 1b") == "written 1"
        # After the offline look's notice (the save dressed with no client), its count.
        text_at = stand_in("zap_shown")
        wait_for(sock, f"read {text_at:#x} 64", lambda r: "DISK" in r.upper(), seconds=5.0)
        text = read_at(sock, text_at, 64).split(b"\x00")[0].upper()
        assert b"DISK 3" in text, text  # 1 shared + the two picked up


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_a_seed_put_is_kept_in_the_helpers_folder(game: Game) -> None:
    # As the Windows helper keeps it (offline.cpp): XDG_DATA_HOME/ZexionAP/seeds/NAME.txt, written
    # whole, "unchanged" the second time; a config is refused (the helper configures itself).
    from client import offline

    seed = offline.seed_text([0] * 154, (0, 0, 0), False, False, None, None, [(100, 103)])
    name = offline.seed_file_name("S", 0, 1)
    with game.connect() as sock:
        assert ask(sock, offline.put_request("seed", seed, name)) == "stored"
        assert ask(sock, offline.put_request("seed", seed, name)) == "unchanged"
        assert ask(sock, offline.put_request("config", "x")) == "error not on the Linux build"
    kept = game.offline / "seeds" / f"{name}.txt"
    assert kept.read_text() == seed
    assert (game.offline.stat().st_mode & 0o777) == 0o700
    assert not list((game.offline / "seeds").glob("*.tmp"))


def test_with_a_seed_file_and_no_client_the_helper_configures_itself(tmp_path: Path) -> None:
    # A seed file left by an earlier run: read at start, the helper configured from the game's
    # own symbols with no client (inactive), so this world's own trap goes off at its pickup.
    from client import offline

    data = tmp_path / "data"
    cell = 100
    seed = offline.seed_text([0] * 154, (0, 0, 0), False, False, None, None, [(cell, 103)])
    (data / "ZexionAP" / "seeds").mkdir(parents=True)
    (data / "ZexionAP" / "seeds" / "S-0-1.txt").write_text(seed)
    started = Game(HOOKTEST, data)
    try:
        at = config_addresses()
        with started.connect() as sock:  # reads only: no config
            assert ask(sock, f"write {at['rando_marker']:#x} 0100") == "written 2"  # its save
            frames = int(ask(sock, "ping").split("frames=")[1])
            wait_for(sock, "ping", lambda r: int(r.split("frames=")[1]) > frames + 5)
            assert ask(sock, f"write {at['map_grid'] + cell:#x} 1b") == "written 1"  # picked up
            assert wait_for(sock, "trap_status", lambda r: "set 103" in r).endswith("set 103")
    finally:
        if started.proc.poll() is None:
            started.proc.kill()
            started.proc.wait()


def test_an_ap_save_gets_its_look_with_no_client_and_its_notices(tmp_path: Path) -> None:
    # Game::watch_offline: a known seed's save in play with no client is dressed (here INTEL
    # CHIP) with the not-connected notice; a client's config takes the look back, `client_back`
    # pops up CLIENT CONNECTED., and `other_save` dresses it again with that client.
    from client import offline

    data = tmp_path / "data"
    seed = offline.seed_text([0] * 154, (0, 0, 0), True, False, None, None)
    (data / "ZexionAP" / "seeds").mkdir(parents=True)
    (data / "ZexionAP" / "seeds" / "S-0-1.txt").write_text(seed)
    started = Game(HOOKTEST, data)
    try:
        at = config_addresses()
        record = stand_in("zap_record_table") + 49 * 100
        shown = stand_in("zap_shown")

        def says(sock: socket.socket, words: str) -> str:
            # The popup's text as the stand-in keeps it: its line breaks dropped.
            want = words.replace(" ", "")
            reply = wait_for(
                sock, f"read {shown:#x} 64", lambda r: want in r.upper().replace(" ", ""), 5.0
            )
            return words if want in reply.upper().replace(" ", "") else reply

        with started.connect() as sock:
            assert wait_for(sock, "assists free", lambda r: r == "free") == "free"  # configured
            assert ask(sock, f"write {at['rando_marker']:#x} 0100") == "written 2"  # its save
            assert "NOT CONNECTED" in says(sock, "NOT CONNECTED")
            assert wait_for(sock, f"read {record + 0x0A:#x} 10", lambda r: "INTEL" in r) == (
                "data INTEL CHIP"
            )
            config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
            assert ask(sock, config) == "ok"  # a client: its own look, the game's record
            wait_for(sock, f"read {record + 0x0A:#x} 8", lambda r: r == "data AREA MAP")
            assert ask(sock, "client_back") == "shown"
            assert "CLIENT CONNECTED" in says(sock, "CLIENT CONNECTED")
            assert ask(sock, "client_back") == "none"  # once
            assert ask(sock, "other_save") == "told"  # a client of another seed
            assert "ANOTHER SEED" in says(sock, "ANOTHER SEED")
            wait_for(sock, f"read {record + 0x0A:#x} 10", lambda r: "INTEL" in r, 5.0)
            assert read_at(sock, record + 0x0A, 10) == b"INTEL CHIP"
        with started.connect() as sock:  # that client gone: dressed afresh, its notice again
            assert "NOT CONNECTED" in says(sock, "NOT CONNECTED")
            assert read_at(sock, record + 0x0A, 10) == b"INTEL CHIP"
    finally:
        if started.proc.poll() is None:
            started.proc.kill()
            started.proc.wait()


def test_with_no_client_nothing_runs_while_the_player_pointer_reads_elsewhere(
    tmp_path: Path,
) -> None:
    # Configured from the game's symbols, the helper touches the game only while its player
    # pointer reads the player state the config's inventory lies in, as the client's attach
    # checks: a pickup meanwhile goes off once it reads there again.
    from client import offline

    data = tmp_path / "data"
    cell = 100
    seed = offline.seed_text([0] * 154, (0, 0, 0), False, False, None, None, [(cell, 103)])
    (data / "ZexionAP" / "seeds").mkdir(parents=True)
    (data / "ZexionAP" / "seeds" / "S-0-1.txt").write_text(seed)
    started = Game(HOOKTEST, data)
    try:
        at = config_addresses()
        pointer = stand_in("zap_main_character")
        with started.connect() as sock:
            player = read_at(sock, pointer, 8)
            # Configured itself (a request that needs it taken), its trap's cell looked at.
            assert wait_for(sock, "assists free", lambda r: r == "free") == "free"
            assert ask(sock, f"write {at['rando_marker']:#x} 0100") == "written 2"
            frames = int(ask(sock, "ping").split("frames=")[1])
            wait_for(sock, "ping", lambda r: int(r.split("frames=")[1]) > frames + 5)
            assert ask(sock, f"write {pointer:#x} {'00' * 8}") == "written 8"  # elsewhere
            assert ask(sock, f"write {at['map_grid'] + cell:#x} 1b") == "written 1"  # picked up
            time.sleep(1.0)
            assert "set 103" not in ask(sock, "trap_status")
            assert ask(sock, f"write {pointer:#x} {player.hex()}") == "written 8"  # back
            assert wait_for(sock, "trap_status", lambda r: "set 103" in r).endswith("set 103")
    finally:
        if started.proc.poll() is None:
            started.proc.kill()
            started.proc.wait()


def test_a_seed_taken_but_not_kept_stays_the_helpers_when_its_client_goes(tmp_path: Path) -> None:
    # No folder (the data folder and HOME not absolute): `error no folder`, the seed the helper's
    # all the same, and still once the client has gone and the files were read again.
    from client import offline

    cell = 100
    seed = offline.seed_text([0] * 154, (0, 0, 0), False, False, None, None, [(cell, 103)])
    put = offline.put_request("seed", seed, offline.seed_file_name("S", 0, 1))
    started = Game(HOOKTEST, Path("relative"), HOME="relative")
    try:
        at = config_addresses()
        with started.connect() as sock:
            config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
            assert ask(sock, config) == "ok"
            assert ask(sock, put) == "error no folder"
        with started.connect() as sock:  # the client gone, the files read again: none
            assert ask(sock, f"write {at['rando_marker']:#x} 0100") == "written 2"
            frames = int(ask(sock, "ping").split("frames=")[1])
            wait_for(sock, "ping", lambda r: int(r.split("frames=")[1]) > frames + 5)
            assert ask(sock, f"write {at['map_grid'] + cell:#x} 1b") == "written 1"
            assert wait_for(sock, "trap_status", lambda r: "set 103" in r).endswith("set 103")
    finally:
        if started.proc.poll() is None:
            started.proc.kill()
            started.proc.wait()


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_the_title_lines_wait_for_the_title(game: Game) -> None:
    # Game::watch_title runs with no save loaded: the stand-in's title layer is empty (null), so
    # nothing is made and nothing is said wrong (the lines themselves: stand-in tests on Windows,
    # native/tests/title_tests.cpp).
    at = config_addresses()
    with game.connect() as sock:
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        assert ask(sock, f"write {at['save_loaded']:#x} ff") == "written 1"  # no save: the title
        frames = int(ask(sock, "ping").split("frames=")[1])
        wait_for(sock, "ping", lambda r: int(r.split("frames=")[1]) > frames + 20)
        assert ask(sock, "title_status") == "0"


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_with_no_seed_file_the_helper_waits_for_a_client(game: Game) -> None:
    with game.connect() as sock:
        frames = int(ask(sock, "ping").split("frames=")[1])
        wait_for(sock, "ping", lambda r: int(r.split("frames=")[1]) > frames + 5)
        assert ask(sock, "intel on") == "refused not configured"


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_in_the_labels_seeds_save_the_map_marks_item_48_as_a_major(game: Game) -> None:
    # The item update's two tests of the ID patched (stand-in: the Linux build's bytes), "3 to 35"
    # made "48, or 3 to 35", while the labels' seed's save is loaded; put back when the client goes.
    from client import aplabels

    at = config_addresses()
    tests = (at["seen_marker_code"] + 3, at["seen_marker_code"] + 0x112 + 3)  # cmp ax, imm8
    with game.connect() as sock:
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        assert [read_at(sock, t, 1) for t in tests] == [b"\x02", b"\x02"]
        assert ask(sock, f"write {at['rando_marker']:#x} 0100") == "written 2"  # a seed's save
        labels = aplabels.labels_request({(3, 2): "Hookshot\nfor Alice"})
        assert ask(sock, labels) == "labeled 1"
        wait_for(sock, f"read {tests[0]:#x} 1", lambda r: r == "data 0")  # 0x30: cmp ax, 48
        assert [read_at(sock, t, 1) for t in tests] == [b"0", b"0"]
    with game.connect() as sock:
        wait_for(sock, f"read {tests[0]:#x} 1", lambda r: r == "data \x02")
        assert [read_at(sock, t, 1) for t in tests] == [b"\x02", b"\x02"]


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_in_a_translation_a_popup_shows_in_its_font(game: Game) -> None:
    # The text in a SmallText entry the game never reads (66), the message given it as the
    # game's pickup gives its own, with the pickup's ints (the Linux build reads them).
    at = config_addresses()
    with game.connect() as sock:
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        assert ask(sock, f"write {at['tr_flag']:#x} 01") == "written 1"  # a translation on
        assert ask(sock, "apply\npopup 3 from Tester") == "applied"
        ints = stand_in("zap_hd_ints")
        wait_for(sock, f"read {ints:#x} 4", lambda r: r != "data " + "\x00" * 4)
        values = struct.unpack("<8i", read_at(sock, ints, 32))
        assert values[:6] == (24, 160, 88, 100, 1, 180)
        assert values[6] in (66, 70) and values[7] == 0  # its entry, SmallText (kind 0)
        shown = read_at(sock, stand_in("zap_hd_shown"), 4 * 64).decode("utf-32-le")
        shown = shown.split("\x00")[0]
        assert shown == "Machine Gun\nFROM TESTER"  # the translation's own name, in its case
        entry = 200 + (0 if values[6] == 66 else 140)
        written = read_at(sock, stand_in("zap_small") + 4 * entry, 4 * 64).decode("utf-32-le")
        assert written.split("\x00")[0] == shown, "the entry the message reads holds the text"


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_the_disks_count_writes_the_scanners_line_and_takes_the_keys_row(game: Game) -> None:
    # The scanner's Text entry 12 the client's line (in capitals), and the pause screen's KEYS
    # branch made a jmp (the Linux build's `jne +0x3C`), both put back with the goal off and when
    # the client goes.
    at = config_addresses()
    scanner = stand_in("zap_texts") + 12 * 32
    branch = at["keys_branch"]
    with game.connect() as sock:
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        line = b"4 OS Install Disks detected!".hex()
        assert ask(sock, f"discs 1 4 {line} 1") == "discs on"
        wait_for(sock, f"read {scanner:#x} 4", lambda r: r != "data ENER")
        assert read_at(sock, scanner, 32).split(b"\x00")[0] == b"4 OS INSTALL DISKS DETECTED!"
        wait_for(sock, f"read {branch:#x} 1", lambda r: r == "data \xeb")
        assert read_at(sock, branch, 2) == b"\xeb\x3c"
        assert ask(sock, "discs_status") is not None
    with game.connect() as sock:  # the client went: the game's again
        wait_for(sock, f"read {branch:#x} 1", lambda r: r == "data u")
        assert read_at(sock, branch, 2) == b"\x75\x3c"
        wait_for(sock, f"read {scanner:#x} 4", lambda r: r == "data ENER")
        assert read_at(sock, scanner, 32).split(b"\x00")[0] == b"ENERGY KEYS DETECTED!"


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_a_received_scraps_drop_is_spawned_after_its_popup(game: Game) -> None:
    # As its pickup does (Game::pump_drops): the type-11 handler called with the player's cell
    # (row 2, column 3) and the item's event from the table, once the player has been in play in
    # one cell for a second: Scrap Metal's, Scrap Energy's, then Scrap Chunk's, with its fade's
    # event checked too.
    at = config_addresses()
    events = stand_in("zap_event_records")
    calls = stand_in("zap_spawn_count")
    with game.connect() as sock:
        assert ask(sock, "drop_status") == "queued 0; last none"
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        for item, event, count in ((46, 3548, 1), (47, 3549, 2), (48, 3550, 3)):
            assert ask(sock, f"apply\npopup {item} from Tester") == "applied"
            want = f"queued 0; last spawned {item}"
            assert wait_for(sock, "drop_status", lambda r, want=want: r == want) == want
            assert read_at(sock, calls, 4) == count.to_bytes(4, "little")
            assert read_at(sock, stand_in("zap_spawn_cell"), 4) == (2 * 256 + 3).to_bytes(
                4, "little"
            )
            spawned = int.from_bytes(read_at(sock, stand_in("zap_spawn_event"), 8), "little")
            assert spawned == events + event * 0x28
        # A notice (the client's own text, no item counted) spawns nothing.
        assert ask(sock, "apply\nnotice 47 Tester's text") == "applied"
        time.sleep(1.5)
        assert read_at(sock, calls, 4) == (3).to_bytes(4, "little")


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_a_received_energy_shield_is_spawned_once_the_player_has_settled(game: Game) -> None:
    # `shield` (Game::spawn_shield): event 3553 through the type-11 handler with the player's
    # cell, once in play in one cell for a second ("refused not settled" until then).
    at = config_addresses()
    calls = stand_in("zap_spawn_count")
    with game.connect() as sock:
        assert ask(sock, "shield") == "refused not configured"
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        assert wait_for(sock, "shield", lambda r: r != "refused not settled") == "spawned"
        assert read_at(sock, calls, 4) == (1).to_bytes(4, "little")
        spawned = int.from_bytes(read_at(sock, stand_in("zap_spawn_event"), 8), "little")
        assert spawned == stand_in("zap_event_records") + 3553 * 0x28
        # A received one's popup spawns nothing itself: the client asks for it (`shield`).
        assert ask(sock, "apply\npopup 50 from Tester") == "applied"
        time.sleep(1.5)
        assert read_at(sock, calls, 4) == (1).to_bytes(4, "little")


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_a_deathlink_death_is_dealt_as_the_damage_code_deals_one(game: Game) -> None:
    # `die` (Game::kill): energy 0, then the death routine with the player and kind 0, once in
    # play in one cell for a second; refused while the game is halted.
    at = config_addresses()
    deaths = stand_in("zap_death_count")
    with game.connect() as sock:
        assert ask(sock, "die") == "refused not configured"
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        halt = at["halt_flag"]
        assert ask(sock, f"write {halt:#x} 01") == "written 1"
        assert wait_for(sock, "die", lambda r: r != "refused not settled") == "refused halted"
        assert ask(sock, f"write {halt:#x} 00") == "written 1"
        assert ask(sock, "die") == "died"
        assert read_at(sock, deaths, 4) == (1).to_bytes(4, "little")
        player = int.from_bytes(read_at(sock, stand_in("zap_death_player"), 8), "little")
        assert player == stand_in("zap_player")
        assert read_at(sock, stand_in("zap_death_kind"), 4) == (0).to_bytes(4, "little")
        assert player_word(sock, at, ENERGY) == 0


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_intel_chips_name_the_area_map_until_the_client_goes(game: Game) -> None:
    # `intel on` (Game::watch_intel): record 49 named INTEL CHIP with its map reveal (3551) off,
    # while on; `intel off`, or the client going, puts the game's back.
    at = config_addresses()
    record = stand_in("zap_record_table") + 49 * 100
    with game.connect() as sock:
        assert ask(sock, "intel on") == "refused not configured"
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        assert ask(sock, "intel on") == "intel on"
        named = wait_for(sock, f"read {record + 0x0A:#x} 10", lambda r: "INTEL" in r)
        assert named.startswith("data INTEL CHIP")
        assert read_at(sock, record + 0x40, 2) == (-1).to_bytes(2, "little", signed=True)
        assert ask(sock, "intel off") == "intel off"
        wait_for(sock, f"read {record + 0x0A:#x} 8", lambda r: r == "data AREA MAP")
        assert read_at(sock, record + 0x40, 2) == (3551).to_bytes(2, "little")
        assert ask(sock, "intel on") == "intel on"
        wait_for(sock, f"read {record + 0x0A:#x} 10", lambda r: "INTEL" in r)
    with game.connect() as sock:  # the last client gone: the game's record again
        reply = wait_for(sock, f"read {record + 0x0A:#x} 8", lambda r: r == "data AREA MAP")
        assert reply == "data AREA MAP"


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_the_assist_lock_is_taken_once_configured(game: Game) -> None:
    # `assists lock` and `assists free` (Game::set_assists_locked; the lock itself needs the
    # options menu, which the stand-in never opens: stand-in tests of it in
    # native/tests/menu_tests.cpp, on Windows).
    at = config_addresses()
    with game.connect() as sock:
        assert ask(sock, "assists lock") == "refused not configured"
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        assert ask(sock, "assists lock") == "locked"
        assert ask(sock, "assists free") == "free"
        assert ask(sock, "assists lock") == "locked"


@pytest.mark.parametrize("game", [HOOKTEST], indirect=True)
def test_return_home_is_asked_for_and_dropped_with_the_disks(game: Game) -> None:
    # Taken as the Windows helper takes it, and watched each frame call: away from home's cell it
    # lapses; at home (the stand-in player's cell, row 2 and column 3: the request in tiles) it
    # waits, as the stand-in has no event 3667 or prompt to open (stand-in tests of the prompt
    # itself: native/tests/mission_tests.cpp), until the goal turns off, which drops it.
    at = config_addresses()
    with game.connect() as sock:
        assert ask(sock, "mission_prompt 191 329") == "refused not configured"
        config = "config " + " ".join(f"{k}={v:#x}" for k, v in at.items())
        assert ask(sock, config) == "ok"
        assert ask(sock, "mission_prompt 9999 1") == "error bad mission_prompt request"
        line = b"4 OS Install Disks detected!".hex()
        assert ask(sock, f"discs 4 4 {line} 1") == "discs on"
        assert ask(sock, "mission_prompt 191 329") == "queued"  # the Landing Pod: not here
        lapsed = wait_for(sock, "mission_status", lambda r: r.startswith("lapsed"))
        assert lapsed == "lapsed: the player left the landing pod"
        assert ask(sock, "mission_prompt 42 40") == "queued"  # home: this cell
        assert not ask(sock, "mission_status").startswith("lapsed")
        assert ask(sock, "discs") == "discs off"
        assert wait_for(sock, "mission_status", lambda r: r == "not asked") == "not asked"
