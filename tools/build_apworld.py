"""Build a release: the world and its client in ``zexion.apworld``, and an archive for each
system with what a player there needs to start.

    .venv/Scripts/python tools/build_apworld.py [--out dist]

It writes three files into ``--out``, the release's assets (``.github/workflows/release.yml``
publishes them as they are):

- ``zexion.apworld``, on its own, for anyone who needs only the world (to generate a game);
- ``zexion-ap-<version>-windows.zip``, for Windows;
- ``zexion-ap-<version>-linux.tar.gz``, for Linux and the Steam Deck.

Each archive unpacks into a folder of its own name without the extension
(``zexion-ap-<version>-windows/``) holding ``zexion.apworld``, the mod for that system
(``helpers``: ``zexion_ap.dll`` and ``version.dll``, or ``libzexion_ap.so``, which the tarball
marks executable, 0755, whatever the file's own mode: the release is built on Windows), the
player guides (GUIDES),
the sample YAMLs and a ``README.md``. The README is made here, never kept by hand: a title naming
the version and the system, a line sending the player to the setup guide, then the setup guide's
own section for that system as it is (PLATFORMS: "Install" for Windows, "Linux and Steam Deck" for
Linux), so it says what the guide says; a guide without that section stops the build. Each
archive is made in memory and read back, and one whose files or modes are not these stops the
build, all before anything is written: a refused build leaves ``--out`` as it was. So does an
``--out`` holding any file but these three (an earlier build's, or another version's), since the
release publishes all it holds: remove them, or build into another folder. Every file in them is
dated by ``SOURCE_DATE_EPOCH`` if set, else the commit's time (``build_time``), and the zip and
gzip headers too, so two builds of one commit are the same bytes.

The apworld is the world package (``worlds/zexion``, without its tests) with the client package
(``client/*.py``, its ``__init__.py`` included) in ``zexion/client/``, so it is the subpackage
``worlds.zexion.client``; the world's launcher entry, Zexion Client, starts it from there
(``worlds/zexion/__init__.py``, ``run_client``). Archipelago's launcher installs the apworld;
the player copies ``zexion_ap.dll`` and ``version.dll`` into Zexion's folder (the setup guide),
or on Linux ``libzexion_ap.so`` with the game's launch options, which only the player does: no
tool here writes there. The player guides are ``zexion-setup-guide.md`` and
``zexion-game-page.md``, copies of ``worlds/zexion/docs/setup_en.md`` and ``en_Zexion.md``. The
sample YAMLs, one per mode, are as they are in ``samples/`` (tools/gen_sample_yamls.py):
``zexion-easy.yaml``, ``zexion-standard.yaml``, ``zexion-ohko.yaml`` and ``zexion-custom.yaml``.

The DLLs are the CMake Release builds (``native/build``), and the Linux helper the Linux
build's (``native/build/libzexion_ap.so``: the release workflow builds it in the Steam Runtime's
SDK and puts it there). The build refuses a missing DLL or Linux helper, a helper of either kind
whose version is not the client's (``client/native.py``, ``VERSION``, which it also refuses to
be without), so they always match, and a build older than its sources (``native/src``,
``native/proxy`` or ``native/linux``, and ``native/CMakeLists.txt``). A Linux helper must also
load in the Steam Runtime the game asks Steam for: it may need no glibc symbol version newer than
the runtime's (GLIBC_MAX, the sniper runtime's 2.31), no glibc version but a numbered one
(``GLIBC_PRIVATE`` and ``GLIBC_ABI_DT_RELR`` are not), and no version of any library but glibc's
(GLIBC_LIBRARIES: ``libc``, ``libm``, ``ld-linux``, and on 2.31 ``libdl``, ``libpthread`` and
``librt``; its C++ runtime is linked in), as ``client/elf.py`` reads them (``.gnu.version_r``).
``--check-linux-helper PATH`` runs those checks, and the version's, on one file alone (CI's
Steam Runtime build). It also refuses a
manifest (``worlds/zexion/archipelago.json``) that does not name the game Zexion or whose
``world_version`` is not ``VERSION`` (a release has one number), two files that would share a
name in the apworld or in an archive, no sample YAML, and a sample that requires another version
of the world than ``VERSION`` (made before the version was raised: run gen_sample_yamls.py again).

The apworld's ``archipelago.json`` is the world's, with the container keys Archipelago's own
builder adds (CONTAINER_KEYS; docs/DEVELOPING.md, "Branches and releases", says why). Reads
this repo and writes only into ``--out``, which may not be inside the game's or the saves'
folder (``tools/protected.py``). Only files git tracks are packed: an untracked file in
``worlds/zexion``, ``client`` or ``samples`` stops the build.
"""

