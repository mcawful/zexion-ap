"""reach.py, the reachability worked out at generation time, against the per-mode results that
tools/gen_world_data.py computed cell by cell into data.MODES (the oracles)."""

from __future__ import annotations

import unittest
from dataclasses import replace

from .. import data, logic, reach
from .bases import preset, with_settings


def sphere0_rooms(seed: reach.Seed) -> list[frozenset[int]]:
    """The locations the starting items reach from each of the 34 start rooms."""
    return [seed.sphere0(room["cell"]).locations for room in data.START_ROOMS]


class TestPresetsMatchTheOracles(unittest.TestCase):
    def test_each_room_reaches_what_the_generator_found(self) -> None:
        # Every item: the same regions and locations from every room; the starting items: each
        # room's own locations.
        for name, m in data.MODES.items():
            seed = preset(name)
            for room, sphere0 in zip(data.START_ROOMS, m["sphere0_indices"], strict=True):
                with self.subTest(mode=name, room=room["index"]):
                    every = seed.every(room["cell"])
                    self.assertEqual(sorted(every.regions), m["regions"])
                    self.assertEqual(sorted(every.locations), m["location_indices"])
                    self.assertEqual(sorted(seed.sphere0(room["cell"]).locations), sphere0)

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


class TestStartRooms(unittest.TestCase):
    def test_start_rooms_are_the_start_tables_rooms_in_its_order(self) -> None:
        self.assertEqual(
            [room["cell"] for room in data.START_ROOMS], [(x, y) for x, y, _ in data.START_TABLE]
        )
        self.assertEqual([room["index"] for room in data.START_ROOMS], list(range(34)))

    def test_every_start_room_is_in_the_region_graph(self) -> None:
        for room in data.START_ROOMS:
            self.assertIn(room["cell"], data.CELL_REGION)

    def test_second_starts_collect_a_location_with_the_first(self) -> None:
        seed = preset("ohko")
        for start in data.START_ROOMS:
            rooms = reach.second_starts(seed, start, data.START_ROOMS)
            for room in rooms:
                self.assertNotEqual(room["cell"], start["cell"])
                self.assertTrue(seed.sphere0_from(start, room).locations)
                self.assertFalse(reach.flagged(room))  # not counted at the start
            # OHKO's starting items count no flagged room, which the world then leaves out.
            others = [r for r in data.START_ROOMS if r is not start and not reach.flagged(r)]
            if seed.sphere0(start["cell"]).locations:
                self.assertEqual(rooms, others)


