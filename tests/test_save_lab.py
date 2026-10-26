"""save_lab on a synthetic save folder. Nothing here touches the real saves."""

from __future__ import annotations

import json
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
    # The backup no longer matches slot 3, so a second install there is refused.
    assert save_lab.main(["install", str(patched), "3", "--yes"]) == 1


def test_restore_puts_a_slot_back_and_backs_up_first(saves: Path, tmp_path: Path) -> None:
    original = (saves / "Save Game 3.sav").read_bytes()
    first = save_lab.backup()
    (tmp_path / "p.sav").write_bytes(b"x" * SIZE)
    assert save_lab.main(["install", str(tmp_path / "p.sav"), "3", "--yes"]) == 0

    assert save_lab.main(["restore", str(first), "--slot", "3", "--yes"]) == 0
    assert (saves / "Save Game 3.sav").read_bytes() == original
    backups = list((tmp_path / "lab" / "backups").iterdir())
    assert len(backups) == 2  # the original, plus the safety copy taken before restoring


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
