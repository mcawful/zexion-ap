"""Item plando (plando_items) through Archipelago's own plando, and the world's checks of it
(worlds/zexion/plando.py)."""

import contextlib
import unittest
from collections import Counter
from types import SimpleNamespace
from typing import Any

from BaseClasses import CollectionState, MultiWorld, PlandoOptions
from Fill import (
    FillError,
    distribute_items_restrictive,
    distribute_planned_blocks,
    parse_planned_blocks,
    resolve_early_locations_for_planned,
)
from Options import OptionError
from test.general import setup_multiworld
from worlds.AutoWorld import World, call_all
from worlds.generic.Rules import exclusion_rules, locality_rules

from .. import plando
from ..custom import MAX_AMMO, MAX_ENERGY
from ..items import AREA_MAP, DATA_DISC, INTEL_CHIP, code_of
from ..locations import LOCATION_ID_BASE, location_name, location_name_groups
from ..world import FOREIGN, FOREIGN_PROGRESSION, ZexionWorld

NO_OPTION = SimpleNamespace(value={})  # a start option a world does not have
# The three locations Landing Pod's starting items reach in Easy (the default start room's).
POD = ["Kresezoa 25,25", "Machinus 27,24", "Machinus 24,29"]


def generate(
    options: list[dict[str, Any]], worlds: list[type[World]] | None = None, seed: int = 1
) -> MultiWorld:
    """Players with these options (Zexion players, or ``worlds``), set up as Archipelago's
    Main.py does up to the fill: the start options' items precollected, item links, locality
    and exclusion, the plando read (with its groups filled in, as Generate.py's option check
    does) and placed, then pre_fill. The test base runs none of the plando steps."""
    worlds = worlds or [ZexionWorld] * len(options)
    multiworld = setup_multiworld(worlds, steps=(), seed=seed, options=options)
    for player, world in multiworld.worlds.items():
        world.options.plando_items.verify(type(world), f"Tester{player}", PlandoOptions.items)
    multiworld.set_item_links()
    multiworld.state = CollectionState(multiworld)
    call_all(multiworld, "generate_early")
    for player, world in multiworld.worlds.items():
        for option in ("start_inventory", "start_inventory_from_pool"):
            for name, n in getattr(world.options, option, NO_OPTION).value.items():
                for _ in range(n):
                    multiworld.push_precollected(multiworld.create_item(name, player))
    for step in ("create_regions", "create_items", "set_rules"):
        call_all(multiworld, step)
    for player, world in multiworld.worlds.items():
        exclusion_rules(multiworld, player, world.options.exclude_locations.value)
    if multiworld.players > 1:
        locality_rules(multiworld)
    multiworld.plando_item_blocks = parse_planned_blocks(multiworld)
    call_all(multiworld, "connect_entrances")
    call_all(multiworld, "generate_basic")
    remove = {
        p: Counter(getattr(w.options, "start_inventory_from_pool", NO_OPTION).value)
        for p, w in multiworld.worlds.items()
    }
    pool = []
    for item in multiworld.itempool:
        if remove[item.player][item.name] > 0:
            remove[item.player][item.name] -= 1
            pool.append(multiworld.worlds[item.player].create_filler())
        else:
            pool.append(item)
    multiworld.itempool[:] = pool
    multiworld.link_items()
    resolve_early_locations_for_planned(multiworld)
    distribute_planned_blocks(
        multiworld, [block for blocks in multiworld.plando_item_blocks.values() for block in blocks]
    )
    call_all(multiworld, "pre_fill")
    return multiworld


def block(items: dict[str, int], locations: list[str], **more: Any) -> dict[str, Any]:
    """A plando_items block, forced, its items from the pool unless ``more`` says otherwise."""
    return {"items": items, "locations": locations, "from_pool": True, "force": True, **more}


def index_of(name: str) -> int:
    return ZexionWorld.location_name_to_id[name] - LOCATION_ID_BASE


class PlandoTestCase(unittest.TestCase):
    def assert_beatable(self, multiworld: MultiWorld) -> None:
        distribute_items_restrictive(multiworld)
        self.assertTrue(multiworld.can_beat_game(CollectionState(multiworld)))
        self.assertTrue(multiworld.fulfills_accessibility())

    def refused(self, options: list[dict[str, Any]], **kwargs: Any) -> str:
        """The error the world refuses these players with, after plando."""
        with self.assertRaises(OptionError) as caught:
            generate(options, **kwargs)
        return str(caught.exception)


