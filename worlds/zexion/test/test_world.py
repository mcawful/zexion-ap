import logging
import random
import types
import unittest

from BaseClasses import Item, ItemClassification, LocationProgressType
from Fill import distribute_items_restrictive
from Options import OptionError

from .. import data, rules, starting
from ..items import DRAWN_AS, SCRAP_CHUNK, SCRAP_ENERGY, SCRAP_METAL, code_of
from ..locations import location_name
from ..logic import FIRST_SPECIAL
from ..options import START_CELLS, Goal, Mode, StartRoom
from ..world import (
    DISC_REPLACES,
    FOREIGN,
    FOREIGN_PROGRESSION,
    GOAL_CELL,
    NOT_HINTED,
    ZexionWorld,
)
from .bases import ZexionTestBase


def fake_world(
    mode: int = Mode.option_standard,
    start_room: int = StartRoom.option_automatic,
    goal: int = Goal.option_core,
    start_inventory: dict[str, int] | None = None,
    placed: int = 16,
    required: int = 12,
) -> ZexionWorld:
    """A world with just the options generate_early reads, to run it alone."""
    world = ZexionWorld.__new__(ZexionWorld)
    world.options = types.SimpleNamespace(
        mode=Mode(mode),
        start_room=StartRoom(start_room),
        local_items=types.SimpleNamespace(value=set()),
        goal=Goal(goal),
        start_inventory=types.SimpleNamespace(value=dict(start_inventory or {})),
        os_install_disks=types.SimpleNamespace(value=placed),
        os_install_disks_required=types.SimpleNamespace(value=required),
    )
    world.random = random.Random(1)
    world.player = 1
    world.multiworld = types.SimpleNamespace(get_player_name=lambda player: "Z")
    return world


class TestDefaultStart(ZexionTestBase):
    """The default options: Standard from its default room, the core goal, DeathLink off and
    assists locked; a few locations reachable with the starting items alone."""

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
        self.assertEqual((slot["start_column"], slot["start_row"]), (191, 329))
        self.assertIsNone(slot["second_start"])
        self.assertEqual(len(slot["locations"]), 154)

    def test_goal_needs_all_four_keys(self) -> None:
        self.collect_all_but(["Omega Key", "Victory"])
        self.assertFalse(self.can_reach_location("Zexion"))
        self.collect_by_name("Omega Key")
        self.assertTrue(self.can_reach_location("Zexion"))

    def test_goal_is_in_the_core(self) -> None:
        core = self.multiworld.get_location("Zexion", self.player).parent_region
        self.assertIn(rules.region_name(rules.cell_region()[GOAL_CELL]), core.name)

    def test_slot_data_says_death_link_is_off(self) -> None:
        self.assertIs(self.world.fill_slot_data()["death_link"], False)

    def test_slot_data_disallows_assists(self) -> None:
        self.assertIs(self.world.fill_slot_data()["allow_assists"], False)

    def test_mode_and_items_are_standards(self) -> None:
        self.assertEqual(self.world.mode_name, "standard")
        self.assertEqual(self.world.logic_settings.energy_total, 20)
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

    def test_location_groups_are_the_areas(self) -> None:
        groups = self.world.location_name_groups
        self.assertIn("Guardia 22,7", groups["Guardia"])
        named = set().union(*(names for g, names in groups.items() if g != "Everywhere"))
        self.assertEqual(len(named), len(data.LOCATIONS))


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


class TestDeathLinkOn(ZexionTestBase):
    options = {"death_link": True}

    def test_slot_data_says_death_link_is_on(self) -> None:
        self.assertIs(self.world.fill_slot_data()["death_link"], True)


class TestEasy(ZexionTestBase):
    options = {"mode": "easy"}

    def test_easy_has_its_own_pool_items_and_settings(self) -> None:
        self.assertEqual(self.world.logic_settings.energy_total, 40)
        self.assertEqual(self.count("Energy Tank"), 5)
        pool = [i.name for i in self.multiworld.itempool if i.player == self.player]
        self.assertEqual(pool.count("Energy Tank"), 35)


class TestOtherStart(ZexionTestBase):
    options = {"start_room": START_CELLS.index(tuple(data.MODES["standard"]["starts"][-1]["cell"]))}

    def test_start_follows_the_option(self) -> None:
        self.assertEqual(self.world.start, data.MODES["standard"]["starts"][-1])


