"""The generator's area lookup, on synthetic MapData and Text (no game data)."""

from __future__ import annotations

import hashlib
import struct

import pytest

import gen_world_data as gwd


def tables(areas: dict[tuple[int, int], int], regions: dict[int, int], names: list[str]):
    """MapData and Text holding the given cell areas, area regions and region names."""
    size = gwd.MAP_AREAS + 2 * (max(regions) + 1)
    map_data = bytearray(max(size, gwd.MAP_CELLS + 54 * 54 * gwd.MAP_CELL_SIZE))
    for (a, b), area in areas.items():
        struct.pack_into("<h", map_data, gwd.MAP_CELLS + (b * 54 + a) * gwd.MAP_CELL_SIZE, area)
    for area, region in regions.items():
        map_data[gwd.MAP_AREAS + area * 2 + 1] = region
    text = bytearray(gwd.TEXT_HEADER + gwd.TEXT_ENTRY * len(names))
    for i, name in enumerate(names):
        start = gwd.TEXT_HEADER + i * gwd.TEXT_ENTRY
        text[start : start + len(name)] = name.encode("latin1")
    return bytes(map_data), bytes(text)


def test_area_names_are_capitalized_around_apostrophes() -> None:
    assert gwd.area_name("GUARDIA") == "Guardia"
    assert gwd.area_name("R'TALVA") == "R'Talva"


def test_a_cell_is_looked_up_with_x_major_index(monkeypatch: pytest.MonkeyPatch) -> None:
    # This repo's cell (a, b) = (7, 22) is X 22, Y 7: record 22 * 54 + 7. Its mirror (22, 7)
    # must not share the record.
    map_data, text = tables({(7, 22): 3, (22, 7): 1}, {1: 0, 3: 2}, ["KRESEZOA", "X", "R'TALVA"])
    monkeypatch.setattr(gwd, "MAP_DATA_SHA256", hashlib.sha256(map_data).hexdigest()[:16])
    monkeypatch.setattr(gwd, "TEXT_SHA256", hashlib.sha256(text).hexdigest()[:16])
    got = gwd.cell_areas(map_data, text, [(7, 22), (22, 7)])
    assert got == {(7, 22): "R'Talva", (22, 7): "Kresezoa"}


def test_other_builds_files_are_refused() -> None:
    map_data, text = tables({}, {0: 0}, ["KRESEZOA"])
    with pytest.raises(SystemExit, match="MapData is not build B1's"):
        gwd.cell_areas(map_data, text, [(0, 0)])
