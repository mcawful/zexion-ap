"""The generator's area lookup, on synthetic MapData and Text (no game data)."""

from __future__ import annotations

import hashlib
import struct
import types

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


def cell(**open_to):
    """A cell open toward the directions given, walled toward the rest."""
    passage = {d: gwd.rc.OPEN if open_to.get(d) else gwd.rc.WALL for d in "NSWE"}
    return types.SimpleNamespace(passage=passage)


def test_components_number_each_strongly_connected_group_once() -> None:
    # A row of four cells: 0 <-> 1 open both ways, 1 -> 2 open one way, 2 <-> 3 open both ways.
    row = [cell(E=True), cell(W=True, E=True), cell(E=True), cell(W=True)]
    comp = gwd.components({(x, 0) for x in range(4)}, [row])
    assert comp[(0, 0)] == comp[(1, 0)]
    assert comp[(2, 0)] == comp[(3, 0)]
    assert comp[(0, 0)] != comp[(2, 0)]
    assert sorted(set(comp.values())) == [0, 1]


def test_extra_cells_regions_come_after_the_others_and_may_not_join_them() -> None:
    # Five cells in a row: 0 <-> 1, 2 -> 3 (one way), 3 <-> 4; 1 and 2 walled apart. The extra
    # cells 3 and 4 make one region, numbered after the base's; cell 2 joining 3 is refused.
    row = [cell(E=True), cell(W=True), cell(E=True), cell(E=True), cell(W=True)]
    base, extra = {(0, 0), (1, 0), (2, 0)}, {(3, 0), (4, 0)}
    comp = gwd.components_after(base, extra, [row])
    assert {c: comp[c] for c in base} == gwd.components(base, [row])
    assert comp[(3, 0)] == comp[(4, 0)] == 2
    row[3] = cell(W=True, E=True)  # 2 <-> 3: the extra cells join a base region
    with pytest.raises(SystemExit):
        gwd.components_after(base, extra, [row])


def test_a_mode_holds_what_its_starts_share_and_refuses_what_they_do_not() -> None:
    assert gwd.shared([[1, 2], [1, 2]], "ohko", "regions") == [1, 2]
    with pytest.raises(SystemExit, match="ohko: its starts differ in regions"):
        gwd.shared([[1], [2]], "ohko", "regions")


def core_map(extra_conditional: bool = False):
    """A synthetic map: a kept cell (0, 0), a door east into (1, 0) needing Abridged Finale,
    then (1, 0) to (2, 0) needing the Jump Booster, Zexion at (2, 0); optionally a second
    conditional passage in the core, off the way, (2, 0) to (2, 1)."""
    wall = {d: gwd.rc.WALL for d in "NSWE"}

    def cell(passage=None, lists=None):
        return gwd.rc.Cell(
            lists={d: (lists or {}).get(d, []) for d in "NSWE"},
            passage={**wall, **(passage or {})},
            item_gated=False,
            item_needs=[],
        )

    grid = [[cell() for _ in range(54)] for _ in range(54)]
    c = gwd.rc.CONDITIONAL
    grid[0][0] = cell({"E": c}, {"E": [gwd.ABRIDGED_FINALE]})
    grid[0][1] = cell({"W": gwd.rc.OPEN, "E": c}, {"E": [(22, 1)]})
    grid[0][2] = cell({"W": gwd.rc.OPEN, **({"S": c} if extra_conditional else {})})
    return grid


def test_the_core_is_the_unkept_zexion_cell_behind_the_abridged_finale_door() -> None:
    core = gwd.core_goal(core_map(), {(0, 0), (5, 5)}, [(5, 5), (2, 0)])
    assert core == {"door": (0, 0), "cell": (2, 0), "needs": [(((22, 1),), None)]}


def test_a_core_with_another_way_through_is_refused() -> None:
    with pytest.raises(SystemExit, match="off the way to Zexion"):
        gwd.core_goal(core_map(extra_conditional=True), {(0, 0)}, [(2, 0)])


def test_a_core_without_its_door_is_refused() -> None:
    with pytest.raises(SystemExit, match="one passage into the core"):
        gwd.core_goal(core_map(), {(0, 0), (1, 0)}, [(2, 0)])
