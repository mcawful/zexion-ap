"""Find byte patterns that locate a data address or a function in Zexion.exe, for use after ASLR
and patches.

    make_pattern.py EXE RVA [--size N] [--count K] [--max-bytes M]

EXE is a copy of Zexion.exe (``game_data.py --exe`` puts one in lab/data/<build>/). RVA is the
data address or function to locate, relative to the image base (``Zexion.exe+0x3E469D`` ->
``0x3E469D``); ``--size`` widens it to a range (a struct or array), so references to its fields
count too.

For each instruction that refers to the range (RIP-relative, a call/jmp rel32, or an
image-base offset), it grows a window of whole instructions, wildcards every field that moves
when code or data moves, and keeps the shortest window that matches once in .text; up to K
such patterns, each with how a match becomes the address (the form client/patterns.py uses).
When RVA starts a function it also prints an "at" pattern over its first instructions. The
file is only read.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

from capstone import CS_ARCH_X86, CS_MODE_64, CS_OP_MEM, Cs
from capstone.x86 import X86_REG_RIP

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))  # the client package
from client import game, pe  # noqa: E402
from client.patterns import Pattern  # noqa: E402  (kind "rva" too: printed, refused by the client)

IMAGE_BASE_MIN_RVA = 0x10000  # an absolute displacement this large is taken as an RVA


class Image:
    def __init__(self, data: bytes):
        self.data = data
        header = pe.parse(data)
        self.sections = header.sections
        self.text = header.section(".text")
        self.text_bytes = data[self.text.raw : self.text.raw + self.text.rsize]
        self.md = Cs(CS_ARCH_X86, CS_MODE_64)
        self.md.detail = True
        # Function ranges from the unwind table: a linear pass over .text would stop at the
        # first byte capstone cannot decode.
        pdata = header.section(".pdata")
        self.functions = []
        for o in range(pdata.raw, pdata.raw + pdata.vsize - 11, 12):
            begin, end, _ = struct.unpack_from("<III", data, o)
            if self.text.va <= begin < end <= self.text.va + self.text.vsize:
                self.functions.append((begin, end))

    def code_at(self, rva: int, length: int) -> bytes:
        off = self.text.raw + rva - self.text.va
        return self.data[off : off + length]

    def instructions(self, rva: int, count: int):
        return list(self.md.disasm(self.code_at(rva, count * 15), rva))[:count]


def _is_rip(ins) -> bool:
    return any(op.type == CS_OP_MEM and op.mem.base == X86_REG_RIP for op in ins.operands)


IMAGE_SIZE_MAX = 0x4000000  # displacements above this are not RVAs into Zexion.exe


def _has_disp32(ins) -> bool:
    """A 32-bit displacement that is an address. capstone's ``disp_size`` cannot be trusted
    here: it reports 2 for a 16-bit store through [rip + disp32]. A non-RIP displacement counts
    as an image-base offset only when it is a plausible RVA (positive, inside the image); such
    patterns are printed but the client refuses them (client/game.py)."""
    if not ins.disp_offset:
        return False
    return _is_rip(ins) or IMAGE_BASE_MIN_RVA <= ins.disp < IMAGE_SIZE_MAX


def _wildcards(ins) -> set[int]:
    """Byte offsets within ``ins`` that hold code or data addresses."""
    out: set[int] = set()
    if _has_disp32(ins):
        out.update(range(ins.disp_offset, ins.disp_offset + 4))
    if ins.imm_size == 4 and ins.imm_offset and ins.mnemonic.startswith(("call", "j")):
        out.update(range(ins.imm_offset, ins.imm_offset + 4))
    return out


def _text(raw: bytes, wild: set[int]) -> str:
    return " ".join("??" if i in wild else f"{b:02X}" for i, b in enumerate(raw))


def _call_target(ins) -> int | None:
    """Where a ``call``/``jmp`` with a 32-bit offset goes, or None."""
    if ins.mnemonic in ("call", "jmp") and ins.imm_size == 4 and ins.imm_offset:
        return ins.operands[0].imm
    return None


def find_references(img: Image, lo: int, hi: int) -> list[tuple[int, str]]:
    """Addresses of instructions referring into [lo, hi), with the kind of reference."""
    out = []
    for begin, end in img.functions:
        for ins in img.md.disasm(img.code_at(begin, end - begin), begin):
            call = _call_target(ins)
            if call is not None:
                if lo <= call < hi:
                    out.append((ins.address, "rip"))
                continue
            if not _has_disp32(ins):
                continue
            rip = _is_rip(ins)
            target = ins.address + ins.size + ins.disp if rip else ins.disp
            if lo <= target < hi:
                out.append((ins.address, "rip" if rip else "rva"))
    return out


def fixed_bytes(p: Pattern) -> int:
    return len(p.text.split()) - p.text.split().count("??")


def _unique_window(img: Image, rva: int, max_bytes: int, min_fixed: int):
    """The shortest window of whole instructions from ``rva`` that matches once in .text, as
    the client counts matches (overlapping ones too): (its first instruction, its pattern
    text), or None."""
    insns = img.instructions(rva, 12)
    raw, wild = b"", set()
    for ins in insns:
        start = len(raw)
        raw += bytes(ins.bytes)
        wild |= {start + w for w in _wildcards(ins)}
        if len(raw) > max_bytes:
            return None
        if len(raw) - len(wild) < min_fixed:
            continue  # too few fixed bytes to survive a patch, or to mean anything
        text = _text(raw, wild)
        if len(game.compile_pattern(text).findall(img.text_bytes)) == 1:
            return insns[0], text
    return None


def pattern_for(
    img: Image, ref_rva: int, kind: str, want: int, max_bytes: int, min_fixed: int
) -> Pattern | None:
    """The shortest unique window starting at the referring instruction, or (``kind`` "at") at
    the start of a function."""
    window = _unique_window(img, ref_rva, max_bytes, min_fixed)
    if window is None:
        return None
    first, text = window
    if kind == "at":
        return Pattern(text=text, disp_at=0, kind="at", rip_end=0, addend=0)
    call = _call_target(first)
    if call is not None:
        target, field = call, first.imm_offset
    elif kind == "rip":
        target, field = first.address + first.size + first.disp, first.disp_offset
    else:
        target, field = first.disp, first.disp_offset
    return Pattern(text=text, disp_at=field, kind=kind, rip_end=first.size, addend=want - target)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("exe", type=Path)
    parser.add_argument("rva", type=lambda s: int(s, 0))
    parser.add_argument("--size", type=lambda s: int(s, 0), default=1)
    parser.add_argument("--count", type=int, default=3)
    parser.add_argument("--max-bytes", type=int, default=48)
    parser.add_argument("--min-fixed", type=int, default=12)
    args = parser.parse_args(argv)

    img = Image(args.exe.read_bytes())
    refs = find_references(img, args.rva, args.rva + args.size)
    print(f"{len(refs)} references into 0x{args.rva:X}..0x{args.rva + args.size:X}")
    found = []
    for ref, kind in refs:
        p = pattern_for(img, ref, kind, args.rva, args.max_bytes, args.min_fixed)
        if p is not None:
            found.append((ref, p))
    # Instruction-pointer-relative first (they hold wherever the image base is kept), then
    # by fixed bytes: more fixed bytes are less likely to match by chance after a patch.
    found.sort(key=lambda rp: (rp[1].kind != "rip", -fixed_bytes(rp[1])))
    for ref, p in found[: args.count]:
        print(
            f"ref 0x{ref:X} ({p.kind}, {fixed_bytes(p)} fixed bytes): disp_at={p.disp_at} "
            f"rip_end={p.rip_end} addend={p.addend:#x}\n  {p.text}"
        )
    if any(begin == args.rva for begin, _ in img.functions):
        p = pattern_for(img, args.rva, "at", args.rva, args.max_bytes, args.min_fixed)
        if p is not None:
            found.append((args.rva, p))
            print(f"function start (at, {fixed_bytes(p)} fixed bytes):\n  {p.text}")
    if not found:
        print("no unique pattern found; try --max-bytes, --min-fixed or a wider --size")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