class TestPlandoPlaces(PlandoTestCase):
    def test_an_own_item_in_an_own_location(self) -> None:
        # The Gravity Harness at Kresezoa 25,25, by name: there, and written there as itself.
        # Kresezoa 25,25 is in the start the world opens: the opener counts there.
        options = {"mode": "easy", "plando_items": [block({"Gravity Harness": 1}, POD[:1])]}
        multiworld = generate([options])
        world = multiworld.worlds[1]
        location = world.get_location(POD[0])
        self.assertEqual((location.item.name, location.item.player), ("Gravity Harness", 1))
        self.assertTrue(location.locked)
        self.assert_beatable(multiworld)
        self.assertEqual(world.location_items()[index_of(POD[0])], code_of["Gravity Harness"])
        # Moved from the pool: one Gravity Harness in all.
        self.assertEqual(plando.own_items(world)[code_of["Gravity Harness"]], 1)

    def test_an_own_item_in_another_zexion_players_location(self) -> None:
        # Alpha's Gravity Harness and Missile Box in Beta's world: stand-ins there, the
        # harness's raised (progression), the box's not (another Zexion player's supply).
        harness = block({"Gravity Harness": 1}, ["Machinus 24,29"], world="Tester2")
        box = block({"Missile Box": 1}, ["Machinus 27,24"], world="Tester2")
        options = [{"mode": "easy", "plando_items": [harness, box]}, {"mode": "easy"}]
        multiworld = generate(options)
        beta = multiworld.worlds[2]
        for name, item in (
            ("Machinus 24,29", "Gravity Harness"),
            ("Machinus 27,24", "Missile Box"),
        ):
            location = beta.get_location(name)
            self.assertEqual((location.item.name, location.item.player), (item, 1))
        self.assert_beatable(multiworld)
        items = beta.location_items()
        self.assertEqual(items[index_of("Machinus 24,29")], FOREIGN_PROGRESSION)
        self.assertEqual(items[index_of("Machinus 27,24")], FOREIGN)
        mine = [loc for loc in multiworld.get_locations(1) if loc.item.player == 1]
        self.assertNotIn("Gravity Harness", {loc.item.name for loc in mine})

    def test_another_games_item_in_a_zexion_location(self) -> None:
        # A ChecksFinder item (progression) plando'd into the Zexion player's Kresezoa 25,25.
        from worlds.checksfinder import ChecksFinderWorld

        finder = {"plando_items": [block({"Map Width": 1}, POD[:1], world="Tester2")]}
        multiworld = generate([finder, {"mode": "easy"}], worlds=[ChecksFinderWorld, ZexionWorld])
        zexion = multiworld.worlds[2]
        location = zexion.get_location(POD[0])
        self.assertEqual((location.item.name, location.item.player), ("Map Width", 1))
        self.assert_beatable(multiworld)
        self.assertEqual(zexion.location_items()[index_of(POD[0])], FOREIGN_PROGRESSION)

    def test_disks_in_another_world_with_the_disks_goal(self) -> None:
        # Three of Alpha's six disks in Beta's world, one in Alpha's own: the goal's count and
        # slot data unchanged, the own one a stand-in in Alpha's disc_locations.
        away = ["Kresezoa 25,25", "Machinus 24,29", "Vallahar 6,14"]
        options = {
            "mode": "easy",
            "goal": "os_install_disks",
            "os_install_disks": 6,
            "os_install_disks_required": 4,
            "plando_items": [
                block({"OS Install Disk": 3}, away, world="Tester2"),
                block({"OS Install Disk": 1}, ["Machinus 27,24"]),
            ],
        }
        multiworld = generate([options, {"mode": "easy"}])
        alpha, beta = multiworld.worlds[1], multiworld.worlds[2]
        for name in away:
            self.assertEqual(beta.get_location(name).item.name, "OS Install Disk")
        self.assert_beatable(multiworld)
        self.assertEqual(plando.own_items(alpha)[DATA_DISC], 6)
        slot = alpha.fill_slot_data()
        self.assertEqual(slot["discs_placed"], 6)
        self.assertIn(index_of("Machinus 27,24"), slot["disc_locations"])
        self.assertEqual(alpha.location_items()[index_of("Machinus 27,24")], FOREIGN_PROGRESSION)
        for name in away:
            self.assertNotIn(index_of(name), slot["disc_locations"])
            self.assertEqual(beta.location_items()[index_of(name)], FOREIGN_PROGRESSION)

    def test_name_groups(self) -> None:
        # An item group in items (Generate.py's check draws one of it) and an area's group in
        # locations.
        options = {"mode": "easy", "plando_items": [block({"Weapons": 1}, ["Machinus"])]}
        multiworld = generate([options])
        world = multiworld.worlds[1]
        placed = [loc for loc in multiworld.get_locations(1) if loc.locked and loc.address]
        self.assertEqual(len(placed), 1)
        self.assertIn(placed[0].item.name, ZexionWorld.item_name_groups["Weapons"])
        self.assertIn(placed[0].name, location_name_groups["Machinus"])
        self.assert_beatable(multiworld)
        self.assertFalse(world.plando_problems())


