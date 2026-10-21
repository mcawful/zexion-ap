"""Look at, and carefully change, the running game's memory. See /find-address.

    memscan.py find HEX [--max N]            where a byte string occurs in writable memory
    memscan.py snap NAME                     snapshot writable memory into lab/mem/NAME/
    memscan.py cmp A B [--from X --to Y] [--max N]   bytes that changed between two snapshots
    memscan.py read ADDR LEN                 hex dump of live memory
    memscan.py write ADDR HEX --yes          write live memory, then read it back

Only the game process is touched; nothing here opens the install or the save folder. Addresses
are only good for one run of the game (ASLR); each output also gives the address relative to
the start of its memory region, and to Zexion.exe's base when it lies inside the module.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import sys
from ctypes import wintypes
from pathlib import Path

import numpy as np

from protected import assert_writable, lab_dir

PROCESS = "Zexion.exe"
MEM_COMMIT = 0x1000
WRITABLE = {0x04, 0x08, 0x40, 0x80}  # READWRITE, WRITECOPY, EXECUTE_READWRITE, EXECUTE_WRITECOPY
PAGE_GUARD = 0x100
TYPE_NAMES = {0x20000: "private", 0x40000: "mapped", 0x1000000: "image"}


class MBI(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_ulonglong),
        ("AllocationBase", ctypes.c_ulonglong),
        ("AllocationProtect", wintypes.DWORD),
        ("PartitionId", wintypes.WORD),
        ("RegionSize", ctypes.c_ulonglong),
        ("State", wintypes.DWORD),
        ("Protect", wintypes.DWORD),
        ("Type", wintypes.DWORD),
    ]


def attach():
    import pymem

    pm = pymem.Pymem(PROCESS)
    module = pymem.process.module_from_name(pm.process_handle, PROCESS)
    return pm, module.lpBaseOfDll, module.SizeOfImage


def regions(pm) -> list[tuple[int, int, str]]:
    """Committed, writable, non-guard regions: (base, size, type)."""
    query = ctypes.windll.kernel32.VirtualQueryEx
    query.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.POINTER(MBI), ctypes.c_size_t]
    query.restype = ctypes.c_size_t
    out, addr, mbi = [], 0, MBI()
    while addr < 0x7FFF_FFFF_FFFF and query(pm.process_handle, addr, ctypes.byref(mbi), 48):
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


def snap_dir(name: str) -> Path:
    return lab_dir() / "mem" / name


def cmd_snap(args: argparse.Namespace) -> int:
    pm, mod_base, mod_size = attach()
    dest = assert_writable(snap_dir(args.name))
    dest.mkdir(parents=True, exist_ok=False)
    index, offset = [], 0
    with (dest / "memory.bin").open("wb") as f:
        for base, size, kind in regions(pm):
            data = read_region(pm, base, size)
            if data is None:
                continue
            f.write(data)
            index.append({"base": base, "size": len(data), "type": kind, "offset": offset})
            offset += len(data)
    meta = {"module_base": mod_base, "module_size": mod_size, "regions": index, "note": args.note}
    (dest / "index.json").write_text(json.dumps(meta, indent=1), encoding="utf-8")
    print(f"Snapshot {args.name}: {len(index)} regions, {offset / 2**20:.1f} MiB -> {dest}")
    return 0


def _load(name: str) -> tuple[dict, np.memmap]:
    folder = snap_dir(name)
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
    pm.write_bytes(addr, new, len(new))
    back = pm.read_bytes(addr, len(new))
    print(f"0x{addr:012x}: {old.hex(' ')} -> {back.hex(' ')}")
    if back != new:
        print("read-back does not match what was written")
        return 1
    return 0


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
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
