"""Generate worlds/zexion/data.py, the APWorld's map and rules, from the game's own files.

    gen_world_data.py [--cells FILE] [--save FILE] [--starts FILE]

Reads (all copies in lab/, never the game folder):

- RandoCells (``game_data.py RandoCells`` copies it to lab/data/<build>/);
- a randomizer save made right at the start of a Standard-mode seed (the game's default), for
  the location list,
  the item pool, the starting inventory and the settings (lab/backups/.../Save Game 28.sav);
- the start-room candidates (lab/data/<build>/start-candidates.json, docs/re/save-format.md).

Cells joined by always-open passages are merged into one region (strongly connected
components of the open passages); each conditional passage between two regions becomes an
entrance carrying the requirement list of the cell it leaves from. Only cells reachable with
every item from some valid start are kept. A start is valid when, with every item, all 154
locations are reachable from it, and with only the starting inventory at least
``MIN_SPHERE_ZERO`` are. The output is a Python module of plain data; worlds/zexion turns it
into regions and rules with worlds/zexion/logic.py.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from collections import Counter
from pathlib import Path

import randocells as rc
from protected import assert_writable

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "worlds" / "zexion" / "data.py"
MIN_SPHERE_ZERO = 3
# Slot 29, made in Standard mode with the default settings (docs/re/randocells.md, "The
# randomizer menu's settings").
DEFAULT_SAVE = ROOT / "lab" / "backups" / "20261020-212018" / "Save Game 29.sav"


def full_inventory(start: Counter, pool: list[int]) -> Counter:
    have = Counter(start)
    for item in pool:
        rc.collect(have, item)
    return have


def components(cells: set, grid) -> dict:
    """Strongly connected components of the open passages among ``cells``: cell -> index."""
    succ = {c: [] for c in cells}
    for x, y in cells:
        for d, (dx, dy) in rc.STEP.items():
            n = (x + dx, y + dy)
            if n in cells and grid[y][x].passage[d] == rc.OPEN:
                succ[(x, y)].append(n)
    index, low, on_stack, stack, comp = {}, {}, set(), [], {}
    counter = 0
    for root in sorted(cells, key=lambda c: (c[1], c[0])):
        if root in index:
            continue
        work = [(root, 0)]
        while work:
            node, i = work.pop()
            if i == 0:
                index[node] = low[node] = counter
                counter += 1
                stack.append(node)
                on_stack.add(node)
            if i < len(succ[node]):
                work.append((node, i + 1))
                nxt = succ[node][i]
                if nxt not in index:
                    work.append((nxt, 0))
                elif nxt in on_stack:
                    low[node] = min(low[node], index[nxt])
                continue
            for nxt in succ[node]:
                if nxt in on_stack:
                    low[node] = min(low[node], low[nxt])
            if low[node] == index[node]:
                k = len(set(comp.values()))
                while True:
                    w = stack.pop()
                    on_stack.discard(w)
                    comp[w] = k
                    if w == node:
                        break
    return comp


def location_ok(grid, cell, have, settings, reached: set) -> bool:
    x, y = cell
    rec = grid[y][x]
    return not rec.item_gated or rc.satisfied(
        rec.item_needs, have, settings, rc.Where(x, y, reached)
    )


PICKUP_COUNTER = 0x935A  # 16-bit; 0 before the first pickup (docs/re/save-format.md)


def check_fresh(data: bytes, name: str) -> None:
    """Refuse a save that is not right at the start of its seed: its inventory would be taken
    for the starting inventory."""
    if len(data) <= rc.SAVE_ITEMS:
        raise SystemExit(f"{name} is not a randomizer save")
    if rc.start_from_save(data) is None:
        raise SystemExit(f"{name} has more than one room revealed: not a fresh save")
    if struct.unpack_from("<H", data, PICKUP_COUNTER)[0] != 0:
        raise SystemExit(f"{name} has picked something up: not a fresh save")


def build(cells_path: Path, save_path: Path, starts_path: Path) -> dict:
    grid = rc.parse(cells_path.read_bytes())
    data = save_path.read_bytes()
    check_fresh(data, save_path.name)
    locations, pool = rc.seed_from_save(data)
    start_inv = rc.inventory_from_save(data)
    settings = rc.settings_from_save(data)
    full = full_inventory(start_inv, pool)
    candidates = json.loads(starts_path.read_text(encoding="utf-8"))

    starts, union = [], set()
    for cand in candidates:
        start = tuple(cand["cell"])
        reach_all = rc.reachable_cells(grid, full, settings, start)
        every = all(
            c in reach_all and location_ok(grid, c, full, settings, reach_all) for c in locations
        )
        reach_0 = rc.reachable_cells(grid, start_inv, settings, start)
        sphere0 = sum(
            1
            for c in locations
            if c in reach_0 and location_ok(grid, c, start_inv, settings, reach_0)
        )
        valid = every and sphere0 >= MIN_SPHERE_ZERO
        print(
            f"start {cand['index']:2d} {start}: all items reach "
            f"{'all' if every else 'NOT all'} locations; sphere 0 has {sphere0}"
            f"{'' if valid else '  -> left out'}"
        )
        if valid:
            starts.append({**cand, "sphere0": sphere0, "cells": reach_all})
            union |= reach_all

    comp = components(union, grid)
    regions = sorted(set(comp.values()))
    region_cells = {r: sorted(c for c in union if comp[c] == r) for r in regions}

    entrances = {}
    for x, y in union:
        here = comp[(x, y)]
        for d, (dx, dy) in rc.STEP.items():
            n = (x + dx, y + dy)
            if n not in union or comp[n] == here:
                continue
            passage = grid[y][x].passage[d]
            if passage == rc.WALL:
                continue
            key = (here, comp[n])
            if passage == rc.OPEN:
                entrances[key] = [None]  # always open: other ways in add nothing
                continue
            if entrances.get(key) == [None]:
                continue
            entries = tuple(grid[y][x].lists[d])
            ways = entrances.setdefault(key, [])
            way = (entries, (x, y) if any(code >= 64 for code, _ in entries) else None)
            if way not in ways:
                ways.append(way)

    location_rules = {}
    for i, (x, y) in enumerate(locations):
        if grid[y][x].item_gated:
            location_rules[i] = tuple(grid[y][x].item_needs)

    return {
        "settings": settings,
        "locations": locations,
        "pool": pool,
        "start_inventory": dict(sorted(start_inv.items())),
        "region_cells": region_cells,
        "entrances": entrances,
        "location_rules": location_rules,
        "starts": [
            {
                "index": s["index"],
                "cell": tuple(s["cell"]),
                "column": s["column"],
                "row": s["row"],
                "sphere0": s["sphere0"],
                "regions": sorted({comp[c] for c in s["cells"]}),
            }
            for s in starts
        ],
        "comp": comp,
    }


def render(d: dict, source: str) -> str:
    s = d["settings"]
    out = [
        '"""The APWorld\'s map and rules. Generated by tools/gen_world_data.py; do not edit.',
        "",
        f"Source: {source}. Grid cells are this repo's (x, y) order (docs/re/randocells.md).",
        '"""',
        "",
        "# fmt: off",
        "# ruff: noqa: E501",
        f"ENERGY_TOTAL = {s.energy_total}",
        f"MODE69 = {s.mode69}",
        f"OPTIONS = bytes.fromhex({s.options.hex()!r})",
        "",
        "# The game's 154 locations, in the order of its location list: cell (x, y).",
        f"LOCATIONS = {[tuple(c) for c in d['locations']]!r}",
        "",
        "# The preset's item pool (item IDs), one per location.",
        f"POOL = {list(d['pool'])!r}",
        "",
        f"START_INVENTORY = {d['start_inventory']!r}",
        "",
        "# Region index -> its cells.",
        "REGION_CELLS = {",
    ]
    for r, cells in d["region_cells"].items():
        out.append(f"    {r}: {cells!r},")
    out += [
        "}",
        "",
        "# (from region, to region) -> ways through: None (always open) or (entries, cell),",
        "# where cell is set when the entries hold a code 64-69, which is checked at a cell.",
        "ENTRANCES = {",
    ]
    for key, ways in sorted(d["entrances"].items()):
        out.append(f"    {key!r}: {ways!r},")
    out += [
        "}",
        "",
        "# Location index -> the requirements for collecting its item, where it has any.",
        f"LOCATION_RULES = {d['location_rules']!r}",
        "",
        "# Valid start rooms: the game's candidate index, the cell, the save point's column and",
        "# row in tiles, how many locations the starting inventory reaches, and the regions every",
        "# item reaches from there.",
        "STARTS = [",
    ]
    for s in d["starts"]:
        out.append(f"    {s!r},")
    out += ["]", ""]
    return "\n".join(out)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--cells", type=Path)
    parser.add_argument("--save", type=Path, default=DEFAULT_SAVE)
    parser.add_argument("--starts", type=Path)
    args = parser.parse_args(argv)
    cells_path = rc.find_randocells(args.cells)
    starts_path = args.starts or cells_path.parent / "start-candidates.json"
    d = build(cells_path, args.save, starts_path)
    digest = hashlib.sha256(args.save.read_bytes()).hexdigest()[:12]
    source = (
        f"RandoCells of build {cells_path.parent.name}; fresh save {args.save.name} "
        f"(SHA-256 {digest})"
    )
    out = assert_writable(OUT)
    out.write_text(render(d, source), encoding="utf-8", newline="\n")
    n_ways = sum(len(w) for w in d["entrances"].values())
    print(
        f"{len(d['region_cells'])} regions, {len(d['entrances'])} region links "
        f"({n_ways} ways), {len(d['location_rules'])} gated locations, "
        f"{len(d['starts'])} valid starts -> {out}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
