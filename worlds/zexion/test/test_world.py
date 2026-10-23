import random
import types
import unittest

from BaseClasses import Item, ItemClassification
from Fill import distribute_items_restrictive
from Options import OptionError

from .. import data, rules
from ..items import code_of
from ..locations import location_name
from ..options import START_CELLS
from ..world import (
    FOREIGN,
    FOREIGN_PROGRESSION,
    GOAL_CELL,
    SCRAP_CHUNK,
    SCRAP_ENERGY,
    SCRAP_METAL,
    ZexionWorld,
)
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

    def test_slot_data_sets_up_the_menu_as_the_mode(self) -> None:
        menu = self.world.fill_slot_data()["menu"]
        self.assertEqual(len(menu["seed_text"]), 16)
        self.assertLessEqual(set(menu["seed_text"]), set("0123456789ABCDEF"))
        self.assertEqual((len(menu["start"]), len(menu["pool"])), (27, 27))
        for t, value in self.world.mode["checked_options"].items():
            self.assertEqual(menu["settings"][t - 10], value, f"setting {t}")
        self.assertEqual(menu["second_target"], [0, 20, 509])


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
            mode=types.SimpleNamespace(value=1),
            start_room=types.SimpleNamespace(value=start_room),
            local_items=types.SimpleNamespace(value=set()),
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


class TestOHKO(ZexionTestBase):
    options = {"mode": "ohko"}

    def test_ohko_has_two_starts_and_leaves_unreachable_locations_out(self) -> None:
        self.assertIsNotNone(self.world.second)
        self.assertNotEqual(self.world.start["cell"], self.world.second["cell"])
        created = [loc for loc in self.multiworld.get_locations(self.player) if loc.address]
        self.assertEqual(len(created), len(self.world.kept_locations))
        self.assertLess(len(created), len(data.LOCATIONS))
        pool = [i for i in self.multiworld.itempool if i.player == self.player]
        self.assertEqual(len(pool), len(created))
        self.assertNotIn("Energy Tank", [i.name for i in pool])
        self.assertEqual(self.count("Energy Tank"), 0)

    def test_the_two_starts_reach_enough_with_the_starting_items(self) -> None:
        start, second = self.world.start, self.world.second
        both = set(start["sphere0_indices"]) | set(second["sphere0_indices"])
        self.assertGreaterEqual(len(both), 3)

    def test_slot_data_names_the_second_target(self) -> None:
        slot = self.world.fill_slot_data()
        self.assertEqual(slot["mode"], "ohko")
        self.assertEqual(slot["second_start"]["cell"], list(self.world.second["cell"]))
        menu = slot["menu"]
        self.assertIsNone(menu["second_target"], "the seed's second start is written instead")
        self.assertEqual(len(menu["start_table"]), 34)
        self.assertEqual(menu["table_build"], data.DATA_BUILD)
        index = slot["second_start"]["index"]
        self.assertEqual(tuple(menu["start_table"][index][:2]), tuple(slot["second_start"]["cell"]))
        self.assertEqual(len(slot["kept_locations"]), len(self.world.kept_locations))


class TestSlotDataForTheClient(ZexionTestBase):
    """What client/save_patch.py writes into the save comes from here."""

    def test_location_items_are_this_worlds_items_as_game_ids(self) -> None:
        distribute_items_restrictive(self.multiworld)
        items = self.world.fill_slot_data()["location_items"]
        self.assertEqual(len(items), len(data.LOCATIONS))
        for i, code in enumerate(items):
            placed = self.multiworld.get_location(location_name(i), self.player).item
            own = code_of[placed.name]
            self.assertEqual(code, SCRAP_METAL if own in (SCRAP_ENERGY, SCRAP_CHUNK) else own)
        self.assertNotIn(FOREIGN, items, "47 only for another player's item")
        self.assertNotIn(FOREIGN_PROGRESSION, items, "48 only for another player's item")

    def test_another_worlds_item_is_scrap_energy_or_for_progression_scrap_chunk(self) -> None:
        kinds = [
            ItemClassification.progression,
            ItemClassification.useful,
            ItemClassification.filler,
            ItemClassification.trap,
            ItemClassification.progression | ItemClassification.useful,
        ]
        for i, kind in enumerate(kinds):
            location = self.multiworld.get_location(location_name(i), self.player)
            location.item = Item("Something", kind, 1, self.player + 1)
        items = self.world.fill_slot_data()["location_items"]
        self.assertEqual(
            items[: len(kinds)],
            [FOREIGN_PROGRESSION, FOREIGN, FOREIGN, FOREIGN, FOREIGN_PROGRESSION],
        )
        slot = self.world.fill_slot_data()
        self.assertEqual((slot["stand_in"], slot["stand_in_progression"]), (47, 48))

    def test_start_items_amounts_and_options_are_the_modes(self) -> None:
        slot = self.world.fill_slot_data()
        start = {int(k): v for k, v in slot["start_items"].items()}
        self.assertEqual(start, data.MODES["standard"]["start_inventory"])
        self.assertEqual(slot["amounts"], [3, 17, 3, 122, 0, 12])
        options = {int(t): v for t, v in slot["options"].items()}
        self.assertEqual(options, data.MODES["standard"]["checked_options"])
        self.assertIn(72, options)  # Gates

    def test_the_energy_shield_stays_in_this_world(self) -> None:
        self.assertIn("Energy Shield", self.world.options.local_items.value)


class TestStartInventoryInSlotData(ZexionTestBase):
    def test_a_second_progressive_copy_is_the_upgrade(self) -> None:
        # As AP's Main does for start_inventory, which the test base does not run.
        for name in ("Progressive Snake", "Progressive Snake", "Missile Box"):
            self.multiworld.push_precollected(self.world.create_item(name))
        start = {int(k): v for k, v in self.world.fill_slot_data()["start_items"].items()}
        self.assertEqual((start.get(5), start.get(15), start.get(36)), (1, 1, 1))


class TestOHKOSlotData(ZexionTestBase):
    options = {"mode": "ohko"}

    def test_left_out_locations_hold_scrap_metal(self) -> None:
        items = self.world.fill_slot_data()["location_items"]
        left_out = set(range(len(data.LOCATIONS))) - self.world.kept_locations
        self.assertTrue(left_out)
        self.assertEqual({items[i] for i in left_out}, {SCRAP_METAL})
