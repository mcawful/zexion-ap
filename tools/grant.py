"""Grant items in the running game by hand, and watch the inventory, for live grant tests.

    grant.py show                              the state, the inventory by name, the pickup counter
    grant.py set ITEM VALUE --expect N --yes   set one item's inventory byte (compare-and-set)
    grant.py watch [--seconds S] [--interval I]   print inventory, counter and state changes

ITEM is an item ID (0 to 49) or its name from the game's ``ItemNames.txt`` ("Alpha Key"; case
and spacing do not matter). ``set`` goes through ``client/game.py``: the build must be known,
every pattern must resolve, the byte must hold ``--expect`` just before the write, and the
write is read back. Every attempt and its outcome is appended to ``lab/grants.jsonl`` first,
so a write that leaves no log line was never made. ``watch`` logs what it sees there too.
Events: ``attempt``, ``written``, ``mismatch`` (written, then changed by the game),
``unknown``, ``unconfirmed``, ``refused``, ``watch``, ``change``, ``watch-ended``. Exit
status: 0 done, 1 refused or failed, 2 written but changed, unconfirmed, or state unknown.

``set`` writes the inventory byte only. For Energy Tanks, Missile Containers and Mega Bombs
that does not change the energy, missile or bomb numbers the game uses (docs/re/memory.md,
"Granting each kind of item"). A grant becomes part of the slot once the game saves, so use
it only on a lab slot, never on a real save.

``set`` refuses unless ``Game.settled_state()`` reads "in play" (a save loaded, no menu open,
the player alive, twice a moment apart); the person playing should still be standing still.
After writing it waits as long again and logs ``unconfirmed`` (exit 2) if the state left
"in play" or the byte no longer holds the value, which is how a reload undoing the write
shows up. ``--yes`` records that they
agreed to the write. The item names are
read from the game folder, read-only; without it, items are shown by ID.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "client"))

import game  # noqa: E402
from protected import assert_writable, game_dir, lab_dir  # noqa: E402

ITEM_NAMES = Path("Files/Translations/English(HD)/ItemNames.txt")


def parse_item_names(text: str) -> dict[int, str]:
    """``N) NAME`` headings from ItemNames.txt, as {ID: name}."""
    return {
        int(m.group(1)): m.group(2).strip()
        for m in re.finditer(r"^[ \t]*(\d+)\)[ \t]*(\S[^\r\n]*?)[ \t\r]*$", text, re.MULTILINE)
    }


def load_item_names() -> dict[int, str]:
    try:
        raw = (game_dir() / ITEM_NAMES).read_bytes()
    except (OSError, RuntimeError):
        return {}
    for encoding in ("utf-16", "utf-8"):
        try:
            return parse_item_names(raw.decode(encoding))
        except UnicodeDecodeError:
            continue
    return {}


def _key(name: str) -> str:
    return re.sub(r"[^a-z0-9]", "", name.lower())


def resolve_item(arg: str, names: dict[int, str]) -> int:
    """An item ID from an ID or an exact name (case, spaces and punctuation ignored)."""
    if arg.isdigit():
        item = int(arg)
        if item not in game.ITEM_IDS:
            raise ValueError(f"item {item} is not an item ID (0 to {game.ITEM_IDS[-1]})")
        return item
    found = [i for i, n in names.items() if _key(n) == _key(arg)]
    if len(found) != 1:
        raise ValueError(f"no single item is named {arg!r}; give its ID instead")
    return found[0]


def label(item: int, names: dict[int, str]) -> str:
    return f"{item} {names[item]}" if item in names else str(item)


def changes(old: bytes, new: bytes, names: dict[int, str]) -> list[str]:
    """One line per inventory byte that differs."""
    return [
        f"{label(i, names)}: {a} -> {b}"
        for i, (a, b) in enumerate(zip(old, new, strict=True))
        if a != b
    ]


def log(entry: dict) -> None:
    """Append to lab/grants.jsonl; a failure here stops the write it describes."""
    path = assert_writable(lab_dir() / "grants.jsonl")
    entry = {"time": dt.datetime.now().isoformat(timespec="milliseconds"), **entry}
    with path.open("a", encoding="utf-8") as f:
        f.write(json.dumps(entry) + "\n")


def cmd_show(args: argparse.Namespace, names: dict[int, str]) -> int:
    g = game.Game.attach()
    inv = g.inventory()
    print(f"build {g.build.name}; {g.state()}; pickup counter {g.pickup_counter()}")
    for i, v in enumerate(inv):
        if v:
            print(f"  {label(i, names)}: {v}")
    return 0


def cmd_set(args: argparse.Namespace, names: dict[int, str]) -> int:
    if not args.yes:
        print("set changes the running game; pass --yes once the person playing has agreed")
        return 1
    item = resolve_item(args.item, names)
    g = game.Game.attach()
    entry = {"build": g.build.name, "pid": g.pid, "item": item, "expect": args.expect}
    entry["value"] = args.value
    log({**entry, "event": "attempt"})
    try:
        state = g.settled_state()
        if state != game.IN_PLAY:
            raise game.GameError(f"the game is not in play ({state}); not written")
        g.set_item(item, expected=args.expect, value=args.value)
        time.sleep(game.SETTLE_SECONDS)
        after, now = g.state(), g.inventory()[item]
    except game.ReadBackMismatch as exc:
        # Checked before GameError: the write was made, so this is not a refusal.
        log({**entry, "event": "mismatch", "read_back": exc.read_back[0], "error": str(exc)})
        print(f"WRITTEN, BUT CHANGED: {exc}")
        return 2
    except game.WriteStateUnknown as exc:
        log({**entry, "event": "unknown", "observed": _observe(g, item), "error": str(exc)})
        print(f"UNKNOWN: {exc}")
        print("Run `grant.py show` before anything else.")
        return 2
    except (game.GameError, ValueError) as exc:
        log({**entry, "event": "refused", "error": str(exc)})
        print(f"REFUSED: {exc}")
        return 1
    if after != game.IN_PLAY or now != args.value:
        log({**entry, "event": "unconfirmed", "state_after": after, "now": now})
        print(f"UNCONFIRMED: written, but then the game was {after} and the byte held {now}")
        return 2
    try:
        log({**entry, "event": "written"})
    except OSError as exc:
        print(f"written and read back, but the log line could not be added: {exc}")
    print(f"{label(item, names)}: {args.expect} -> {args.value} (read back)")
    return 0


def _observe(g: game.Game, item: int) -> int | str:
    """What an item's byte holds now, for the log; the error text if it cannot be read."""
    try:
        return g.inventory()[item]
    except game.GameError as exc:
        return f"unreadable: {exc}"


