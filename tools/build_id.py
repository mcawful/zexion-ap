"""Fingerprint the installed game build, read-only, as a block to paste into docs/re/builds.md.

Every fact in docs/re/ names the build it was measured on. The game is still being patched, so
the exe hash is the identity; the PE timestamp and Steam's build id make it readable. The data
files the randomizer reads are hashed too, since a patch can change them without the exe.

A Linux install holds the ``Zexion`` binary instead of ``Zexion.exe``: its hash, its GNU build
id, its ELF file type and whether it keeps its symbol table are read instead of the PE header.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import re
import sys
from pathlib import Path

from protected import ROOT, STEAM_APP_ID, game_dir, game_file

sys.path.insert(0, str(ROOT))  # the client package
from client import elf, pe  # noqa: E402

DATA_FILES = ("RandoCells", "MapData", "Objects", "AreaEvents", "Enemies", "Flags", "Text",
              "Bosses", "Missiles")  # fmt: skip
# The COFF machine type, and the DllCharacteristics bits of the optional header worth naming.
MACHINES = {0x8664: "x64", 0x14C: "x86", 0xAA64: "ARM64"}
ELF_MACHINES = {elf.EM_X86_64: "x86-64", 183: "AArch64"}
ELF_TYPES = {elf.ET_EXEC: "EXEC (fixed addresses)", elf.ET_DYN: "DYN (position-independent)"}
DLL_FLAGS = {0x0020: "HighEntropyVA", 0x0040: "DynamicBase (ASLR)", 0x0100: "NX"}


def steam_build_id(install: Path) -> str | None:
    manifest = install.parent.parent / f"appmanifest_{STEAM_APP_ID}.acf"
    if not manifest.is_file():
        return None
    match = re.search(
        r'"buildid"\s+"(\d+)"', manifest.read_text(encoding="utf-8", errors="replace")
    )
    return match.group(1) if match else None


def _exe_lines(install: Path, exe: bytes) -> list[str]:
    header = pe.parse(exe)
    stamp = dt.datetime.fromtimestamp(header.timestamp, dt.UTC)
    machine = MACHINES.get(header.machine, hex(header.machine))
    flags = [name for bit, name in DLL_FLAGS.items() if header.dll_characteristics & bit]
    dlls = sorted(p.name for p in install.glob("*.dll"))
    return [
        f"- Zexion.exe SHA-256: `{hashlib.sha256(exe).hexdigest()}`",
        f"- Size: {len(exe):,} bytes",
        f"- PE timestamp: {stamp:%Y-%m-%d %H:%M:%S} UTC",
        f"- Machine: {machine}; DllCharacteristics: {', '.join(flags) or 'none of interest'}",
        f"- DLLs beside the exe: {', '.join(dlls) or 'none'}",
    ]


def _elf_lines(install: Path, binary: bytes) -> list[str]:
    image = elf.parse(binary)
    machine = ELF_MACHINES.get(image.machine, str(image.machine))
    kind = ELF_TYPES.get(image.type, str(image.type))
    has_symtab = any(s.type == elf.SHT_SYMTAB for s in image.sections)
    libs = sorted(p.name for p in install.glob("*.so*") if p.is_file())
    return [
        f"- Zexion (Linux) SHA-256: `{hashlib.sha256(binary).hexdigest()}`",
        f"- Size: {len(binary):,} bytes",
        f"- GNU build id: `{image.build_id() or 'none'}`",
        f"- Machine: {machine}; type: {kind}; symbol table: {'kept' if has_symtab else 'none'}",
        f"- Libraries beside the binary: {', '.join(libs) or 'none'}",
    ]


def fingerprint(install: Path) -> str:
    path = game_file(install)
    data = path.read_bytes()
    lines = _exe_lines(install, data) if path.suffix == ".exe" else _elf_lines(install, data)
    lines += [
        f"- Steam build id: {steam_build_id(install) or 'not found'}",
        "- Data files (SHA-256, first 16 hex):",
    ]
    for name in DATA_FILES:
        path = install / "Files" / name
        if path.is_file():
            digest = hashlib.sha256(path.read_bytes()).hexdigest()[:16]
            lines.append(f"  - `Files/{name}`: `{digest}` ({path.stat().st_size:,} bytes)")
        else:
            lines.append(f"  - `Files/{name}`: missing")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=Path, help="override the install folder")
    args = parser.parse_args(argv)
    install = args.game_dir or game_dir()
    print(fingerprint(install))
    return 0


if __name__ == "__main__":
    sys.exit(main())