class TestPlandoAndTheStart(PlandoTestCase):
    """The world opens the start after plando (ZexionWorld.open_the_start): a plando'd item
    there counts, and a start plando fills leaves it nothing to place in."""

    # A plando'd item in the start: TestPlandoPlaces.test_an_own_item_in_an_own_location.

    def test_a_start_filled_by_plando_warns(self) -> None:
        # Scrap Metal in all three of Landing Pod's start locations.
        options = {"mode": "easy", "plando_items": [block({"Scrap Metal": 3}, POD)]}
        multiworld = generate([options])
        with self.assertLogs(level="WARNING") as logs, contextlib.suppress(FillError):
            distribute_items_restrictive(multiworld)
        text = "every one of those locations is excluded or already filled (plando)"
        self.assertTrue(any(text in line for line in logs.output), logs.output)

    def test_with_random_start(self) -> None:
        # The start room is drawn before plando; a plando'd item in a location of any room.
        options = {
            "mode": "ohko",
            "start_room": "random_start",
            "plando_items": [block({"Gravity Harness": 1}, ["Vallahar 6,14"])],
        }
        multiworld = generate([options], seed=3)
        self.assertEqual(
            multiworld.worlds[1].get_location("Vallahar 6,14").item.name, "Gravity Harness"
        )
        self.assert_beatable(multiworld)


