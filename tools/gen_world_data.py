"""Generate worlds/zexion/data.py, the APWorld's map and rules, from the game's own files.

    gen_world_data.py [--cells FILE] [--starts FILE] [--mode NAME=SAVE ...]

Reads (all copies in lab/, never the game folder):

- RandoCells (``game_data.py RandoCells`` copies it to lab/data/<build>/);
- for each mode, a randomizer save made right at the start of a seed in that mode, for its
  item pool, starting inventory and settings (by default Easy: slot 28's and Standard:
  slot 29's fresh saves, docs/re/randocells.md, "The randomizer's modes"); the location list
  is the same in every seed;
- the start-room candidates (lab/data/<build>/start-candidates.json, docs/re/save-format.md).

Cells joined by always-open passages are merged into one region (strongly connected
components of the open passages); each conditional passage between two regions becomes an
entrance carrying the requirement list of the cell it leaves from. The map is shared by all
modes; a mode's settings only change how the rules evaluate. Only cells reachable with every
item from some mode's valid start are kept. A start is valid for a mode when, with every item
of that mode, all 154 locations are reachable from it, and with only that mode's starting
inventory at least ``MIN_SPHERE_ZERO`` are. The output is a Python module of plain data;
worlds/zexion turns it into regions and rules with worlds/zexion/logic.py.
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
BACKUP = ROOT / "lab" / "backups" / "20261020-212018"
# Fresh saves matched to the modes (docs/re/randocells.md, "The randomizer's modes").
DEFAULT_MODES = {"easy": BACKUP / "Save Game 28.sav", "standard": BACKUP / "Save Game 29.sav"}


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
        raise SystemExit(f"{name} has no start record")
    revealed = sum(1 for v in data[rc.MAP_GRID : rc.MAP_GRID + rc.W * rc.H] if v)
    if revealed > 1:
        raise SystemExit(f"{name} has {revealed} rooms revealed: not a fresh save")
    if struct.unpack_from("<H", data, PICKUP_COUNTER)[0] != 0:
        raise SystemExit(f"{name} has picked something up: not a fresh save")


def mode_starts(grid, locations, pool, start_inv, settings, candidates, name) -> list[dict]:
    """The candidates valid for one mode, each with the cells every item reaches from it."""
    full = full_inventory(start_inv, pool)
    starts = []
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
            f"{name}: start {cand['index']:2d} {start}: all items reach "
            f"{'all' if every else 'NOT all'} locations; sphere 0 has {sphere0}"
            f"{'' if valid else '  -> left out'}"
        )
        if valid:
            starts.append({**cand, "sphere0": sphere0, "cells": reach_all})
    return starts


def build(cells_path: Path, mode_saves: dict[str, Path], starts_path: Path) -> dict:
    grid = rc.parse(cells_path.read_bytes())
    candidates = json.loads(starts_path.read_text(encoding="utf-8"))
    modes, locations, union = {}, None, set()
    for name, save_path in mode_saves.items():
        data = save_path.read_bytes()
        check_fresh(data, save_path.name)
        mode_locations, pool = rc.seed_from_save(data)
        if locations is None:
            locations = mode_locations
        elif mode_locations != locations:
            raise SystemExit(f"{save_path.name}: its location list differs from the others")
        start_inv = rc.inventory_from_save(data)
        settings = rc.settings_from_save(data)
        starts = mode_starts(grid, locations, pool, start_inv, settings, candidates, name)
        for st in starts:
            union |= st["cells"]
        modes[name] = {
            "settings": settings,
            "pool": pool,
            "start_inventory": dict(sorted(start_inv.items())),
            "starts": starts,
            "source": save_path,
        }

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

    for mode in modes.values():
        mode["starts"] = [
            {
                "index": st["index"],
                "cell": tuple(st["cell"]),
                "column": st["column"],
                "row": st["row"],
                "sphere0": st["sphere0"],
                "regions": sorted({comp[c] for c in st["cells"]}),
            }
            for st in mode["starts"]
        ]
    return {
        "locations": locations,
        "modes": modes,
        "region_cells": region_cells,
        "entrances": entrances,
        "location_rules": location_rules,
    }


def render(d: dict, source: str) -> str:
    out = [
        '"""The APWorld\'s map and rules. Generated by tools/gen_world_data.py; do not edit.',
        "",
        f"Source: {source}. Grid cells are this repo's (x, y) order (docs/re/randocells.md).",
        '"""',
        "",
        "# fmt: off",
        "# ruff: noqa: E501",
        "# The game's 154 locations, in the order of its location list: cell (x, y).",
        f"LOCATIONS = {[tuple(c) for c in d['locations']]!r}",
        "",
        "# Per mode: the seed's settings, its item pool (item IDs, one per location), its starting",
        "# inventory, and its valid start rooms (the game's candidate index, the cell, the save",
        "# point's column and row in tiles, how many locations the starting inventory reaches, and",
        "# the regions every item reaches from there).",
        "MODES = {",
    ]
    for name, m in d["modes"].items():
        s = m["settings"]
        out += [
            f"    {name!r}: {{",
            f"        'energy_total': {s.energy_total},",
            f"        'mode69': {s.mode69},",
            f"        'options': bytes.fromhex({s.options.hex()!r}),",
            f"        'pool': {list(m['pool'])!r},",
            f"        'start_inventory': {m['start_inventory']!r},",
            "        'starts': [",
        ]
        for st in m["starts"]:
            out.append(f"            {st!r},")
        out += ["        ],", "    },"]
    out += ["}", "", "# Region index -> its cells.", "REGION_CELLS = {"]
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
    ]
    return "\n".join(out)


def parse_mode(text: str) -> tuple[str, Path]:
    name, sep, path = text.partition("=")
    if not sep or not name or not path:
        raise argparse.ArgumentTypeError(f"expected NAME=SAVE, got {text!r}")
    return name, Path(path)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--cells", type=Path)
    parser.add_argument("--starts", type=Path)
    parser.add_argument("--mode", type=parse_mode, action="append", help="NAME=SAVE")
    args = parser.parse_args(argv)
    mode_saves = dict(args.mode) if args.mode else dict(DEFAULT_MODES)
    cells_path = rc.find_randocells(args.cells)
    starts_path = args.starts or cells_path.parent / "start-candidates.json"
    d = build(cells_path, mode_saves, starts_path)
    saves = ", ".join(
        f"{name}: {path.name} (SHA-256 {hashlib.sha256(path.read_bytes()).hexdigest()[:12]})"
        for name, path in mode_saves.items()
    )
    source = f"RandoCells of build {cells_path.parent.name}; fresh saves {saves}"
    out = assert_writable(OUT)
    out.write_text(render(d, source), encoding="utf-8", newline="\n")
    n_ways = sum(len(w) for w in d["entrances"].values())
    per_mode = ", ".join(f"{n}: {len(m['starts'])} starts" for n, m in d["modes"].items())
    print(
        f"{len(d['region_cells'])} regions, {len(d['entrances'])} region links "
        f"({n_ways} ways), {len(d['location_rules'])} gated locations; {per_mode} -> {out}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
