"""Parse Zexion's RandoCells file and check which locations a seed can reach.

The format and the rules below are from docs/re/randocells.md; read its statuses before
relying on them. In short: 54 x 54 cells, row-major (index = y * 54 + x), 85 bytes each:

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
    randocells.py SAVE.sav --start X,Y        any randomizer save, from a given cell
        [--cells FILE]
"""

from __future__ import annotations

import argparse
import struct
import sys
from collections import Counter
from collections.abc import Sequence
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

W = H = 54
RECORD = 85
LIST_OFFSET = {"N": 0x00, "S": 0x08, "W": 0x10, "E": 0x18}
PASSAGE_OFFSET = {"S": 0x40, "W": 0x41, "E": 0x42, "N": 0x43}
STEP = {"N": (0, -1), "S": (0, 1), "W": (-1, 0), "E": (1, 0)}
ITEM_FLAG, ITEM_LIST, ITEM_TIERS = 0x44, 0x45, 0x4D
WALL, OPEN, CONDITIONAL = 0xFF, 0, 1
FIRST_SPECIAL = 50

# Where a randomizer save keeps its seed (docs/re/save-format.md).
SAVE_CELLS, SAVE_ITEMS, LOCATIONS = 0x12814, 0x12A80, 154
PLAYER_STATE = 0x92F3  # one byte per code; items 0-49 are the inventory
MAP_GRID = 0x877F  # 54 x 54, one byte per cell
# The save's randomizer section is loaded to RVA 0xCDD6D0; settings sit at these RVAs in it.
RANDO_BLOB_RVA = 0xCDD6D0
ENERGY_WORDS_RVA = 0xCDDD90  # starting Energy, Energy in the pool (16-bit each)
OPTIONS_RVA = 0xCDDF4B  # one byte per option, indexed by a code-68 entry's tier byte
MODE_BITS_RVA = (0xCDDF76, 0xCDDF75)  # code 69's mode = first * 2 + second

Entry = tuple[int, int]  # (code, tier/count byte)


@dataclass(frozen=True)
class Cell:
    lists: dict[str, list[Entry]]
    passage: dict[str, int]
    item_gated: bool
    item_needs: list[Entry]


@dataclass(frozen=True)
class Settings:
    """The seed's randomizer settings that the logic reads."""

    energy_total: int = 20  # starting Energy + Energy in the pool
    mode69: int = 0  # 0 or 3: code 69 always passes; 1 or 2: it checks energy
    options: bytes = field(default=bytes(64))

    @classmethod
    def from_save(cls, data: bytes) -> Settings:
        def at(rva: int) -> int:
            return SAVE_CELLS + (rva - RANDO_BLOB_RVA)

        start, pool = struct.unpack_from("<hh", data, at(ENERGY_WORDS_RVA))
        hi, lo = (data[at(r)] for r in MODE_BITS_RVA)
        return cls(start + pool, hi * 2 + lo, bytes(data[at(OPTIONS_RVA) : at(OPTIONS_RVA) + 64]))


DEFAULT_SETTINGS = Settings()


@dataclass(frozen=True)
class Where:
    """What a check needs to know about where it happens."""

    x: int
    y: int
    reached: frozenset[tuple[int, int]] | set[tuple[int, int]]


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


def _f32(v: float) -> float:
    return float(np.float32(v))


def combat_tier(c: list[int], settings: Settings) -> int:
    """Code 62, as the game derives it (RVA 0xE3530)."""
    if c[12] or settings.mode69 == 3:  # Plasma Burst, or both mode bits set
        return 4
    energy = c[0] * 2 if c[25] else c[0]  # Magnus Armor doubles Energy
    total = settings.energy_total
    if total <= 0:
        energy = 40
    elif total != 20:  # energy is judged on a 20-point scale of the seed's total
        energy = int(_f32(_f32(20.0 / total) * _f32(energy)))
    missiles = c[36] * 10 + c[1]
    damage = c[33] + c[13] + c[8]  # Power Chip, Power Cartridge, Power Core
    gun = bool(c[3] or c[4])  # Machine Gun or Force Blaster

    def armed(missile_floor: int) -> bool:
        return (gun and damage >= 1) or bool(c[15]) or bool(c[7]) or missiles >= missile_floor

    strong = (gun and damage >= 2) or c[7] or missiles >= 35
    if energy > 10 and strong and c[24] + c[22] + c[19] >= 2:
        return 4
    if missiles >= 25 and c[18]:
        return 4
    if energy > 10 and armed(25):
        return 3
    if missiles >= 20 and c[18]:
        return 3
    if energy >= 8 and armed(25):
        return 2
    if missiles >= 15 and c[37] * 10 + c[2] >= 2:
        return 2
    if (energy >= 5 and armed(10)) or (energy >= 10 and c[60]) or missiles >= 15:
        return 1
    return 0


