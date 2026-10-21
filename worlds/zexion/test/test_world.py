from .. import data, rules
from ..locations import location_name
from ..world import GOAL_CELL
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


class TestOtherStart(ZexionTestBase):
    options = {"start_room": len(data.STARTS) - 1}

    def test_start_follows_the_option(self) -> None:
        self.assertEqual(self.world.start, data.STARTS[-1])


class TestNames(ZexionTestBase):
    def test_location_names_use_the_games_coordinate_order(self) -> None:
        x, y = data.LOCATIONS[0]
        self.assertEqual(location_name(0), f"Map {x},{y}")
