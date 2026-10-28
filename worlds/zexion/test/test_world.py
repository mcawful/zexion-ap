import random
import types
import unittest
from argparse import Namespace
from typing import Any
from unittest import mock

from BaseClasses import CollectionState, Item, ItemClassification, LocationProgressType, MultiWorld
from Fill import distribute_items_restrictive
from Options import OptionError
from worlds.AutoWorld import call_all

from .. import data, reach, rules
from ..items import DRAWN_AS, GAME, SCRAP_CHUNK, SCRAP_ENERGY, SCRAP_METAL, ZexionItem, code_of
from ..locations import LOCATION_ID_BASE, location_name
from ..logic import FIRST_SPECIAL
from ..options import START_CELLS, Goal, Mode, StartRoom
from ..world import (
    CORE_REGION,
    DISC_REPLACES,
    FOREIGN,
    FOREIGN_PROGRESSION,
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
        goal=Goal(goal),
        start_inventory=types.SimpleNamespace(value=dict(start_inventory or {})),
        os_install_disks=types.SimpleNamespace(value=placed),
        os_install_disks_required=types.SimpleNamespace(value=required),
    )
    world.random = random.Random(1)
    world.player = 1
    world.multiworld = types.SimpleNamespace(get_player_name=lambda player: "Z")
    return world


def disks(mode: str = "standard", placed: int = 20, required: int = 15) -> dict[str, Any]:
    """Options for the OS Install Disks goal."""
    return {
        "mode": mode,
        "goal": "os_install_disks",
        "os_install_disks": placed,
        "os_install_disks_required": required,
    }


def two_zexion_players(options: dict[str, Any]) -> MultiWorld:
    """Two Zexion players, Alpha (1) and Beta (2), with the same options, set up as the
    generator does through generate_basic: item_links in the options make real groups
    (MultiWorld.set_item_links), and link_items is left to the test."""
    multiworld = MultiWorld(2)
    multiworld.game = {1: GAME, 2: GAME}
    multiworld.player_name = {1: "Alpha", 2: "Beta"}
    multiworld.set_seed(1)
    args = Namespace()
    for name, option in ZexionWorld.options_dataclass.type_hints.items():
        value = options.get(name, option.default)
        setattr(args, name, {player: option.from_any(value) for player in (1, 2)})
    multiworld.set_options(args)
    multiworld.set_item_links()
    multiworld.state = CollectionState(multiworld)  # after the groups, which it counts
    steps = ("generate_early", "create_regions", "create_items", "set_rules")
    for step in (*steps, "connect_entrances", "generate_basic"):
        call_all(multiworld, step)
    return multiworld


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

    def test_goal_is_in_the_core_entered_from_the_door(self) -> None:
        core = self.multiworld.get_location("Zexion", self.player).parent_region
        self.assertEqual(core.name, CORE_REGION)
        self.assertNotIn(data.CORE["cell"], data.CELL_REGION)  # RandoCells needs Abridged Finale
        door = rules.region_name(data.CELL_REGION[data.CORE["door"]])
        self.assertEqual([e.parent_region.name for e in core.entrances], [door])

    def test_the_way_into_the_core_needs_its_lists(self) -> None:
        # B1's core: (27, 42) to (28, 42) needs code 51 (Wall Jump Boots with G-Diffuser or
        # Gravity Harness, or Zenith Thrusters) or the Jump Booster.
        self.assertEqual(data.CORE["needs"], [(((51, 1), (22, 1)), None)])
        entrance = self.multiworld.get_entrance("Into the core", self.player)
        for names, expected in (
            ((), False),
            (("Wall Jump Boots",), False),
            (("Jump Booster",), True),
            (("Wall Jump Boots", "G-Diffuser"), True),
            (("Zenith Thrusters",), True),
        ):
            state = CollectionState(self.multiworld)
            for name in names:
                state.collect(self.world.create_item(name), True)
            with self.subTest(names=names):
                self.assertEqual(entrance.access_rule(state), expected)

    def test_slot_data_says_death_link_is_off_and_assists_are_locked(self) -> None:
        slot = self.world.fill_slot_data()
        self.assertEqual((slot["death_link"], slot["allow_assists"]), (False, False))

    def test_assist_unlock_is_not_in_the_pool_but_can_be_made(self) -> None:
        self.assertEqual(self.count("Assist Unlock"), 0)
        item = self.world.create_item("Assist Unlock")
        self.assertEqual(item.code, 3392510_000 + 100)
        self.assertFalse(item.advancement)

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