class TestPlandoRefused(PlandoTestCase):
    """What the world refuses after plando (plando.problems), end to end."""

    def test_a_second_copy_of_an_item_held_once(self) -> None:
        extra = block({"Wall Jump Boots": 1}, ["Vallahar 6,14"], from_pool=False)
        message = self.refused([{"mode": "easy", "plando_items": [extra]}])
        self.assertIn("Zexion (Tester1): plando_items gives items the game does not hold", message)
        self.assertIn("2 Wall Jump Boots: the game holds one", message)

    def test_a_third_progressive_weapon(self) -> None:
        extra = block({"Progressive Lance": 1}, ["Vallahar 6,14"], from_pool=False)
        message = self.refused([{"mode": "easy", "plando_items": [extra]}])
        self.assertIn("3 Progressive Lance: a weapon and its upgrade are 2", message)

    def test_an_upgrade_with_guns_first(self) -> None:
        # The pool holds none, so Archipelago makes one.
        extra = block({"Pentashot": 1}, ["Vallahar 6,14"])
        message = self.refused([{"mode": "easy", "plando_items": [extra]}])
        self.assertIn("Pentashot: with Guns First on, the upgrade is a second Progressive", message)

    def test_disks_with_the_core_goal(self) -> None:
        extra = block({"OS Install Disk": 1}, ["Vallahar 6,14"])
        message = self.refused([{"mode": "easy", "plando_items": [extra]}])
        self.assertIn("1 OS Install Disk: only the OS Install Disks goal counts them", message)

    def test_more_disks_than_the_options_make(self) -> None:
        extra = block({"OS Install Disk": 1}, ["Vallahar 6,14"], from_pool=False)
        options = {
            "mode": "easy",
            "goal": "os_install_disks",
            "os_install_disks": 6,
            "os_install_disks_required": 4,
            "plando_items": [extra],
        }
        message = self.refused([options])
        self.assertIn("7 OS Install Disks, more than the 6 the options make", message)

    def test_an_area_map_with_area_map_hints(self) -> None:
        extra = block({"Area Map": 1}, ["Vallahar 6,14"])
        message = self.refused([{"mode": "easy", "plando_items": [extra]}])
        self.assertIn("Area Map: with Area Map Hints on, the Area Maps are Intel Chips", message)

    def test_an_intel_chip_without_area_map_hints(self) -> None:
        extra = block({"Intel Chip": 1}, ["Vallahar 6,14"])
        options = {"mode": "easy", "area_map_hints": False, "plando_items": [extra]}
        message = self.refused([options])
        self.assertIn(
            "Intel Chip: with Area Map Hints off, an Intel Chip would hint nothing", message
        )

    def test_an_area_map_in_another_world_without_area_map_hints(self) -> None:
        # Kept in its own world (local_items), as the fill keeps it: not placed, and with
        # `force: true` the generator stops, after the world's warning says why.
        away = block({"Area Map": 1}, ["Vallahar 6,14"], world="Tester2")
        options = [{"mode": "easy", "area_map_hints": False, "plando_items": [away]}]
        with self.assertLogs(level="WARNING") as logs, self.assertRaises(Exception) as caught:
            generate([*options, {"mode": "easy"}])
        self.assertIn("Could not place", str(caught.exception.__cause__))
        want = "Zexion (Tester1): plando_items puts an Area Map in another world, but Area Map"
        self.assertTrue(any(want in line for line in logs.output), logs.output)

    def test_an_area_map_in_another_world_silently(self) -> None:
        # force: silent asks for no warning: Archipelago logs only at debug, and so does the world.
        away = block({"Area Map": 1}, ["Vallahar 6,14"], world="Tester2", force="silent")
        options = {"mode": "easy", "area_map_hints": False, "plando_items": [away]}
        world = generate([options, {"mode": "easy"}]).worlds[1]
        self.assertEqual(plando.area_maps_away(world), [])

    def test_an_area_map_in_its_own_world_without_area_map_hints(self) -> None:
        own = block({"Area Map": 1}, ["Vallahar 6,14"])
        options = {"mode": "easy", "area_map_hints": False, "plando_items": [own]}
        world = generate([options, {"mode": "easy"}]).worlds[1]
        self.assertEqual(world.get_location("Vallahar 6,14").item.name, "Area Map")
        self.assertEqual(plando.area_maps_away(world), [])

    def test_too_many_energy_tanks_in_another_world(self) -> None:
        # Copies made for another player's locations count too: Alpha's Easy seed with enough
        # Energy Tanks plando'd into Beta's world to pass 40 in all.
        probe = generate([{"mode": "easy"}])
        tanks = plando.own_items(probe.worlds[1])[code_of["Energy Tank"]]
        extra = MAX_ENERGY + 1 - tanks
        names = sorted(loc.name for loc in probe.get_locations(1) if loc.address)[:extra]
        flood = block({"Energy Tank": extra}, names, world="Tester2", from_pool=False)
        message = self.refused([{"mode": "easy", "plando_items": [flood]}, {"mode": "easy"}])
        self.assertIn(f"{MAX_ENERGY + 1} Energy Tanks, the starting ones included", message)


class TestNoPlando(PlandoTestCase):
    def test_start_inventory_past_the_limits_is_not_plandos(self) -> None:
        # 35 Energy Tanks and 90 Missile Boxes on top of Easy's pool, which passes 40 tanks and
        # 999 missiles in all, as the start options allow (worlds/zexion/starting.py counts the
        # starting items alone): no plando, nothing refused here.
        start = {"Energy Tank": 35, "Missile Box": 90}
        world = generate([{"mode": "easy", "start_inventory": start}]).worlds[1]
        counts = plando.own_items(world)
        self.assertGreater(counts[code_of["Energy Tank"]], MAX_ENERGY)
        self.assertEqual(world.plando_problems(), [])
        # start_inventory_from_pool: the pool's items moved, Scrap Metal in their places.
        moved = {"mode": "easy", "start_inventory_from_pool": {"Slide Mod": 1, "Energy Tank": 2}}
        world = generate([moved]).worlds[1]
        self.assertEqual(world.made_items, plando.own_items(world))
        disks = {
            "mode": "easy",
            "goal": "os_install_disks",
            "os_install_disks": 10,
            "os_install_disks_required": 10,
            "start_inventory_from_pool": {"OS Install Disk": 2},
        }
        world = generate([disks]).worlds[1]
        self.assertEqual(world.made_items[DATA_DISC], 10)
        self.assertEqual(world.plando_problems(), [])