class TestStartRoomNotOffered(unittest.TestCase):
    def test_a_room_the_mode_does_not_offer_is_replaced(self) -> None:
        saved = data.MODES["standard"]["starts"]
        data.MODES["standard"]["starts"] = [s for s in saved if tuple(s["cell"]) != START_CELLS[0]]
        try:
            world = fake_world(start_room=0)
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
                fake_world(start_room=0).generate_early()
        finally:
            data.MODES["standard"]["starts"] = saved

    def test_every_room_random_can_roll_gives_a_start_of_the_mode(self) -> None:
        # 'random' rolls any of StartRoom's values, in any mode.
        logger = logging.getLogger()
        for mode, name in Mode.name_lookup.items():
            offered = {tuple(s["cell"]) for s in data.MODES[name]["starts"]}
            for value in StartRoom.name_lookup:
                world = fake_world(mode=mode, start_room=value)
                with self.subTest(mode=name, room=value):
                    level = logger.level
                    logger.setLevel(logging.ERROR)  # the warning for a room not offered
                    try:
                        world.generate_early()
                    finally:
                        logger.setLevel(level)
                    self.assertIn(tuple(world.start["cell"]), offered)
                    if world.second is not None:
                        self.assertIn(tuple(world.second["cell"]), offered)
                        self.assertNotEqual(world.start["cell"], world.second["cell"])


class TestRandomModeAndStartRoom(ZexionTestBase):
    options = {"mode": "random", "start_room": "random"}

    def test_the_start_is_one_the_mode_offers(self) -> None:
        self.assertIn(self.world.start, data.MODES[self.world.mode_name]["starts"])


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
        second = self.world.second
        self.assertEqual(
            slot["second_start"],
            {"index": second["index"], "column": second["column"], "row": second["row"]},
        )
        menu = slot["menu"]
        self.assertIsNone(menu["second_target"], "the seed's second start is written instead")
        self.assertEqual(len(menu["start_table"]), 34)
        self.assertEqual(menu["table_build"], data.DATA_BUILD)
        index = slot["second_start"]["index"]
        self.assertEqual(tuple(menu["start_table"][index][:2]), tuple(second["cell"]))
        self.assertEqual(len(slot["kept_locations"]), len(self.world.kept_locations))


class TestSlotDataForTheClient(ZexionTestBase):
    """What client/save_patch.py writes into the save comes from here."""

    def test_location_items_are_this_worlds_items_as_game_ids(self) -> None:
        distribute_items_restrictive(self.multiworld)
        items = self.world.fill_slot_data()["location_items"]
        self.assertEqual(len(items), len(data.LOCATIONS))
        for i, code in enumerate(items):
            placed = self.multiworld.get_location(location_name(i), self.player).item
            own = DRAWN_AS.get(code_of[placed.name], code_of[placed.name])  # Intel Chip: 49
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

    def test_area_maps_stay_area_maps(self) -> None:
        pool = pool_names(self)
        self.assertNotIn("Intel Chip", pool)
        self.assertEqual(pool.count("Area Map"), 9)
        self.assertIs(self.world.fill_slot_data()["intel_chip"], False)


class TestIntelChips(ZexionTestBase):
    """With hints on (the default) the Area Maps are Intel Chips, which the game holds as the
    Area Map."""

    def test_area_maps_become_intel_chips(self) -> None:
        pool = pool_names(self)
        self.assertNotIn("Area Map", pool)
        self.assertEqual(pool.count("Intel Chip"), 9)
        chip = self.world.create_item("Intel Chip")
        self.assertEqual(chip.code, 3392510_000 + 101)
        self.assertEqual(chip.classification, ItemClassification.filler, "as the Area Map")
        self.assertIs(self.world.fill_slot_data()["intel_chip"], True)

    def test_the_game_holds_an_intel_chip_as_the_area_map(self) -> None:
        distribute_items_restrictive(self.multiworld)
        slot = self.world.fill_slot_data()
        own = [
            i
            for i in range(len(data.LOCATIONS))
            if self.multiworld.get_location(location_name(i), self.player).item.name == "Intel Chip"
        ]
        self.assertTrue(own)
        self.assertEqual({slot["location_items"][i] for i in own}, {49})
        self.assertNotIn(101, slot["location_items"])


class TestIntelChipsLeaveFillerForExcludedLocations(ZexionTestBase):
    """Standard with the OS Install Disks goal's defaults has no Scrap left: its filler is the
    Intel Chips, which an excluded location can take (found by the logic review)."""

    options = {"goal": "os_install_disks", "os_install_disks": 16, "os_install_disks_required": 12}

    def test_an_excluded_location_fills(self) -> None:
        # Excluded as the generator excludes one (worlds.generic.Rules.exclusion_rules).
        location = self.multiworld.get_location(location_name(0), self.player)
        location.progress_type = LocationProgressType.EXCLUDED
        distribute_items_restrictive(self.multiworld)
        item = location.item
        self.assertIsNotNone(item)
        self.assertEqual(item.classification, ItemClassification.filler)


def pool_names(test: ZexionTestBase) -> list[str]:
    return [i.name for i in test.multiworld.itempool if i.player == test.player]


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
        self.assertEqual(pool.count("Intel Chip"), 9 - 4, "Area Maps, as hints")
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
        self.assertNotIn("Intel Chip", pool)
        self.assertEqual(pool.count("Energy Shield"), 1)


