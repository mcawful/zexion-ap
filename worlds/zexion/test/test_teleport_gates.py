"""Double Teleport's second target and the Gates setting, as the game's logic has them (built
2026-10-28; docs/re/randocells.md, "Starting Location and the second teleport target" and "What
each setting does in the game")."""

import dataclasses
import unittest
from unittest import mock

from .. import custom, data, logic, reach
from ..world import ZexionWorld
from .bases import FLAGGED, ZexionTestBase, preset, with_settings
from .test_world import fake_world


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
        self.assertTrue(logic.second_counts({}, flagged=False))
        self.assertFalse(logic.second_counts({0: 26}, flagged=True))
        self.assertTrue(logic.second_counts({0: 27}, flagged=True))
        self.assertTrue(logic.second_counts({0: 14, 25: 1}, flagged=True))

    def test_below_the_score_the_pair_reaches_what_the_start_does(self) -> None:
        seed = preset("ohko")
        self.assertLess(logic.useful_score(dict(seed.start_items)), logic.SECOND_TARGET_SCORE)
        start = data.START_ROOMS[6]
        for i in FLAGGED:
            room = data.START_ROOMS[i]
            self.assertEqual(seed.sphere0_from(start, room), seed.sphere0(start["cell"]))
            # Every item reaches the score, so the room counts then.
            self.assertEqual(seed.every_from(start, room), seed.every(start["cell"], room["cell"]))

    def test_a_flagged_room_once_it_counts_from_the_start(self) -> None:
        # OHKO's starting score leaves the flagged rooms out (test_reach.py, TestStartRooms: the
        # world's guard; the game draws from all 33); 27 Energy brings them in.
        seed = preset("ohko")
        start = data.START_ROOMS[6]
        energy = reach.inventory({**dict(seed.start_items), 0: 27})
        rich = dataclasses.replace(seed, start_items=energy)
        rooms = reach.second_starts(rich, start, data.START_ROOMS)
        self.assertEqual({r["index"] for r in rooms}, set(range(34)) - {6})


class TestGatesOpen(unittest.TestCase):
    def test_a_reached_gate_cell_leads_to_the_gate_cells(self) -> None:
        targets = {data.CELL_REGION[c] for c in logic.GATE_TARGETS}
        triggers = [c for c, _ in reach.GATE_TRIGGERS]
        added = 0
        for name in ("standard", "ohko"):
            shut = preset(name)
            seed = with_settings(shut, gates_open=True)
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

    def test_rtalvas_gate_opens_no_travel(self) -> None:
        # The game's logic counts it; the world does not (the owner's guard, 2026-10-28).
        # From its cell with no items, the world's flood reaches no gate cell beyond it.
        self.assertIn((reach.RTALVA_GATE, True), logic.GATE_TRIGGERS)
        settings = with_settings(preset("standard"), gates_open=True).settings
        targets = {data.CELL_REGION[c] for c in logic.GATE_TARGETS}
        got = reach.reach(settings, (), (reach.RTALVA_GATE,)).regions
        self.assertFalse(targets & got)
        self.assertLessEqual(targets, reach.reach(settings, (), ((12, 30),)).regions)

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
        # Each of the three cells leads to the other two; not from R'Talva's (51, 34).
        self.assertEqual(len(gates), 6)
        self.assertFalse(any(n.startswith("Gate from X34 Y51") for n in gates))


REAL_PARTNERS = ZexionWorld.partners


def flagged_partners(world: ZexionWorld, room: dict) -> list[dict]:
    return [r for r in REAL_PARTNERS(world, room) if r["index"] in FLAGGED]


def plain_partners(world: ZexionWorld, room: dict) -> list[dict]:
    return [r for r in REAL_PARTNERS(world, room) if r["index"] not in FLAGGED]


class CustomFlaggedSecondTarget(ZexionTestBase):
    """A Custom seed on OHKO's base (Double Teleport on) whose starting Energy brings the
    useful-item score to 27, so the world may draw a flagged room, here made the draw: it
    counts from the start, and the default tests fill and beat it."""

    options = {"mode": "custom", "custom_base": "ohko", "starting_energy_tanks": 27}

    def world_setup(self, *args, **kwargs) -> None:
        with mock.patch.object(ZexionWorld, "partners", flagged_partners):
            super().world_setup(*args, **kwargs)

    def test_the_entrance_has_the_score_rule_and_passes_from_the_start(self) -> None:
        self.assertIn(self.world.second["index"], FLAGGED)
        entrance = self.multiworld.get_entrance("Second teleport target", self.player)
        start = logic.useful_score(dict(self.world.seed_reach.start_items))
        self.assertGreaterEqual(start, logic.SECOND_TARGET_SCORE)
        self.assertTrue(entrance.access_rule(self.multiworld.state))
        empty = self.multiworld.state.__class__(self.multiworld)
        empty.prog_items[self.player].clear()
        self.assertFalse(entrance.access_rule(empty))


class CustomUnflaggedSecondTarget(ZexionTestBase):
    options = {"mode": "custom", "custom_base": "ohko"}

    def world_setup(self, *args, **kwargs) -> None:
        with mock.patch.object(ZexionWorld, "partners", plain_partners):
            super().world_setup(*args, **kwargs)

    def test_the_entrance_is_open(self) -> None:
        self.assertNotIn(self.world.second["index"], FLAGGED)
        entrance = self.multiworld.get_entrance("Second teleport target", self.player)
        self.assertTrue(entrance.access_rule(self.multiworld.state))
