"""Double Teleport's second target and the Gates setting, as the game's logic has them
(docs/custom-mode.md, "The build plan", step D; docs/re/randocells.md, "Starting Location and
the second teleport target" and "What each setting does in the game")."""

import dataclasses
import unittest
from unittest import mock

from .. import custom, data, logic, reach
from ..world import ZexionWorld
from .bases import ZexionTestBase
from .test_world import fake_world

FLAGGED = {i for i, (_, _, flag) in enumerate(data.START_TABLE) if flag}


def preset(name: str) -> reach.Seed:
    mode = data.MODES[name]
    settings = custom.Menu.preset(name).logic_settings()
    return reach.Seed.of(settings, mode["start_inventory"], mode["pool"])


def gates_open(seed: reach.Seed) -> reach.Seed:
    return dataclasses.replace(seed, settings=dataclasses.replace(seed.settings, gates_open=True))


class TestTheUsefulItemScore(unittest.TestCase):
    def test_the_menu_items_are_the_menus(self) -> None:
        self.assertEqual(logic.MENU_ITEM_CODES, custom.MENU_CODES)

    def test_what_it_counts(self) -> None:
        score = logic.useful_score
        self.assertEqual(score({0: 5}), 5)
        self.assertEqual(score({0: 5, 25: 1}), 10)  # Energy doubled; the armor not counted
        self.assertEqual(score({1: 3, 2: 4}), 7)
        self.assertEqual(score({36: 1, 37: 2}), 30)
        self.assertEqual(score({32: 1}), 0)  # the Navcore is not counted
        self.assertEqual(score({3: 1, 9: 1, 16: 1}), 3)  # a gun and its upgrade count apart
        self.assertEqual(score({27: 1, 46: 5}), 0)  # keys and filler are not menu items


class TestTheSecondTarget(unittest.TestCase):
    def test_a_flagged_room_waits_for_the_score(self) -> None:
        flagged = data.START_ROOMS[min(FLAGGED)]
        plain = data.START_ROOMS[min(set(range(34)) - FLAGGED)]
        self.assertTrue(reach.second_counts((), plain))
        self.assertFalse(reach.second_counts(((0, 26),), flagged))
        self.assertTrue(reach.second_counts(((0, 27),), flagged))
        self.assertTrue(reach.second_counts(((0, 14), (25, 1)), flagged))

    def test_below_the_score_the_pair_reaches_what_the_start_does(self) -> None:
        seed = preset("ohko")
        self.assertLess(logic.useful_score(dict(seed.start_items)), logic.SECOND_TARGET_SCORE)
        start = data.START_ROOMS[6]
        for i in FLAGGED:
            room = data.START_ROOMS[i]
            self.assertEqual(seed.sphere0_from(start, room), seed.sphere0(start["cell"]))
            # Every item reaches the score, so the room counts then.
            self.assertEqual(seed.every_from(start, room), seed.every(start["cell"], room["cell"]))

    def test_every_room_but_the_start_can_be_drawn(self) -> None:
        # From Landing Pod the starting items collect a location alone, so every other room,
        # flagged ones too, is a second target, as the game draws from all 33.
        seed = preset("ohko")
        start = data.START_ROOMS[6]
        self.assertTrue(seed.sphere0(start["cell"]).locations)
        rooms = reach.second_starts(seed, start, data.START_ROOMS)
        self.assertEqual(len(rooms), 33)
        self.assertNotIn(start, rooms)

    def test_a_start_that_reaches_nothing_needs_a_target_that_does(self) -> None:
        seed = preset("ohko")
        for start in data.START_ROOMS:
            if seed.sphere0(start["cell"]).locations:
                continue
            for room in reach.second_starts(seed, start, data.START_ROOMS):
                with self.subTest(start=start["index"], room=room["index"]):
                    self.assertTrue(seed.sphere0_from(start, room).locations)
                    self.assertNotIn(room["index"], FLAGGED)  # not counted at the start