class TestSettingsChangeTheResults(unittest.TestCase):
    """Custom mode varies these settings (docs/re/randocells.md, "What each setting does in the
    game"); the results follow them."""

    def assert_reaches_more(self, seed: reach.Seed, more: reach.Seed) -> None:
        """From every room ``more``'s starting items reach what ``seed``'s do, and more in all."""
        before, after = sphere0_rooms(seed), sphere0_rooms(more)
        for room, a, b in zip(data.START_ROOMS, before, after, strict=True):
            self.assertLessEqual(a, b, f"room {room['index']}")
        self.assertGreater(sum(map(len, after)), sum(map(len, before)))

    def test_safety_without_energy_checks_reaches_more(self) -> None:
        # Code 69 always passes in modes 0 and 3 (Standard's preset is 2): 64 locations in all
        # from the 34 rooms become 75 and 76; mode 1 keeps each room's.
        seed = preset("standard")
        for mode69 in (0, 3):
            with self.subTest(mode69=mode69):
                self.assert_reaches_more(seed, with_settings(seed, mode69=mode69))
        self.assertEqual(sphere0_rooms(with_settings(seed, mode69=1)), sphere0_rooms(seed))
        light, off = (sphere0_rooms(with_settings(seed, mode69=m)) for m in (0, 3))
        self.assertEqual(sum(map(len, sphere0_rooms(seed))), 64)
        self.assertEqual(sum(map(len, light)), 75)
        self.assertEqual(sum(map(len, off)), 76)
        # Off also counts as combat tier 4 (code 62): from Below Arena, location 60 too.
        more = {r["index"]: b - a for r, a, b in zip(data.START_ROOMS, light, off, strict=True)}
        self.assertEqual({i: d for i, d in more.items() if d}, {3: {60}})

    def test_a_lower_energy_total_counts_the_same_tanks_for_more(self) -> None:
        # Code 69 scales Energy up when the seed's total is under 20: 64 become 75.
        seed = preset("standard")
        self.assert_reaches_more(seed, with_settings(seed, energy_total=5))

    def test_x_crystals_opens_ways_for_the_starting_items(self) -> None:
        seed = preset("standard")
        options = bytearray(seed.settings.options)
        options[30] = 1
        self.assert_reaches_more(seed, with_settings(seed, options=bytes(options)))

    def test_secret_shuffle_adds_its_location_only_when_on(self) -> None:
        # The 155th location, index 154 (data.SECRET_LOCATION), in six cells only code 68
        # with Secret Shuffle's tier opens (docs/re/locations.md, "The region graph holds what
        # the settings reach").
        seed = preset("standard")
        options = bytearray(seed.settings.options)
        options[reach.SECRET_SHUFFLE] = 1
        shuffled = with_settings(seed, options=bytes(options))
        self.assertEqual(len(reach.seed_locations(seed.settings)), 154)
        self.assertEqual(reach.seed_locations(shuffled.settings)[154], data.SECRET_LOCATION)
        for room in data.START_ROOMS:
            with self.subTest(room=room["cell"]):
                every = shuffled.every(room["cell"]).locations
                self.assertEqual(every, frozenset(range(155)))
                self.assertNotIn(154, seed.every(room["cell"]).locations)
        self.assertNotIn(data.CELL_REGION[data.SECRET_LOCATION], seed.every((23, 22)).regions)

    def test_the_presets_regions_keep_their_numbers(self) -> None:
        # Secret Shuffle's cells' regions are numbered after every other (tools/
        # gen_world_data.py, components_after), so none a preset reaches changed.
        secret = {data.CELL_REGION[(a, b)] for a in (20, 21) for b in (46, 47, 48)}
        reached = set().union(*(data.MODES[m]["regions"] for m in data.MODES))
        self.assertGreater(min(secret), max(reached))
        self.assertEqual(max(data.CELL_REGION.values()), max(secret))

    def test_x_blocks_short_way_needs_only_the_snake(self) -> None:
        # (7, 27) -> (7, 26) (code 68, X Blocks) -> (6, 26) (the Snake) -> (6, 27), the graph's
        # two cells X Blocks opens (docs/re/locations.md, "The region graph holds what the
        # settings reach").
        settings = preset("standard").settings
        options = bytearray(settings.options)
        options[31] = 1
        blocks = replace(settings, options=bytes(options))
        snake = reach.inventory({5: 1})
        target = data.CELL_REGION[(6, 27)]
        self.assertIn((6, 26), data.CELL_REGION)
        self.assertIn(target, reach.reach(blocks, snake, ((7, 27),)).regions)
        self.assertNotIn(target, reach.reach(settings, snake, ((7, 27),)).regions)
        self.assertNotIn(target, reach.reach(blocks, (), ((7, 27),)).regions)


class TestBoxesInTheRules(unittest.TestCase):
    """The generator counts a held box only as ten missiles or bombs for code 62, never as
    Missile Containers or Mega Bombs in a list, and a box entry in a list only raises another
    entry's need, which no B1 list has (docs/re/randocells.md, "The evaluator works on a table
    of 64 counts", 0xE3C80, 0xE3530)."""

    def test_a_held_box_is_no_containers(self) -> None:
        # Tier 50 in a group: ten Missile Containers ("??? 48,20" by region 452).
        self.assertFalse(logic.satisfied([(12, 2), (1, 50)], {7: 1, 12: 1, 36: 10}))
        self.assertTrue(logic.satisfied([(12, 2), (1, 50)], {7: 1, 12: 1, 1: 10}))

    def test_the_x_missile_needs_five_containers(self) -> None:
        self.assertFalse(logic.satisfied([(18, 1)], {18: 1, 36: 5, 1: 4}))
        self.assertTrue(logic.satisfied([(18, 1)], {18: 1, 1: 5}))

    def test_no_list_has_a_box_entry(self) -> None:
        lists = [
            entries
            for ways in data.ENTRANCES.values()
            for way in ways
            if way
            for entries in way[:1]
        ]
        lists += list(data.LOCATION_RULES.values()) + [way[0] for way in data.CORE["needs"]]
        self.assertTrue(lists)
        boxes = [entries for entries in lists if {36, 37} & {code for code, _ in entries}]
        self.assertEqual(boxes, [])


if __name__ == "__main__":
    unittest.main()
