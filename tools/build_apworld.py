"""Build a release: the world and its client in ``zexion.apworld``, beside the two helper DLLs.

    .venv/Scripts/python tools/build_apworld.py [--out dist]

The apworld is the world package (``worlds/zexion``, without its tests) with the client package
(``client/*.py``, its ``__init__.py`` included) in ``zexion/client/``, so it is the subpackage
``worlds.zexion.client``; the world's launcher entry, Zexion Client, starts it from there
(``worlds/zexion/__init__.py``, ``run_client``). Archipelago's launcher installs the apworld;
the player copies ``zexion_ap.dll`` and ``version.dll`` into Zexion's folder (the setup guide),
which only the player does: no tool here writes there.

The DLLs are the CMake Release builds (``native/build``). The build refuses a helper whose
version is not the client's (``client/native.py``, ``VERSION``), so the three always match,
and a DLL older than its sources (``native/src`` or ``native/proxy``, and
``native/CMakeLists.txt``).

The apworld's ``archipelago.json`` is the world's, with the container keys Archipelago's own
builder adds (``APWorldContainer.get_manifest()`` in AP 0.6.8's ``worlds/Files.py``, merged as
``worlds/LauncherComponents.py``'s Build APWorlds does): without ``version`` and
``compatible_version``, AP 0.6.8 logs the manifest as invalid and ignores ``world_version``
and ``minimum_ap_version``. Reads this repo and writes only into ``--out``, which may not be
inside the game's or the saves' folder (``tools/protected.py``). Only files git tracks are
packed: an untracked file in ``worlds/zexion`` or ``client`` stops the build.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

from protected import assert_writable

ROOT = Path(__file__).resolve().parent.parent
WORLD = ROOT / "worlds" / "zexion"
CLIENT = ROOT / "client"
HELPER = ROOT / "native" / "build" / "Release" / "zexion_ap.dll"
PROXY = ROOT / "native" / "build" / "proxy" / "Release" / "version.dll"
# What each DLL is built from: a DLL older than any of these is stale.
CMAKE = ROOT / "native" / "CMakeLists.txt"
HELPER_SOURCES = (ROOT / "native" / "src", CMAKE)
PROXY_SOURCES = (ROOT / "native" / "proxy", CMAKE)
SKIP_DIRS = {"__pycache__", "test"}
MANIFEST = "zexion/archipelago.json"
# The keys APWorldContainer.get_manifest() gives in AP 0.6.8 (worlds/Files.py: container_version
# 7, and an APWorldContainer's compatible_version 7) besides "game", which the world's has.
CONTAINER_KEYS = {"version": 7, "compatible_version": 7}


def client_version() -> str:
    text = (CLIENT / "native.py").read_text(encoding="utf-8")
    match = re.search(r'^VERSION = "([0-9.]+)"', text, re.M)
    if not match:
        raise SystemExit("client/native.py has no VERSION")
    return match.group(1)


def helper_has_version(dll: Path, version: str) -> bool:
    """Whether the helper names ``version`` in its hello: kVersion, a C string in the DLL."""
    return (
        re.search(rb"(?<![0-9.])" + re.escape(version.encode()) + rb"\x00", dll.read_bytes())
        is not None
    )


def newest_mtime(sources: tuple[Path, ...]) -> float:
    files = [p for s in sources for p in ([s] if s.is_file() else s.rglob("*")) if p.is_file()]
    return max(p.stat().st_mtime for p in files)


def manifest() -> dict:
    """The apworld's archipelago.json: the world's, with Archipelago's container keys."""
    out = json.loads((WORLD / "archipelago.json").read_text(encoding="utf-8"))
    if out.get("game") != "Zexion":
        raise SystemExit("worlds/zexion/archipelago.json does not name the game Zexion")
    return {**out, **CONTAINER_KEYS}


def tracked(folder: Path) -> set[Path]:
    """The files git tracks under ``folder`` (the build packs only those)."""
    try:
        listed = subprocess.run(
            ["git", "ls-files", "-z", "--", str(folder)],
            cwd=ROOT,
            capture_output=True,
            check=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(
            f"cannot list the files git tracks ({exc}): build from a checkout"
        ) from exc
    return {(ROOT / name).resolve() for name in listed.decode("utf-8").split("\0") if name}


def only_tracked(files: list[tuple[Path, str]], folder: Path) -> list[tuple[Path, str]]:
    known = tracked(folder)
    untracked = [path for path, _ in files if path.resolve() not in known]
    if untracked:
        names = ", ".join(str(p.relative_to(ROOT)) for p in untracked)
        raise SystemExit(f"not tracked by git, so not packed: {names} (commit or remove it)")
    return files


def world_files() -> list[tuple[Path, str]]:
    out = []
    for path in sorted(WORLD.rglob("*")):
        rel = path.relative_to(WORLD)
        if path.is_dir() or any(part in SKIP_DIRS for part in rel.parts):
            continue
        if path.suffix in (".pyc", ".pyo") or rel.as_posix() == "archipelago.json":
            continue  # the manifest is written with its container keys (manifest)
        out.append((path, f"zexion/{rel.as_posix()}"))
    return only_tracked(out, WORLD)


def client_files() -> list[tuple[Path, str]]:
    files = [(path, f"zexion/client/{path.name}") for path in sorted(CLIENT.glob("*.py"))]
    return only_tracked(files, CLIENT)


def build(out: Path) -> Path:
    info = manifest()
    version = client_version()
    for dll in (HELPER, PROXY):
        if not dll.is_file():
            raise SystemExit(f"{dll} is missing: build native/ first (CLAUDE.md)")
    if not helper_has_version(HELPER, version):
        raise SystemExit(f"the built helper is not version {version}: rebuild native/")
    for dll, sources in ((HELPER, HELPER_SOURCES), (PROXY, PROXY_SOURCES)):
        if newest_mtime(sources) > dll.stat().st_mtime:
            listed = " or ".join(str(s) for s in sources)
            raise SystemExit(f"{dll.name} is older than {listed}: rebuild native/")
    files = world_files() + client_files()
    names = [name for _, name in files] + [MANIFEST]
    if len(names) != len(set(names)):
        raise SystemExit("two files would share a name in the apworld")
    out = assert_writable(out)
    out.mkdir(parents=True, exist_ok=True)
    apworld = out / "zexion.apworld"
    with zipfile.ZipFile(apworld, "w", zipfile.ZIP_DEFLATED) as zf:
        for path, name in files:
            zf.write(path, name)
        zf.writestr(MANIFEST, json.dumps(info, indent=4))
    for dll in (HELPER, PROXY):
        shutil.copy2(dll, out / dll.name)
    return apworld


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=ROOT / "dist", help="output folder")
    args = parser.parse_args(argv)
    apworld = build(args.out)
    print(f"{apworld} (client and helper {client_version()}), with zexion_ap.dll and version.dll")
    return 0


if __name__ == "__main__":
    sys.exit(main())
