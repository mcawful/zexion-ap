"""Look at, and carefully change, the running game's memory. See /find-address.

    memscan.py find HEX [--max N]            where a byte string occurs in writable memory
    memscan.py snap NAME [--module-only]     snapshot writable memory into lab/mem/NAME/
    memscan.py cmp A B [--from X --to Y] [--max N]   bytes that changed between two snapshots
    memscan.py read ADDR LEN                 hex dump of live memory
    memscan.py write ADDR HEX --yes          write live memory, then read it back
    memscan.py classify LABEL=SNAP,SNAP ... [--target LABEL] [--out NAME]
                                             bytes that tell one game state from the others
    memscan.py record NAME (--candidates NAME | --rva RVA,...) [--interval S] [--seconds S]
                                             log every change of those bytes, with timestamps

Only the game process is touched; nothing here opens the install or the save folder. Addresses
are only good for one run of the game (ASLR); each output also gives the address relative to
the start of its memory region, and to Zexion.exe's base when it lies inside the module.

``classify`` and ``record`` work in RVAs, so they hold across restarts: ``classify`` keeps the
bytes whose value is stable within each group of ``snap --module-only`` snapshots and differs
in the target group (``--out`` saves them to lab/mem/NAME.json); ``record`` polls them, read
only, and logs changes to lab/mem/NAME.log. Both refuse to mix builds.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import sys
import time
from pathlib import Path

import numpy as np

from protected import ROOT, assert_writable, lab_dir

sys.path.insert(0, str(ROOT))  # the client package
from client import pe, process  # noqa: E402

PROCESS = "Zexion.exe"
MEM_COMMIT = 0x1000
WRITABLE = {0x04, 0x08, 0x40, 0x80}  # READWRITE, WRITECOPY, EXECUTE_READWRITE, EXECUTE_WRITECOPY
PAGE_GUARD = 0x100
TYPE_NAMES = {0x20000: "private", 0x40000: "mapped", 0x1000000: "image"}


class MBI(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_ulonglong),
        ("AllocationBase", ctypes.c_ulonglong),
        ("AllocationProtect", ctypes.c_uint32),
        ("PartitionId", ctypes.c_uint16),
        ("RegionSize", ctypes.c_ulonglong),
        ("State", ctypes.c_uint32),
        ("Protect", ctypes.c_uint32),
        ("Type", ctypes.c_uint32),
    ]


def attach() -> tuple[process.Process, int, int]:
    """The running game (client/process.py), its module's base and its size."""
    pm = process.Process(PROCESS)
    module = pm.module()
    if module is None or not module[0]:
        pm.close_process()
        raise RuntimeError(f"{PROCESS} is running but its module was not found")
    return pm, module[0], module[1]


def build_of(pm, mod_base: int) -> dict:
    """The running module's PE TimeDateStamp and SizeOfImage, which identify the build."""
    try:
        header = pe.parse(pm.read_bytes(mod_base, 0x1000))
    except pe.PEError as exc:
        raise RuntimeError(f"Zexion.exe's header: {exc}") from exc
    return {"timestamp": header.timestamp, "size_of_image": header.size_of_image}


def regions(pm) -> list[tuple[int, int, str]]:
    """Committed, writable, non-guard regions: (base, size, type)."""
    query = ctypes.windll.kernel32.VirtualQueryEx
    query.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(MBI), ctypes.c_size_t]
    query.restype = ctypes.c_size_t
    out, addr, mbi = [], 0, MBI()
    while addr < 0x7FFF_FFFF_FFFF and query(
        pm.process_handle, addr, ctypes.byref(mbi), ctypes.sizeof(MBI)
    ):
        prot = mbi.Protect & 0xFF
        if mbi.State == MEM_COMMIT and prot in WRITABLE and not mbi.Protect & PAGE_GUARD:
            out.append((mbi.BaseAddress, mbi.RegionSize, TYPE_NAMES.get(mbi.Type, hex(mbi.Type))))
        addr = mbi.BaseAddress + mbi.RegionSize
    return out


def read_region(pm, base: int, size: int) -> bytes | None:
    try:
        return pm.read_bytes(base, size)
    except Exception:
        return None  # freed or protected since the query


def describe(addr: int, region_base: int, mod_base: int, mod_size: int) -> str:
    where = f"region+0x{addr - region_base:x}"
    if mod_base <= addr < mod_base + mod_size:
        where += f"  Zexion.exe+0x{addr - mod_base:x}"
    return f"0x{addr:012x}  {where}"