class TestDeathLinkAndAssistsOn(ZexionTestBase):
    options = {"death_link": True, "allow_assists": True}

    def test_slot_data_says_so(self) -> None:
        slot = self.world.fill_slot_data()
        self.assertEqual((slot["death_link"], slot["allow_assists"]), (True, True))


class TestEasy(ZexionTestBase):
    options = {"mode": "easy"}

    def test_easy_has_its_own_pool_items_and_settings(self) -> None:
        self.assertEqual(self.world.logic_settings.energy_total, 40)
        self.assertEqual(self.count("Energy Tank"), 5)
        self.assertEqual(pool_names(self).count("Energy Tank"), 35)


class TestOtherStart(ZexionTestBase):
    options = {"start_room": START_CELLS.index(tuple(data.MODES["standard"]["starts"][-1]["cell"]))}

    def test_start_follows_the_option(self) -> None:
        self.assertEqual(self.world.start, data.MODES["standard"]["starts"][-1])


class TestStartRoomNotOffered(unittest.TestCase):
    def test_a_room_the_mode_does_not_offer_is_replaced(self) -> None:
        starts = [s for s in data.MODES["standard"]["starts"] if s["cell"] != START_CELLS[0]]
        with mock.patch.object(reach, "offered_starts", return_value=starts):
            world = fake_world(start_room=0)
            with self.assertLogs(level="WARNING"):
                world.generate_early()
            self.assertIn(world.start, starts)

    def test_settings_that_offer_no_room_are_refused(self) -> None:
        with (
            mock.patch.object(reach, "offered_starts", return_value=[]),
            self.assertRaisesRegex(OptionError, "no start room"),
        ):
            fake_world().generate_early()

    def test_settings_that_do_not_reach_the_cores_door_are_refused(self) -> None:
        door = data.CELL_REGION[data.CORE["door"]]
        real = reach.Seed.every

        def without_door(seed: reach.Seed, *starts: tuple[int, int]) -> reach.Reach:
            got = real(seed, *starts)
            return reach.Reach(got.regions - {door}, got.locations)

        with (
            mock.patch.object(reach.Seed, "every", without_door),
            self.assertRaisesRegex(OptionError, "core's door"),
        ):
            fake_world().generate_early()

    def test_every_room_random_can_roll_gives_a_start_of_the_mode(self) -> None:
        # 'random' rolls any of StartRoom's values, in any mode.
        for mode, name in Mode.name_lookup.items():
            offered = {s["cell"] for s in data.MODES[name]["starts"]}
            for value in StartRoom.name_lookup:
                world = fake_world(mode=mode, start_room=value)
                with self.subTest(mode=name, room=value):
                    with mock.patch("logging.warning"):  # for a room not offered
                        world.generate_early()
                    self.assertIn(world.start["cell"], offered)
                    if world.second is not None:
                        self.assertIn(world.second["cell"], offered)
                        self.assertNotEqual(world.start["cell"], world.second["cell"])


class TestNames(unittest.TestCase):
    def test_location_names_give_the_area_and_the_games_x_then_y(self) -> None:
        # Cell (7, 22), where the owner read X22, Y7 in Guardia (docs/re/locations.md).
        self.assertEqual(location_name(data.LOCATIONS.index((7, 22))), "Guardia 22,7")
        self.assertEqual(location_name(data.LOCATIONS.index((1, 22))), "Exodia 22,1")

    def test_location_names_are_unique(self) -> None:
        names = [location_name(i) for i in range(len(data.LOCATIONS))]
        self.assertEqual(len(set(names)), len(names))

    def test_start_rooms_are_named_the_same_way(self) -> None:
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
        start, second, seed = self.world.start, self.world.second, self.world.seed_reach
        both = seed.sphere0(start["cell"]).locations | seed.sphere0(second["cell"]).locations
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

    def test_left_out_locations_hold_scrap_metal(self) -> None:
        items = self.world.fill_slot_data()["location_items"]
        left_out = set(range(len(data.LOCATIONS))) - self.world.kept_locations
        self.assertTrue(left_out)
        self.assertEqual({items[i] for i in left_out}, {SCRAP_METAL})


