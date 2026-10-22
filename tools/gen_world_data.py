"""Generate worlds/zexion/data.py, the APWorld's map and rules, from the game's own files.

    gen_world_data.py [--cells FILE] [--starts FILE] [--mode NAME=SAVE ...]

Reads (all copies in lab/, never the game folder):

- RandoCells (``game_data.py RandoCells`` copies it to lab/data/<build>/);
- for each mode, a randomizer save made right at the start of a seed in that mode, for its
  item pool, starting inventory and settings (by default Easy: slot 28's and Standard:
  slot 29's fresh saves, docs/re/randocells.md, "The randomizer's modes"); the location list
  is the same in every seed;
- the start-room candidates (lab/data/<build>/start-candidates.json, docs/re/save-format.md);
- the game's exe (lab/data/<build>/Zexion.exe, ``game_data.py --exe``), for each mode's preset
  of the randomizer menu (docs/re/memory.md, "The game-mode panel"), which the client's
  ARCHIPELAGO choice sets up; each preset is checked against the mode's fresh save.

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
DEFAULT_MODES = {
    "easy": BACKUP / "Save Game 28.sav",
    "standard": BACKUP / "Save Game 29.sav",
    "ohko": ROOT / "lab" / "work" / "slot5-ohko-fresh.sav",
}


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


AMOUNTS = 0x12ED4  # six 16-bit amounts (docs/re/save-format.md)
# The settings the logic depends on (option index; docs/re/randocells.md): code 68's tiers,
# Double Teleport, Safety (code 69's mode) and Gates. The client refuses a save whose
# settings differ from the mode's here.
CHECKED_OPTIONS = (30, 31, 32, 33, 37, 42, 43, 72, 73)
PICKUP_COUNTER = 0x935A  # 16-bit; 0 before the first pickup (docs/re/save-format.md)
START_WITH, POOL = 0x12E9D, 0x12EB8  # the section's 27-entry arrays (docs/re/save-format.md)
SECOND_TARGET = 0x12EE0  # candidate index, tile column, tile row
ARRAY = 27
# Each mode's menu preset in the exe's .rdata, per build: the start-with array, the pool
# array, and settings 10 to 73, as the mode buttons (0x1458C0) copy them (docs/re/memory.md,
# "The game-mode panel"). Checked against the fresh saves below, so a wrong address fails.
PRESETS = {
    "6d8a94bd4d64": {
        "easy": (0x20D3E8, 0x20D1E0, 0x20D190),
        "standard": (0x20E3D0, 0x20E3B0, 0x20D220),
        "ohko": (0x20D260, 0x20D200, 0x20D420),
    },
}
GUNS = range(4, 9)  # the random gun and guns first change these and their upgrades'
GUN_UPGRADES = range(13, 18)
COLOR = 12  # the player's choice, not the mode's


def rdata(exe: bytes, rva: int, size: int) -> bytes:
    pe = struct.unpack_from("<I", exe, 0x3C)[0]
    sections = struct.unpack_from("<H", exe, pe + 6)[0]
    first = pe + 24 + struct.unpack_from("<H", exe, pe + 20)[0]
    for i in range(sections):
        size_v, va, size_raw, raw = struct.unpack_from("<IIII", exe, first + 40 * i + 8)
        if va <= rva and rva + size <= va + max(size_v, size_raw):
            return exe[raw + rva - va : raw + rva - va + size]
    raise SystemExit(f"RVA {rva:#x} is in no section of the exe")


def menu_preset(exe: bytes, build: str, name: str, save: bytes) -> dict:
    """The mode's menu preset, checked against its fresh save: settings 10 to 73 (but the
    color) equal, and every array entry outside the guns as the placement resolves it."""
    if build not in PRESETS or name not in PRESETS[build]:
        raise SystemExit(f"no menu preset known for {name} on build {build}")
    start_at, pool_at, settings_at = PRESETS[build][name]
    start, pool = rdata(exe, start_at, ARRAY), rdata(exe, pool_at, ARRAY)
    settings = rdata(exe, settings_at, 64)
    if not set(start) <= {0, 1} or not set(pool) <= {0, 1} or not set(settings) <= {0, 1}:
        raise SystemExit(f"{name}'s preset at {start_at:#x} is not 0/1 entries")
    saved = save[rc.OPTIONS_IN_SAVE + 10 : rc.OPTIONS_IN_SAVE + 74]
    wrong = [t + 10 for t in range(64) if t + 10 != COLOR and settings[t] != saved[t]]
    if wrong:
        raise SystemExit(f"{name}'s preset settings {wrong} differ from its fresh save")
    for i in range(ARRAY):
        if i in GUNS or i in GUN_UPGRADES:
            continue
        got = (save[START_WITH + i], save[POOL + i])
        want = {(1, 0), (0, 1)} if (start[i], pool[i]) == (1, 1) else {(start[i], pool[i])}
        if got not in want:
            raise SystemExit(f"{name}'s preset entry {i} is not its fresh save's {got}")
    return {"start": list(start), "pool": list(pool), "settings": list(settings)}


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


def pair_starts(grid, locations, pool, start_inv, settings, candidates, name) -> list[dict]:
    """For a mode with a second teleport target: every candidate, with the cells and locations
    every item reaches from it alone, and the locations its starting inventory reaches. The
    world picks a pair and combines two of these; flooding from both at once can only reach
    more (codes 64-67 look at reached cells), so the combination is on the safe side."""
    full = full_inventory(start_inv, pool)
    out = []
    for cand in candidates:
        start = tuple(cand["cell"])
        reach_all = rc.reachable_cells(grid, full, settings, start)
        reach_0 = rc.reachable_cells(grid, start_inv, settings, start)
        every = [
            i
            for i, c in enumerate(locations)
            if c in reach_all and location_ok(grid, c, full, settings, reach_all)
        ]
        sphere0 = [
            i
            for i, c in enumerate(locations)
            if c in reach_0 and location_ok(grid, c, start_inv, settings, reach_0)
        ]
        out.append(
            {**cand, "cells": reach_all, "location_indices": every, "sphere0_indices": sphere0}
        )
        print(
            f"{name}: start {cand['index']:2d} {start}: every item reaches {len(every)} "
            f"locations, the starting items {len(sphere0)}"
        )
    return out


def build(cells_path: Path, mode_saves: dict[str, Path], starts_path: Path, exe_path: Path) -> dict:
    grid = rc.parse(cells_path.read_bytes())
    exe = exe_path.read_bytes()
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
        double = bool(data[rc.OPTIONS_IN_SAVE + rc.DOUBLE_TELEPORT])
        pick = pair_starts if double else mode_starts
        starts = pick(grid, locations, pool, start_inv, settings, candidates, name)
        for st in starts:
            union |= st["cells"]
        modes[name] = {
            "amounts": struct.unpack_from("<6h", data, AMOUNTS),
            "checked_options": {t: data[rc.OPTIONS_IN_SAVE + t] for t in CHECKED_OPTIONS},
            "double_teleport": double,
            "settings": settings,
            "pool": pool,
            "start_inventory": dict(sorted(start_inv.items())),
            "starts": starts,
            "source": save_path,
            "menu": menu_preset(exe, cells_path.parent.name, name, data),
            "second_target": None if double else struct.unpack_from("<3H", data, SECOND_TARGET),
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
                "regions": sorted({comp[c] for c in st["cells"]}),
                **{k: st[k] for k in ("sphere0", "location_indices", "sphere0_indices") if k in st},
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
        "# Per mode: the starting and pool Energy, missiles and bombs as the save holds them",
        "# (docs/re/save-format.md), the settings the client checks in the save, whether it has",
        "# a second teleport target, the seed's settings (as the logic reads them),",
        "# its item pool (item IDs, one per location), its starting inventory, and its start",
        "# rooms (the game's candidate index, the cell, the save point's column and row in tiles,",
        "# and the regions every item reaches from there). A mode without a second target lists",
        "# only valid rooms, with how many locations the starting inventory reaches; a mode with",
        "# one lists every candidate, with the locations every item and the starting items reach",
        "# from it alone. Then the mode's randomizer-menu preset (the start-with and pool arrays",
        "# and settings 10 to 73) and, with one start, the second target the game writes.",
        "MODES = {",
    ]
    for name, m in d["modes"].items():
        s = m["settings"]
        out += [
            f"    {name!r}: {{",
            f"        'amounts': {m['amounts']!r},",
            f"        'checked_options': {m['checked_options']!r},",
            f"        'double_teleport': {m['double_teleport']},",
            f"        'energy_total': {s.energy_total},",
            f"        'mode69': {s.mode69},",
            f"        'options': bytes.fromhex({s.options.hex()!r}),",
            f"        'pool': {list(m['pool'])!r},",
            f"        'start_inventory': {m['start_inventory']!r},",
            "        'starts': [",
        ]
        for st in m["starts"]:
            out.append(f"            {st!r},")
        out += [
            "        ],",
            f"        'menu': {m['menu']!r},",
            f"        'second_target': {m['second_target']!r},",
            "    },",
        ]
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
    parser.add_argument("--exe", type=Path, help="default: Zexion.exe next to RandoCells")
    args = parser.parse_args(argv)
    mode_saves = dict(args.mode) if args.mode else dict(DEFAULT_MODES)
    cells_path = rc.find_randocells(args.cells)
    starts_path = args.starts or cells_path.parent / "start-candidates.json"
    exe_path = args.exe or cells_path.parent / "Zexion.exe"
    d = build(cells_path, mode_saves, starts_path, exe_path)
    saves = ", ".join(
        f"{name}: {path.name} (SHA-256 {hashlib.sha256(path.read_bytes()).hexdigest()[:12]})"
        for name, path in mode_saves.items()
    )
    source = f"RandoCells and Zexion.exe of build {cells_path.parent.name}; fresh saves {saves}"
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