from __future__ import annotations

import argparse
import gzip
import io
import json
import os
import re
import subprocess
import sys
import tarfile
import time
import zipfile
from dataclasses import dataclass
from pathlib import Path

from protected import ROOT, assert_writable
from versions import client_version

sys.path.insert(0, str(ROOT))  # the client package
from client import elf  # noqa: E402

WORLD = ROOT / "worlds" / "zexion"
CLIENT = ROOT / "client"
HELPER = ROOT / "native" / "build" / "Release" / "zexion_ap.dll"
PROXY = ROOT / "native" / "build" / "proxy" / "Release" / "version.dll"
LINUX_HELPER = ROOT / "native" / "build" / "libzexion_ap.so"
# What each build is built from: one older than any of these is stale.
CMAKE = ROOT / "native" / "CMakeLists.txt"
HELPER_SOURCES = (ROOT / "native" / "src", CMAKE)
PROXY_SOURCES = (ROOT / "native" / "proxy", CMAKE)
LINUX_SOURCES = (ROOT / "native" / "src", ROOT / "native" / "linux", CMAKE)
# The newest glibc symbol version the Linux helper may need: the Steam Runtime's, sniper (3.0),
# the runtime the game asks Steam for (native/README.md, "Its own runtime"; a newer host glibc
# may stand in for it, which a build for 2.31 loads on too). And the libraries whose versions it
# may need at all: glibc's (its C++ runtime is linked in), which before 2.34 split dl, pthread
# and rt out of libc.
GLIBC_MAX = (2, 31)
GLIBC_LIBRARIES = {
    "libc.so.6",
    "libm.so.6",
    "ld-linux-x86-64.so.2",
    "libdl.so.2",
    "libpthread.so.0",
    "librt.so.1",
}
SKIP_DIRS = {"__pycache__", "test"}
# The player guides, put into each archive under these names (setup_en.md names the game page's
# file, and en_Zexion.md the setup guide's).
SETUP_GUIDE = WORLD / "docs" / "setup_en.md"
GUIDES = {
    SETUP_GUIDE: "zexion-setup-guide.md",
    WORLD / "docs" / "en_Zexion.md": "zexion-game-page.md",
}
SAMPLES = ROOT / "samples"  # the sample YAMLs, put into each archive as they are


@dataclass(frozen=True)
class Platform:
    """One system's archive: its file's extension, the system as its README names it, and the
    setup guide's section (its ``## `` heading) the README copies."""

    suffix: str
    system: str
    section: str


PLATFORMS = {
    "windows": Platform(".zip", "Windows", "Install"),
    "linux": Platform(".tar.gz", "Linux and the Steam Deck", "Linux and Steam Deck"),
}
FILE_MODE, EXECUTABLE = 0o644, 0o755  # the modes the tarball gives its files, and the .so
README = "README.md"
APWORLD = "zexion.apworld"
README_INTRO = (
    "How to install it, from the setup guide, `zexion-setup-guide.md` in this folder. The guide "
    'has the rest, with each of its sections named here ("the setup guide\'s ..."): what you '
    "need, making your YAML, generating and hosting, playing, updating and troubleshooting. The "
    "game page, `zexion-game-page.md`, has the goals and options."
)
MANIFEST = "zexion/archipelago.json"
# The keys APWorldContainer.get_manifest() gives in AP 0.6.8 besides "game", which the world's
# has, merged as worlds/LauncherComponents.py's Build APWorlds does.
CONTAINER_KEYS = {"version": 7, "compatible_version": 7}


