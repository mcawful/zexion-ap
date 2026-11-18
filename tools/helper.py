"""Load, check and unload the native helper (native/) in the running game, for live tests.

    helper.py status            whether the helper is loaded; if so, its hello and frame rate
    helper.py load --yes        load the built helper into the game (Zexion must be a known build)
    helper.py unload --yes      ask the helper to unhook and unload itself

``load`` and ``unload`` change the running game: ask the person playing first, every time;
``--yes`` records that they agreed. Every attempt and its outcome is appended to
``lab/helper.jsonl`` before and after. ``load`` refuses a build ``client/game.py`` does not
know, and the helper itself hooks nothing in an unknown build. ``ZEXION_DLL`` points at
another build of the helper than native/build/Release/zexion_ap.dll.

On Linux, with the game's Linux build, only ``status`` runs: the Linux helper is preloaded by
the game's launch options (native/README.md, "The Linux helper"), not loaded into a running
game. ``status`` there also reads the game's ``.rodata`` through the helper and compares it
with the binary's, which says the binary is at its link addresses.
"""

from __future__ import annotations

import argparse
import sys
import time

from protected import ROOT, append_log, lab_dir

sys.path.insert(0, str(ROOT))  # the client package
from client import elf, game, linux_helper, native, procfs  # noqa: E402

LINUX = sys.platform.startswith("linux")


def log(entry: dict) -> None:
    """Append to lab/helper.jsonl; a failure here stops the change it describes."""
    append_log(lab_dir() / "helper.jsonl", entry)


def frames(helper: native.Helper) -> int:
    reply = helper.request("ping")
    if not reply.startswith("pong frames=") or not reply[len("pong frames=") :].isdigit():
        raise native.HelperError(f"unexpected reply to ping: {reply!r}")
    return int(reply[len("pong frames=") :])


def cmd_status(_: argparse.Namespace, g: game.Game) -> int:
    if not native.is_loaded(g.pm.process_handle):
        print("helper: not loaded")
        return 0
    with native.Helper(g.pid) as helper:
        print("helper:", helper.request("hello"))
        first = frames(helper)
        time.sleep(1.0)
        print(f"frames: {first}, then {frames(helper) - first} in the next second")
    return 0


def cmd_load(_: argparse.Namespace, g: game.Game) -> int:
    dll = native.default_dll()
    log(
        {"event": "attempt", "action": "load", "pid": g.pid, "build": g.build.name, "dll": str(dll)}
    )
    try:
        native.load(g.pm.process_handle, g.pid, dll)
        with native.Helper(g.pid) as helper:
            hello = helper.request("hello")
    except native.HelperError as exc:
        log({"event": "failed", "action": "load", "pid": g.pid, "error": str(exc)})
        print(f"not loaded: {exc}")
        return 1
    log({"event": "loaded", "pid": g.pid, "hello": hello})
    print("loaded:", hello)
    return 0


def cmd_unload(_: argparse.Namespace, g: game.Game) -> int:
    if not native.is_loaded(g.pm.process_handle):
        print("helper: not loaded")
        return 0
    log({"event": "attempt", "action": "unload", "pid": g.pid})
    try:
        with native.Helper(g.pid) as helper:
            reply = helper.request("unload")
    except native.HelperError as exc:
        log({"event": "failed", "action": "unload", "pid": g.pid, "error": str(exc)})
        print(f"not unloaded: {exc}")
        return 1
    try:
        gone = native.wait_unloaded(g.pm.process_handle)
    except native.GameExited:
        log({"event": "game-exited", "action": "unload", "pid": g.pid, "reply": reply})
        print("the game exited during the unload")
        return 2
    log({"event": "unloaded" if gone else "still-loaded", "pid": g.pid, "reply": reply})
    print("unloaded" if gone else "the helper said bye but is still loaded")
    return 0 if gone else 2


def linux_status(g: linux_helper.LinuxGame) -> int:
    print(f"game: pid {g.pid} (pid {g.inner_pid} in its own namespace), Linux build {g.build.name}")
    with linux_helper.LinuxHelper(g) as helper:
        print("helper:", helper.request("hello"))
        first = helper.frames()
        time.sleep(1.0)
        print(f"frames: {first}, then {helper.frames() - first} in the next second")
        rodata = g.image.section(".rodata")
        size = min(rodata.size, 4096)
        try:
            same = helper.read(rodata.addr, size) == g.image.read(rodata.addr, size)
        except linux_helper.LinuxHelperError as exc:
            print(f"memory: not readable ({exc})")
            return 1
        where = "at its link addresses" if same else "NOT as in the binary"
        print(f"memory: readable; .rodata ({size} bytes at {rodata.addr:#x}) {where}")
        return 0 if same else 1


def linux_main(command: str) -> int:
    if command != "status":
        print(
            f"{command} is for Windows: on Linux the helper is preloaded by the game's launch "
            'options (native/README.md, "The Linux helper")'
        )
        return 1
    try:
        return linux_status(linux_helper.find_game())
    except (linux_helper.LinuxHelperError, procfs.ProcessError, elf.ELFError) as exc:
        print(f"{exc}")
        return 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status").set_defaults(func=cmd_status)
    for name, func in (("load", cmd_load), ("unload", cmd_unload)):
        p = sub.add_parser(name)
        p.add_argument("--yes", action="store_true")
        p.set_defaults(func=func)
    args = parser.parse_args(argv)
    if LINUX:
        return linux_main(args.command)
    if not getattr(args, "yes", True):  # load and unload
        print(f"{args.command} changes the running game; pass --yes once the player has agreed")
        return 1
    try:
        g = game.Game.attach()
    except game.GameError as exc:
        print(f"{exc}")
        return 1
    try:
        return args.func(args, g)
    except native.HelperError as exc:  # status, or anything the commands did not catch
        print(f"helper: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
