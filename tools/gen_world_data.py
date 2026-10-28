"""Generate worlds/zexion/data.py, the APWorld's map and rules, from the game's own files.

    gen_world_data.py [--cells FILE] [--starts FILE] [--mode NAME=SAVE ...]

Reads (all copies in lab/, never the game folder):

- RandoCells (``game_data.py RandoCells`` copies it to lab/data/<build>/);
- for each of the three modes, a randomizer save made right at the start of a seed in that
  mode, for its item pool, starting inventory and settings (by default Easy: slot 28's and
  Standard: slot 29's fresh saves of a backup in lab/backups/, and OHKO:
  lab/work/slot5-ohko-fresh.sav; docs/re/randocells.md, "The randomizer's modes"); ``--mode``
  replaces one mode's save; the location list is the same in every seed;
- the start-room candidates (lab/data/<build>/start-candidates.json, docs/re/save-format.md);
- the game's exe (lab/data/<build>/Zexion.exe, ``game_data.py --exe``), for each mode's preset
  of the randomizer menu (docs/re/memory.md, "The game-mode panel"), which the client's
  ARCHIPELAGO choice sets up; each preset is checked against the mode's fresh save;
- MapData and Text (``game_data.py MapData Text``), for the area each location and start room
  is in, as the pause screen's REGION: names it (docs/re/locations.md, "Location names"), and
  MapData for the cells holding Zexion (the core goal, ``CORE``: docs/re/locations.md, "The
  core goal is Zexion's cell in the core").

Cells joined by always-open passages are merged into one region (strongly connected components
of the open passages); each conditional passage between two regions becomes an entrance carrying
the requirement list of the cell it leaves from. The map is shared by all modes; a mode's
settings only change how the rules evaluate. Only cells reachable with every item from some
mode's valid start are kept, and those every item of a mode reaches from any start room with X
Crystals and X Blocks on (``opened_cells``). A start is valid for a mode when, with every item
of that mode, all 154 locations are reachable from it, and with only that mode's starting
inventory at least ``MIN_SPHERE_ZERO`` are. Every start of a mode must reach the same regions
and locations with every item, which the mode then holds once; the generator refuses data where
they differ. The output is a Python module of plain data; worlds/zexion turns it into regions
and rules with worlds/zexion/logic.py. The world works out each seed's reachable regions,
locations and start rooms itself when the seed is generated (worlds/zexion/reach.py), over the
map and the 34 start rooms (``START_ROOMS``) written here; each mode's lists here (``regions``,
``location_indices``, ``starts``) are what its tests compare those results with.
"""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import struct
import sys
from collections import Counter
from pathlib import Path

import randocells as rc
from protected import ROOT, assert_writable

sys.path.insert(0, str(ROOT))  # the client package
from client import newgame, pe  # noqa: E402
from client import save_patch as sp  # noqa: E402

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
    counter = found = 0
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
                while True:
                    w = stack.pop()
                    on_stack.discard(w)
                    comp[w] = found
                    if w == node:
                        break
                found += 1
    return comp


ARRAY = 27
# The section's 27-entry arrays, the start-with array then the pool (docs/re/save-format.md).
START_WITH, POOL = newgame.START_WITH, newgame.START_WITH + ARRAY
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
# The array entries of the five guns (4-8) and their upgrades (13-17), which the Random Gun
# and Guns First settings change.
GUNS = range(4, 9)
GUN_UPGRADES = range(13, 18)
COLOR = 12  # the player's choice, not the mode's
OBJECT_25 = 25  # a start room holding it is flagged in the menu's start table


def rdata(exe: bytes, rva: int, size: int) -> bytes:
    try:
        return pe.read_rva(exe, rva, size)
    except pe.PEError as exc:
        raise SystemExit(f"{exc} of the exe") from exc


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
    saved = save[sp.OPTIONS + 10 : sp.OPTIONS + 74]
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
    """Refuse a save that is not right at the start of its seed (save_patch's check of a fresh
    save): its inventory would be taken for the starting inventory."""
    try:
        sp.check_fresh(data)
    except sp.PatchError as exc:
        raise SystemExit(f"{name}: {exc}") from exc
    if rc.start_from_save(data) is None:
        raise SystemExit(f"{name} has no start record")


