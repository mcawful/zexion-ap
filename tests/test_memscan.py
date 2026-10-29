"""tools/memscan.py's classify and record helpers, on synthetic snapshots (no game)."""

from __future__ import annotations

import ctypes
import json

import numpy as np
import pytest
from fakes import FakeProcess, scripted_sleep

import memscan


def arr(*values: int) -> np.ndarray:
    return np.array(values, dtype=np.uint8)


def test_classify_keeps_bytes_stable_per_state_that_tell_the_target_apart() -> None:
    groups = {
        # byte: 0 mode  1 noise  2 same everywhere  3 play differs only from title
        "play": [arr(1, 5, 9, 4), arr(1, 6, 9, 4)],
        "pause": [arr(2, 7, 9, 4), arr(2, 7, 9, 4)],
        "title": [arr(3, 8, 9, 0), arr(3, 8, 9, 0)],
    }
    idx, values = memscan.classify(groups, "play")
    assert idx.tolist() == [0]
    assert {k: v.tolist() for k, v in values.items()} == {"play": [1], "pause": [2], "title": [3]}


def test_classify_target_can_be_any_group() -> None:
    groups = {"play": [arr(1, 4), arr(1, 4)], "dead": [arr(1, 0), arr(1, 0)]}
    idx, _ = memscan.classify(groups, "dead")
    assert idx.tolist() == [1]
    with pytest.raises(ValueError, match="not one of the groups"):
        memscan.classify(groups, "menu")


def test_groups_parse_from_label_and_snapshots() -> None:
    assert memscan.parse_group("play=a,b") == ("play", ["a", "b"])
    for bad in ("play", "=a", "play="):
        with pytest.raises(Exception, match="LABEL=SNAP"):
            memscan.parse_group(bad)


def test_spans_merge_close_addresses() -> None:
    assert memscan.spans([0x100, 0x102, 0x101, 0x900], gap=256) == [
        (0x100, 3, [0x100, 0x101, 0x102]),
        (0x900, 1, [0x900]),
    ]
    assert memscan.spans([0x100, 0x300], gap=0x300) == [(0x100, 0x201, [0x100, 0x300])]


def test_changes_between_lists_each_changed_byte() -> None:
    assert memscan.changes_between({1: 0, 2: 5}, {1: 0, 2: 6}) == [(2, 5, 6)]


def test_the_region_query_struct_matches_windows_x64() -> None:
    assert ctypes.sizeof(memscan.MBI) == 48
    offsets = [getattr(memscan.MBI, f).offset for f, _ in memscan.MBI._fields_]
    assert offsets == [0, 8, 16, 20, 24, 32, 36, 40]


B1 = {"timestamp": 1, "size_of_image": 2}


@pytest.fixture
def lab(tmp_path, monkeypatch):
    monkeypatch.setenv("ZEXION_LAB_DIR", str(tmp_path))
    return tmp_path


def snapshot(lab, name, module_base, regions: list[tuple[int, bytes]], build=B1) -> None:
    """A snapshot as ``snap`` writes one; ``build=None`` for one that records no build."""
    folder = lab / "mem" / name
    folder.mkdir(parents=True)
    index, offset, blob = [], 0, b""
    for base, data in regions:
        index.append({"base": base, "size": len(data), "type": "image", "offset": offset})
        blob += data
        offset += len(data)
    (folder / "memory.bin").write_bytes(blob)
    meta = {"module_base": module_base, "module_size": 0x10000, "regions": index, "note": ""}
    meta |= {"build": build} if build else {}
    (folder / "index.json").write_text(json.dumps(meta), encoding="utf-8")


def candidate_rvas(lab, name: str) -> list[int]:
    saved = json.loads((lab / "mem" / f"{name}.json").read_text(encoding="utf-8"))
    return [c["rva"] for c in saved["candidates"]]


def test_classify_command_aligns_snapshots_by_rva_across_restarts(lab) -> None:
    # Two runs of the game with different module bases: the data region is at RVA 0x2000.
    snapshot(lab, "p1", 0x10000, [(0x12000, bytes([1, 0, 7]))], build=None)
    snapshot(lab, "p2", 0x50000, [(0x52000, bytes([1, 0, 8]))], build=None)
    snapshot(lab, "t1", 0x10000, [(0x12000, bytes([3, 0, 9]))], build=None)
    snapshot(lab, "t2", 0x50000, [(0x52000, bytes([3, 0, 9]))], build=None)
    assert memscan.main(["classify", "play=p1,p2", "title=t1,t2", "--out", "cand"]) == 0
    saved = json.loads((lab / "mem" / "cand.json").read_text(encoding="utf-8"))
    assert saved["target"] == "play"
    assert saved["candidates"] == [{"rva": 0x2000, "values": {"play": 1, "title": 3}}]


