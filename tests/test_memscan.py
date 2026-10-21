"""tools/memscan.py's classify and record helpers, on synthetic snapshots (no game)."""

from __future__ import annotations

import ctypes
import json

import numpy as np
import pytest

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
    assert memscan.spans([0x100, 0x102, 0x101, 0x900], gap=256) == [(0x100, 3), (0x900, 1)]
    assert memscan.spans([0x100, 0x300], gap=0x300) == [(0x100, 0x201)]


def test_changes_between_lists_each_changed_byte() -> None:
    assert memscan.changes_between({1: 0, 2: 5}, {1: 0, 2: 6}) == [(2, 5, 6)]


def write_snapshot(lab, name: str, module_base: int, regions: list[tuple[int, bytes]]) -> None:
    folder = lab / "mem" / name
    folder.mkdir(parents=True)
    index, offset, blob = [], 0, b""
    for base, data in regions:
        index.append({"base": base, "size": len(data), "type": "image", "offset": offset})
        blob += data
        offset += len(data)
    (folder / "memory.bin").write_bytes(blob)
    meta = {"module_base": module_base, "module_size": 0x10000, "regions": index, "note": ""}
    (folder / "index.json").write_text(json.dumps(meta))


def test_classify_command_aligns_snapshots_by_rva_across_restarts(tmp_path, monkeypatch) -> None:
    monkeypatch.setenv("ZEXION_LAB_DIR", str(tmp_path))
    # Two runs of the game with different module bases: the data region is at RVA 0x2000.
    write_snapshot(tmp_path, "p1", 0x10000, [(0x12000, bytes([1, 0, 7]))])
    write_snapshot(tmp_path, "p2", 0x50000, [(0x52000, bytes([1, 0, 8]))])
    write_snapshot(tmp_path, "t1", 0x10000, [(0x12000, bytes([3, 0, 9]))])
    write_snapshot(tmp_path, "t2", 0x50000, [(0x52000, bytes([3, 0, 9]))])
    assert memscan.main(["classify", "play=p1,p2", "title=t1,t2", "--out", "cand"]) == 0
    saved = json.loads((tmp_path / "mem" / "cand.json").read_text())
    assert saved["target"] == "play"
    assert saved["candidates"] == [{"rva": 0x2000, "values": {"play": 1, "title": 3}}]


def test_the_region_query_struct_matches_windows_x64() -> None:
    assert ctypes.sizeof(memscan.MBI) == 48
    offsets = [getattr(memscan.MBI, f).offset for f, _ in memscan.MBI._fields_]
    assert offsets == [0, 8, 16, 20, 24, 32, 36, 40]


B1 = {"timestamp": 1, "size_of_image": 2}


def snapshot(lab, name, module_base, regions, build=B1) -> None:
    write_snapshot(lab, name, module_base, regions)
    if build is not None:
        index = lab / "mem" / name / "index.json"
        meta = json.loads(index.read_text())
        meta["build"] = build
        index.write_text(json.dumps(meta))


def test_classify_lines_up_regions_split_differently(tmp_path, monkeypatch, capsys) -> None:
    monkeypatch.setenv("ZEXION_LAB_DIR", str(tmp_path))
    base = 0x10000
    snapshot(tmp_path, "p1", base, [(base + 0x2000, bytes([1, 0, 0, 5]))])
    snapshot(tmp_path, "p2", base, [(base + 0x2000, bytes([1, 0])), (base + 0x2002, bytes([0, 5]))])
    snapshot(tmp_path, "t1", base, [(base + 0x2000, bytes([2, 0, 0, 6]))])
    snapshot(tmp_path, "t2", base, [(base + 0x2000, bytes([2, 0])), (base + 0x2002, bytes([0, 6]))])
    assert memscan.main(["classify", "play=p1,p2", "title=t1,t2", "--out", "c"]) == 0
    rvas = [c["rva"] for c in json.loads((tmp_path / "mem" / "c.json").read_text())["candidates"]]
    assert rvas == [0x2000, 0x2003]
    assert "compared 4 bytes" in capsys.readouterr().out


