"""Clone Archipelago at the tag in AP_VERSION into .ap/Archipelago and install its requirements.

Once ``worlds/zexion/`` exists, it is also linked into the checkout's ``worlds/`` folder (a
directory junction on Windows, a symlink elsewhere), so AP's own tests load the world from this
repo without copying it.

    .venv/Scripts/python tools/ap_checkout.py            # clone or verify, link, install deps
    .venv/Scripts/python tools/ap_checkout.py --no-deps  # skip AP's requirements
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

from protected import ROOT

AP_REPO = "https://github.com/ArchipelagoMW/Archipelago.git"


def git(*args: str, cwd: Path | None = None) -> str:
    result = subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True)
    return result.stdout.strip()


def ensure_checkout(dest: Path, tag: str) -> None:
    if not dest.exists():
        dest.parent.mkdir(parents=True, exist_ok=True)
        print(f"Cloning Archipelago {tag} into {dest}")
        git("clone", "--depth", "1", "--branch", tag, AP_REPO, str(dest))
        return
    current = git("describe", "--tags", "--exact-match", cwd=dest)
    if current != tag:
        print(f"Checkout is at {current}; switching to {tag}")
        git("fetch", "--depth", "1", "origin", "tag", tag, cwd=dest)
        git("checkout", "--detach", tag, cwd=dest)


def link_world(checkout: Path) -> None:
    world = ROOT / "worlds" / "zexion"
    link = checkout / "worlds" / "zexion"
    if not world.is_dir():
        print("worlds/zexion/ does not exist yet; nothing to link")
        return
    if link.exists():
        if link.resolve() != world.resolve():
            raise RuntimeError(f"{link} exists and does not point at {world}")
        return
    if sys.platform == "win32":
        subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(world)], check=True)
    else:
        os.symlink(world, link, target_is_directory=True)
    print(f"Linked {link} -> {world}")


def install_requirements(checkout: Path) -> None:
    # AP's own updater also installs each world's requirements, which its tests import.
    print("Installing Archipelago's requirements into", sys.executable)
    subprocess.run(
        [sys.executable, "-c", "import ModuleUpdate; ModuleUpdate.update(yes=True)"],
        cwd=checkout,
        check=True,
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-deps", action="store_true", help="skip installing AP's requirements")
    args = parser.parse_args(argv)

    tag = (ROOT / "AP_VERSION").read_text(encoding="utf-8").strip()
    checkout = ROOT / ".ap" / "Archipelago"
    ensure_checkout(checkout, tag)
    link_world(checkout)
    if not args.no_deps:
        install_requirements(checkout)
    print(f"Archipelago {tag} ready at {checkout}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
