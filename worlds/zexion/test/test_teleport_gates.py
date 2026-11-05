"""Double Teleport's second target and the Gates setting, as the game's logic has them
(docs/re/randocells.md, "Starting Location and the second teleport target" and "What each
setting does in the game"), and Gates Vanilla made Unlocked for the OS Install Disks goal."""

import dataclasses
import io
import logging
import unittest
from unittest import mock

from Fill import distribute_items_restrictive

from .. import custom, data, logic, reach
from ..custom import Menu
from ..options import Gates
from ..world import ZexionWorld
from .bases import FLAGGED, ZexionTestBase, preset, with_settings
from .test_world import disks, fake_world


class TestTheUsefulItemScore(unittest.TestCase):
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
        # The game's logic counts it; the world does not (reach.RTALVA_GATE).
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
        # Off is taken with either goal, the core's too: the core's door is four key slots that
        # read the keys alone (docs/re/memory.md, "Keys, the gates and the key markers").
        for goal in ("core", "os_install_disks"):
            for gates in ("off", "vanilla", "unlocked", "open"):
                world = fake_world(mode="custom", gates=gates, goal=goal)
                world.generate_early()
                with self.subTest(goal=goal, gates=gates):
                    self.assertEqual(world.logic_settings.gates_open, gates == "open")


VANILLA, UNLOCKED = Gates.option_vanilla, Gates.option_unlocked


def generated(**options) -> ZexionWorld:
    world = fake_world(**options)
    world.generate_early()
    return world


class TestGatesWithTheDisksGoal(unittest.TestCase):
    """The OS Install Disks goal places no keys, so Gates Vanilla, whose gates take all four,
    becomes Unlocked (world.py, read_menu; docs/re/memory.md, "Keys, the gates and the key
    markers")."""

    def test_ohkos_vanilla_becomes_unlocked_at_info(self) -> None:
        self.assertEqual(Menu.preset("ohko").value(custom.GATES), VANILLA)
        for options in ({"mode": "ohko"}, {"mode": "custom", "custom_base": "ohko"}):
            with self.subTest(**options):
                with self.assertLogs(level="INFO") as logs:
                    world = generated(**options, goal="os_install_disks")
                self.assertEqual({r.levelno for r in logs.records}, {logging.INFO})
                (line,) = [r.getMessage() for r in logs.records if "Gates" in r.getMessage()]
                self.assertIn("Zexion (Z): ohko mode's Gates Vanilla", line)
                self.assertTrue(world.gates_unlocked)
                self.assertEqual(world.menu.value(custom.GATES), UNLOCKED)
                self.assertFalse(world.logic_settings.gates_open)
                # Nothing else in OHKO's menu changes.
                self.assertEqual(world.menu.with_value(custom.GATES, VANILLA), Menu.preset("ohko"))

    def test_the_yamls_vanilla_becomes_unlocked_with_a_warning(self) -> None:
        with self.assertLogs(level="WARNING") as logs:
            world = generated(mode="custom", gates="vanilla", goal="os_install_disks")
        (line,) = [r.getMessage() for r in logs.records]
        self.assertIn("Zexion (Z): gates: vanilla takes all four keys", line)
        self.assertIn("Unlocked instead", line)
        self.assertEqual(world.menu.value(custom.GATES), UNLOCKED)

    def test_the_other_gates_stay_as_set(self) -> None:
        for gates in ("off", "unlocked", "open"):
            for base in ("standard", "ohko"):
                with self.subTest(gates=gates, base=base):
                    with self.assertNoLogs(level="INFO"):
                        world = generated(
                            mode="custom", custom_base=base, gates=gates, goal="os_install_disks"
                        )
                    self.assertFalse(world.gates_unlocked)
                    self.assertEqual(world.menu.value(custom.GATES), Gates.options[gates])
        # Standard's and Easy's own Unlocked, too.
        for mode in ("easy", "standard"):
            world = generated(mode=mode, goal="os_install_disks")
            self.assertEqual(world.menu, Menu.preset(mode))

    def test_the_core_goal_keeps_vanilla(self) -> None:
        for options in ({"mode": "ohko"}, {"mode": "custom", "gates": "vanilla"}):
            with self.subTest(**options):
                with self.assertNoLogs(level="INFO"):
                    world = generated(**options, goal="core")
                self.assertFalse(world.gates_unlocked)
                self.assertEqual(world.menu.value(custom.GATES), VANILLA)


class OHKODisksGates(ZexionTestBase):
    """A whole OHKO seed with the OS Install Disks goal: its slot data and spoiler carry the
    Unlocked gates, and the default tests fill and beat it."""

    options = disks("ohko", 16, 12)

    def test_the_slot_data_and_the_spoiler(self) -> None:
        distribute_items_restrictive(self.multiworld)
        self.multiworld.seed_name = "1"
        slot = self.world.fill_slot_data()
        self.assertEqual((slot["options"]["72"], slot["options"]["73"]), (0, 1))
        settings = slot["menu"]["settings"]
        self.assertEqual((settings[72 - 10], settings[73 - 10]), (0, 1))
        self.assertEqual(slot["mode"], "ohko")  # the YAML's mode, though the game reads Custom
        spoiler = io.StringIO()
        self.world.write_spoiler_header(spoiler)
        self.assertIn("Gates:                           Unlocked", spoiler.getvalue())


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