def test_classify_lines_up_regions_split_differently(lab, capsys) -> None:
    base = 0x10000
    snapshot(lab, "p1", base, [(base + 0x2000, bytes([1, 0, 0, 5]))])
    snapshot(lab, "p2", base, [(base + 0x2000, bytes([1, 0])), (base + 0x2002, bytes([0, 5]))])
    snapshot(lab, "t1", base, [(base + 0x2000, bytes([2, 0, 0, 6]))])
    snapshot(lab, "t2", base, [(base + 0x2000, bytes([2, 0])), (base + 0x2002, bytes([0, 6]))])
    assert memscan.main(["classify", "play=p1,p2", "title=t1,t2", "--out", "c"]) == 0
    assert candidate_rvas(lab, "c") == [0x2000, 0x2003]
    assert "compared 4 bytes" in capsys.readouterr().out


def test_bytes_missing_from_one_snapshot_are_not_candidates(lab) -> None:
    base = 0x10000
    snapshot(lab, "p1", base, [(base + 0x2000, bytes([1, 0]))])  # a missing byte reads 0
    snapshot(lab, "p2", base, [(base + 0x2000, bytes([1]))])  # 0x2001 not held
    snapshot(lab, "t1", base, [(base + 0x2000, bytes([2, 9]))])
    snapshot(lab, "t2", base, [(base + 0x2000, bytes([2, 9]))])
    assert memscan.main(["classify", "play=p1,p2", "title=t1,t2", "--out", "c"]) == 0
    assert candidate_rvas(lab, "c") == [0x2000]


def test_classify_refuses_mixed_builds_and_repeated_labels(lab) -> None:
    base = 0x10000
    snapshot(lab, "p1", base, [(base + 0x2000, bytes([1]))])
    snapshot(lab, "p2", base, [(base + 0x2000, bytes([1]))], build={"timestamp": 9})
    snapshot(lab, "t1", base, [(base + 0x2000, bytes([2]))])
    assert memscan.main(["classify", "play=p1,p2", "title=t1"]) == 1
    assert memscan.main(["classify", "play=p1", "play=t1"]) == 1
    assert memscan.main(["classify", "play=p1", "title=t1", "--target", "menu"]) == 1


class GoneProcess(FakeProcess):
    def read_bytes(self, address: int, size: int) -> bytes:
        raise OSError("process gone")


def run_record(lab, monkeypatch, pm, *steps) -> tuple[int, list[str]]:
    monkeypatch.setattr(memscan, "attach", lambda: (pm, 0, 0x1000))
    monkeypatch.setattr(memscan, "build_of", lambda pm, base: B1)
    monkeypatch.setattr(memscan.time, "sleep", scripted_sleep(*steps))
    code = memscan.main(["record", "r", "--rva", "0x10,0x20"])
    return code, (lab / "mem" / "r.log").read_text(encoding="utf-8").splitlines()


def test_record_logs_changes_and_an_end_line_on_ctrl_c(lab, monkeypatch) -> None:
    pm = FakeProcess(0x100)
    code, lines = run_record(lab, monkeypatch, pm, lambda: pm.memory.__setitem__(0x20, 3))
    assert code == 0
    assert any(line.endswith("+0x20: 0 -> 3") for line in lines)
    assert "end: stopped; 1 poll(s)" in lines[-1]


def test_record_logs_an_end_line_when_the_first_read_fails(lab, monkeypatch) -> None:
    code, lines = run_record(lab, monkeypatch, GoneProcess(0x100))
    assert code == 1
    assert "  start: " in lines[0]
    assert "end: read failed" in lines[-1]


def test_record_refuses_candidates_from_another_build(lab, monkeypatch) -> None:
    (lab / "mem").mkdir()
    cands = {"target": "play", "groups": {}, "build": B1, "candidates": [{"rva": 16, "values": {}}]}
    (lab / "mem" / "c.json").write_text(json.dumps(cands), encoding="utf-8")
    monkeypatch.setattr(memscan, "attach", lambda: (FakeProcess(0x100), 0, 0x1000))
    monkeypatch.setattr(memscan, "build_of", lambda pm, base: {"timestamp": 9})
    assert memscan.main(["record", "r", "--candidates", "c"]) == 1