def reach(grid, locations, have, settings, start) -> tuple[set, list[int]]:
    """The cells ``have`` reaches from ``start``, and the indices of the locations it collects."""
    cells = rc.reachable_cells(grid, have, settings, start)
    found = [
        i
        for i, c in enumerate(locations)
        if c in cells and rc.collectible(grid, c, have, settings, cells)
    ]
    return cells, found


def mode_starts(grid, locations, pool, start_inv, settings, candidates, name, double) -> list:
    """A mode's starts, each with the cells and locations every item reaches from it alone, and
    the locations its starting inventory reaches: the valid candidates, or for a mode with a
    second teleport target (``double``) every candidate. The world picks the pair itself at
    generation (worlds/zexion/reach.py, ``second_starts``); these lists are its tests'
    oracle."""
    full = full_inventory(start_inv, pool)
    starts = []
    for cand in candidates:
        start = tuple(cand["cell"])
        reach_all, every = reach(grid, locations, full, settings, start)
        _, sphere0 = reach(grid, locations, start_inv, settings, start)
        valid = len(every) == len(locations) and len(sphere0) >= MIN_SPHERE_ZERO
        print(
            f"{name}: start {cand['index']:2d} {start}: every item reaches {len(every)} "
            f"locations, the starting items {len(sphere0)}"
            f"{'' if valid or double else '  -> left out'}"
        )
        if valid or double:
            starts.append(
                {**cand, "cells": reach_all, "location_indices": every, "sphere0_indices": sphere0}
            )
    if not starts:
        raise SystemExit(f"{name}: no start room is valid")
    return starts


def shared(values: list, name: str, what: str) -> list:
    """The one value every start of mode ``name`` has for ``what``, which the mode holds once
    (every start has the same, on build B1)."""
    if any(v != values[0] for v in values):
        raise SystemExit(f"{name}: its starts differ in {what}; the world needs them the same")
    return values[0]


# The area of each map cell, as the pause screen shows it (docs/re/locations.md, "Location
# names"; B1): MapData's cell records are 0x9A bytes each, indexed X * 54 + Y as the game
# displays the cell (this repo's cell (a, b) is X = b, Y = a); MAP_CELLS is record 0's 16-bit
# area field, 0x48 into the record (the records start at 0x3E5701). The area's region is the
# second byte of its 2-byte entry at MAP_AREAS, an index into Text (32-byte entries after a
# 6-byte header).
MAP_DATA_SHA256 = "79a0510f4dbbee0f"
TEXT_SHA256 = "3bf8cd5e07ede18f"
MAP_CELLS, MAP_CELL_SIZE, MAP_AREAS = 0x3E5749, 0x9A, 0x45F94D
TEXT_HEADER, TEXT_ENTRY = 6, 32


def area_name(text: str) -> str:
    """GUARDIA -> Guardia, R'TALVA -> R'Talva."""
    return "'".join(part.capitalize() for part in text.split("'"))


def check_b1(name: str, data: bytes, want: str) -> None:
    got = hashlib.sha256(data).hexdigest()[:16]
    if got != want:
        raise SystemExit(f"{name} is not build B1's ({got}); its area offsets are B1's")


def cell_areas(map_data: bytes, text: bytes, cells) -> dict[tuple, str]:
    """The area name of each cell (a, b) given, from MapData and Text of build B1."""
    check_b1("MapData", map_data, MAP_DATA_SHA256)
    check_b1("Text", text, TEXT_SHA256)
    out = {}
    for a, b in cells:
        area = struct.unpack_from("<h", map_data, MAP_CELLS + (b * 54 + a) * MAP_CELL_SIZE)[0]
        index = map_data[MAP_AREAS + area * 2 + 1]
        start = TEXT_HEADER + index * TEXT_ENTRY
        out[(a, b)] = area_name(text[start : start + TEXT_ENTRY].split(b"\0")[0].decode("latin1"))
    return out


