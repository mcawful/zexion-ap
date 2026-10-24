import random
import types
import unittest

from BaseClasses import Item, ItemClassification
from Fill import distribute_items_restrictive
from Options import OptionError

from .. import data, rules
from ..items import code_of
from ..locations import location_name
from ..logic import FIRST_SPECIAL
from ..options import START_CELLS
from ..world import (
    DISC_REPLACES,
    FOREIGN,
    FOREIGN_PROGRESSION,
    GOAL_CELL,
    NOT_HINTED,
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


class TestDeathLinkIsOffByDefault(ZexionTestBase):
    def test_slot_data_says_death_link_is_off(self) -> None:
        self.assertIs(self.world.fill_slot_data()["death_link"], False)


class TestAssistsAreLockedByDefault(ZexionTestBase):
    def test_slot_data_disallows_assists(self) -> None:
        self.assertIs(self.world.fill_slot_data()["allow_assists"], False)


class TestAssistsAllowed(ZexionTestBase):
    options = {"allow_assists": True}

    def test_slot_data_allows_assists(self) -> None:
        self.assertIs(self.world.fill_slot_data()["allow_assists"], True)


class TestAssistUnlockIsNeverPlaced(ZexionTestBase):
    def test_it_is_not_in_the_pool(self) -> None:
        self.assertEqual(self.count("Assist Unlock"), 0)

    def test_it_can_be_made(self) -> None:
        item = self.world.create_item("Assist Unlock")
        self.assertEqual(item.code, 3392510_000 + 100)
        self.assertFalse(item.advancement)


class TestAssistUnlockAtTheStart(ZexionTestBase):
    options = {"start_inventory": {"Assist Unlock": 1}}

    def test_it_is_not_written_into_the_save(self) -> None:
        self.assertNotIn("100", self.world.fill_slot_data()["start_items"])


class TestDeathLinkOn(ZexionTestBase):
    options = {"death_link": True}

    def test_slot_data_says_death_link_is_on(self) -> None:
        self.assertIs(self.world.fill_slot_data()["death_link"], True)


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
            goal=types.SimpleNamespace(value=0),
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
    def test_location_names_give_the_area_and_the_games_x_then_y(self) -> None:
        # Cell (7, 22), where the owner read X22, Y7 in Guardia (docs/re/locations.md).
        self.assertEqual(location_name(data.LOCATIONS.index((7, 22))), "Guardia 22,7")
        self.assertEqual(location_name(data.LOCATIONS.index((1, 22))), "Exodia 22,1")

    def test_location_names_are_unique(self) -> None:
        names = [location_name(i) for i in range(len(data.LOCATIONS))]
        self.assertEqual(len(set(names)), len(names))

    def test_start_rooms_are_named_the_same_way(self) -> None:
        from ..options import StartRoom

        self.assertIn("kresezoa_16_13", StartRoom.options)


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

    def test_the_energy_shield_can_go_to_any_world(self) -> None:
        # Received, the client's helper runs its pickup event, which gives the shield.
        self.assertNotIn("Energy Shield", self.world.options.local_items.value)


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


class TestAreaMapHints(ZexionTestBase):
    """Area Maps are hints by default: the slot data lists where this player's progression
    items are, for the client (client/hints.py)."""

    def test_the_hint_list_holds_every_placed_progression_item_but_supplies_once(self) -> None:
        distribute_items_restrictive(self.multiworld)
        hints = self.world.fill_slot_data()["hints"]
        placed = [
            loc
            for loc in self.multiworld.get_filled_locations()
            if loc.item.player == self.player
            and loc.item.advancement
            and loc.address is not None
            and code_of[loc.item.name] not in NOT_HINTED
        ]
        self.assertEqual(len(hints), len(placed))
        self.assertEqual(len({(p, loc) for p, loc, _ in hints}), len(hints), "no location twice")
        for player, address, code in hints:
            location = self.multiworld.get_location(
                self.multiworld.worlds[player].location_id_to_name[address], player
            )
            self.assertEqual(code_of[location.item.name], code)
            self.assertTrue(location.item.advancement)

    def test_the_goal_event_and_supplies_are_not_hinted(self) -> None:
        distribute_items_restrictive(self.multiworld)
        codes = {code for _, _, code in self.world.fill_slot_data()["hints"]}
        self.assertTrue({27, 28, 29, 30} <= codes, "the four keys are hinted")
        self.assertTrue(all(isinstance(code, int) and 0 <= code <= 50 for code in codes))
        self.assertFalse(codes & NOT_HINTED)


class TestAreaMapHintsOff(ZexionTestBase):
    options = {"area_map_hints": False}

    def test_no_hints_without_the_option(self) -> None:
        distribute_items_restrictive(self.multiworld)
        self.assertEqual(self.world.fill_slot_data()["hints"], [])


def pool_names(test: ZexionTestBase) -> list[str]:
    return [i.name for i in test.multiworld.itempool if i.player == test.player]


class TestCoreIsTheDefaultGoal(ZexionTestBase):
    def test_no_discs_and_the_keys_in_the_pool(self) -> None:
        pool = pool_names(self)
        self.assertNotIn("OS Install Disk", pool)
        self.assertEqual(
            sum(pool.count(k) for k in ("Alpha Key", "Beta Key", "Gamma Key", "Omega Key")), 4
        )
        slot = self.world.fill_slot_data()
        self.assertEqual(
            (slot["goal"], slot["discs_required"], slot["disc_locations"]), ("core", 0, [])
        )


class DiscHuntBase(ZexionTestBase):
    mode = "standard"
    placed, required = 20, 15

    @property
    def run_default_tests(self) -> bool:
        return type(self) is not DiscHuntBase

    def setUp(self) -> None:
        self.options = {
            "mode": self.mode,
            "goal": "os_install_disks",
            "os_install_disks": self.placed,
            "os_install_disks_required": self.required,
        }
        super().setUp()


class TestDiscHuntStandard(DiscHuntBase):
    def test_the_keys_are_gone_and_the_discs_are_in(self) -> None:
        pool = pool_names(self)
        self.assertEqual(pool.count("OS Install Disk"), 20)
        for key in ("Alpha Key", "Beta Key", "Gamma Key", "Omega Key"):
            self.assertNotIn(key, pool)
        self.assertEqual(len(pool), len(data.LOCATIONS))

    def test_discs_take_scrap_then_area_maps(self) -> None:
        # Standard: 12 Scrap and the keys' 4 places, then 4 of its 9 Area Maps.
        pool = pool_names(self)
        for name in ("Scrap Metal", "Scrap Energy", "Scrap Chunk"):
            self.assertNotIn(name, pool)
        self.assertEqual(pool.count("Area Map"), 9 - 4)
        self.assertEqual(pool.count("Energy Shield"), 6)
        self.assertEqual(pool.count("Missile Container"), 62, "supplies untouched")

    def test_no_core_event_and_the_discs_win(self) -> None:
        self.assertNotIn("Zexion", [loc.name for loc in self.multiworld.get_locations(self.player)])
        self.assertFalse(self.multiworld.has_beaten_game(self.multiworld.state))
        discs = self.get_items_by_name("OS Install Disk")
        self.collect(discs[:14])
        self.assertFalse(self.multiworld.has_beaten_game(self.multiworld.state))
        self.collect(discs[14])
        self.assertTrue(self.multiworld.has_beaten_game(self.multiworld.state))

    def test_discs_are_hinted_but_not_balanced(self) -> None:
        distribute_items_restrictive(self.multiworld)
        hints = self.world.fill_slot_data()["hints"]
        self.assertEqual(sum(1 for _, _, code in hints if code == 44), 20, "every disk")
        disc = self.world.create_item("OS Install Disk")
        self.assertTrue(disc.advancement)
        self.assertTrue(disc.skip_in_prog_balancing)

    def test_own_discs_are_stand_ins_listed_for_the_client(self) -> None:
        distribute_items_restrictive(self.multiworld)
        slot = self.world.fill_slot_data()
        own = [
            i
            for i in range(len(data.LOCATIONS))
            if self.multiworld.get_location(location_name(i), self.player).item.name
            == "OS Install Disk"
        ]
        self.assertTrue(own)
        self.assertEqual(slot["disc_locations"], own)
        self.assertEqual({slot["location_items"][i] for i in own}, {FOREIGN_PROGRESSION})
        self.assertNotIn(44, slot["location_items"], "the game never hands out a disc")
        self.assertEqual(
            (slot["goal"], slot["discs_required"], slot["discs_placed"]),
            ("os_install_disks", 15, 20),
        )
        self.assertEqual(slot["discs_start"], 0)


class TestDiscHuntStandardFull(DiscHuntBase):
    placed, required = 30, 30  # the options' most

    def test_every_area_map_and_most_energy_shields_give_way(self) -> None:
        pool = pool_names(self)
        self.assertEqual(pool.count("OS Install Disk"), 30)
        self.assertNotIn("Area Map", pool)
        self.assertEqual(pool.count("Energy Shield"), 1)


class TestDiscHuntOHKO(DiscHuntBase):
    mode = "ohko"
    placed, required = 30, 30

    def test_ohko_fits_30_in_scrap_after_its_left_out_locations(self) -> None:
        pool = pool_names(self)
        self.assertEqual(pool.count("OS Install Disk"), 30)
        self.assertEqual(pool.count("Area Map"), 5, "no hint given up")
        self.assertEqual(len(pool), len(self.world.kept_locations))


class TestDiscHuntEasy(DiscHuntBase):
    mode = "easy"
    placed, required = 10, 10

    def test_ten_discs_take_scrap_only(self) -> None:
        pool = pool_names(self)
        self.assertEqual(pool.count("OS Install Disk"), 10)
        self.assertEqual(pool.count("Area Map"), 4)
        self.assertEqual(pool.count("Scrap Metal"), 20 + 4 - 10)


class TestDiscStartInventory(DiscHuntBase):
    def test_a_starting_disc_is_counted_not_written(self) -> None:
        self.multiworld.push_precollected(self.world.create_item("OS Install Disk"))
        slot = self.world.fill_slot_data()
        self.assertEqual(slot["discs_start"], 1)
        self.assertNotIn("44", slot["start_items"])


class TestDiscOptionsChecked(unittest.TestCase):
    def world(self, mode: int, placed: int, required: int) -> ZexionWorld:
        world = ZexionWorld.__new__(ZexionWorld)
        world.options = types.SimpleNamespace(
            mode=types.SimpleNamespace(value=mode),
            start_room=types.SimpleNamespace(value=len(START_CELLS)),
            local_items=types.SimpleNamespace(value=set()),
            goal=types.SimpleNamespace(value=1),
            os_install_disks=types.SimpleNamespace(value=placed),
            os_install_disks_required=types.SimpleNamespace(value=required),
        )
        world.random = random.Random(1)
        world.player = 1
        world.multiworld = types.SimpleNamespace(get_player_name=lambda player: "Z")
        return world

    def test_room_per_mode(self) -> None:
        for mode, room in ((0, 31), (1, 31), (2, 41)):
            world = self.world(mode, 1, 1)
            world.generate_early()
            self.assertEqual(world.disc_room(), room, mode)

    def test_too_many_discs_is_an_error(self) -> None:
        with self.assertRaises(OptionError):
            self.world(1, 32, 1).generate_early()

    def test_more_required_than_placed_is_an_error(self) -> None:
        with self.assertRaises(OptionError):
            self.world(1, 10, 11).generate_early()

    def test_what_discs_replace_is_read_by_no_rule(self) -> None:
        codes = set()
        for reqs in data.LOCATION_RULES.values():
            codes |= {c for c, _ in reqs}
        for ways in data.ENTRANCES.values():
            for way in ways:
                if way is not None:
                    codes |= {c for c, _ in way[0]}
        # Codes from 50 up are derived from the items (logic.derive), not item counts: 50 there
        # is not the Energy Shield.
        items = {c for c in codes if c < FIRST_SPECIAL}
        self.assertFalse(items & (set(DISC_REPLACES) | {27, 28, 29, 30, 44}))
