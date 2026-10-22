"""Load, check and unload the native helper (native/) in the running game, for live tests.

    helper.py status            whether the helper is loaded; if so, its hello and frame rate
    helper.py load --yes        load the built helper into the game (Zexion must be a known build)
    helper.py unload --yes      ask the helper to unhook and unload itself

``load`` and ``unload`` change the running game: ask the person playing first, every time;
``--yes`` records that they agreed. Every attempt and its outcome is appended to
``lab/helper.jsonl`` before and after. ``load`` refuses a build ``client/game.py`` does not
know, and the helper itself hooks nothing in an unknown build. ``ZEXION_DLL`` points at
another build of the helper than native/build/Release/zexion_ap.dll.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "client"))

import game  # noqa: E402
import native  # noqa: E402
from protected import assert_writable, lab_dir  # noqa: E402


def log(entry: dict) -> None:
    """Append to lab/helper.jsonl; a failure here stops the change it describes."""
    path = assert_writable(lab_dir() / "helper.jsonl")
    entry = {"time": dt.datetime.now().isoformat(timespec="milliseconds"), **entry}
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as f:
        f.write(json.dumps(entry) + "\n")


def frames(helper: native.Helper) -> int:
    return int(helper.request("ping").split("frames=")[1])


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


def cmd_load(args: argparse.Namespace, g: game.Game) -> int:
    if not args.yes:
        print("load changes the running game; pass --yes once the person playing has agreed")
        return 1
    dll = native.default_dll()
    log(
        {"event": "attempt", "action": "load", "pid": g.pid, "build": g.build.name, "dll": str(dll)}
    )
    try:
        native.load(g.pm.process_handle, dll)
        with native.Helper(g.pid) as helper:
            hello = helper.request("hello")
    except native.HelperError as exc:
        log({"event": "failed", "action": "load", "pid": g.pid, "error": str(exc)})
        print(f"not loaded: {exc}")
        return 1
    log({"event": "loaded", "pid": g.pid, "hello": hello})
    print("loaded:", hello)
    return 0


def cmd_unload(args: argparse.Namespace, g: game.Game) -> int:
    if not args.yes:
        print("unload changes the running game; pass --yes once the person playing has agreed")
        return 1
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
    gone = native.wait_unloaded(g.pm.process_handle)
    log({"event": "unloaded" if gone else "still-loaded", "pid": g.pid, "reply": reply})
    print("unloaded" if gone else "the helper said bye but is still loaded")
    return 0 if gone else 2


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
    try:
        g = game.Game.attach()
    except game.GameError as exc:
        print(f"{exc}")
        return 1
    return args.func(args, g)


if __name__ == "__main__":
    sys.exit(main())