def helper_has_version(dll: Path, version: str) -> bool:
    """Whether the helper names ``version`` in its hello: kVersion, a C string in the DLL."""
    # Not a tail of a longer one: another version, or a symbol version (GLIBC_2.2.5).
    return (
        re.search(rb"(?<![0-9A-Za-z._])" + re.escape(version.encode()) + rb"\x00", dll.read_bytes())
        is not None
    )


def linux_helper_problem(path: Path) -> str | None:
    """Why the Linux helper at ``path`` would not load in the Steam Runtime, or None: a glibc
    symbol version newer than GLIBC_MAX, or a version needed of another library."""
    try:
        needs = elf.parse(path.read_bytes()).version_needs()
    except elf.ELFError as exc:
        return f"it can't be read as an ELF file ({exc})"
    others = sorted(lib for lib in needs if lib not in GLIBC_LIBRARIES)
    if others:
        return (
            f"it needs versions of {', '.join(others)}, which are not glibc's (its C++ runtime "
            "must be linked in: -static-libstdc++ -static-libgcc)"
        )
    newest = (0, 0)
    for versions in needs.values():
        for version in sorted(versions):
            match = re.fullmatch(r"GLIBC_(\d+)\.(\d+)(?:\.\d+)?", version)
            if not match:
                # GLIBC_PRIVATE is not stable across the glibc the runtime may take from the
                # host; GLIBC_ABI_DT_RELR (packed relocations) needs glibc 2.36.
                return f"it needs {version}, which no numbered glibc version covers"
            newest = max(newest, (int(match.group(1)), int(match.group(2))))
    if newest > GLIBC_MAX:
        need, most = ".".join(map(str, newest)), ".".join(map(str, GLIBC_MAX))
        return (
            f"it needs glibc {need}, newer than the Steam Runtime's {most}: build it in the "
            'Steam Runtime\'s SDK (docs/DEVELOPING.md, "Branches and releases")'
        )
    return None


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


def sample_files(version: str) -> list[Path]:
    """The sample YAMLs: tracked, and each for this ``version`` of the world."""
    files = sorted(SAMPLES.glob("*.yaml"))
    if not files:
        raise SystemExit(f"no sample YAMLs in {SAMPLES}: run tools/gen_sample_yamls.py")
    known = tracked(SAMPLES)
    for path in files:
        if path.resolve() not in known:
            raise SystemExit(f"not tracked by git, so not released: {path.relative_to(ROOT)}")
        if f"Zexion: {version} " not in path.read_text(encoding="utf-8"):
            raise SystemExit(
                f"{path.name} does not require Zexion {version}: run tools/gen_sample_yamls.py"
            )
    return files