# The core goal (docs/re/locations.md, "The core goal is Zexion's cell in the core"). MapData's
# per-cell item byte, field 0x3A of the same records (docs/re/randocells.md, "The pool"), is 31
# (Zexion) in two cells: the core's, and the ship's. RandoCells lets its flood into the core only
# through one passage whose list is code 68 with Abridged Finale's tier, so with the modes'
# settings no start reaches the core and it has no region. The world puts its goal there,
# entered from the last cell it keeps, with the lists RandoCells has inside the core.
MAP_ITEM, ZEXION = 0x3E5701 + 0x3A, 31
ABRIDGED_FINALE = (68, 32)  # the code-68 entry that passes only with Abridged Finale on


def zexion_cells(map_data: bytes) -> list[tuple[int, int]]:
    """The cells (a, b) whose item is Zexion, from MapData of build B1."""
    check_b1("MapData", map_data, MAP_DATA_SHA256)
    return [
        (i % 54, i // 54)
        for i in range(54 * 54)
        if map_data[MAP_ITEM + i * MAP_CELL_SIZE] == ZEXION
    ]


def core_goal(grid, kept: set, zexion: list) -> dict:
    """The core: Zexion's cell that no start reaches (``kept`` is every cell with a region), the
    kept cell whose passage leads in (needing only Abridged Finale), and the requirement lists on
    the way from there to Zexion, as ENTRANCES writes a way. Refuses a map where that is not one
    door and one way through, which a new build would need looked at by hand."""
    inside = [c for c in zexion if c not in kept]
    if len(inside) != 1:
        raise SystemExit(f"expected one Zexion cell outside the kept cells, got {inside}")
    goal = inside[0]
    doors = []
    for x, y in sorted(kept):
        for d, (dx, dy) in rc.STEP.items():
            n = (x + dx, y + dy)
            if n not in kept and grid[y][x].lists[d] == [ABRIDGED_FINALE]:
                doors.append(((x, y), n))
    if len(doors) != 1:
        raise SystemExit(f"expected one passage into the core needing Abridged Finale: {doors}")
    door, first = doors[0]
    came = {first: None}
    todo = [first]
    inner = []  # every conditional passage among the core's cells
    while todo:
        x, y = todo.pop(0)
        for d, (dx, dy) in rc.STEP.items():
            n = (x + dx, y + dy)
            passage = grid[y][x].passage[d]
            if n in kept or passage == rc.WALL or not (0 <= n[0] < 54 and 0 <= n[1] < 54):
                continue
            if passage == rc.CONDITIONAL:
                inner.append(((x, y), n))
            if n not in came:
                came[n] = ((x, y), passage)
                todo.append(n)
    if goal not in came:
        raise SystemExit(f"the core's door {door} does not lead to Zexion's cell {goal}")
    needs, step = [], goal
    while came[step] is not None:
        (x, y), passage = came[step]
        if passage == rc.CONDITIONAL:
            d = next(d for d, (dx, dy) in rc.STEP.items() if (x + dx, y + dy) == step)
            entries = tuple(grid[y][x].lists[d])
            inner.remove(((x, y), step))
            needs.insert(0, (entries, (x, y) if any(c >= 64 for c, _ in entries) else None))
        step = (x, y)
    if inner:
        raise SystemExit(f"the core has conditional passages off the way to Zexion: {inner}")
    return {"door": door, "cell": goal, "needs": needs}


# Settings that open cells no preset reaches (docs/re/locations.md, "The region graph holds what
# the settings reach"): X Crystals and X Blocks, whose cells (6, 26) and (7, 26) are a shorter way
# between two regions. Abridged Finale stays off: it opens the core, which the world enters by its
# own entrance (``core_goal``). Secret Shuffle's cells hold its 155th location, which the world
# does not have yet.
OPENING = (30, 31)


def opened_cells(grid, modes: dict, candidates: list) -> set:
    """The cells each mode's every item reaches from every start room with OPENING on."""
    cells = set()
    for mode in modes.values():
        settings = mode["settings"]
        options = bytearray(settings.options)
        for t in OPENING:
            options[t] = 1
        opened = dataclasses.replace(settings, options=bytes(options))
        full = full_inventory(Counter(mode["start_inventory"]), mode["pool"])
        for cand in candidates:
            cells |= rc.reachable_cells(grid, full, opened, tuple(cand["cell"]))
    return cells


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
        double = bool(data[sp.OPTIONS + rc.DOUBLE_TELEPORT])
        starts = mode_starts(grid, locations, pool, start_inv, settings, candidates, name, double)
        for st in starts:
            union |= st["cells"]
        modes[name] = {
            "amounts": struct.unpack_from("<6h", data, sp.AMOUNTS),
            "double_teleport": double,
            "settings": settings,
            "pool": pool,
            "start_inventory": dict(sorted(start_inv.items())),
            "starts": starts,
            "menu": menu_preset(exe, cells_path.parent.name, name, data),
            "second_target": None if double else struct.unpack_from("<3H", data, sp.SECOND_START),
        }

    union |= opened_cells(grid, modes, candidates)
    comp = components(union, grid)
    cell_region = {c: comp[c] for c in sorted(union)}

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

    for name, mode in modes.items():
        starts = mode["starts"]
        regions = [sorted({comp[c] for c in st["cells"]}) for st in starts]
        mode["regions"] = shared(regions, name, "regions")
        locations_of = [st["location_indices"] for st in starts]
        mode["location_indices"] = shared(locations_of, name, "locations")
        mode["starts"] = [
            {
                "index": st["index"],
                "cell": tuple(st["cell"]),
                "column": st["column"],
                "row": st["row"],
                # A mode with a second teleport target: each start's own sphere 0, which
                # the world's tests compare with reach.py's.
                **({"sphere0_indices": st["sphere0_indices"]} if mode["double_teleport"] else {}),
            }
            for st in starts
        ]
    return {
        "locations": locations,
        "modes": modes,
        "cell_region": cell_region,
        "entrances": entrances,
        "location_rules": location_rules,
        "build": cells_path.parent.name,
        # Every start-room candidate, for the world to judge with the seed's settings.
        "start_rooms": [
            {"index": c["index"], "cell": tuple(c["cell"]), "column": c["column"], "row": c["row"]}
            for c in candidates
        ],
        # The randomizer menu's start table, as it builds it when it opens: every candidate
        # room in order, with its flag (the room holds object 25), checked against the game's
        # own table (docs/re/memory.md, "Starting a new game" on OHKO).
        "start_table": [
            (c["cell"][0], c["cell"][1], int(OBJECT_25 in c["objects"])) for c in candidates
        ],
    }


def render(d: dict, source: str) -> str:
    out = [
        '"""The APWorld\'s map and rules. Generated by tools/gen_world_data.py; do not edit.',
        "",
        f"Source: {source}.",
        "A cell (a, b) is shown in the game as X = b, Y = a (docs/re/randocells.md).",
        '"""',
        "",
        "# fmt: off",
        "# ruff: noqa: E501",
        "# The game's 154 locations, in the order of its location list: cell (a, b), X = b, Y = a.",
        f"LOCATIONS = {[tuple(c) for c in d['locations']]!r}",
        "",
        "# The earlier rule for a start room, now the tests' oracle: every item reaches all 154",
        "# locations from it and its mode's starting items at least this many.",
        f"MIN_SPHERE_ZERO = {MIN_SPHERE_ZERO}",
        "",
        "# Per mode, from its fresh save and the exe's preset ('menu'). 'regions' and",
        "# 'location_indices': what every item reaches from each of the mode's starts, the same",
        "# from all of them. A mode with a second teleport target lists every candidate, with",
        "# its own 'sphere0_indices'. The world works these out itself when a seed is generated",
        "# (reach.py); its tests compare the results with them.",
        "MODES = {",
    ]
    for name, m in d["modes"].items():
        s = m["settings"]
        out += [
            f"    {name!r}: {{",
            f"        'amounts': {m['amounts']!r},",
            f"        'double_teleport': {m['double_teleport']},",
            f"        'energy_total': {s.energy_total},",
            f"        'mode69': {s.mode69},",
            f"        'options': bytes.fromhex({s.options.hex()!r}),",
            f"        'pool': {list(m['pool'])!r},",
            f"        'start_inventory': {m['start_inventory']!r},",
            f"        'regions': {m['regions']!r},",
            f"        'location_indices': {m['location_indices']!r},",
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
    out += [
        "}",
        "",
        "# Cell -> the index of its region.",
        f"CELL_REGION = {d['cell_region']!r}",
        "",
        "# (from region, to region) -> ways through: None (always open) or (entries, cell),",
        "# where cell is set when the entries hold a code 64-69, which is checked at a cell.",
        f"ENTRANCES = {dict(sorted(d['entrances'].items()))!r}",
        "",
        "# Location index -> the requirements for collecting its item, where it has any.",
        f"LOCATION_RULES = {d['location_rules']!r}",
        "",
        "# Every start-room candidate, in the start table's order: its index, cell and the",
        "# save point's tile column and row (docs/re/save-format.md).",
        f"START_ROOMS = {d['start_rooms']!r}",
        "",
        "# The randomizer menu's start table: each candidate room's cell (x, y) and flag, and",
        "# the start of the SHA-256 of the exe whose data it came from (the build it holds in).",
        f"START_TABLE = {d['start_table']!r}",
        f"DATA_BUILD = {d['build']!r}",
        "",
        "# The area of each location's and start room's cell, as the pause screen's REGION: names",
        "# it (from MapData and Text), for the names players see.",
        f"CELL_AREAS = {d['cell_areas']!r}",
        "",
        "# The core goal: Zexion's cell in the core, which no start reaches here (RandoCells",
        "# opens the core only with Abridged Finale); the kept cell whose passage leads in; and",
        "# the ways on from there to Zexion's cell, all needed, each as ENTRANCES writes a way.",
        f"CORE = {d['core']!r}",
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
    parser.add_argument("--map-data", type=Path, help="default: MapData next to RandoCells")
    parser.add_argument("--text", type=Path, help="default: Text next to RandoCells")
    args = parser.parse_args(argv)
    unknown = sorted({name for name, _ in args.mode or []} - set(DEFAULT_MODES))
    if unknown:
        # data.py needs every mode the world's Mode option offers (generate_early reads it).
        raise SystemExit(f"no such mode {unknown}: the modes are {sorted(DEFAULT_MODES)}")
    mode_saves = {**DEFAULT_MODES, **dict(args.mode or [])}
    cells_path = rc.find_randocells(args.cells)
    starts_path = args.starts or cells_path.parent / "start-candidates.json"
    exe_path = args.exe or cells_path.parent / "Zexion.exe"
    d = build(cells_path, mode_saves, starts_path, exe_path)
    map_path = args.map_data or cells_path.parent / "MapData"
    text_path = args.text or cells_path.parent / "Text"
    named = {tuple(c) for c in d["locations"]} | {(x, y) for x, y, _ in d["start_table"]}
    map_data = map_path.read_bytes()
    d["cell_areas"] = dict(sorted(cell_areas(map_data, text_path.read_bytes(), named).items()))
    grid = rc.parse(cells_path.read_bytes())
    d["core"] = core_goal(grid, set(d["cell_region"]), zexion_cells(map_data))
    saves = ", ".join(
        f"{name}: {path.name} (SHA-256 {hashlib.sha256(path.read_bytes()).hexdigest()[:12]})"
        for name, path in mode_saves.items()
    )
    source = (
        f"RandoCells, MapData, Text and Zexion.exe of build {cells_path.parent.name}; "
        f"fresh saves {saves}"
    )
    out = assert_writable(OUT)
    out.write_text(render(d, source), encoding="utf-8", newline="\n")
    n_ways = sum(len(w) for w in d["entrances"].values())
    per_mode = ", ".join(f"{n}: {len(m['starts'])} starts" for n, m in d["modes"].items())
    print(
        f"{len(set(d['cell_region'].values()))} regions, {len(d['entrances'])} region links "
        f"({n_ways} ways), {len(d['location_rules'])} gated locations; {per_mode}; "
        f"Zexion at {d['core']['cell']}, the core entered from {d['core']['door']} -> {out}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