def derive(c: list[int], settings: Settings) -> None:
    """Fill in codes 50-63 from item counts, in place, as RVA 0xE3530 does."""
    hyper = bool(c[26]) and not c[63]  # Hyper Jump stands in, unless listed itself
    gh = c[24]
    c[54] = int(bool((gh and c[22]) or hyper))
    if gh:  # Gravity Harness counts as Jump Booster and G-Diffuser
        c[22] = c[34] = 1
    wj = c[19]
    c[50] = int(bool(wj or gh or c[22] or hyper))
    c[51] = int(bool((wj and (c[34] or gh)) or hyper))
    c[52] = int(bool((wj and c[22]) or hyper))
    c[53] = int(bool((wj and (c[34] or gh)) or c[22] or hyper))
    for base, upgrade in ((5, 15), (6, 10), (7, 12), (3, 9), (4, 14)):
        if not c[base]:  # a weapon upgrade needs its weapon
            c[upgrade] = 0
    if not c[16]:  # S-Blade and Rocket Slide need Slide Mod
        c[17] = c[11] = 0
    c[57] = int(bool(c[15] or c[6] or c[17] or c[12]))
    c[61] = int(bool(c[12] or c[1]))
    c[60] = int(bool(c[7] or c[6] or c[3] or c[5] or c[4]))
    c[58] = int(bool(c[12] or c[4] or c[3] or c[5]))
    c[59] = int(bool(c[3] or c[5] or c[12]))
    c[62] = combat_tier(c, settings)
    c[56] = 1
    if hyper:
        c[21] = c[22] = c[34] = c[19] = c[24] = 1
    if c[18] and c[1] < 5:  # X-Missile needs five Missile Containers
        c[18] = 0


def _energy_69(have: Counter, settings: Settings) -> int:
    """The energy figure code 69 compares (it reads the raw counts, not the derived ones)."""
    energy = have[0] * 2 if have[25] else have[0]
    total = settings.energy_total
    if total <= 0:
        return 40
    if total != 20:
        factor = _f32(20.0 / total)
        if factor > 1.0:  # code 69 only scales up, unlike code 62
            energy = int(_f32(_f32(energy) * factor))
    return energy


def _weapons_69(have: Counter) -> bool:
    """Code 69, tier 16: a weapon good enough (RVA 0xE483E)."""
    missiles = have[36] * 10 + have[1]
    if have[5] and (have[15] or have[8] or missiles >= 20):
        return True
    if have[3] or have[7]:
        return True
    if have[4] and have[33] and have[13]:
        return True
    return missiles >= 40