class TestSlotDataForTheClient(ZexionTestBase):
    """The slot data the client builds its plan and new game from."""

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

    def test_another_zexion_players_supplies_are_minor_like_this_players_own(self) -> None:
        # Progression for the rules' counts, but the game marks supplies minor on the map.
        other = self.player + 1
        ids = self.world.item_name_to_id
        skip_balancing = ItemClassification.progression_skip_balancing
        placed = [
            ZexionItem("Missile Container", skip_balancing, ids["Missile Container"], other),
            ZexionItem("Energy Tank", skip_balancing, ids["Energy Tank"], other),
            ZexionItem("Navcore", ItemClassification.progression, ids["Navcore"], other),
            # Another game's item that happens to share a supply's name: not a Zexion supply.
            Item("Energy Tank", ItemClassification.progression, 1, other),
        ]
        for i, item in enumerate(placed):
            self.multiworld.get_location(location_name(i), self.player).item = item
        items = self.world.fill_slot_data()["location_items"]
        self.assertEqual(items[:4], [FOREIGN, FOREIGN, FOREIGN_PROGRESSION, FOREIGN_PROGRESSION])

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

    def test_a_second_progressive_copy_is_the_upgrade(self) -> None:
        # As AP's Main does for start_inventory, which the test base does not run.
        for name in ("Progressive Snake", "Progressive Snake", "Missile Box"):
            self.multiworld.push_precollected(self.world.create_item(name))
        start = {int(k): v for k, v in self.world.fill_slot_data()["start_items"].items()}
        self.assertEqual((start.get(5), start.get(15), start.get(36)), (1, 1, 1))


class TestAreaMapHints(ZexionTestBase):
    """Area Maps are hints by default: Intel Chips, which the game holds as the Area Map, and
    the slot data lists where this player's progression items are (client/hints.py)."""

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
        codes = {code for _, _, code in hints}
        self.assertTrue({27, 28, 29, 30} <= codes, "the four keys are hinted")
        self.assertFalse(codes & NOT_HINTED)

    def test_an_item_links_items_are_hinted_to_its_members(self) -> None:
        # The group's item goes to every member, so it is this player's to find.
        mine, other = self.player + 1, self.player + 2
        self.multiworld.groups[mine] = {"players": {self.player, other}}
        self.multiworld.groups[other] = {"players": {other}}
        ids = self.world.item_name_to_id
        progression = ItemClassification.progression
        placed = [
            ZexionItem("Slide Mod", progression, ids["Slide Mod"], mine),
            ZexionItem("Phaser", progression, ids["Phaser"], other),
        ]
        locations = [self.multiworld.get_location(location_name(i), self.player) for i in (0, 1)]
        for location, item in zip(locations, placed, strict=True):
            location.place_locked_item(item)
        hints = self.world.fill_slot_data()["hints"]
        self.assertIn([self.player, locations[0].address, code_of["Slide Mod"]], hints)
        self.assertNotIn(locations[1].address, [address for _, address, _ in hints])

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
        own = own_locations(self, "Intel Chip")
        self.assertTrue(own)
        self.assertEqual({slot["location_items"][i] for i in own}, {49})
        self.assertNotIn(101, slot["location_items"])