def linked(pool: list[str], replacement: str | None, link_replacement: bool) -> dict[str, Any]:
    """Easy, in item link "L" with these settings."""
    link = {
        "name": "L",
        "item_pool": pool,
        "replacement_item": replacement,
        "link_replacement": link_replacement,
    }
    return {"mode": "easy", "item_links": [link]}


class TestItemLinks(PlandoTestCase):
    """An item link's replacement items, made after generate_basic (plando.linked_items), are
    the links', not plando's."""

    def test_replacements_are_not_refused(self) -> None:
        # Two players linking an item held once or supplies, the replacement a second copy of
        # an item held once or more supplies: generated with no refusal, as 1.0.0 did.
        cases = [
            (["Gravity Harness"], "Wall Jump Boots"),
            (["Missile Box", "Bomb Box"], "Energy Tank"),
            (["Energy Tank"], "Navcore"),
        ]
        for pool, replacement in cases:
            for link_replacement in (False, True):
                with self.subTest(pool=pool, replacement=replacement, link=link_replacement):
                    options = linked(pool, replacement, link_replacement)
                    multiworld = generate([options, options])
                    worlds = [multiworld.worlds[1], multiworld.worlds[2]]
                    for world in worlds:
                        self.assertEqual(world.plando_problems(), [])
                    code = code_of[replacement]
                    extra = sum(plando.own_items(w)[code] - w.made_items[code] for w in worlds)
                    # The link's copies are the players' with link_replacement off.
                    self.assertEqual(extra > 0, not link_replacement)
        multiworld = generate([linked(*cases[0], False)] * 2)
        self.assert_beatable(multiworld)

    def test_plando_past_a_limit_is_still_refused(self) -> None:
        # The link adds at most one Wall Jump Boots each; two more by plando pass that, and a
        # second Navcore is plando's alone.
        for extra, line in (
            ({"Wall Jump Boots": 2}, "Wall Jump Boots: the game holds one"),
            ({"Navcore": 1}, "2 Navcore: the game holds one"),
        ):
            with self.subTest(extra=extra):
                names = ["Vallahar 6,14", "Machinus 27,24"][: sum(extra.values())]
                more = block(extra, names, from_pool=False)
                options = linked(["Gravity Harness"], "Wall Jump Boots", False)
                message = self.refused([{**options, "plando_items": [more]}, options])
                self.assertIn("Zexion (Tester1): plando_items gives items", message)
                self.assertIn(line, message)