def build(out: Path) -> list[Path]:
    """The release's assets in ``out``: the apworld, then each platform's archive."""
    info = manifest()
    version = client_version()
    if info.get("world_version") != version:
        raise SystemExit(
            f"worlds/zexion/archipelago.json has world_version {info.get('world_version')!r}, "
            f"and the client is {version}: a release has one number (set both)"
        )
    dlls = {HELPER: HELPER_SOURCES, PROXY: PROXY_SOURCES, LINUX_HELPER: LINUX_SOURCES}
    for dll, sources in dlls.items():
        if not dll.is_file():
            raise SystemExit(f"{dll} is missing: build native/ first (CLAUDE.md)")
        if newest_mtime(sources) > dll.stat().st_mtime:
            listed = " or ".join(str(s) for s in sources)
            raise SystemExit(f"{dll.name} is older than {listed}: rebuild native/")
    if not helper_has_version(HELPER, version):
        raise SystemExit(f"the built helper is not version {version}: rebuild native/")
    problem = check_linux_helper(LINUX_HELPER, version)
    if problem:
        raise SystemExit(f"{LINUX_HELPER.name}: {problem}: rebuild native/")
    files = packed_files()  # the guides among them: tracked
    samples = sample_files(version)
    names = [name for _, name in files] + [MANIFEST]
    if len(names) != len(set(names)):
        raise SystemExit("two files would share a name in the apworld")
    stamp = build_time()
    entries = [(name, path.read_bytes(), FILE_MODE) for path, name in files]
    entries.append((MANIFEST, json.dumps(info, indent=4).encode("utf-8"), FILE_MODE))
    # Everything is made, and each archive read back, before anything is written.
    assets = {APWORLD: zip_bytes(entries, stamp)}
    shared = [(APWORLD, assets[APWORLD], FILE_MODE)]
    shared += [(name, path.read_bytes(), FILE_MODE) for path, name in GUIDES.items()]
    shared += [(path.name, path.read_bytes(), FILE_MODE) for path in samples]
    for platform in PLATFORMS:
        mod = [
            (p.name, p.read_bytes(), EXECUTABLE if p.suffix == ".so" else FILE_MODE)
            for p in helpers(platform)
        ]
        contents = [(README, readme(platform, version).encode("utf-8"), FILE_MODE), *mod, *shared]
        name, data = archive(platform, version, contents, stamp)
        assets[name] = data
    out = assert_writable(out)
    if out.is_dir():
        others = sorted(p.name for p in out.iterdir() if p.name not in assets)
        if others:
            raise SystemExit(
                f"{out} holds files that are not this release's: {', '.join(others)} (remove "
                "them, or build into another --out): the release publishes all it holds"
            )
    out.mkdir(parents=True, exist_ok=True)
    written = []
    for name, data in assets.items():
        path, part = out / name, out / f"{name}.part"
        part.write_bytes(data)
        part.replace(path)
        written.append(path)
    return written


def helpers(platform: str) -> tuple[Path, ...]:
    """The mod for ``platform``: the helper and its proxy on Windows, the Linux helper on Linux."""
    return (HELPER, PROXY) if platform == "windows" else (LINUX_HELPER,)


def archive_folder(platform: str, version: str) -> str:
    """The folder ``platform``'s archive unpacks into, and its file's name without the suffix."""
    return f"zexion-ap-{version}-{platform}"


def archive_name(platform: str, version: str) -> str:
    return archive_folder(platform, version) + PLATFORMS[platform].suffix


def guide_section(heading: str) -> str:
    """The setup guide's ``## heading`` section, its heading included, as it is in the guide."""
    text = SETUP_GUIDE.read_text(encoding="utf-8")
    match = re.search(rf"^## {re.escape(heading)}[ \t]*\n(.*?)(?=^## |\Z)", text, re.M | re.S)
    if not match or not match.group(1).strip():
        raise SystemExit(
            f'{SETUP_GUIDE.name} has no "## {heading}" section, which the archives\' '
            f"{README} copies"
        )
    return f"## {heading}\n\n{match.group(1).strip()}\n"


def readme(platform: str, version: str) -> str:
    """The ``README.md`` in ``platform``'s archive: a title, README_INTRO, the guide's section."""
    system = PLATFORMS[platform].system
    section = guide_section(PLATFORMS[platform].section)
    return f"# Zexion AP {version} for {system}\n\n{README_INTRO}\n\n{section}"


def build_time() -> int:
    """The time every file in the release is stamped with, so that two builds of one commit are
    the same bytes: ``SOURCE_DATE_EPOCH`` if set, else the commit's time."""
    env = os.environ.get("SOURCE_DATE_EPOCH")
    if env:
        try:
            return int(env)
        except ValueError:
            raise SystemExit(f"SOURCE_DATE_EPOCH is not a number of seconds: {env!r}") from None
    try:
        out = subprocess.run(
            ["git", "log", "-1", "--format=%ct"],
            cwd=ROOT,
            capture_output=True,
            check=True,
            text=True,
        ).stdout
        return int(out.strip())
    except (OSError, subprocess.CalledProcessError, ValueError) as exc:
        raise SystemExit(f"cannot read the commit's time ({exc}): build from a checkout") from exc


Entry = tuple[str, bytes, int]  # a file's name in an archive, its bytes and its mode


