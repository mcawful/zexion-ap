"""Parse Zexion's RandoCells file and check which locations a seed can reach.

The format is from docs/re/randocells.md; read its statuses before relying on them. The rules
themselves live in worlds/zexion/logic.py, shared with the APWorld. In short: 54 x 54 cells,
row-major (index = y * 54 + x), 85 bytes each:

    +0x00  4 x 8  requirement lists for leaving the cell to the N, S, W, E
    +0x20  4 x 8  the tier/count byte for each list entry
    +0x40  4      passage to the S, W, E, N: 0 open, 1 needs the list, 0xFF wall
    +0x44  1      1 when collecting this cell's item has its own requirements
    +0x45  8      those requirements
    +0x4D  8      their tier/count bytes

A list entry is a code: 0-49 an item, 50-63 derived from the items (``derive``), 64-69 tied
to the cell, the options and energy (``cell_code``; decided before anything else in the
list). Tier/count byte: 1 = enough alone; 2 = part of the group needed together; 3 = this
item does not count here; 4 = required; 11-40 = enough alone with (value - 10) of the item;
41-90 = in the group, with (value - 40). Not modeled yet: Missile and Bomb Boxes adding 10
to a group's count.

    randocells.py SAVE.sav --fresh            a save made at the start of its seed
    randocells.py SAVE.sav --start X,Y        any randomizer save, from one given cell
        [--cells FILE]

--start takes X,Y as the pause screen shows them; cells printed are this repo's (x, y), which
the game shows as X = y, Y = x (docs/re/randocells.md, "Grid order").
"""

from __future__ import annotations

import argparse
import importlib.util
import struct
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))  # the client package, whose save offsets these are

from client import save_patch  # noqa: E402


def _load_logic():
    """worlds/zexion/logic.py, the rules shared with the APWorld, loaded by path so the world's
    other modules (data, test, ...) do not land on sys.path as top-level names."""
    spec = importlib.util.spec_from_file_location(
        "zexion_logic", ROOT / "worlds" / "zexion" / "logic.py"
    )
    module = importlib.util.module_from_spec(spec)
    sys.modules["zexion_logic"] = module
    spec.loader.exec_module(module)
    return module


logic = _load_logic()
H, W = logic.H, logic.W


def find_randocells(explicit: Path | None = None) -> Path:
    """The RandoCells copy to use: ``explicit``, or the only build in lab/data/.

    With several builds there, the choice is refused rather than guessed: file times are the
    game's own (``game_data.py`` keeps them), so they cannot tell builds apart.
    """
    if explicit is not None:
        return explicit
    found = sorted((ROOT / "lab" / "data").glob("*/RandoCells"))
    if len(found) == 1:
        return found[0]
    if not found:
        raise SystemExit("no lab/data/*/RandoCells; run tools/game_data.py RandoCells first")
    listed = ", ".join(p.parent.name for p in found)
    raise SystemExit(f"several builds in lab/data ({listed}); choose one with --cells")


RECORD = 85
LIST_OFFSET = {"N": 0x00, "S": 0x08, "W": 0x10, "E": 0x18}
PASSAGE_OFFSET = {"S": 0x40, "W": 0x41, "E": 0x42, "N": 0x43}
STEP = {"N": (0, -1), "S": (0, 1), "W": (-1, 0), "E": (1, 0)}
ITEM_FLAG, ITEM_LIST, ITEM_TIERS = 0x44, 0x45, 0x4D
WALL, OPEN, CONDITIONAL = 0xFF, 0, 1

# A randomizer save's section (save_patch.SECTION, docs/re/save-format.md) is loaded to RVA
# 0xCDD6D0; settings sit at these RVAs in it.
RANDO_BLOB_RVA = 0xCDD6D0
ENERGY_WORDS_RVA = 0xCDDD90  # starting Energy, Energy in the pool (16-bit each)
OPTIONS_RVA = 0xCDDF4B  # one byte per option, indexed by a code-68 entry's tier byte
MODE_BITS_RVA = (0xCDDF76, 0xCDDF75)  # code 69's mode = first * 2 + second


@dataclass(frozen=True)
class Cell:
    lists: dict[str, list[logic.Entry]]
    passage: dict[str, int]
    item_gated: bool
    item_needs: list[logic.Entry]


def settings_from_save(data: bytes) -> logic.Settings:
    """The seed's settings, from a randomizer save's appended section."""

    def at(rva: int) -> int:
        return save_patch.SECTION + (rva - RANDO_BLOB_RVA)

    start, pool = struct.unpack_from("<hh", data, at(ENERGY_WORDS_RVA))
    hi, lo = (data[at(r)] for r in MODE_BITS_RVA)
    options = bytes(data[at(OPTIONS_RVA) : at(OPTIONS_RVA) + 64])
    return logic.Settings(start + pool, hi * 2 + lo, options)


def _entries(rec: bytes, base: int, tiers: int) -> list[logic.Entry]:
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


def collectible(grid, cell: tuple[int, int], have: Counter, settings, reached: set) -> bool:
    """Whether the item at a reached cell can be picked up: its own requirements, if any."""
    x, y = cell
    rec = grid[y][x]
    return not rec.item_gated or logic.satisfied(
        rec.item_needs, have, settings, cell, reached.__contains__
    )


