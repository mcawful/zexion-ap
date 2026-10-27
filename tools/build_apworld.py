"""Build a release: the world and its client in ``zexion.apworld``, beside the two helper DLLs
and the player guides.

    .venv/Scripts/python tools/build_apworld.py [--out dist]

The apworld is the world package (``worlds/zexion``, without its tests) with the client package
(``client/*.py``, its ``__init__.py`` included) in ``zexion/client/``, so it is the subpackage
``worlds.zexion.client``; the world's launcher entry, Zexion Client, starts it from there
(``worlds/zexion/__init__.py``, ``run_client``). Archipelago's launcher installs the apworld;
the player copies ``zexion_ap.dll`` and ``version.dll`` into Zexion's folder (the setup guide),
which only the player does: no tool here writes there. The player guides go beside them as
plain files (GUIDES), so a player has them with the download: ``zexion-setup-guide.md`` and
``zexion-game-page.md``, copies of ``worlds/zexion/docs/setup_en.md`` and ``en_Zexion.md``.

The DLLs are the CMake Release builds (``native/build``). The build refuses a missing DLL, a
helper whose version is not the client's (``client/native.py``, ``VERSION``, which it also
refuses to be without), so the three always match, and a DLL older than its sources
(``native/src`` or ``native/proxy``, and ``native/CMakeLists.txt``). It also refuses a
manifest (``worlds/zexion/archipelago.json``) that does not name the game Zexion or whose
``world_version`` is not ``VERSION`` (a release has one number), and two files that would
share a name in the apworld.

The apworld's ``archipelago.json`` is the world's, with the container keys Archipelago's own
builder adds (CONTAINER_KEYS): without them, AP 0.6.8 logs the manifest as invalid and ignores
``world_version`` and ``minimum_ap_version``. Reads this repo and writes only into ``--out``,
which may not be inside the game's or the saves' folder (``tools/protected.py``). Only files
git tracks are packed: an untracked file in ``worlds/zexion`` or ``client`` stops the build.
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

from protected import ROOT, assert_writable

WORLD = ROOT / "worlds" / "zexion"
CLIENT = ROOT / "client"
HELPER = ROOT / "native" / "build" / "Release" / "zexion_ap.dll"
PROXY = ROOT / "native" / "build" / "proxy" / "Release" / "version.dll"
# What each DLL is built from: a DLL older than any of these is stale.
CMAKE = ROOT / "native" / "CMakeLists.txt"
HELPER_SOURCES = (ROOT / "native" / "src", CMAKE)
PROXY_SOURCES = (ROOT / "native" / "proxy", CMAKE)
SKIP_DIRS = {"__pycache__", "test"}
# The player guides, copied beside the apworld under these names (setup_en.md names the game
# page's file, and en_Zexion.md the setup guide's).
GUIDES = {
    WORLD / "docs" / "setup_en.md": "zexion-setup-guide.md",
    WORLD / "docs" / "en_Zexion.md": "zexion-game-page.md",
}
MANIFEST = "zexion/archipelago.json"
# The keys APWorldContainer.get_manifest() gives in AP 0.6.8 (worlds/Files.py: container_version
# 7, and an APWorldContainer's compatible_version 7) besides "game", which the world's has,
# merged as worlds/LauncherComponents.py's Build APWorlds does.
CONTAINER_KEYS = {"version": 7, "compatible_version": 7}


def version_in(text: str, where: str) -> str:
    """The ``VERSION`` that ``text``, client/native.py here or at a commit (``where``), sets."""
    match = re.search(r'^VERSION = "([0-9.]+)"', text, re.M)
    if not match:
        raise SystemExit(f"{where} has no VERSION")
    return match.group(1)


def client_version() -> str:
    return version_in((CLIENT / "native.py").read_text(encoding="utf-8"), "client/native.py")


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


def tracked(*folders: Path) -> set[Path]:
    """The files git tracks under ``folders`` (the build packs only those)."""
    try:
        listed = subprocess.run(
            ["git", "ls-files", "-z", "--", *map(str, folders)],
            cwd=ROOT,
            capture_output=True,
            check=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(
            f"cannot list the files git tracks ({exc}): build from a checkout"
        ) from exc
    return {(ROOT / name).resolve() for name in listed.decode("utf-8").split("\0") if name}


def packed_files() -> list[tuple[Path, str]]:
    """The world's files and the client's, each with its name in the apworld; all tracked."""
    files = []
    for path in sorted(WORLD.rglob("*")):
        rel = path.relative_to(WORLD)
        if path.is_dir() or any(part in SKIP_DIRS for part in rel.parts):
            continue
        if path.suffix in (".pyc", ".pyo") or rel.as_posix() == "archipelago.json":
            continue  # the manifest is written with its container keys (manifest)
        files.append((path, f"zexion/{rel.as_posix()}"))
    files += [(path, f"zexion/client/{path.name}") for path in sorted(CLIENT.glob("*.py"))]
    known = tracked(WORLD, CLIENT)
    untracked = [path for path, _ in files if path.resolve() not in known]
    if untracked:
        names = ", ".join(str(p.relative_to(ROOT)) for p in untracked)
        raise SystemExit(f"not tracked by git, so not packed: {names} (commit or remove it)")
    return files


def build(out: Path) -> Path:
    info = manifest()
    version = client_version()
    if info.get("world_version") != version:
        raise SystemExit(
            f"worlds/zexion/archipelago.json has world_version {info.get('world_version')!r}, "
            f"and the client is {version}: a release has one number (set both)"
        )
    dlls = {HELPER: HELPER_SOURCES, PROXY: PROXY_SOURCES}
    for dll, sources in dlls.items():
        if not dll.is_file():
            raise SystemExit(f"{dll} is missing: build native/ first (CLAUDE.md)")
        if newest_mtime(sources) > dll.stat().st_mtime:
            listed = " or ".join(str(s) for s in sources)
            raise SystemExit(f"{dll.name} is older than {listed}: rebuild native/")
    if not helper_has_version(HELPER, version):
        raise SystemExit(f"the built helper is not version {version}: rebuild native/")
    files = packed_files()  # the guides among them: tracked
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
    for dll in dlls:
        shutil.copy2(dll, out / dll.name)
    for guide, name in GUIDES.items():
        shutil.copyfile(guide, out / name)
    return apworld


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=ROOT / "dist", help="output folder")
    args = parser.parse_args(argv)
    apworld = build(args.out)
    print(f"{apworld} ({client_version()}), with zexion_ap.dll, version.dll and the player guides")
    return 0


if __name__ == "__main__":
    sys.exit(main())