def cmd_find(args: argparse.Namespace) -> int:
    pm, mod_base, mod_size = attach()
    needle = bytes.fromhex(args.hex)
    hits = 0
    for base, size, kind in regions(pm):
        data = read_region(pm, base, size)
        if data is None:
            continue
        start = data.find(needle)
        while start != -1 and hits < args.max:
            print(f"{describe(base + start, base, mod_base, mod_size)}  ({kind})")
            hits += 1
            start = data.find(needle, start + 1)
    print(f"{hits} hit(s){' (limit reached)' if hits >= args.max else ''}")
    return 0


def mem_path(name: str) -> Path:
    """lab/mem/NAME (a snapshot, candidates, a log), checked writable, lab/mem made."""
    path = assert_writable(lab_dir() / "mem" / name)
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def cmd_snap(args: argparse.Namespace) -> int:
    pm, mod_base, mod_size = attach()
    dest = mem_path(args.name)
    dest.mkdir(exist_ok=False)
    index, offset = [], 0
    with (dest / "memory.bin").open("wb") as f:
        for base, size, kind in regions(pm):
            if args.module_only and not (mod_base <= base < mod_base + mod_size):
                continue
            data = read_region(pm, base, size)
            if data is None:
                continue
            f.write(data)
            index.append({"base": base, "size": len(data), "type": kind, "offset": offset})
            offset += len(data)
    meta = {
        "module_base": mod_base,
        "module_size": mod_size,
        "build": build_of(pm, mod_base),
        "regions": index,
        "note": args.note,
    }
    (dest / "index.json").write_text(json.dumps(meta, indent=1), encoding="utf-8")
    print(f"Snapshot {args.name}: {len(index)} regions, {offset / 2**20:.1f} MiB -> {dest}")
    return 0


def _load(name: str) -> tuple[dict, np.memmap]:
    folder = mem_path(name)
    meta = json.loads((folder / "index.json").read_text(encoding="utf-8"))
    return meta, np.memmap(folder / "memory.bin", dtype=np.uint8, mode="r")


def cmd_cmp(args: argparse.Namespace) -> int:
    meta_a, mem_a = _load(args.a)
    meta_b, mem_b = _load(args.b)
    regions_b = {(r["base"], r["size"]): r for r in meta_b["regions"]}
    mod_base, mod_size = meta_b["module_base"], meta_b["module_size"]
    shown = total = 0
    for ra in meta_a["regions"]:
        rb = regions_b.get((ra["base"], ra["size"]))
        if rb is None:
            continue  # region came or went; compare only what both have
        a = mem_a[ra["offset"] : ra["offset"] + ra["size"]]
        b = mem_b[rb["offset"] : rb["offset"] + rb["size"]]
        mask = a != b
        if args.frm is not None:
            mask &= a == args.frm
        if args.to is not None:
            mask &= b == args.to
        idx = np.flatnonzero(mask)
        total += len(idx)
        for i in idx[: max(0, args.max - shown)]:
            addr = ra["base"] + int(i)
            where = describe(addr, ra["base"], mod_base, mod_size)
            print(f"{where}  {a[i]:02x} -> {b[i]:02x}  ({ra['type']})")
            shown += 1
    print(f"{total} changed byte(s), {shown} shown")
    return 0


def cmd_read(args: argparse.Namespace) -> int:
    pm, _, _ = attach()
    addr, data = int(args.addr, 0), pm.read_bytes(int(args.addr, 0), args.length)
    for i in range(0, len(data), 16):
        print(f"0x{addr + i:012x}  {data[i : i + 16].hex(' ')}")
    return 0


def cmd_write(args: argparse.Namespace) -> int:
    if not args.yes:
        print("write changes the running game; pass --yes once the user has agreed")
        return 1
    pm, _, _ = attach()
    addr, new = int(args.addr, 0), bytes.fromhex(args.hex)
    old = pm.read_bytes(addr, len(new))
    pm.write_bytes(addr, new)
    back = pm.read_bytes(addr, len(new))
    print(f"0x{addr:012x}: {old.hex(' ')} -> {back.hex(' ')}")
    if back != new:
        print("read-back does not match what was written")
        return 1
    return 0


def module_image(meta: dict, mem: np.ndarray, lo: int, hi: int) -> tuple[np.ndarray, np.ndarray]:
    """A snapshot's module bytes over RVAs [lo, hi), and which of them the snapshot holds.

    Regions are placed by RVA, so snapshots whose regions were split differently (Windows splits
    a section where page protection differs) still line up byte for byte."""
    data = np.zeros(hi - lo, dtype=np.uint8)
    present = np.zeros(hi - lo, dtype=bool)
    base = meta["module_base"]
    for r in meta["regions"]:
        rva = r["base"] - base
        a, b = max(rva, lo), min(rva + r["size"], hi)
        if a < b:
            src = r["offset"] + (a - rva)
            data[a - lo : b - lo] = mem[src : src + (b - a)]
            present[a - lo : b - lo] = True
    return data, present