class TestItemLinkHints(unittest.TestCase):
    """A real item link between two Zexion players (item_links, then AP's link_items): the
    group's items, wherever they land, are in both members' hints."""

    def test_the_groups_items_are_hinted_to_both_members(self) -> None:
        pool = ["Slide Mod", "Phaser", "Wall Jump Boots"]
        link = {"name": "Linked", "item_pool": pool, "replacement_item": None}
        multiworld = two_zexion_players({"item_links": [{**link, "link_replacement": True}]})
        multiworld.link_items()
        multiworld._all_state = None  # as the generator does after linking
        distribute_items_restrictive(multiworld)
        (group,) = multiworld.groups
        linked = {
            (loc.player, loc.address, code_of[loc.item.name])
            for loc in multiworld.get_filled_locations()
            if loc.item.player == group and loc.item.advancement and loc.address is not None
        }
        self.assertEqual({code for _, _, code in linked}, {code_of[name] for name in pool})
        for player in (1, 2):
            hints = {tuple(h) for h in multiworld.worlds[player].fill_slot_data()["hints"]}
            self.assertLessEqual(linked, hints, multiworld.player_name[player])


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


class TestExcludedLocationsNeedFiller(ZexionTestBase):
    """Standard with 16 disks keeps 9 filler items, the Intel Chips: a tenth excluded location
    would stop AP's fill, so the world refuses it first, naming the options."""

    options = disks(placed=16, required=12)

    def exclude(self, n: int) -> list:
        # Excluded as the generator excludes one (worlds.generic.Rules.exclusion_rules).
        kept = sorted(self.world.kept_locations)[:n]
        out = [self.multiworld.get_location(location_name(i), self.player) for i in kept]
        for location in out:
            location.progress_type = LocationProgressType.EXCLUDED
        return out

    def test_as_many_excluded_as_filler_left_fill_with_it(self) -> None:
        excluded = self.exclude(9)
        ZexionWorld.stage_pre_fill(self.multiworld)
        distribute_items_restrictive(self.multiworld)
        kinds = {location.item.classification for location in excluded}
        self.assertEqual(kinds, {ItemClassification.filler})

    def test_one_more_is_refused_with_the_options_named(self) -> None:
        self.exclude(10)
        with self.assertRaises(OptionError) as caught:
            ZexionWorld.stage_pre_fill(self.multiworld)
        message = str(caught.exception)
        self.assertIn("10 excluded locations, but only 9 filler items", message)
        self.assertIn("OS Install Disks (16", message)
        self.assertIn("exclude_locations", message)

    def test_another_players_filler_makes_up_the_difference(self) -> None:
        self.exclude(10)
        other = Item("Something", ItemClassification.filler, 1, self.player + 1)
        self.multiworld.itempool.append(other)
        ZexionWorld.stage_pre_fill(self.multiworld)


class TestExcludedLocationsNameThePlayerOver(unittest.TestCase):
    """Two Zexion players with 9 filler items each: Alpha excludes 9 locations, as many as
    their filler, and Beta 10. The multiworld is one short, and the error names Beta."""

    def test_the_player_over_is_named_not_the_one_at_their_filler(self) -> None:
        multiworld = two_zexion_players(disks(placed=16, required=12))
        for player, n in ((1, 9), (2, 10)):
            kept = sorted(multiworld.worlds[player].kept_locations)[:n]
            for i in kept:
                location = multiworld.get_location(location_name(i), player)
                location.progress_type = LocationProgressType.EXCLUDED
        with self.assertRaises(OptionError) as caught:
            ZexionWorld.stage_pre_fill(multiworld)
        message = str(caught.exception)
        self.assertIn("Zexion (Beta): 10 excluded locations, but only 9 filler items", message)


class TestAssistUnlockStaysOutOfItsOwnWorld(ZexionTestBase):
    """An Assist Unlock at this player's own location would be handed out as Scrap Metal and
    never received, so the assist lock would never lift: an item rule keeps it out (plando
    places through the same rule). Sent from the server console, it is just an item ID."""

    def test_only_this_players_own_assist_unlock_is_refused(self) -> None:
        state = self.multiworld.state
        mine = self.world.create_item("Assist Unlock")
        theirs = ZexionItem(mine.name, mine.classification, mine.code, self.player + 1)
        scrap = self.world.create_item("Scrap Metal")
        for location in self.multiworld.get_locations(self.player):
            if location.address is None:
                continue
            with self.subTest(location.name):
                self.assertFalse(location.can_fill(state, mine, check_access=False))
                self.assertTrue(location.can_fill(state, theirs, check_access=False))
                self.assertTrue(location.can_fill(state, scrap, check_access=False))

    def test_the_server_console_can_still_send_it(self) -> None:
        # /send looks the name up in the game's item names (MultiServer, _cmd_send_multiple).
        self.assertIn("Assist Unlock", self.world.item_name_to_id)


