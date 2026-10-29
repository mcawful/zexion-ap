"""Save experiments, done safely. See .claude/skills/save-lab/SKILL.md for the workflow.

    save_lab.py slots                         list save slots (read-only)
    save_lab.py backup                        copy the whole save folder into lab/backups/<time>/
    save_lab.py copy SLOT [--out FILE]        copy one slot into lab/work/
    save_lab.py diff A B [--merge-gap N] [--max-runs N]   changed byte runs (200 shown by default)
    save_lab.py patch FILE SPEC [--out FILE]  apply a JSON patch to a copy
    save_lab.py install FILE SLOT --yes       put a lab file into an empty slot (after a backup)
    save_lab.py restore BACKUP [--slot N] --yes   put a backup back (after backing up the present)

A save is named by slot number (``2``), ``slot:2``, or a file path. Everything except ``install``
and ``restore`` only reads the save folder. Both refuse while Zexion is running (looked for on
Windows, the game's platform), and when the process list can't be read: the game keeps the slots
in memory and writes them back over what they wrote. ``install`` fills only an empty slot, and
only when a backup from the last 12 hours holds that slot as it is now. ``restore`` may
overwrite slots in use; it refuses a backup of another save folder, checks the backup against
its manifest, and backs up the present state first.

A slot is empty in two ways, and ``slots`` names both. "empty" holds the unused-slot template.
"empty (deleted, data kept)" holds a save deleted in the game: deleting keeps the file and all
its data, and only sets the empty marker (``0x9360``) to ``0xFF`` and the randomizer marker
(``0x9402``) to 0, so the game marks the slot empty. Anything else, including a file of an odd
size or one that can't be read ("IN USE (unreadable)", its hash shown as question marks), is in use.
``install`` fills either kind of empty slot; over a deleted save it says the data it replaced is
in the backup it required, and ``lab/installs.log`` records which kind it filled (``was=``).

``--save-dir`` (or ``ZEXION_SAVE_DIR``) points at another save folder, and ``ZEXION_LAB_DIR`` at
another lab folder; the tests use both.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import shutil
import struct
import sys
import time
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

from protected import ROOT, assert_writable, lab_dir, save_dir

sys.path.insert(0, str(ROOT))  # the client package
from client import process  # noqa: E402

SLOT_NAME = "Save Game {}.sav"
GAME_EXE = "Zexion.exe"  # client/game.py's PROCESS
WINDOWS = sys.platform == "win32"  # the game's platform: only there is it looked for
BACKUP_MAX_AGE_HOURS = 12
# An unused slot is not all zeros: the game writes one fixed template into every unused slot
# (docs/re/save-format.md, "Unused slots hold a fixed template"). The template is recognized
# by the content that page records (``unused_template``), or as the content at least this many
# slots share, which no slot in use would.
EMPTY_TEMPLATE_MIN_COPIES = 3
# Deleting a save in the game keeps the file and its data and sets two bytes: the empty marker
# to 0xFF and the 16-bit randomizer marker to 0 (docs/re/save-format.md, "Clearing a slot in the
# game sets two bytes and keeps the rest"). A slot counts as deleted only with both, as every
# deletion seen left it, in a file of a size a save has been seen to have ("Save sizes": a
# normal game, and a randomizer save with its section) that starts with the save's E8 03.
SAVE_SIZES = (75_794, 78_108)
SAVE_HEADER = b"\xe8\x03"
EMPTY_MARKER = 0x9360
RANDO_MARKER = 0x9402

# What ``slot_state`` says of a slot.
TEMPLATE = "empty"
DELETED = "empty (deleted, data kept)"
IN_USE = "IN USE"
UNREADABLE = "IN USE (unreadable)"


def slot_path(slot: int) -> Path:
    return save_dir() / SLOT_NAME.format(slot)


def resolve(ref: str) -> Path:
    """A slot number, ``slot:N``, or a path."""
    text = ref.removeprefix("slot:")
    if text.isdigit():
        return slot_path(int(text))
    return Path(ref)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _read(path: Path) -> bytes | None:
    try:
        return path.read_bytes()
    except OSError:
        return None


def slot_files() -> list[Path]:
    return sorted(save_dir().glob("Save Game *.sav"), key=lambda p: int(p.stem.split()[-1]))


def empty_template_hash() -> str | None:
    """SHA-256 of the unused-slot template: the content shared by the most slots, if enough."""
    contents = (_read(p) for p in slot_files())
    counts = Counter(hashlib.sha256(data).hexdigest() for data in contents if data is not None)
    if not counts:
        return None
    digest, copies = counts.most_common(1)[0]
    return digest if copies >= EMPTY_TEMPLATE_MIN_COPIES else None


def is_deleted_save(data: bytes) -> bool:
    """A save deleted in the game: a save's size and header, the empty marker, no randomizer."""
    return (
        len(data) in SAVE_SIZES
        and data[: len(SAVE_HEADER)] == SAVE_HEADER
        and data[EMPTY_MARKER] == 0xFF
        and data[RANDO_MARKER : RANDO_MARKER + 2] == b"\x00\x00"
    )