class TestDiscHuntOHKO(DiscHuntBase):
    mode = "ohko"
    placed, required = 30, 30

    def test_ohko_fits_30_in_scrap_after_its_left_out_locations(self) -> None:
        pool = pool_names(self)
        self.assertEqual(pool.count("OS Install Disk"), 30)
        self.assertEqual(pool.count("Intel Chip"), 5, "no hint given up")
        self.assertEqual(len(pool), len(self.world.kept_locations))


class TestDiscHuntEasy(DiscHuntBase):
    mode = "easy"
    placed, required = 10, 10

    def test_ten_discs_take_scrap_only(self) -> None:
        pool = pool_names(self)
        self.assertEqual(pool.count("OS Install Disk"), 10)
        self.assertEqual(pool.count("Intel Chip"), 4)
        self.assertEqual(pool.count("Scrap Metal"), 20 + 4 - 10)


class TestDiscStartInventory(DiscHuntBase):
    def test_a_starting_disc_is_counted_not_written(self) -> None:
        self.multiworld.push_precollected(self.world.create_item("OS Install Disk"))
        slot = self.world.fill_slot_data()
        self.assertEqual(slot["discs_start"], 1)
        self.assertNotIn("44", slot["start_items"])


class TestDiscOptionsChecked(unittest.TestCase):
    def world(self, mode: int, placed: int, required: int) -> ZexionWorld:
        return fake_world(mode, goal=Goal.option_os_install_disks, placed=placed, required=required)

    def test_room_per_mode(self) -> None:
        for mode, room in ((0, 31), (1, 31), (2, 41)):
            world = self.world(mode, 1, 1)
            world.generate_early()
            self.assertEqual(world.disc_room(), room, mode)

    def test_too_many_discs_is_an_error(self) -> None:
        # Beyond the option's range: every mode has room for its most (30), so this guards only
        # data regenerated with less room.
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


class TestStartInventoryChecked(unittest.TestCase):
    """start_inventory the client could not give is refused in generation (starting.py)."""

    def refused(self, start_inventory: dict[str, int], **options) -> str:
        world = fake_world(start_inventory=start_inventory, **options)
        with self.assertRaises(OptionError) as caught:
            world.generate_early()
        return str(caught.exception)

    def test_what_a_new_game_can_start_with_passes(self) -> None:
        fine = {
            "Energy Tank": 4,
            "Missile Container": 6,  # 9 with Standard's own 3
            "Missile Box": 3,
            "Progressive Snake": 2,
            "Slide Mod": 1,
            "Alpha Key": 1,
            "Beta Key": 1,
        }
        fake_world(start_inventory=fine).generate_early()
        disks = {"OS Install Disk": 3}
        fake_world(goal=Goal.option_os_install_disks, start_inventory=disks).generate_early()

    def test_items_no_new_game_can_hold_are_refused(self) -> None:
        for name in (
            "Energy Shield",
            "Scrap Metal",
            "Scrap Energy",
            "Scrap Chunk",
            "Area Map",
            "Intel Chip",
            "Assist Unlock",
            "Mega Bomb",
            "Bomb Box",
        ):
            with self.subTest(name):
                self.assertIn(name, self.refused({name: 1}))

    def test_missile_containers_count_the_modes_own(self) -> None:
        self.assertIn("10 Missile Containers", self.refused({"Missile Container": 7}))
        # OHKO starts with 3 too; Easy's own are 3.
        self.assertIn("Missile Containers", self.refused({"Missile Container": 7}, mode=2))

    def test_keys_must_be_the_first_ones(self) -> None:
        self.assertIn("keys", self.refused({"Beta Key": 1}))
        self.assertIn("keys", self.refused({"Alpha Key": 1, "Gamma Key": 1}))
        self.assertIn("Alpha Key", self.refused({"Alpha Key": 2}))

    def test_one_of_an_upgrade_and_two_of_a_weapon(self) -> None:
        self.assertIn("Slide Mod", self.refused({"Slide Mod": 2}))
        # Easy starts with a Power Cartridge of its own.
        self.assertIn("Power Cartridge", self.refused({"Power Cartridge": 1}, mode=0))
        self.assertIn("Progressive Lance", self.refused({"Progressive Lance": 3}))

    def test_a_disk_needs_the_disk_goal(self) -> None:
        self.assertIn("OS Install Disk", self.refused({"OS Install Disk": 1}))

    def test_the_modes_own_starting_items_pass(self) -> None:
        for mode in data.MODES.values():
            self.assertEqual(starting.problems(mode["start_inventory"], False), [])


class TestStartInventoryAccepted(ZexionTestBase):
    options = {"start_inventory": {"Alpha Key": 1, "Missile Box": 2, "Progressive Snake": 2}}

    def test_the_seed_generates(self) -> None:
        self.assertEqual(self.world.mode_name, "standard")