def test_bytes_missing_from_one_snapshot_are_not_candidates(tmp_path, monkeypatch) -> None:
    monkeypatch.setenv("ZEXION_LAB_DIR", str(tmp_path))
    base = 0x10000
    snapshot(tmp_path, "p1", base, [(base + 0x2000, bytes([1, 7]))])
    snapshot(tmp_path, "p2", base, [(base + 0x2000, bytes([1]))])  # 0x2001 not held
    snapshot(tmp_path, "t1", base, [(base + 0x2000, bytes([2, 9]))])
    snapshot(tmp_path, "t2", base, [(base + 0x2000, bytes([2, 9]))])
    assert memscan.main(["classify", "play=p1,p2", "title=t1,t2", "--out", "c"]) == 0
    rvas = [c["rva"] for c in json.loads((tmp_path / "mem" / "c.json").read_text())["candidates"]]
    assert rvas == [0x2000]


def test_classify_refuses_mixed_builds_and_repeated_labels(tmp_path, monkeypatch) -> None:
    monkeypatch.setenv("ZEXION_LAB_DIR", str(tmp_path))
    base = 0x10000
    snapshot(tmp_path, "p1", base, [(base + 0x2000, bytes([1]))])
    snapshot(tmp_path, "p2", base, [(base + 0x2000, bytes([1]))], build={"timestamp": 9})
    snapshot(tmp_path, "t1", base, [(base + 0x2000, bytes([2]))])
    assert memscan.main(["classify", "play=p1,p2", "title=t1"]) == 1
    assert memscan.main(["classify", "play=p1", "play=t1"]) == 1
    assert memscan.main(["classify", "play=p1", "title=t1", "--target", "menu"]) == 1


class FakePM:
    def __init__(self, memory: bytearray, fail_after: int | None = None):
        self.memory, self.reads, self.fail_after = memory, 0, fail_after

    def read_bytes(self, address: int, size: int) -> bytes:
        self.reads += 1
        if self.fail_after is not None and self.reads > self.fail_after:
            raise OSError("process gone")
        return bytes(self.memory[address : address + size])


def run_record(tmp_path, monkeypatch, pm, sleeps, build=B1) -> tuple[int, list[str]]:
    monkeypatch.setenv("ZEXION_LAB_DIR", str(tmp_path))
    monkeypatch.setattr(memscan, "attach", lambda: (pm, 0, 0x1000))
    monkeypatch.setattr(memscan, "build_of", lambda pm, base: build)
    steps = iter(sleeps)

    def sleep(_):
        step = next(steps)
        if step is KeyboardInterrupt:
            raise KeyboardInterrupt
        step()

    monkeypatch.setattr(memscan.time, "sleep", sleep)
    code = memscan.main(["record", "r", "--rva", "0x10,0x20"])
    return code, (tmp_path / "mem" / "r.log").read_text().splitlines()


def test_record_logs_changes_and_an_end_line_on_ctrl_c(tmp_path, monkeypatch) -> None:
    pm = FakePM(bytearray(0x100))
    code, lines = run_record(
        tmp_path, monkeypatch, pm, [lambda: pm.memory.__setitem__(0x20, 3), KeyboardInterrupt]
    )
    assert code == 0
    assert any(line.endswith("+0x20: 0 -> 3") for line in lines)
    assert "end: stopped; 1 poll(s)" in lines[-1]


def test_record_logs_an_end_line_when_the_first_read_fails(tmp_path, monkeypatch) -> None:
    pm = FakePM(bytearray(0x100), fail_after=0)
    code, lines = run_record(tmp_path, monkeypatch, pm, [])
    assert code == 1
    assert "  start: " in lines[0]
    assert "end: read failed" in lines[-1]


def test_record_refuses_candidates_from_another_build(tmp_path, monkeypatch) -> None:
    monkeypatch.setenv("ZEXION_LAB_DIR", str(tmp_path))
    (tmp_path / "mem").mkdir()
    cands = {"target": "play", "groups": {}, "build": B1, "candidates": [{"rva": 16, "values": {}}]}
    (tmp_path / "mem" / "c.json").write_text(json.dumps(cands))
    monkeypatch.setattr(memscan, "attach", lambda: (FakePM(bytearray(0x100)), 0, 0x1000))
    monkeypatch.setattr(memscan, "build_of", lambda pm, base: {"timestamp": 9})
    assert memscan.main(["record", "r", "--candidates", "c"]) == 1