def reachable_cells(
    grid, have: Counter, settings: logic.Settings, start: tuple[int, int] | list[tuple[int, int]]
) -> set[tuple[int, int]]:
    """Flood fill from ``start`` (one cell, or a list of them), repeated until stable (codes
    64-67 look at reached cells)."""
    seen: set[tuple[int, int]] = set(start) if isinstance(start, list) else {start}
    while True:
        before = len(seen)
        todo = list(seen)
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
                if passage == CONDITIONAL and not logic.satisfied(
                    cell.lists[d], have, settings, (x, y), seen.__contains__
                ):
                    continue
                seen.add((nx, ny))
                todo.append((nx, ny))
        if len(seen) == before:
            return seen


# A weapon and its upgrade are progressive (logic.WEAPON_UPGRADES; docs/re/randocells.md).
REDIRECT_IF_MISSING = {upgrade: weapon for weapon, upgrade in logic.WEAPON_UPGRADES.items()}


def collect(have: Counter, item: int) -> None:
    """Add one picked-up item to the counts, as the generator does (RVA 0xE5680)."""
    upgrade = logic.WEAPON_UPGRADES.get(item)
    if upgrade is not None and have[item] > 0:
        have[upgrade] += 1
        return
    base = REDIRECT_IF_MISSING.get(item)
    if base is not None and have[base] == 0:
        have[base] = 1
        return
    have[item] += 1


def sweep(
    grid,
    locations,
    items,
    start: tuple[int, int] | list[tuple[int, int]],
    initial=None,
    settings: logic.Settings = logic.DEFAULT_SETTINGS,
) -> set[int]:
    """Collect everything reachable from ``start`` (one cell or a list), repeating until
    nothing new opens up."""
    have: Counter = Counter(initial or {})
    got: set[int] = set()
    while True:
        cells = reachable_cells(grid, have, settings, start)
        new = False
        for i, cell in enumerate(locations):
            if i in got or cell not in cells or not collectible(grid, cell, have, settings, cells):
                continue
            got.add(i)
            collect(have, items[i])
            new = True
        if not new:
            return got


def seed_from_save(data: bytes) -> tuple[list[tuple[int, int]], list[int]]:
    """The seed's location cells and the item at each (docs/re/save-format.md)."""
    cells, items, count = save_patch.SECTION, save_patch.ITEM_TABLE, save_patch.LOCATIONS
    locations = [struct.unpack_from("<HH", data, cells + 4 * i) for i in range(count)]
    return locations, list(data[items : items + count])


def inventory_from_save(data: bytes) -> Counter:
    """Item counts 0-49 from the save's player state (file 0x92F3 + item ID).

    For a save made right at the start of a seed, this is the seed's starting inventory.
    """
    inv = save_patch.INVENTORY  # one byte per code; items 0-49 are the inventory
    return Counter({i: data[inv + i] for i in range(logic.FIRST_SPECIAL) if data[inv + i]})


def _record_cell(data: bytes, offset: int) -> tuple[int, int]:
    """The cell of a start record (candidate index - 1, tile column, tile row; 16-bit each),
    which gives a save point's position in tiles: a map cell is 14 tiles along x and 20 along
    y (docs/re/save-format.md, "The continue point and the seed's start")."""
    _, column, row = struct.unpack_from("<3H", data, offset)
    return (column // 14, row // 20)


def start_from_save(data: bytes) -> tuple[int, int] | None:
    """The seed's start cell, from the start record at the head of the randomizer section;
    None when the file is too short to hold the record (a normal-mode save, for instance)."""
    if len(data) < save_patch.START_RECORD + 6:
        return None
    return _record_cell(data, save_patch.START_RECORD)


DOUBLE_TELEPORT = 33  # option index (docs/re/randocells.md, "Where each setting is stored")


def starts_from_save(data: bytes) -> list[tuple[int, int]]:
    """The seed's start, plus its second teleport target (a start record too) when Double
    Teleport is on; empty for a file too short to be a randomizer save."""
    start = start_from_save(data)
    if start is None or len(data) <= save_patch.OPTIONS + DOUBLE_TELEPORT:
        return []
    if data[save_patch.OPTIONS + DOUBLE_TELEPORT]:
        return [start, _record_cell(data, save_patch.SECOND_START)]
    return [start]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("save", type=Path, help="a randomizer .sav (use a copy in lab/)")
    parser.add_argument(
        "--cells", type=Path, help="RandoCells copy (default: the only build in lab/data/)"
    )
    parser.add_argument("--start", help="X,Y of the start cell (default: from a --fresh save)")
    parser.add_argument(
        "--fresh",
        action="store_true",
        help=(
            "the save was made at the start of the seed: use its inventory, its start record "
            "and, with Double Teleport, its second teleport target"
        ),
    )
    args = parser.parse_args(argv)
    cells_path = find_randocells(args.cells)
    grid = parse(cells_path.read_bytes())
    data = args.save.read_bytes()
    locations, items = seed_from_save(data)
    initial = inventory_from_save(data) if args.fresh else Counter()
    settings = settings_from_save(data)
    if args.start:
        shown_x, shown_y = (int(v) for v in args.start.split(","))
        start = (shown_y, shown_x)
    elif args.fresh and starts_from_save(data):
        start = starts_from_save(data)
    else:
        print("give --start X,Y, or --fresh with a save made at the start of the seed")
        return 1
    got = sweep(grid, locations, items, start, initial, settings)
    on = [t for t in range(64) if settings.options[t]]
    print(
        f"{len(got)}/{save_patch.LOCATIONS} locations reachable from {start}; starting inventory "
        f"{dict(initial) or 'empty'}; energy total {settings.energy_total}, code-69 mode "
        f"{settings.mode69}, options on {on}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