def classify(groups: dict[str, list[np.ndarray]], target: str) -> tuple[np.ndarray, dict]:
    """Indices where each group's snapshots all agree and the target group's value differs from
    every other group's, with each group's value there."""
    if target not in groups:
        raise ValueError(f"target {target!r} is not one of the groups")
    stable = None
    first = {}
    for label, arrays in groups.items():
        same = np.ones(len(arrays[0]), dtype=bool)
        for a in arrays[1:]:
            same &= a == arrays[0]
        stable = same if stable is None else stable & same
        first[label] = arrays[0]
    mask = stable
    for label, values in first.items():
        if label != target:
            mask &= first[target] != values
    idx = np.flatnonzero(mask)
    return idx, {label: np.asarray(v[idx]) for label, v in first.items()}


def parse_group(text: str) -> tuple[str, list[str]]:
    """``LABEL=SNAP,SNAP`` -> (label, [snapshots])."""
    label, sep, names = text.partition("=")
    snaps = [n for n in names.split(",") if n]
    if not sep or not label or not snaps:
        raise argparse.ArgumentTypeError(f"expected LABEL=SNAP[,SNAP...], got {text!r}")
    return label, snaps


def one_build(metas: dict[str, dict]) -> dict | None:
    """The build all the snapshots share; None if none records one. Raises if they differ."""
    builds = {json.dumps(m["build"], sort_keys=True) for m in metas.values() if "build" in m}
    unrecorded = [n for n, m in metas.items() if "build" not in m]
    if len(builds) > 1:
        raise ValueError(f"the snapshots come from different builds: {sorted(builds)}")
    if unrecorded:
        print(f"note: no build recorded in {', '.join(unrecorded)}; cannot check they match")
    return json.loads(builds.pop()) if builds else None


def cmd_classify(args: argparse.Namespace) -> int:
    labels = [label for label, _ in args.groups]
    repeated = {label for label in labels if labels.count(label) > 1}
    if repeated:
        print(f"label(s) given twice: {', '.join(sorted(repeated))}; list each state once")
        return 1
    groups_named = dict(args.groups)
    target = args.target or labels[0]
    if target not in groups_named:
        print(f"--target {target!r} is not one of the states: {', '.join(labels)}")
        return 1
    for label, names in groups_named.items():
        if len(names) < 2:
            print(f"note: {label} has one snapshot, so nothing in it can be called stable")
    loaded = {n: _load(n) for names in groups_named.values() for n in names}
    try:
        build = one_build({n: meta for n, (meta, _) in loaded.items()})
    except ValueError as exc:
        print(f"REFUSED: {exc}")
        return 1
    rvas = [
        (r["base"] - meta["module_base"], r["size"])
        for meta, _ in loaded.values()
        for r in meta["regions"]
        if meta["module_base"] <= r["base"] < meta["module_base"] + meta["module_size"]
    ]
    if not rvas:
        print("no module memory in these snapshots; were they taken with --module-only?")
        return 1
    lo, hi = min(r for r, _ in rvas), max(r + n for r, n in rvas)
    present_all = np.ones(hi - lo, dtype=bool)
    images = {}
    for n, (meta, mem) in loaded.items():
        images[n], present = module_image(meta, mem, lo, hi)
        present_all &= present
    groups = {label: [images[n] for n in names] for label, names in groups_named.items()}
    idx, values = classify(groups, target)
    keep = present_all[idx]
    idx = idx[keep]
    values = {label: v[keep] for label, v in values.items()}
    print(
        f"compared {int(present_all.sum()):,} bytes held by every snapshot, of "
        f"{hi - lo:,} between RVA 0x{lo:x} and 0x{hi:x}"
    )
    found = [
        (lo + int(i), {label: int(v[k]) for label, v in values.items()}) for k, i in enumerate(idx)
    ]
    found.sort(key=lambda f: (-len(set(f[1].values())), f[0]))
    print(f"{len(found)} byte(s) tell {target} from the other states")
    for rva, vals in found[: args.max]:
        shown = "  ".join(f"{label}={v}" for label, v in vals.items())
        print(f"  Zexion.exe+0x{rva:x}  {shown}")
    if args.out:
        out = mem_path(f"{args.out}.json")
        kept = [{"rva": rva, "values": vals} for rva, vals in found[: args.keep]]
        meta = {"target": target, "groups": groups_named, "build": build, "candidates": kept}
        out.write_text(json.dumps(meta, indent=1), encoding="utf-8")
        print(f"{len(kept)} saved to {out}")
    return 0


def spans(rvas: list[int], gap: int = 256) -> list[tuple[int, int, list[int]]]:
    """(start, length, the RVAs in it) ranges covering ``rvas``, merging neighbors less than
    ``gap`` bytes apart."""
    groups: list[list[int]] = []
    for rva in sorted(set(rvas)):
        if groups and rva - groups[-1][-1] <= gap:
            groups[-1].append(rva)
        else:
            groups.append([rva])
    return [(g[0], g[-1] - g[0] + 1, g) for g in groups]