def _stamp() -> str:
    return dt.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def cmd_watch(args: argparse.Namespace, names: dict[int, str]) -> int:
    """Print and log every inventory and counter change; a change undone within one interval
    is not seen."""
    g = game.Game.attach()
    inv, counter, state = g.inventory(), g.pickup_counter(), g.state()
    owned = {label(i, names): v for i, v in enumerate(inv) if v}
    print(f"{_stamp()}  watching; {state}; counter {counter}; inventory {owned}; Ctrl+C stops")
    log(
        {"event": "watch", "pid": g.pid, "state": state, "counter": counter, "inventory": list(inv)}
    )
    deadline = time.monotonic() + args.seconds if args.seconds else None
    # Every way out logs "watch-ended", so a quiet log cannot be read as "nothing changed"
    # for a time nobody was watching.
    reason, status = "deadline", 0
    try:
        while deadline is None or time.monotonic() < deadline:
            time.sleep(args.interval)
            new_inv, new_counter, new_state = g.inventory(), g.pickup_counter(), g.state()
            lines = changes(inv, new_inv, names)
            if new_counter != counter:
                lines.append(f"pickup counter: {counter} -> {new_counter}")
            if new_state != state:
                lines.append(f"state: {state} -> {new_state}")
            for line in lines:
                print(f"{_stamp()}  {line}")
                try:
                    log({"event": "change", "pid": g.pid, "change": line})
                except OSError as exc:
                    print(f"{_stamp()}  (not logged: {exc})")
            inv, counter, state = new_inv, new_counter, new_state
    except KeyboardInterrupt:
        reason = "stopped"
    except game.GameError as exc:
        print(f"{_stamp()}  FAILED: {exc}")
        reason, status = str(exc), 1
    print(f"{_stamp()}  watch ended: {reason}")
    log({"event": "watch-ended", "pid": g.pid, "reason": reason})
    return status


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("show").set_defaults(func=cmd_show)
    p = sub.add_parser("set")
    p.add_argument("item")
    p.add_argument("value", type=int)
    p.add_argument("--expect", type=int, required=True, help="what the byte must hold now")
    p.add_argument("--yes", action="store_true")
    p.set_defaults(func=cmd_set)
    p = sub.add_parser("watch")
    p.add_argument("--seconds", type=float, default=0, help="stop after this long (0: never)")
    p.add_argument("--interval", type=float, default=0.05)
    p.set_defaults(func=cmd_watch)
    args = parser.parse_args(argv)
    names = load_item_names()
    try:
        return args.func(args, names)
    except ValueError as exc:
        print(f"REFUSED: {exc}")
        return 1
    except game.GameError as exc:
        print(f"FAILED: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
