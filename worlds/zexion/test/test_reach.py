"""reach.py, the reachability worked out at generation time, against the per-mode results that
tools/gen_world_data.py computed cell by cell into data.MODES (the oracles)."""

from __future__ import annotations

import unittest
from dataclasses import replace

from .. import data, logic, reach


def preset(name: str) -> reach.Seed:
    m = data.MODES[name]
    settings = logic.Settings(m["energy_total"], m["mode69"], m["options"])
    return reach.Seed.of(settings, m["start_inventory"], m["pool"])


def with_settings(seed: reach.Seed, **changes) -> reach.Seed:
    return replace(seed, settings=replace(seed.settings, **changes))


def offered_cells(seed: reach.Seed, double: bool = False) -> set[tuple[int, int]]:
    return {room["cell"] for room in reach.offered_starts(seed, double)}


class TestPresetsMatchTheOracles(unittest.TestCase):
    def test_the_same_start_rooms(self) -> None:
        for name, m in data.MODES.items():
            with self.subTest(mode=name):
                got = reach.offered_starts(preset(name), m["double_teleport"])
                want = [{k: v for k, v in s.items() if k != "sphere0_indices"} for s in m["starts"]]
                self.assertEqual(got, want)

    def test_every_item_reaches_the_same_regions_and_locations_from_each_start(self) -> None:
        for name, m in data.MODES.items():
            seed = preset(name)
            for start in m["starts"]:
                with self.subTest(mode=name, start=start["cell"]):
                    every = seed.every(start["cell"])
                    self.assertEqual(sorted(every.regions), m["regions"])
                    self.assertEqual(sorted(every.locations), m["location_indices"])

    def test_ohko_starting_items_reach_the_same_locations_from_each_room(self) -> None:
        seed = preset("ohko")
        for start in data.MODES["ohko"]["starts"]:
            with self.subTest(start=start["cell"]):
                got = sorted(seed.sphere0(start["cell"]).locations)
                self.assertEqual(got, start["sphere0_indices"])

    def test_ohko_every_pair_of_rooms_reaches_what_each_room_does(self) -> None:
        # The world floods from the start and the second target together.
        m, seed = data.MODES["ohko"], preset("ohko")
        for a in data.START_ROOMS:
            for b in data.START_ROOMS:
                if a is b:
                    continue
                every = seed.every(a["cell"], b["cell"])
                with self.subTest(start=a["cell"], second=b["cell"]):
                    self.assertEqual(sorted(every.regions), m["regions"])
                    self.assertEqual(sorted(every.locations), m["location_indices"])

    def test_the_minimum_is_the_generators(self) -> None:
        self.assertEqual(reach.MIN_SPHERE_ZERO, data.MIN_SPHERE_ZERO)


class TestStartRooms(unittest.TestCase):
    def test_start_rooms_are_the_start_tables_rooms_in_its_order(self) -> None:
        self.assertEqual(
            [room["cell"] for room in data.START_ROOMS], [(x, y) for x, y, _ in data.START_TABLE]
        )
        self.assertEqual([room["index"] for room in data.START_ROOMS], list(range(34)))

    def test_every_start_room_is_in_the_region_graph(self) -> None:
        for room in data.START_ROOMS:
            self.assertIn(room["cell"], data.CELL_REGION)

    def test_second_starts_reach_enough_with_the_first(self) -> None:
        seed = preset("ohko")
        for start in data.START_ROOMS:
            first = seed.sphere0(start["cell"]).locations
            for room in reach.second_starts(seed, start["cell"], data.START_ROOMS):
                self.assertNotEqual(room["cell"], start["cell"])
                both = first | seed.sphere0(room["cell"]).locations
                self.assertGreaterEqual(len(both), reach.MIN_SPHERE_ZERO)


class TestSettingsChangeTheResults(unittest.TestCase):
    """Custom mode will vary these settings (docs/custom-mode.md); the results follow them."""

    def test_safety_without_energy_checks_offers_more_rooms(self) -> None:
        # Code 69 always passes in modes 0 and 3 (Standard's preset is 2).
        seed = preset("standard")
        rooms = offered_cells(seed)
        for mode69 in (0, 3):
            with self.subTest(mode69=mode69):
                self.assertGreater(offered_cells(with_settings(seed, mode69=mode69)), rooms)
        self.assertEqual(offered_cells(with_settings(seed, mode69=1)), rooms)

    def test_a_lower_energy_total_counts_the_same_tanks_for_more(self) -> None:
        # Code 69 scales Energy up when the seed's total is under 20.
        seed = preset("standard")
        self.assertGreater(offered_cells(with_settings(seed, energy_total=5)), offered_cells(seed))

    def test_x_crystals_opens_ways_for_the_starting_items(self) -> None:
        seed = preset("standard")
        options = bytearray(seed.settings.options)
        options[30] = 1
        crystals = with_settings(seed, options=bytes(options))
        for room in data.START_ROOMS:
            cell = room["cell"]
            self.assertLessEqual(seed.sphere0(cell).locations, crystals.sphere0(cell).locations)
        total = sum(len(seed.sphere0(r["cell"]).locations) for r in data.START_ROOMS)
        more = sum(len(crystals.sphere0(r["cell"]).locations) for r in data.START_ROOMS)
        self.assertGreater(more, total)


class TestCollect(unittest.TestCase):
    def test_a_weapon_twice_counts_as_its_upgrade(self) -> None:
        have: dict[int, int] = {}
        logic.collect(have, 9)  # Pulse Wave first: counts as the Machine Gun
        self.assertEqual(have, {3: 1})
        logic.collect(have, 3)  # then a Machine Gun: counts as the Pulse Wave
        self.assertEqual(have, {3: 1, 9: 1})


if __name__ == "__main__":
    unittest.main()