def zip_bytes(entries: list[Entry], stamp: int) -> bytes:
    """A zip of ``entries``, each dated ``stamp`` and with its Unix mode."""
    # A zip's dates are local times from 1980 on, with no zone: given as UTC here.
    date = max(time.gmtime(stamp)[:6], (1980, 1, 1, 0, 0, 0))
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        for name, data, mode in entries:
            info = zipfile.ZipInfo(name, date_time=date)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3  # Unix, so its mode is read on every system
            info.external_attr = (0o100000 | mode) << 16
            zf.writestr(info, data)
    return buf.getvalue()


def tar_bytes(folder: str, entries: list[Entry], stamp: int) -> bytes:
    """A gzipped tarball of ``folder`` and ``entries`` in it, each dated ``stamp``, owned by
    root (0/0) and with its own mode."""
    buf = io.BytesIO()
    with (
        gzip.GzipFile(filename="", mode="wb", fileobj=buf, mtime=stamp) as gz,
        tarfile.open(fileobj=gz, mode="w", format=tarfile.PAX_FORMAT) as tf,
    ):
        top = tarfile.TarInfo(folder)
        top.type, top.mode, top.mtime = tarfile.DIRTYPE, EXECUTABLE, stamp
        tf.addfile(top)
        for name, data, mode in entries:
            info = tarfile.TarInfo(name)
            info.size, info.mode, info.mtime = len(data), mode, stamp
            tf.addfile(info, io.BytesIO(data))
    return buf.getvalue()


def listing(name: str, data: bytes) -> dict[str, int]:
    """Each file in the archive ``name`` (``data``), with its mode."""
    if name.endswith(".zip"):
        with zipfile.ZipFile(io.BytesIO(data)) as zf:
            return {i.filename: i.external_attr >> 16 & 0o7777 for i in zf.infolist()}
    with tarfile.open(fileobj=io.BytesIO(data)) as tf:
        return {m.name: m.mode for m in tf.getmembers() if m.isfile()}


def archive(platform: str, version: str, contents: list[Entry], stamp: int) -> tuple[str, bytes]:
    """``platform``'s archive, its name and its bytes: ``contents`` in the archive's folder.
    Read back, and refused unless it holds just them, with their modes."""
    folder = archive_folder(platform, version)
    entries = [(f"{folder}/{name}", data, mode) for name, data, mode in contents]
    names = [name for name, _, _ in entries]
    if len(names) != len(set(names)):
        raise SystemExit(f"two files would share a name in the {platform} archive")
    name = archive_name(platform, version)
    data = zip_bytes(entries, stamp) if name.endswith(".zip") else tar_bytes(folder, entries, stamp)
    if listing(name, data) != {entry: mode for entry, _, mode in entries}:
        raise SystemExit(f"{name} does not hold what was put into it")
    return name, data


def check_linux_helper(path: Path, version: str | None = None) -> str | None:
    """Why the Linux helper at ``path`` can't be released as ``version`` (the client's by
    default), or None. ``build`` runs it after its own check of the file's age, which a file
    made elsewhere (CI's artifact, ``--check-linux-helper``) can't be judged by."""
    if not path.is_file():
        return f"{path} is missing"
    version = version or client_version()
    if not helper_has_version(path, version):
        return f"it is not version {version}"
    problem = linux_helper_problem(path)
    return f"it would not load in the Steam Runtime: {problem}" if problem else None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=ROOT / "dist", help="output folder")
    parser.add_argument(
        "--check-linux-helper",
        type=Path,
        metavar="PATH",
        help="only check the Linux helper at PATH, as the build would, and build nothing",
    )
    args = parser.parse_args(argv)
    if args.check_linux_helper is not None:
        problem = check_linux_helper(args.check_linux_helper)
        if problem:
            raise SystemExit(f"{args.check_linux_helper.name}: {problem}")
        print(f"{args.check_linux_helper}: version {client_version()}, loads in the Steam Runtime")
        return 0
    for asset in build(args.out):
        print(asset)
        if asset.name != APWORLD:
            for name, mode in listing(asset.name, asset.read_bytes()).items():
                print(f"  {name} ({mode:o})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
