import random
import types
import unittest

from Options import OptionError

from .. import data, rules
from ..locations import location_name
from ..options import START_CELLS
from ..world import GOAL_CELL, ZexionWorld
from .bases import ZexionTestBase


class TestDefaultStart(ZexionTestBase):
    """The default start: every location reachable with everything, a few with nothing."""

    def test_start_is_the_default_presets(self) -> None:
        self.assertEqual(tuple(self.world.start["cell"]), (13, 16))

    def test_starting_items_reach_some_locations(self) -> None:
        reachable = [
            loc
            for loc in self.multiworld.get_locations(self.player)
            if loc.can_reach(self.multiworld.state)
        ]
        self.assertGreaterEqual(len(reachable), 3)

    def test_slot_data_names_the_start(self) -> None:
        slot = self.world.fill_slot_data()
        self.assertEqual(slot["start_cell"], [13, 16])
        self.assertEqual((slot["start_column"], slot["start_row"]), (191, 329))
        self.assertEqual(len(slot["locations"]), 154)

    def test_goal_needs_all_four_keys(self) -> None:
        self.collect_all_but(["Omega Key", "Victory"])
        self.assertFalse(self.can_reach_location("Zexion"))
        self.collect_by_name("Omega Key")
        self.assertTrue(self.can_reach_location("Zexion"))

    def test_goal_is_in_the_core(self) -> None:
        core = self.multiworld.get_location("Zexion", self.player).parent_region
        self.assertIn(rules.region_name(rules.cell_region()[GOAL_CELL]), core.name)


class TestStandardIsTheDefault(ZexionTestBase):
    def test_mode_and_items_are_standards(self) -> None:
        self.assertEqual(self.world.mode_name, "standard")
        self.assertEqual(self.world.settings.energy_total, 20)
        self.assertEqual(self.count("Energy Tank"), 3)  # the starting inventory
        self.assertEqual(self.world.fill_slot_data()["mode"], "standard")


class TestEasy(ZexionTestBase):
    options = {"mode": "easy"}

    def test_easy_has_its_own_pool_items_and_settings(self) -> None:
        self.assertEqual(self.world.settings.energy_total, 40)
        self.assertEqual(self.count("Energy Tank"), 5)
        pool = [i.name for i in self.multiworld.itempool if i.player == self.player]
        self.assertEqual(pool.count("Energy Tank"), 35)


class TestOtherStart(ZexionTestBase):
    options = {"start_room": START_CELLS.index(tuple(data.MODES["standard"]["starts"][-1]["cell"]))}

    def test_start_follows_the_option(self) -> None:
        self.assertEqual(self.world.start, data.MODES["standard"]["starts"][-1])


class TestStartRoomNotOffered(unittest.TestCase):
    def world(self, start_room: int) -> ZexionWorld:
        world = ZexionWorld.__new__(ZexionWorld)
        world.options = types.SimpleNamespace(
            mode=types.SimpleNamespace(value=1), start_room=types.SimpleNamespace(value=start_room)
        )
        world.random = random.Random(1)
        world.player = 1
        world.multiworld = types.SimpleNamespace(get_player_name=lambda player: "Z")
        return world

    def test_a_room_the_mode_does_not_offer_is_replaced(self) -> None:
        saved = data.MODES["standard"]["starts"]
        data.MODES["standard"]["starts"] = [s for s in saved if tuple(s["cell"]) != START_CELLS[0]]
        try:
            world = self.world(0)
            with self.assertLogs(level="WARNING"):
                world.generate_early()
            self.assertIn(world.start, data.MODES["standard"]["starts"])
        finally:
            data.MODES["standard"]["starts"] = saved

    def test_a_mode_without_rooms_is_an_error(self) -> None:
        saved = data.MODES["standard"]["starts"]
        data.MODES["standard"]["starts"] = []
        try:
            with self.assertRaises(OptionError):
                self.world(0).generate_early()
        finally:
            data.MODES["standard"]["starts"] = saved


class TestNames(ZexionTestBase):
    def test_location_names_use_the_games_coordinate_order(self) -> None:
        x, y = data.LOCATIONS[0]
        self.assertEqual(location_name(0), f"Map {x},{y}")