def unused_template() -> bytes:
    """The unused-slot template, built from its description in save-format.md."""
    data = bytearray(SAVE_SIZES[0])
    data[: len(SAVE_HEADER)] = SAVE_HEADER
    data[0x7BAE:0x877F] = bytes([0xFF]) * (0x877F - 0x7BAE)
    data[EMPTY_MARKER] = 0xFF
    data[0x9364:0x93D4] = bytes.fromhex("ffffffff 00000000 3000 0200 0000") * 8
    return bytes(data)


UNUSED_TEMPLATE = unused_template()


def slot_state(path: Path, template: str | None) -> str:
    """``TEMPLATE``, ``DELETED``, ``IN_USE`` or ``UNREADABLE``. Only the first two are empty.

    The known template is checked before the deleted test, which it also passes, so a template
    slot is never called deleted even when too few slots share it for ``empty_template_hash``.
    """
    data = _read(path)
    if data is None:
        return UNREADABLE
    if data == UNUSED_TEMPLATE:
        return TEMPLATE
    if template is not None and hashlib.sha256(data).hexdigest() == template:
        return TEMPLATE
    return DELETED if is_deleted_save(data) else IN_USE


def is_empty_slot(path: Path, template: str | None) -> bool:
    return slot_state(path, template) in (TEMPLATE, DELETED)


def _now() -> str:
    return dt.datetime.now().strftime("%Y%m%d-%H%M%S")


# --- read-only commands ---------------------------------------------------------------


def cmd_slots(_: argparse.Namespace) -> int:
    template = empty_template_hash()
    for path in slot_files():
        state = slot_state(path, template)
        digest = "?" * 12 if state == UNREADABLE else sha256(path)[:12]
        stat = path.stat()
        mtime = dt.datetime.fromtimestamp(stat.st_mtime).strftime("%Y-%m-%d %H:%M")
        print(f"{path.stem:<14} {stat.st_size:>7} bytes  {mtime}  {digest}  {state}")
    return 0


@dataclass
class Run:
    offset: int
    a: bytes
    b: bytes


def diff_bytes(a: bytes, b: bytes, merge_gap: int = 0) -> list[Run]:
    """Runs of differing bytes over the common length. Runs closer than ``merge_gap`` merge."""
    runs: list[Run] = []
    n = min(len(a), len(b))
    i = 0
    while i < n:
        if a[i] == b[i]:
            i += 1
            continue
        start = i
        end = i
        while i < n:
            if a[i] != b[i]:
                end = i
                i += 1
            elif i - end <= merge_gap:
                i += 1
            else:
                break
        runs.append(Run(start, a[start : end + 1], b[start : end + 1]))
    return runs


def readings(chunk: bytes) -> str:
    parts = []
    if len(chunk) == 1:
        parts.append(f"u8={chunk[0]}")
    if len(chunk) == 2:
        parts.append(f"u16={struct.unpack('<H', chunk)[0]}")
    if len(chunk) == 4:
        parts.append(f"u32={struct.unpack('<I', chunk)[0]}")
        parts.append(f"f32={struct.unpack('<f', chunk)[0]:.6g}")
    return " ".join(parts)


def cmd_diff(args: argparse.Namespace) -> int:
    pa, pb = resolve(args.a), resolve(args.b)
    a, b = pa.read_bytes(), pb.read_bytes()
    print(f"A: {pa} ({len(a)} bytes)")
    print(f"B: {pb} ({len(b)} bytes)")
    if len(a) != len(b):
        print(
            f"Sizes differ by {len(b) - len(a):+d} bytes; comparing the first {min(len(a), len(b))}"
        )
    runs = diff_bytes(a, b, args.merge_gap)
    print(f"{len(runs)} changed run(s)")
    for run in runs[: args.max_runs]:
        print(f"  0x{run.offset:05x} len {len(run.a):>4}  A: {run.a[:16].hex(' ')}")
        print(f"  {'':>14}  B: {run.b[:16].hex(' ')}")
        ra, rb = readings(run.a), readings(run.b)
        if ra:
            print(f"  {'':>14}  {ra}  ->  {rb}")
    if len(runs) > args.max_runs:
        print(f"  ... {len(runs) - args.max_runs} more (raise --max-runs)")
    return 0


# --- commands that write only into lab/ -------------------------------------------------