class TestGatesOpen(unittest.TestCase):
    def test_a_reached_gate_cell_leads_to_the_gate_cells(self) -> None:
        targets = {data.CELL_REGION[c] for c in logic.GATE_TARGETS}
        triggers = [c for c, _ in logic.GATE_TRIGGERS]
        added = 0
        for name in ("standard", "ohko"):
            shut = preset(name)
            seed = gates_open(shut)
            for room in data.START_ROOMS:
                for got, before in (
                    (seed.sphere0(room["cell"]), shut.sphere0(room["cell"])),
                    (seed.every(room["cell"]), shut.every(room["cell"])),
                ):
                    with self.subTest(mode=name, room=room["index"]):
                        self.assertLessEqual(before.regions, got.regions)
                        reached = any(data.CELL_REGION[c] in got.regions for c in triggers)
                        self.assertEqual(targets <= got.regions, reached)
                        added += got.regions != before.regions
        self.assertTrue(added)  # the gates take some starts further

    def test_the_two_location_cells_have_no_list_of_their_own(self) -> None:
        # The game reads (24, 44) and (51, 34) as "collectible"; with no list of their own that
        # is "reached", as the AP entrances have it (world.py, connect_gates).
        for cell, as_location in logic.GATE_TRIGGERS:
            if as_location:
                self.assertNotIn(reach.LOCATION_AT[cell], data.LOCATION_RULES)

    def test_every_gate_cell_is_in_the_region_graph(self) -> None:
        for cell, _ in logic.GATE_TRIGGERS:
            self.assertIn(cell, data.CELL_REGION)

    def test_only_open_travels(self) -> None:
        for gates in ("off", "vanilla", "unlocked", "open"):
            world = fake_world(mode="custom", gates=gates)
            world.generate_early()
            self.assertEqual(world.logic_settings.gates_open, gates == "open")


class TestGatesOffWithTheCoreGoal(unittest.TestCase):
    def test_taken(self) -> None:
        # The core's door is four key slots that read the keys alone (docs/re/memory.md, "Keys,
        # the gates and the key markers"): Off disables only the gates' travel.
        fake_world(mode="custom", gates="off").generate_early()
        fake_world(mode="custom", gates="off", goal=1).generate_early()


class CustomGatesOpen(ZexionTestBase):
    options = {"mode": "custom", "gates": "open"}

    def test_the_gate_cells_lead_to_each_other(self) -> None:
        names = {e.name for e in self.multiworld.get_entrances(self.player)}
        gates = {n for n in names if n.startswith("Gate from")}
        # Each of the four cells leads to the three targets but itself: 4 x 3 - 3.
        self.assertEqual(len(gates), 9)


REAL_PARTNERS = ZexionWorld.partners


def flagged_partners(world: ZexionWorld, room: dict) -> list[dict]:
    return [r for r in REAL_PARTNERS(world, room) if r["index"] in FLAGGED]


def plain_partners(world: ZexionWorld, room: dict) -> list[dict]:
    return [r for r in REAL_PARTNERS(world, room) if r["index"] not in FLAGGED]


class CustomFlaggedSecondTarget(ZexionTestBase):
    """A Custom seed on OHKO's base (Double Teleport on) whose second target the draw makes a
    flagged room: the default tests fill it and beat it with the score rule. It starts in Ocean
    Cliff, from which OHKO's starting items collect 9 locations: below the score the flagged
    room adds none, and from fewer Archipelago's fill can fail (docs/re/locations.md, "Which
    start rooms the world offers")."""

    options = {"mode": "custom", "custom_base": "ohko", "start_room": "ocean_cliff"}

    def world_setup(self, *args, **kwargs) -> None:
        with mock.patch.object(ZexionWorld, "partners", flagged_partners):
            super().world_setup(*args, **kwargs)

    def test_the_entrance_waits_for_the_score(self) -> None:
        self.assertIn(self.world.second["index"], FLAGGED)
        entrance = self.multiworld.get_entrance("Second teleport target", self.player)
        start = logic.useful_score(dict(self.world.seed_reach.start_items))
        self.assertLess(start, logic.SECOND_TARGET_SCORE)
        self.assertFalse(entrance.access_rule(self.multiworld.state))
        for name in ("Magnus Armor", "Energy Tank", "Energy Tank", "Missile Box", "Missile Box"):
            self.collect_by_name(name)
        self.assertTrue(entrance.access_rule(self.multiworld.state))


class CustomUnflaggedSecondTarget(ZexionTestBase):
    options = {"mode": "custom", "custom_base": "ohko"}

    def world_setup(self, *args, **kwargs) -> None:
        with mock.patch.object(ZexionWorld, "partners", plain_partners):
            super().world_setup(*args, **kwargs)

    def test_the_entrance_is_open(self) -> None:
        self.assertNotIn(self.world.second["index"], FLAGGED)
        entrance = self.multiworld.get_entrance("Second teleport target", self.player)
        self.assertTrue(entrance.access_rule(self.multiworld.state))
