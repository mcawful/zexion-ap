"""Copy game data files (read-only) into lab/data/<build>/ so they can be analyzed freely.

    game_data.py RandoCells MapData Objects     copy these from the install's Files/ folder
    game_data.py --list                         list what Files/ holds (no copying)
    game_data.py --exe                          copy Zexion.exe too (for disassembly)

The install is only read. Copies go into lab/ (gitignored): game files must never be committed.
The build folder name is the first 12 hex digits of Zexion.exe's SHA-256, so data from
different game builds never mix.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import sys

from protected import assert_writable, game_dir, lab_dir

MEDIA = {".wav", ".ogg", ".nsf", ".png", ".svg"}  # left out of --list


def build_tag() -> str:
    return hashlib.sha256((game_dir() / "Zexion.exe").read_bytes()).hexdigest()[:12]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("names", nargs="*", help="file names inside the install's Files/ folder")
    parser.add_argument("--list", action="store_true", help="list Files/ instead of copying")
    parser.add_argument("--exe", action="store_true", help="also copy Zexion.exe")
    args = parser.parse_args(argv)

    files = game_dir() / "Files"
    if args.list:
        for path in sorted(files.iterdir()):
            if path.is_file() and path.suffix.lower() not in MEDIA:
                print(f"{path.stat().st_size:>10,}  {path.name}")
        return 0

    dest = assert_writable(lab_dir() / "data" / build_tag())
    dest.mkdir(parents=True, exist_ok=True)
    if args.exe:
        shutil.copy2(game_dir() / "Zexion.exe", dest / "Zexion.exe")
        print(f"Zexion.exe -> {dest / 'Zexion.exe'}")
    for name in args.names:
        src = files / name
        if src.parent != files or not src.is_file():
            print(f"skip {name}: not a file in Files/")
            continue
        shutil.copy2(src, dest / name)
        print(f"{name} -> {dest / name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