class TestPlandoProblems(unittest.TestCase):
    """plando.problems on its own: the limits."""

    def problems(
        self, counts: dict[str, int], made: dict[str, int] | None = None, **settings: Any
    ) -> list[str]:
        """plando.problems for these counts by name, the world having ``made`` none of them
        unless given."""
        base = {"guns_first": True, "disc_hunt": False, "area_map_hints": True}

        def codes(by_name: dict[str, int]) -> Counter[int]:
            return Counter({code_of[n]: c for n, c in by_name.items()})

        return plando.problems(codes(counts), codes(made or {}), **{**base, **settings})

    def test_the_counts(self) -> None:
        self.assertEqual(self.problems({"Energy Tank": MAX_ENERGY, "Missile Box": 99}), [])
        self.assertEqual(self.problems({"Missile Container": 9, "Missile Box": 99}), [])
        (line,) = self.problems({"Missile Container": 10, "Missile Box": 99})
        self.assertIn(f"{MAX_AMMO + 1} missiles (10 per Missile Box)", line)
        (line,) = self.problems({"Mega Bomb": MAX_AMMO + 1})
        self.assertIn(f"{MAX_AMMO + 1} Mega Bombs (10 per Bomb Box)", line)
        many = {"Scrap Metal": 200, "Scrap Energy": 9, "Energy Shield": 9, "Intel Chip": 9}
        self.assertEqual(self.problems({**many, "Assist Unlock": 3}), [])

    def test_held_once(self) -> None:
        self.assertEqual(self.problems({"Navcore": 1, "Progressive Snake": 2}), [])
        (line,) = self.problems({"Navcore": 2})
        self.assertEqual(line, "2 Navcore: the game holds one, so the others would give nothing")
        # With Guns First off, a weapon and an upgrade once each.
        off = {"guns_first": False}
        self.assertEqual(self.problems({"Progressive Snake": 1, "Tunneler": 1}, **off), [])
        self.assertEqual(len(self.problems({"Progressive Snake": 2, "Tunneler": 2}, **off)), 2)

    def test_disks_and_maps(self) -> None:
        made = {"OS Install Disk": 20}
        self.assertEqual(self.problems(made, made, disc_hunt=True), [])
        (line,) = self.problems({"OS Install Disk": 21}, made, disc_hunt=True)
        self.assertIn("21 OS Install Disks, more than the 20 the options make", line)
        self.assertEqual(self.problems({"Area Map": 3}, area_map_hints=False), [])
        self.assertEqual(len(self.problems({"Area Map": 1, "Intel Chip": 1})), 1)

    def test_only_what_plando_added(self) -> None:
        # Past a limit already without plando (start_inventory's supplies on top of the pool):
        # not refused; more on top of that: refused.
        made = {"Energy Tank": 50, "Missile Box": 120}
        self.assertEqual(self.problems(made, made), [])
        self.assertEqual(len(self.problems({"Energy Tank": 51, "Missile Box": 121}, made)), 2)

    def test_every_preset_and_goal_holds(self) -> None:
        # What the world makes, with no plando, never trips the check.
        for mode in ("easy", "standard", "ohko"):
            for goal in ("core", "os_install_disks"):
                for hints in (True, False):
                    with self.subTest(mode=mode, goal=goal, area_map_hints=hints):
                        options = {"mode": mode, "goal": goal, "area_map_hints": hints}
                        world = generate([options]).worlds[1]
                        counts = plando.own_items(world)
                        self.assertFalse(world.plando_problems())
                        self.assertEqual(counts[AREA_MAP] > 0, not hints)
                        self.assertEqual(counts[INTEL_CHIP] > 0, hints)


class TestPlandoLeftOutLocations(unittest.TestCase):
    """A plando naming one of this world's locations the seed does not have: a warning that says
    why (plando.left_out), then Archipelago's own handling (with force: true, its error)."""

    def warnings(self, options: dict[str, Any], name: str) -> list[str]:
        options = {**options, "plando_items": [block({"Scrap Metal": 1}, [name], force=False)]}
        with self.assertLogs(level="WARNING") as logs:
            generate([options])
        return [line for line in logs.output if "plando_items names" in line]

    def test_secret_shuffles_location(self) -> None:
        name = location_name(154)
        (line,) = self.warnings({"mode": "easy"}, name)
        self.assertIn(f"Tester1's plando_items names {name}, Secret Shuffle's location", line)

    def test_a_left_out_location(self) -> None:
        # OHKO leaves out the locations its starts do not reach.
        probe = generate([{"mode": "ohko"}]).worlds[1]
        index = min(set(range(len(probe.location_cells))) - probe.kept_locations)
        (line,) = self.warnings({"mode": "ohko"}, location_name(index))
        self.assertIn("which this seed leaves out", line)

    def test_a_location_another_world_of_the_plando_has(self) -> None:
        # An OHKO and an Easy player; the Easy one plandos into any world a location OHKO leaves
        # out and Easy has: Archipelago places it in the Easy world, with no warning.
        probe = generate([{"mode": "ohko"}, {"mode": "easy"}])
        ohko, easy = probe.worlds[1], probe.worlds[2]
        cells = set(range(len(ohko.location_cells)))
        name = location_name(min((cells - ohko.kept_locations) & easy.kept_locations))
        harness = block({"Gravity Harness": 1}, [name], world=None)
        multiworld = generate([{"mode": "ohko"}, {"mode": "easy", "plando_items": [harness]}])
        self.assertEqual(plando.left_out(multiworld.worlds[1]), [])
        location = multiworld.worlds[2].get_location(name)
        self.assertEqual((location.item.name, location.item.player), ("Gravity Harness", 2))
        # Aimed at the OHKO world alone, the warning stays.
        harness = block({"Gravity Harness": 1}, [name], world="Tester1", force=False)
        multiworld = generate([{"mode": "ohko"}, {"mode": "easy", "plando_items": [harness]}])
        (line,) = plando.left_out(multiworld.worlds[1])
        self.assertIn(f"Tester2's plando_items names {name}, which this seed leaves out", line)
