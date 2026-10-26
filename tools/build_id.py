"""Fingerprint the installed game build, read-only, as a block to paste into docs/re/builds.md.

Every fact in docs/re/ names the build it was measured on. The game is still being patched, so
the exe hash is the identity; the PE timestamp and Steam's build id make it readable. The data
files the randomizer reads are hashed too, since a patch can change them without the exe.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import re
import sys
from pathlib import Path

from protected import STEAM_APP_ID, game_dir

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))  # the client package
from client import pe  # noqa: E402

DATA_FILES = (
    "RandoCells",
    "MapData",
    "Objects",
    "AreaEvents",
    "Enemies",
    "Flags",
    "Text",
    "Bosses",
)


MACHINES = {0x8664: "x64", 0x14C: "x86", 0xAA64: "ARM64"}
DLL_FLAGS = {0x0020: "HighEntropyVA", 0x0040: "DynamicBase (ASLR)", 0x0100: "NX"}


def pe_timestamp(exe: bytes) -> dt.datetime:
    return dt.datetime.fromtimestamp(pe.parse(exe).timestamp, dt.UTC)


def pe_machine_and_flags(exe: bytes) -> tuple[str, list[str]]:
    """The COFF machine type and the named DllCharacteristics bits of the optional header."""
    header = pe.parse(exe)
    flags = [name for bit, name in DLL_FLAGS.items() if header.dll_characteristics & bit]
    return MACHINES.get(header.machine, hex(header.machine)), flags


def steam_build_id(install: Path) -> str | None:
    manifest = install.parent.parent / f"appmanifest_{STEAM_APP_ID}.acf"
    if not manifest.is_file():
        return None
    match = re.search(
        r'"buildid"\s+"(\d+)"', manifest.read_text(encoding="utf-8", errors="replace")
    )
    return match.group(1) if match else None


def fingerprint(install: Path) -> str:
    exe = (install / "Zexion.exe").read_bytes()
    machine, flags = pe_machine_and_flags(exe)
    dlls = sorted(p.name for p in install.glob("*.dll"))
    lines = [
        f"- Zexion.exe SHA-256: `{hashlib.sha256(exe).hexdigest()}`",
        f"- Size: {len(exe):,} bytes",
        f"- PE timestamp: {pe_timestamp(exe):%Y-%m-%d %H:%M:%S} UTC",
        f"- Machine: {machine}; DllCharacteristics: {', '.join(flags) or 'none of interest'}",
        f"- DLLs beside the exe: {', '.join(dlls) or 'none'}",
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