def cell_code(
    code: int, tier: int, have: Counter, c: list[int], settings: Settings, where: Where
) -> bool | None:
    """Codes 64-69 (RVA 0xE3C80): True or False decides the whole check; None = not applicable."""
    if 64 <= code <= 67:
        index = where.y * W + where.x
        if code == 64 and where.x > 0:
            index -= 1  # west
        elif code == 65 and where.x < W:
            index += 1  # east (the game steps by index, so x = 53 wraps to the next row)
        elif code == 66 and where.y > 0:
            index -= W  # north
        elif code == 67 and where.y < H - 1:
            index += W  # south
        else:
            return None
        return (index % W, index // W) in where.reached
    if code == 68:  # an option, chosen by the tier byte
        return True if tier > 10 and settings.options[tier] else None
    if code == 69:
        if settings.mode69 in (0, 3):
            return True
        if tier <= 10:
            return None
        energy = _energy_69(have, settings)
        if tier == 11:
            return energy > 9
        if tier == 12:
            return c[62] >= 1 and energy > 9
        if tier == 13:
            return c[62] >= 2 and energy > 9
        if tier == 14:
            return energy > (19 if settings.mode69 == 1 else 29)
        if tier == 15:
            return c[62] >= 1 and energy > 7
        if tier == 16:
            return energy > 4 and _weapons_69(have)
        return None
    return None


def satisfied(
    entries: Sequence[Entry],
    have: Counter,
    settings: Settings = DEFAULT_SETTINGS,
    where: Where | None = None,
) -> bool:
    """The game's check (RVA 0xE3C80)."""
    c = [have[i] if i < FIRST_SPECIAL else 0 for i in range(64)]
    for code, tier in entries:
        if code < 64 and tier == 3:  # this item does not count here
            c[code] = 0
        if code == 55:  # "without the Gravity Harness"
            c[24] = 0
        if code == 26:  # Hyper Jump listed: it doesn't stand in for other items
            c[63] = 1
    derive(c, settings)

    # First pass, in list order: codes 64-69 decide outright; a missing tier-4 entry fails.
    for code, tier in entries:
        if code >= 64:
            if where is None:
                raise ValueError("codes 64-69 need to know where the check happens")
            decided = cell_code(code, tier, have, c, settings, where)
            if decided is not None:
                return decided
        elif tier == 4 and c[code] == 0:
            return False

    group = []
    for code, tier in entries:
        if code >= 64:
            continue
        if tier in (1, 4) and c[code] >= 1:
            return True
        if 11 <= tier <= 40 and c[code] >= tier - 10:
            return True
        if tier == 2:
            group.append((code, 1))
        elif tier > 40:
            group.append((code, tier - 40))
    return bool(group) and all(c[k] >= n for k, n in group)


def reachable_cells(
    grid, have: Counter, settings: Settings, start: tuple[int, int]
) -> set[tuple[int, int]]:
    """Flood fill from ``start``, repeated until stable (codes 64-67 look at reached cells)."""
    seen: set[tuple[int, int]] = {start}
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
                if passage == CONDITIONAL and not satisfied(
                    cell.lists[d], have, settings, Where(x, y, seen)
                ):
                    continue
                seen.add((nx, ny))
                todo.append((nx, ny))
        if len(seen) == before:
            return seen


# Item definition redirects (the live item table, fields +0x54 and +0x56; docs/re/randocells.md):
# a weapon picked up again counts as its upgrade; an upgrade picked up first counts as its
# weapon. In effect each weapon and its upgrade are progressive.
REDIRECT_IF_OWNED = {3: 9, 4: 14, 5: 15, 6: 10, 7: 12}
REDIRECT_IF_MISSING = {9: 3, 10: 6, 12: 7, 14: 4, 15: 5}


def collect(have: Counter, item: int) -> None:
    """Add one picked-up item to the counts, as the generator does (RVA 0xE5680)."""
    upgrade = REDIRECT_IF_OWNED.get(item)
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
    start: tuple[int, int],
    initial=None,
    settings: Settings = DEFAULT_SETTINGS,
) -> set[int]:
    """Collect everything reachable, repeating until nothing new opens up."""
    have: Counter = Counter(initial or {})
    got: set[int] = set()
    while True:
        cells = reachable_cells(grid, have, settings, start)
        new = False
        for i, (x, y) in enumerate(locations):
            if i in got or (x, y) not in cells:
                continue
            cell = grid[y][x]
            if cell.item_gated and not satisfied(
                cell.item_needs, have, settings, Where(x, y, cells)
            ):
                continue
            got.add(i)
            collect(have, items[i])
            new = True
        if not new:
            return got


def seed_from_save(data: bytes) -> tuple[list[tuple[int, int]], list[int]]:
    locations = [struct.unpack_from("<HH", data, SAVE_CELLS + 4 * i) for i in range(LOCATIONS)]
    return locations, list(data[SAVE_ITEMS : SAVE_ITEMS + LOCATIONS])


def inventory_from_save(data: bytes) -> Counter:
    """Item counts 0-49 from the save's player state (file 0x92F3 + item ID).

    For a save made right at the start of a seed, this is the seed's starting inventory.
    """
    return Counter(
        {i: data[PLAYER_STATE + i] for i in range(FIRST_SPECIAL) if data[PLAYER_STATE + i]}
    )


def start_from_save(data: bytes) -> tuple[int, int] | None:
    """The start cell of a save made at the start of a seed: its only revealed map cell.

    A new randomizer game begins in a save room, chosen per seed.
    """
    grid = data[MAP_GRID : MAP_GRID + W * H]
    cells = [(k % W, k // W) for k, v in enumerate(grid) if v]
    return cells[0] if len(cells) == 1 else None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("save", type=Path, help="a randomizer .sav (use a copy in lab/)")
    parser.add_argument("--cells", type=Path, help="RandoCells copy (default: newest in lab/data/)")
    parser.add_argument("--start", help="X,Y of the start cell (default: from a --fresh save)")
    parser.add_argument(
        "--fresh",
        action="store_true",
        help="the save was made at the start of the seed: use its inventory and start cell",
    )
    args = parser.parse_args(argv)
    cells_path = args.cells or max(
        Path("lab/data").glob("*/RandoCells"), key=lambda p: p.stat().st_mtime
    )
    grid = parse(cells_path.read_bytes())
    data = args.save.read_bytes()
    locations, items = seed_from_save(data)
    initial = inventory_from_save(data) if args.fresh else Counter()
    settings = Settings.from_save(data)
    if args.start:
        start = tuple(int(v) for v in args.start.split(","))
    elif args.fresh and start_from_save(data):
        start = start_from_save(data)
    else:
        print("give --start X,Y, or --fresh with a save made at the start of the seed")
        return 1
    got = sweep(grid, locations, items, start, initial, settings)
    on = [t for t in range(64) if settings.options[t]]
    print(
        f"{len(got)}/{LOCATIONS} locations reachable from {start}; starting inventory "
        f"{dict(initial) or 'empty'}; energy total {settings.energy_total}, code-69 mode "
        f"{settings.mode69}, options on {on}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
