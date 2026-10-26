"""Check client/patterns.py against a copy of the exe, or against the running game.

    check_patterns.py EXE      resolve every pattern in a copy of Zexion.exe (read-only)
    check_patterns.py --live   attach to the running game, locate everything, show the state

Offline, each target must resolve (every pattern once, all agreeing). On a known build it must
also land on the address recorded for that build (``Target.rvas`` in client/patterns.py, by the
build's name); an unknown build's addresses, or ones not yet recorded, are reported for
docs/re/memory.md. Live, it reads (never writes) the game state, the pickup counter, the inventory,
the map and the loaded seed's first location cell.
"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))  # the client package

from client import game, pe  # noqa: E402
from client.patterns import KNOWN_BUILDS, TARGETS  # noqa: E402


def offline(exe: Path) -> int:
    data = exe.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    build = next((b for b in KNOWN_BUILDS if b.sha256 == digest), None)
    print(f"build: {build.name if build else 'unknown'} (SHA-256 {digest[:12]})")
    text = pe.parse(data).section(".text")
    text_rva, code = text.va, data[text.raw : text.raw + text.vsize]
    try:
        found = game.locate_all(code, text_rva)
    except game.PatternError as exc:
        print(f"FAILED: {exc}")
        return 1
    status = 0
    for t in TARGETS:
        expected = t.rvas.get(build.name) if build is not None else None
        if expected is None:
            note = "not recorded for this build"
        elif found[t.name] == expected:
            note = f"matches {build.name}"
        else:
            note = f"FAILED: on {build.name} it must be at {expected:#x}"
            status = 1
        print(f"{t.name:<15} RVA {found[t.name]:#x}  {note}")
    if build is None:
        print("unknown build: record the new addresses in docs/re/memory.md before trusting them")
    return status


def live() -> int:
    try:
        g = game.Game.attach()
    except game.GameError as exc:
        print(f"FAILED: {exc}")
        return 1
    print(f"build: {g.build.name}")
    for name, rva in g.rva.items():
        print(f"{name:<15} Zexion.exe+{rva:#x}")
    inv = g.inventory()
    owned = {i: v for i, v in enumerate(inv) if v}
    grid = g.map_grid()
    print(f"state: {g.state()}; energy {g.energy()}")
    print(f"pickup counter {g.pickup_counter()}; inventory {owned}")
    print(
        f"map: {sum(1 for v in grid if v)} cells explored, "
        f"{sum(1 for v in grid if v == game.COLLECTED)} collected"
    )
    try:
        seed = g.seed()
    except game.GameError as exc:
        print(f"seed: not readable ({exc}); is a randomizer save loaded?")
    else:
        print(f"seed: {len(seed.locations)} locations, first at {seed.locations[0]}, checks out")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", nargs="?", type=Path)
    parser.add_argument("--live", action="store_true")
    args = parser.parse_args(argv)
    if args.live:
        return live()
    if args.exe is None:
        parser.error("give a copy of Zexion.exe, or --live")
    return offline(args.exe)


if __name__ == "__main__":
    sys.exit(main())