def pool_names(test: ZexionTestBase) -> list[str]:
    return [i.name for i in test.multiworld.itempool if i.player == test.player]


def own_locations(test: ZexionTestBase, name: str) -> list[int]:
    """The location indices holding this player's item ``name`` (one player in these tests)."""
    found = test.multiworld.find_item_locations(name, test.player)
    return sorted(loc.address - LOCATION_ID_BASE for loc in found)


class TestDiscHuntStandard(ZexionTestBase):
    options = disks()

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
        regions = [r.name for r in self.multiworld.get_regions(self.player)]
        self.assertNotIn(CORE_REGION, regions)
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
        own = own_locations(self, "OS Install Disk")
        self.assertTrue(own)
        self.assertEqual(slot["disc_locations"], own)
        self.assertEqual({slot["location_items"][i] for i in own}, {FOREIGN_PROGRESSION})
        self.assertNotIn(44, slot["location_items"], "the game never hands out a disc")
        self.assertEqual(
            (slot["goal"], slot["discs_required"], slot["discs_placed"]),
            ("os_install_disks", 15, 20),
        )
        self.assertEqual(slot["discs_start"], 0)

    def test_a_starting_disc_is_counted_not_written(self) -> None:
        self.multiworld.push_precollected(self.world.create_item("OS Install Disk"))
        slot = self.world.fill_slot_data()
        self.assertEqual(slot["discs_start"], 1)
        self.assertNotIn("44", slot["start_items"])


class TestDiscHuntStandardFull(ZexionTestBase):
    options = disks(placed=30, required=30)  # the options' most

    def test_every_area_map_and_most_energy_shields_give_way(self) -> None:
        pool = pool_names(self)
        self.assertEqual(pool.count("OS Install Disk"), 30)
        self.assertNotIn("Intel Chip", pool)
        self.assertEqual(pool.count("Energy Shield"), 1)


class TestDiscHuntEasyFull(TestDiscHuntStandardFull):
    options = disks("easy", 30, 30)


class TestDiscHuntOHKO(ZexionTestBase):
    options = disks("ohko", 30, 30)

    def test_ohko_fits_30_in_scrap_after_its_left_out_locations(self) -> None:
        pool = pool_names(self)
        self.assertEqual(pool.count("OS Install Disk"), 30)
        self.assertEqual(pool.count("Intel Chip"), 5, "no hint given up")
        self.assertEqual(len(pool), len(self.world.kept_locations))


class TestDiscHuntEasy(ZexionTestBase):
    options = disks("easy", 10, 10)

    def test_ten_discs_take_scrap_only(self) -> None:
        pool = pool_names(self)
        self.assertEqual(pool.count("OS Install Disk"), 10)
        self.assertEqual(pool.count("Intel Chip"), 4)
        self.assertEqual(pool.count("Scrap Metal"), 20 + 4 - 10)


class TestDiscOptionsChecked(unittest.TestCase):
    def world(self, mode: int, placed: int, required: int) -> ZexionWorld:
        return fake_world(mode, goal=Goal.option_os_install_disks, placed=placed, required=required)

    def test_more_required_than_placed_is_an_error(self) -> None:
        with self.assertRaises(OptionError):
            self.world(1, 10, 11).generate_early()

    def test_starting_disks_count_toward_the_required(self) -> None:
        def world(required: int) -> ZexionWorld:
            return fake_world(
                goal=Goal.option_os_install_disks,
                start_inventory={"OS Install Disk": 5},
                placed=10,
                required=required,
            )

        world(15).generate_early()  # 10 placed and 5 to start with
        with self.assertRaises(OptionError) as caught:
            world(16).generate_early()
        self.assertIn("plus the 5 in start_inventory", str(caught.exception))

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
