"""Parse Zexion's RandoCells file and check which locations a seed can reach.

The format and the rules below are from docs/re/randocells.md; read its statuses before
relying on them. In short: 54 x 54 cells, row-major (index = y * 54 + x), 85 bytes each:

    +0x00  4 x 8  requirement lists for leaving the cell to the N, S, W, E
    +0x20  4 x 8  the tier/count byte for each list entry
    +0x40  4      passage to the S, W, E, N: 0 open, 1 needs the list, 0xFF wall
    +0x44  1      1 when collecting this cell's item has its own requirements
    +0x45  8      those requirements
    +0x4D  8      their tier/count bytes

A list entry is an item ID (0-49) or a special condition (50-69, meaning not yet known).
Tier/count byte: 1 (and 3, 4, assumed) = this entry alone is enough; 2 = part of the AND
group; 11-40 = alone, needs (value - 10) of the item; 41-90 = in the AND group, needs
(value - 40) of the item.

    randocells.py SAVE.sav [--cells FILE] [--specials never|always] [--start X,Y]
        how many of the seed's 154 locations are reachable
"""

from __future__ import annotations

import argparse
import struct
import sys
from collections import Counter
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

W = H = 54
RECORD = 85
LIST_OFFSET = {"N": 0x00, "S": 0x08, "W": 0x10, "E": 0x18}
PASSAGE_OFFSET = {"S": 0x40, "W": 0x41, "E": 0x42, "N": 0x43}
STEP = {"N": (0, -1), "S": (0, 1), "W": (-1, 0), "E": (1, 0)}
ITEM_FLAG, ITEM_LIST, ITEM_TIERS = 0x44, 0x45, 0x4D
WALL, OPEN, CONDITIONAL = 0xFF, 0, 1
FIRST_SPECIAL = 50
START = (25, 25)

# Where a randomizer save keeps its seed (docs/re/save-format.md).
SAVE_CELLS, SAVE_ITEMS, LOCATIONS = 0x12814, 0x12A80, 154

Entry = tuple[int, int]  # (item or special code, tier/count byte)


@dataclass(frozen=True)
class Cell:
    lists: dict[str, list[Entry]]
    passage: dict[str, int]
    item_gated: bool
    item_needs: list[Entry]


def _entries(rec: bytes, base: int, tiers: int) -> list[Entry]:
    out = []
    for k in range(8):
        if rec[base + k] == 0xFF:
            break
        out.append((rec[base + k], rec[tiers + k]))
    return out


def parse(data: bytes) -> list[list[Cell]]:
    """Cells as grid[y][x]."""
    if len(data) != W * H * RECORD:
        raise ValueError(f"RandoCells should be {W * H * RECORD} bytes, got {len(data)}")
    grid = []
    for y in range(H):
        row = []
        for x in range(W):
            rec = data[(y * W + x) * RECORD : (y * W + x + 1) * RECORD]
            row.append(
                Cell(
                    lists={d: _entries(rec, o, o + 0x20) for d, o in LIST_OFFSET.items()},
                    passage={d: rec[o] for d, o in PASSAGE_OFFSET.items()},
                    item_gated=rec[ITEM_FLAG] == 1,
                    item_needs=_entries(rec, ITEM_LIST, ITEM_TIERS),
                )
            )
        grid.append(row)
    return grid


def satisfied(entries: Sequence[Entry], have: Counter, special: Callable[[int], bool]) -> bool:
    """Any standalone entry is enough; otherwise every AND-group entry must hold."""

    def ok(code: int, count: int = 1) -> bool:
        return special(code) if code >= FIRST_SPECIAL else have[code] >= count

    group = []
    for code, tier in entries:
        if tier in (1, 3, 4) and ok(code):
            return True
        if 11 <= tier <= 40 and ok(code, tier - 10):
            return True
        if tier == 2:
            group.append((code, 1))
        elif tier > 40:
            group.append((code, tier - 40))
    return bool(group) and all(ok(c, n) for c, n in group)


def reachable_cells(grid, have: Counter, special, start=START) -> set[tuple[int, int]]:
    seen, todo = {start}, [start]
    while todo:
        x, y = todo.pop()
        cell = grid[y][x]
        for d, (dx, dy) in STEP.items():
            nx, ny = x + dx, y + dy
            if not (0 <= nx < W and 0 <= ny < H) or (nx, ny) in seen:
                continue
            passage = cell.passage[d]
            if passage == WALL:
                continue
            if passage == CONDITIONAL and not satisfied(cell.lists[d], have, special):
                continue
            seen.add((nx, ny))
            todo.append((nx, ny))
    return seen


def sweep(grid, locations, items, special, start=START) -> set[int]:
    """Collect everything reachable, repeating until nothing new opens up."""
    have: Counter = Counter()
    got: set[int] = set()
    while True:
        cells = reachable_cells(grid, have, special, start)
        new = False
        for i, (x, y) in enumerate(locations):
            if i in got or (x, y) not in cells:
                continue
            cell = grid[y][x]
            if cell.item_gated and not satisfied(cell.item_needs, have, special):
                continue
            got.add(i)
            have[items[i]] += 1
            new = True
        if not new:
            return got


def seed_from_save(data: bytes) -> tuple[list[tuple[int, int]], list[int]]:
    locations = [struct.unpack_from("<HH", data, SAVE_CELLS + 4 * i) for i in range(LOCATIONS)]
    return locations, list(data[SAVE_ITEMS : SAVE_ITEMS + LOCATIONS])


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("save", type=Path, help="a randomizer .sav (use a copy in lab/)")
    parser.add_argument("--cells", type=Path, help="RandoCells copy (default: newest in lab/data/)")
    parser.add_argument("--specials", choices=("never", "always"), default="always")
    parser.add_argument("--start", default="25,25")
    args = parser.parse_args(argv)
    cells_path = args.cells or max(
        Path("lab/data").glob("*/RandoCells"), key=lambda p: p.stat().st_mtime
    )
    grid = parse(cells_path.read_bytes())
    locations, items = seed_from_save(args.save.read_bytes())
    special = (lambda c: True) if args.specials == "always" else (lambda c: False)
    start = tuple(int(v) for v in args.start.split(","))
    got = sweep(grid, locations, items, special, start)
    print(f"{len(got)}/{LOCATIONS} locations reachable (specials {args.specials})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
