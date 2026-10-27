"""save_lab on a synthetic save folder. Nothing here touches the real saves."""

from __future__ import annotations

import json
import os
import shutil
from pathlib import Path

import pytest

import save_lab
from protected import ProtectedPathError, append_log

SIZE = 4096


def template() -> bytes:
    # Shaped like the real unused-slot template: header, zeros, an FF block.
    return b"\xe8\x03" + bytes(2000) + b"\xff" * 1000 + bytes(SIZE - 3002)


@pytest.fixture
def saves(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    folder = tmp_path / "saves"
    folder.mkdir()
    for slot in range(1, 7):
        (folder / f"Save Game {slot}.sav").write_bytes(template())
    progress = bytearray(template())
    progress[10:14] = b"\x01\x02\x03\x04"
    (folder / "Save Game 1.sav").write_bytes(bytes(progress))
    (folder / "GameUnlocks.sav").write_bytes(b"unlocks")
    monkeypatch.setenv("ZEXION_SAVE_DIR", str(folder))
    monkeypatch.setenv("ZEXION_LAB_DIR", str(tmp_path / "lab"))
    return folder


def test_empty_template_is_the_shared_content(saves: Path) -> None:
    template_hash = save_lab.empty_template_hash()
    assert save_lab.is_empty_slot(saves / "Save Game 2.sav", template_hash)
    assert not save_lab.is_empty_slot(saves / "Save Game 1.sav", template_hash)


def test_no_template_when_too_few_slots_agree(saves: Path) -> None:
    for slot in range(2, 6):
        (saves / f"Save Game {slot}.sav").write_bytes(bytes([slot]) * SIZE)
    assert save_lab.empty_template_hash() is None  # only slot 6 is left with the template


def test_diff_finds_runs_and_merges_gaps() -> None:
    a = bytes(32)
    b = bytearray(a)
    b[4] = 1
    b[6] = 2
    b[20:24] = b"\xff\xff\xff\xff"
    runs = save_lab.diff_bytes(a, bytes(b))
    assert [(r.offset, len(r.a)) for r in runs] == [(4, 1), (6, 1), (20, 4)]
    merged = save_lab.diff_bytes(a, bytes(b), merge_gap=2)
    assert [(r.offset, len(r.a)) for r in merged] == [(4, 3), (20, 4)]


def test_patch_checks_old_bytes() -> None:
    data = bytes(16)
    spec = {"patches": [{"offset": "0x4", "old": "00 00", "new": "2e 2e"}]}
    assert save_lab.apply_patch(data, spec)[4:6] == b"\x2e\x2e"
    wrong = {"patches": [{"offset": "0x4", "old": "01 00", "new": "2e 2e"}]}
    with pytest.raises(ValueError, match="expected 01 00"):
        save_lab.apply_patch(data, wrong)
    with pytest.raises(ValueError, match="outside"):
        save_lab.apply_patch(data, {"patches": [{"offset": 15, "old": "00 00", "new": "01 01"}]})


def test_backup_copies_everything_with_hashes(saves: Path) -> None:
    dest = save_lab.backup()
    manifest = json.loads((dest / "manifest.json").read_text(encoding="utf-8"))
    assert set(manifest["files"]) == {p.name for p in saves.iterdir()}
    assert (dest / "Save Game 1.sav").read_bytes() == (saves / "Save Game 1.sav").read_bytes()


def test_install_requires_yes_backup_and_empty_slot(saves: Path, tmp_path: Path) -> None:
    patched = tmp_path / "patched.sav"
    patched.write_bytes(b"\xe8\x03" + b"\x2e" * (SIZE - 2))

    assert save_lab.main(["install", str(patched), "3"]) == 1  # no --yes
    assert save_lab.main(["install", str(patched), "3", "--yes"]) == 1  # no backup yet
    save_lab.backup()
    assert save_lab.main(["install", str(patched), "1", "--yes"]) == 1  # slot 1 is in use
    assert save_lab.main(["install", str(patched), "30", "--yes"]) == 1  # no such slot
    assert save_lab.main(["install", str(patched), "3", "--yes"]) == 0
    assert (saves / "Save Game 3.sav").read_bytes() == patched.read_bytes()
    # Slot 3 now holds the installed save, so a second install there is refused as in use.
    assert save_lab.main(["install", str(patched), "3", "--yes"]) == 1


def _patched(tmp_path: Path) -> Path:
    path = tmp_path / "patched.sav"
    path.write_bytes(b"\xe8\x03" + b"\x2e" * (SIZE - 2))
    return path


def _edit_manifest(backup: Path, change) -> None:
    path = backup / "manifest.json"
    manifest = json.loads(path.read_text(encoding="utf-8"))
    change(manifest)
    path.write_text(json.dumps(manifest), encoding="utf-8")


def _backups(tmp_path: Path) -> list[Path]:
    return sorted((tmp_path / "lab" / "backups").iterdir())


def test_install_refuses_a_backup_older_than_12_hours(saves: Path, tmp_path: Path) -> None:
    patched = _patched(tmp_path)
    old = save_lab.backup()
    age = save_lab.BACKUP_MAX_AGE_HOURS * 3600 + 60

    def make_old(manifest: dict) -> None:
        manifest["created"] -= age

    _edit_manifest(old, make_old)
    assert save_lab.main(["install", str(patched), "3", "--yes"]) == 1
    assert (saves / "Save Game 3.sav").read_bytes() == template()
    save_lab.backup()  # a fresh one is enough
    assert save_lab.main(["install", str(patched), "3", "--yes"]) == 0


def test_install_refuses_a_slot_changed_since_the_backup(saves: Path, tmp_path: Path) -> None:
    patched = _patched(tmp_path)
    (saves / "Save Game 4.sav").write_bytes(b"x" * SIZE)
    save_lab.backup()
    (saves / "Save Game 4.sav").write_bytes(template())  # emptied after the backup
    assert save_lab.main(["install", str(patched), "4", "--yes"]) == 1
    assert (saves / "Save Game 4.sav").read_bytes() == template()


def test_install_refuses_a_manifest_without_the_slot_as_it_is(saves: Path, tmp_path: Path) -> None:
    patched = _patched(tmp_path)
    backup = save_lab.backup()

    def misstate_slot_3(manifest: dict) -> None:
        manifest["files"]["Save Game 3.sav"] = "0" * 64

    _edit_manifest(backup, misstate_slot_3)  # the copy itself still matches the slot
    assert save_lab.main(["install", str(patched), "3", "--yes"]) == 1
    assert (saves / "Save Game 3.sav").read_bytes() == template()


def test_install_refuses_a_backup_copy_unlike_its_manifest(saves: Path, tmp_path: Path) -> None:
    patched = _patched(tmp_path)
    backup = save_lab.backup()
    (backup / "Save Game 3.sav").write_bytes(b"y" * SIZE)  # the manifest still holds the slot
    assert save_lab.main(["install", str(patched), "3", "--yes"]) == 1
    assert (saves / "Save Game 3.sav").read_bytes() == template()


def test_a_save_write_that_reads_back_wrong_fails(
    saves: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    patched = _patched(tmp_path)
    save_lab.backup()
    replace = os.replace

    def replace_then_spoil(src, dst) -> None:
        replace(src, dst)
        Path(dst).write_bytes(b"spoiled")

    monkeypatch.setattr(save_lab.os, "replace", replace_then_spoil)
    with pytest.raises(RuntimeError, match="read-back"):
        save_lab.main(["install", str(patched), "3", "--yes"])
    assert not (tmp_path / "lab" / "installs.log").exists()  # not logged as installed


def test_a_failed_replace_leaves_no_temporary_file(
    saves: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    patched = _patched(tmp_path)
    save_lab.backup()

    def held_open(src, dst) -> None:
        raise PermissionError(f"{dst} is in use")

    monkeypatch.setattr(save_lab.os, "replace", held_open)
    with pytest.raises(PermissionError):
        save_lab.main(["install", str(patched), "3", "--yes"])
    assert (saves / "Save Game 3.sav").read_bytes() == template()
    assert list(saves.glob("*.zexion-ap-tmp")) == []


def test_restore_puts_a_slot_back_and_backs_up_first(saves: Path, tmp_path: Path) -> None:
    original = (saves / "Save Game 3.sav").read_bytes()
    first = save_lab.backup()
    (tmp_path / "p.sav").write_bytes(b"x" * SIZE)
    assert save_lab.main(["install", str(tmp_path / "p.sav"), "3", "--yes"]) == 0

    assert save_lab.main(["restore", str(first), "--slot", "3", "--yes"]) == 0
    assert (saves / "Save Game 3.sav").read_bytes() == original
    backups = list((tmp_path / "lab" / "backups").iterdir())
    assert len(backups) == 2  # the original, plus the safety copy taken before restoring


def test_restore_refuses_a_backup_unlike_its_manifest(saves: Path, tmp_path: Path) -> None:
    backup = save_lab.backup()
    (backup / "Save Game 3.sav").write_bytes(b"y" * SIZE)  # corrupted after it was taken
    (saves / "Save Game 3.sav").write_bytes(b"x" * SIZE)
    with pytest.raises(RuntimeError, match="does not match its manifest"):
        save_lab.main(["restore", str(backup), "--slot", "3", "--yes"])
    assert (saves / "Save Game 3.sav").read_bytes() == b"x" * SIZE
    assert _backups(tmp_path) == [backup]  # refused before the safety backup


def test_restore_refuses_a_backup_of_another_folder(
    saves: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    other = tmp_path / "folder-copy"
    shutil.copytree(saves, other)
    (other / "Save Game 3.sav").write_bytes(b"x" * SIZE)
    monkeypatch.setenv("ZEXION_SAVE_DIR", str(other))
    backup = save_lab.backup()
    monkeypatch.setenv("ZEXION_SAVE_DIR", str(saves))
    before = {p.name: p.read_bytes() for p in saves.iterdir()}

    assert save_lab.main(["restore", str(backup), "--yes"]) == 1
    assert {p.name: p.read_bytes() for p in saves.iterdir()} == before
    assert _backups(tmp_path) == [backup]  # refused before the safety backup
    # Into the folder it came from, it restores.
    assert save_lab.main(["--save-dir", str(other), "restore", str(backup), "--yes"]) == 0


def test_lab_outputs_refuse_protected_folders(saves: Path, tmp_path: Path) -> None:
    bad = tmp_path / "Gallant Leaf" / "Zexion" / "x.sav"
    with pytest.raises(ProtectedPathError):
        save_lab.main(["copy", "1", "--out", str(bad)])


def test_append_log_stamps_each_line_and_refuses_protected_folders(tmp_path: Path) -> None:
    path = tmp_path / "lab" / "x.jsonl"
    append_log(path, {"event": "a"})
    append_log(path, {"event": "b", "pid": 7})
    lines = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
    assert [list(e) for e in lines] == [["time", "event"], ["time", "event", "pid"]]
    assert [e["event"] for e in lines] == ["a", "b"]
    bad = tmp_path / "Gallant Leaf" / "Zexion" / "x.jsonl"
    with pytest.raises(ProtectedPathError):
        append_log(bad, {"event": "a"})
    assert not bad.parent.exists()
