"""Prepare a save slot for an Archipelago seed: the client's only write to the save folder.

The player starts a new randomizer game in the seed's mode in a slot, saves at once, and
closes the game. ``prepare`` then:

1. refuses while Zexion is running (the game keeps every slot in memory and would not see
   the change, or would save over it);
2. checks the slot holds a fresh game of the seed's mode (client/save_patch.py);
3. copies the slot file to the backup folder and verifies the copy by SHA-256;
4. writes the patched save through a temporary file in the save folder, replacing the slot
   file in one step, and reads it back.

A slot that already holds this seed is left as it is. Steam Cloud may upload the patched file
as the game's newest save; that is the point, but pausing it first keeps a cloud copy of the
fresh game to fall back on.
"""

from __future__ import annotations

import datetime as dt
import hashlib
import os
import shutil
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

import save_patch

SLOT_NAME = "Save Game {}.sav"
SLOTS = range(1, 31)
TMP_SUFFIX = ".zexion-ap-tmp"


class PrepareError(RuntimeError):
    """The slot was not prepared; nothing in the save folder changed."""


def save_dir() -> Path:
    """The game's save folder; ``ZEXION_SAVE_DIR`` overrides it, as for the tools."""
    override = os.environ.get("ZEXION_SAVE_DIR")
    if override:
        return Path(override)
    appdata = os.environ.get("APPDATA")
    if not appdata:
        raise PrepareError("APPDATA is not set; set ZEXION_SAVE_DIR to the save folder")
    return Path(appdata) / "Gallant Leaf" / "Zexion"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


@dataclass(frozen=True)
class Prepared:
    slot: int
    backup: Path | None  # None when the slot already held the seed
    changed: bool


def prepare(
    slot: int,
    plan: save_patch.SeedPlan,
    backup_root: Path,
    game_running: Callable[[], bool],
    folder: Path | None = None,
) -> Prepared:
    if slot not in SLOTS:
        raise PrepareError(f"slot {slot} does not exist; slots are 1 to 30")
    if game_running():
        raise PrepareError("close Zexion first: it keeps every slot in memory")
    folder = save_dir() if folder is None else folder
    target = folder / SLOT_NAME.format(slot)
    if not target.is_file():
        raise PrepareError(f"{target.name} was not found in {folder}")
    original = target.read_bytes()
    try:
        patched = save_patch.patch(original, plan)
    except save_patch.PatchError as exc:
        raise PrepareError(f"slot {slot}: {exc}") from exc
    if patched == original:
        return Prepared(slot, None, False)

    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    backup_dir = backup_root / stamp
    suffix = 0
    while backup_dir.exists():
        suffix += 1
        backup_dir = backup_root / f"{stamp}-{suffix}"
    backup_dir.mkdir(parents=True)
    backup = backup_dir / target.name
    shutil.copy2(target, backup)
    if sha256(backup.read_bytes()) != sha256(original):
        raise PrepareError(f"the backup of {target.name} does not match it; nothing written")

    if target.read_bytes() != original:
        raise PrepareError(f"{target.name} changed while it was being prepared; nothing written")
    tmp = target.with_name(target.name + TMP_SUFFIX)
    try:
        tmp.write_bytes(patched)
        if game_running():  # started since the first check
            raise PrepareError("Zexion was started; close it and run /prepare again")
        os.replace(tmp, target)
    finally:
        tmp.unlink(missing_ok=True)
    if target.read_bytes() != patched:
        raise PrepareError(f"{target.name} does not read back as written; restore it from {backup}")
    return Prepared(slot, backup, True)
