"""Save experiments, done safely. See .claude/skills/save-lab/SKILL.md for the workflow.

    save_lab.py slots                         list save slots (read-only)
    save_lab.py backup                        copy the whole save folder into lab/backups/<time>/
    save_lab.py copy SLOT [--out FILE]        copy one slot into lab/work/
    save_lab.py diff A B [--merge-gap N] [--max-runs N]   changed byte runs (200 shown by default)
    save_lab.py patch FILE SPEC [--out FILE]  apply a JSON patch to a copy
    save_lab.py install FILE SLOT --yes       put a lab file into an EMPTY slot (after a backup)
    save_lab.py restore BACKUP [--slot N] --yes   put a backup back (after backing up the present)

A save is named by slot number (``2``), ``slot:2``, or a file path. Everything except ``install``
and ``restore`` only reads the save folder. ``install`` fills only an empty slot, and only when a
backup from the last 12 hours holds that slot as it is now. ``restore`` may overwrite slots in
use; it checks the backup against its manifest and backs up the present state first.

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

from protected import assert_writable, lab_dir, save_dir

SLOT_NAME = "Save Game {}.sav"
BACKUP_MAX_AGE_HOURS = 12
# An unused slot is not all zeros: the game writes one fixed template into every unused slot
# (docs/re/save-format.md, "Unused slots hold a fixed template"). The template is recognized
# as the content at least this many slots share, which no slot in use would.
EMPTY_TEMPLATE_MIN_COPIES = 3


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


def slot_files() -> list[Path]:
    return sorted(save_dir().glob("Save Game *.sav"), key=lambda p: int(p.stem.split()[-1]))


def empty_template_hash() -> str | None:
    """SHA-256 of the unused-slot template: the content shared by the most slots, if enough."""
    counts = Counter(sha256(p) for p in slot_files())
    if not counts:
        return None
    digest, copies = counts.most_common(1)[0]
    return digest if copies >= EMPTY_TEMPLATE_MIN_COPIES else None


def is_empty_slot(path: Path, template: str | None) -> bool:
    return path.exists() and template is not None and sha256(path) == template


def _now() -> str:
    return dt.datetime.now().strftime("%Y%m%d-%H%M%S")


# --- read-only commands ---------------------------------------------------------------


def cmd_slots(_: argparse.Namespace) -> int:
    template = empty_template_hash()
    for path in slot_files():
        data = path.read_bytes()
        state = "empty" if is_empty_slot(path, template) else "IN USE"
        mtime = dt.datetime.fromtimestamp(path.stat().st_mtime).strftime("%Y-%m-%d %H:%M")
        digest = hashlib.sha256(data).hexdigest()[:12]
        print(f"{path.stem:<14} {len(data):>7} bytes  {mtime}  {digest}  {state}")
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
    tmp.write_bytes(data)
    os.replace(tmp, target)
    if target.read_bytes() != data:
        raise RuntimeError(f"read-back of {target} does not match what was written")


def _log(line: str) -> None:
    log = lab_dir() / "installs.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("a", encoding="utf-8") as f:
        f.write(f"{dt.datetime.now().isoformat(timespec='seconds')} {line}\n")


def cmd_install(args: argparse.Namespace) -> int:
    if not args.yes:
        print("install writes into the real save folder; pass --yes once the user has agreed")
        return 1
    source = resolve(args.file)
    target = slot_path(args.slot)
    if not target.exists():
        print(f"{target.name} does not exist; install only fills an existing empty slot.")
        return 1
    if not is_empty_slot(target, empty_template_hash()):
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
    _log(f"install {source} -> {target.name} sha256={digest} backup={found.name}")
    print(f"Installed {source} into {target.name}. Backup: {found}")
    return 0


def cmd_restore(args: argparse.Namespace) -> int:
    if not args.yes:
        print("restore writes into the real save folder; pass --yes once the user has agreed")
        return 1
    chosen = Path(args.backup)
    if not chosen.is_absolute() and not chosen.exists():
        chosen = lab_dir() / "backups" / args.backup
    manifest = json.loads((chosen / "manifest.json").read_text(encoding="utf-8"))
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
