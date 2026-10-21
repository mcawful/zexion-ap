"""Check client/patterns.py against a copy of the exe, or against the running game.

    check_patterns.py EXE      resolve every pattern in a copy of Zexion.exe (read-only)
    check_patterns.py --live   attach to the running game, locate everything, show the state

Offline, each target must resolve (every pattern once, all agreeing). On build B1 it must also
land on the recorded B1 address; on another build a difference is expected and is reported for
docs/re/memory.md. Live, it reads (never writes) the pickup counter, the inventory, the map
and the loaded seed's first location cell.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "client"))

import game  # noqa: E402
from patterns import KNOWN_BUILDS, TARGETS  # noqa: E402


def offline(exe: Path) -> int:
    data = exe.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    build = next((b for b in KNOWN_BUILDS if b.sha256 == digest), None)
    print(f"build: {build.name if build else 'unknown'} (SHA-256 {digest[:12]})")
    text_rva, text_size = game.code_section(data[:0x1000])
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    raw = None
    for i in range(nsec):
        o = pe + 24 + optsz + 40 * i
        if data[o : o + 8].rstrip(b"\0") == b".text":
            raw = struct.unpack_from("<I", data, o + 20)[0]
    code = data[raw : raw + text_size]
    try:
        found = game.locate_all(code, text_rva)
    except game.PatternError as exc:
        print(f"FAILED: {exc}")
        return 1
    status = 0
    for t in TARGETS:
        same = found[t.name] == t.b1_rva
        print(f"{t.name:<15} RVA {found[t.name]:#x}  {'matches B1' if same else 'differs from B1'}")
        if build is not None and build.name == "B1" and not same:
            print(f"  FAILED: this is B1, so {t.name} must be at {t.b1_rva:#x}")
            status = 1
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