def changes_between(old: dict[int, int], new: dict[int, int]) -> list[tuple[int, int, int]]:
    """(RVA, old, new) for each byte whose value differs."""
    return [(rva, old.get(rva), new[rva]) for rva in sorted(new) if old.get(rva) != new[rva]]


def cmd_record(args: argparse.Namespace) -> int:
    expected_build = None
    if args.candidates:
        meta = json.loads(mem_path(f"{args.candidates}.json").read_text("utf-8"))
        rvas = [c["rva"] for c in meta["candidates"]]
        expected_build = meta.get("build")
    else:
        rvas = [int(r, 0) for r in args.rva.split(",") if r]
    if not rvas:
        print("no addresses to record")
        return 1
    pm, mod_base, _ = attach()
    build = build_of(pm, mod_base)
    if expected_build is not None and build != expected_build:
        print(f"REFUSED: the running build {build} is not the candidates' {expected_build}")
        return 1
    plan = spans(rvas)  # each range read once, with the RVAs inside it

    def sample() -> dict[int, int]:
        values = {}
        for start, length, inside in plan:
            data = pm.read_bytes(mod_base + start, length)
            for rva in inside:
                values[rva] = data[rva - start]
        return values

    path = mem_path(f"{args.name}.log")
    t0 = time.monotonic()
    deadline = t0 + args.seconds if args.seconds else None
    reason, polls, longest, last = "deadline", 0, 0.0, None
    with path.open("a", encoding="utf-8") as log:

        def emit(line: str) -> None:
            stamp = time.strftime("%H:%M:%S") + f" +{time.monotonic() - t0:8.3f}s"
            print(f"{stamp}  {line}", flush=True)
            log.write(f"{stamp}  {line}\n")
            log.flush()

        emit(
            f"start: {len(set(rvas))} byte(s) in {len(plan)} range(s); module base "
            f"0x{mod_base:x}; build {build}"
        )
        try:
            current = sample()
            emit("values " + " ".join(f"+0x{r:x}={v}" for r, v in sorted(current.items())))
            last = time.monotonic()
            while deadline is None or time.monotonic() < deadline:
                time.sleep(args.interval)
                new = sample()
                now = time.monotonic()
                polls += 1
                longest = max(longest, now - last)
                last = now
                for rva, a, b in changes_between(current, new):
                    emit(f"+0x{rva:x}: {a} -> {b}")
                current = new
        except KeyboardInterrupt:
            reason = "stopped"
        except Exception as exc:  # the game exited, or its memory went away
            reason = f"read failed: {exc}"
        finally:
            emit(f"end: {reason}; {polls} poll(s), longest gap {longest * 1000:.0f} ms")
    return 0 if reason in ("deadline", "stopped") else 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("find")
    p.add_argument("hex")
    p.add_argument("--max", type=int, default=50)
    p.set_defaults(func=cmd_find)
    p = sub.add_parser("snap")
    p.add_argument("name")
    p.add_argument("--note", default="")
    p.add_argument("--module-only", action="store_true", help="only Zexion.exe's own data")
    p.set_defaults(func=cmd_snap)
    p = sub.add_parser("cmp")
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--from", dest="frm", type=lambda s: int(s, 0))
    p.add_argument("--to", type=lambda s: int(s, 0))
    p.add_argument("--max", type=int, default=100)
    p.set_defaults(func=cmd_cmp)
    p = sub.add_parser("read")
    p.add_argument("addr")
    p.add_argument("length", type=int)
    p.set_defaults(func=cmd_read)
    p = sub.add_parser("write")
    p.add_argument("addr")
    p.add_argument("hex")
    p.add_argument("--yes", action="store_true")
    p.set_defaults(func=cmd_write)
    p = sub.add_parser("classify")
    p.add_argument("groups", nargs="+", type=parse_group, help="LABEL=SNAP,SNAP (module-only)")
    p.add_argument("--target", help="the state to tell apart (default: the first group)")
    p.add_argument("--max", type=int, default=60)
    p.add_argument("--out", help="save the candidates to lab/mem/OUT.json")
    p.add_argument("--keep", type=int, default=5000, help="how many candidates --out saves")
    p.set_defaults(func=cmd_classify)
    p = sub.add_parser("record")
    p.add_argument("name")
    src = p.add_mutually_exclusive_group(required=True)
    src.add_argument("--candidates", help="a classify --out NAME")
    src.add_argument("--rva", help="comma-separated RVAs")
    p.add_argument("--interval", type=float, default=0.02)
    p.add_argument("--seconds", type=float, default=0)
    p.set_defaults(func=cmd_record)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