def backup() -> Path:
    """Copy the whole save folder into lab/backups/<time>/ and verify every copy by hash."""
    source = save_dir()
    root = lab_dir() / "backups"
    stamp = _now()
    dest = root / stamp
    suffix = 0
    while dest.exists():  # two backups in one second; "-1" still sorts after the bare stamp
        suffix += 1
        dest = root / f"{stamp}-{suffix}"
    dest.mkdir(parents=True)
    files: dict[str, str] = {}
    for path in sorted(source.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(source).as_posix()
        target = dest / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        digest = sha256(path)
        if sha256(target) != digest:
            raise RuntimeError(f"backup copy of {rel} does not match the original")
        files[rel] = digest
    manifest = {"source": str(source), "created": time.time(), "files": files}
    (dest / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return dest


def cmd_backup(_: argparse.Namespace) -> int:
    dest = backup()
    count = len(json.loads((dest / "manifest.json").read_text(encoding="utf-8"))["files"])
    print(f"Backed up {count} files to {dest}")
    return 0


def cmd_copy(args: argparse.Namespace) -> int:
    src = resolve(args.slot)
    out = (
        Path(args.out)
        if args.out
        else lab_dir() / "work" / f"{src.stem.replace(' ', '_')}-{_now()}.sav"
    )
    assert_writable(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, out)
    print(f"Copied {src} -> {out}")
    return 0


def apply_patch(data: bytes, spec: dict) -> bytes:
    """Spec: {"patches": [{"offset": "0x10", "old": "01 02", "new": "2e 2e"}, ...]}.

    Every ``old`` must match what is there, so a patch written for one save cannot silently
    land on a different one.
    """
    buf = bytearray(data)
    for i, patch in enumerate(spec["patches"]):
        offset = int(str(patch["offset"]), 0)
        old = bytes.fromhex(patch["old"])
        new = bytes.fromhex(patch["new"])
        if len(old) != len(new):
            raise ValueError(f"patch {i}: old and new differ in length")
        if offset < 0 or offset + len(old) > len(buf):
            raise ValueError(f"patch {i}: 0x{offset:x} is outside the file")
        found = bytes(buf[offset : offset + len(old)])
        if found != old:
            raise ValueError(
                f"patch {i} at 0x{offset:x}: expected {old.hex(' ')}, found {found.hex(' ')}"
            )
        buf[offset : offset + len(new)] = new
    return bytes(buf)


def cmd_patch(args: argparse.Namespace) -> int:
    src = resolve(args.file)
    spec = json.loads(Path(args.spec).read_text(encoding="utf-8"))
    out = Path(args.out) if args.out else lab_dir() / "work" / f"{src.stem}-patched-{_now()}.sav"
    assert_writable(out)
    patched = apply_patch(src.read_bytes(), spec)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(patched)
    print(f"Applied {len(spec['patches'])} patch(es): {src} -> {out}")
    return 0


# --- the only two commands that write into the save folder ------------------------------


def latest_backup_covering(path: Path) -> Path | None:
    """The newest backup, if it is recent and holds ``path`` exactly as it is now."""
    root = lab_dir() / "backups"
    if not root.is_dir():
        return None
    backups = sorted((p for p in root.iterdir() if (p / "manifest.json").is_file()), reverse=True)
    if not backups:
        return None
    newest = backups[0]
    manifest = json.loads((newest / "manifest.json").read_text(encoding="utf-8"))
    if time.time() - manifest["created"] > BACKUP_MAX_AGE_HOURS * 3600:
        return None
    rel = path.relative_to(save_dir()).as_posix()
    current = sha256(path) if path.exists() else None
    if manifest["files"].get(rel) != current:
        return None
    if current is not None and sha256(newest / rel) != current:
        return None
    return newest


def _write_into_save_dir(target: Path, data: bytes) -> None:
    tmp = target.with_name(target.name + ".zexion-ap-tmp")
    try:
        tmp.write_bytes(data)
        os.replace(tmp, target)
    except BaseException:
        # Left behind, it would sit in the save folder and ride along in every later backup.
        tmp.unlink(missing_ok=True)
        raise
    if target.read_bytes() != data:
        raise RuntimeError(f"read-back of {target} does not match what was written")


def game_running() -> bool:
    """Whether Zexion is running (looked for on Windows only). ProcessError (an OSError) when
    the process list can't be read."""
    return WINDOWS and process.find_pid(GAME_EXE) is not None


def _refused_while_game_runs(command: str) -> bool:
    """True, with the reason printed, when Zexion is running or that can't be told. The game
    keeps the slots in memory and writes them back over the folder: an install made while it
    ran was replaced by the game's own empty slot (docs/re/memory.md, "Feats and
    achievements", 2026-10-28)."""
    try:
        running = game_running()
    except OSError as exc:
        print(f"Could not tell whether Zexion is running ({exc}); {command} did not write.")
        return True
    if running:
        print(
            f"Zexion is running; close it first. It keeps the slots in memory and writes them "
            f"back, over what {command} would write."
        )
    return running


def _log(line: str) -> None:
    log = lab_dir() / "installs.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("a", encoding="utf-8") as f:
        f.write(f"{dt.datetime.now().isoformat(timespec='seconds')} {line}\n")


def cmd_install(args: argparse.Namespace) -> int:
    if not args.yes:
        print("install writes into the real save folder; pass --yes once the user has agreed")
        return 1
    if _refused_while_game_runs("install"):
        return 1
    source = resolve(args.file)
    target = slot_path(args.slot)
    if not target.exists():
        print(f"{target.name} does not exist; install only fills an existing empty slot.")
        return 1
    state = slot_state(target, empty_template_hash())
    if state not in (TEMPLATE, DELETED):
        print(f"{target.name} is in use. install only fills empty slots; pick another slot.")
        return 1
    found = latest_backup_covering(target)
    if found is None:
        print(f"No backup from the last {BACKUP_MAX_AGE_HOURS} h holds {target.name} as it is now.")
        print("Run `save_lab.py backup` first.")
        return 1
    data = source.read_bytes()
    _write_into_save_dir(target, data)
    digest = hashlib.sha256(data).hexdigest()
    was = "deleted" if state == DELETED else "template"
    _log(f"install {source} -> {target.name} sha256={digest} backup={found.name} was={was}")
    print(f"Installed {source} into {target.name}. Backup: {found}")
    if state == DELETED:
        print(
            f"{target.name} held a save deleted in the game, whose data the game had kept; "
            f"that data is now only in the backup ({found / target.name})."
        )
    return 0


def _same_folder(a: Path, b: Path) -> bool:
    return os.path.normcase(a.resolve()) == os.path.normcase(b.resolve())


def cmd_restore(args: argparse.Namespace) -> int:
    if not args.yes:
        print("restore writes into the real save folder; pass --yes once the user has agreed")
        return 1
    if _refused_while_game_runs("restore"):
        return 1
    chosen = Path(args.backup)
    if not chosen.is_absolute() and not chosen.exists():
        chosen = lab_dir() / "backups" / args.backup
    manifest = json.loads((chosen / "manifest.json").read_text(encoding="utf-8"))
    if not _same_folder(Path(manifest["source"]), save_dir()):
        print(f"{chosen} is a backup of {manifest['source']}, not of {save_dir()}; not restoring.")
        print("Pass --save-dir (or ZEXION_SAVE_DIR) for the folder it came from.")
        return 1
    files = manifest["files"]
    if args.slot is not None:
        name = SLOT_NAME.format(args.slot)
        files = {name: files[name]}
    for rel, digest in files.items():
        if sha256(chosen / rel) != digest:
            raise RuntimeError(f"{chosen / rel} does not match its manifest; not restoring")
    safety = backup()
    print(f"Backed up the current state first: {safety}")
    for rel in files:
        target = save_dir() / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        _write_into_save_dir(target, (chosen / rel).read_bytes())
        _log(f"restore {chosen.name}/{rel} (safety backup {safety.name})")
    print(f"Restored {len(files)} file(s) from {chosen}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--save-dir", help="override the save folder (same as ZEXION_SAVE_DIR)")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("slots").set_defaults(func=cmd_slots)
    sub.add_parser("backup").set_defaults(func=cmd_backup)

    p = sub.add_parser("copy")
    p.add_argument("slot")
    p.add_argument("--out")
    p.set_defaults(func=cmd_copy)

    p = sub.add_parser("diff")
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--merge-gap", type=int, default=0, help="merge runs this many bytes apart")
    p.add_argument("--max-runs", type=int, default=200)
    p.set_defaults(func=cmd_diff)

    p = sub.add_parser("patch")
    p.add_argument("file")
    p.add_argument("spec")
    p.add_argument("--out")
    p.set_defaults(func=cmd_patch)

    p = sub.add_parser("install")
    p.add_argument("file")
    p.add_argument("slot", type=int)
    p.add_argument("--yes", action="store_true")
    p.set_defaults(func=cmd_install)

    p = sub.add_parser("restore")
    p.add_argument("backup")
    p.add_argument("--slot", type=int)
    p.add_argument("--yes", action="store_true")
    p.set_defaults(func=cmd_restore)

    args = parser.parse_args(argv)
    if args.save_dir:
        os.environ["ZEXION_SAVE_DIR"] = args.save_dir
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
