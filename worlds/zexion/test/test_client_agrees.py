"""The client (this repo's ``client`` package) against the world: the codes it shares
(client/ids.py), and the slot data it reads, for each mode and both goals.

Run from the source checkout (this folder linked into an Archipelago checkout), where the
repo's root is found through the link; skipped where the client is not there to import (an
installed apworld).
"""

from __future__ import annotations

import os
import sys
import unittest

from Fill import distribute_items_restrictive

from .. import data, items, locations, logic, starting
from ..locations import location_name
from ..world import FOREIGN, FOREIGN_PROGRESSION
from .bases import ZexionTestBase

_ROOT = os.path.normpath(
    os.path.join(os.path.dirname(os.path.realpath(__file__)), os.pardir, os.pardir, os.pardir)
)
try:
    if not os.path.isfile(os.path.join(_ROOT, "client", "ids.py")):
        raise ImportError("no client package beside this world")
    if _ROOT not in sys.path:
        sys.path.append(_ROOT)
    from client import aplabels, discs, ids, newgame, patterns, save_patch
except ImportError as error:
    CLIENT = None
    WHY = f"the repo's client package is not importable: {error}"
else:
    CLIENT = ids
    WHY = ""
    try:  # the client's whole connection state; its modules reach the Windows API
        from client import state
    except (ImportError, ValueError):
        state = None


@unittest.skipIf(CLIENT is None, WHY)
class TestClientCodes(unittest.TestCase):
    def test_the_id_bases(self) -> None:
        self.assertEqual(ids.ITEM_ID_BASE, items.ITEM_ID_BASE)
        self.assertEqual(ids.LOCATION_ID_BASE, locations.LOCATION_ID_BASE)
        self.assertEqual(items.item_name_to_id[items.NAMES[0]], ids.ITEM_ID_BASE)
        self.assertEqual(locations.location_name_to_id[location_name(0)], ids.LOCATION_ID_BASE)

    def test_the_items_of_the_worlds_own(self) -> None:
        self.assertEqual(ids.ASSIST_UNLOCK, items.ASSIST_UNLOCK)
        self.assertEqual(ids.ASSIST_UNLOCK, 100)
        self.assertEqual((ids.AREA_MAP, ids.INTEL_CHIP), (items.AREA_MAP, items.INTEL_CHIP))
        self.assertEqual((ids.AREA_MAP, ids.INTEL_CHIP), (49, 101))
        self.assertEqual(ids.DRAWN_AS, items.DRAWN_AS)

    def test_the_games_items(self) -> None:
        self.assertEqual(ids.GAME_ITEMS, items.GAME_ITEMS)
        self.assertEqual(ids.GAME_ITEMS, range(0, 51))
        self.assertEqual(ids.PROGRESSIVE, items.PROGRESSIVE)
        self.assertEqual(ids.PROGRESSIVE, logic.WEAPON_UPGRADES)
        self.assertEqual(ids.DATA_DISC, items.DATA_DISC)
        self.assertEqual(ids.DATA_DISC, 44)
        self.assertEqual(ids.KEYS, items.KEY_CODES)
        self.assertEqual(ids.SCRAP_METAL, items.SCRAP_METAL)
        self.assertEqual(ids.ENERGY_SHIELD, items.ENERGY_SHIELD)

    def test_the_stand_ins(self) -> None:
        self.assertEqual((ids.STAND_IN, ids.STAND_IN_PROGRESSION), (FOREIGN, FOREIGN_PROGRESSION))
        self.assertEqual((ids.STAND_IN, ids.STAND_IN_PROGRESSION), (47, 48))

    def test_the_locations(self) -> None:
        self.assertEqual(ids.LOCATIONS, len(data.LOCATIONS))

    def test_the_starting_items_generation_accepts(self) -> None:
        # starting.py copies what the client's new game can start with: they must agree, or
        # generation would accept a seed the client then refuses.
        upgrades = set(ids.PROGRESSIVE.values())
        self.assertEqual(starting.START_WITH, set(newgame.START_WITH_ORDER) - upgrades)
        most = starting.MAX_CONTAINERS
        self.assertEqual(starting.MISSILE_CONTAINER, ids.MISSILE_CONTAINER)
        newgame.start_with({ids.MISSILE_CONTAINER: most})
        with self.assertRaises(newgame.NewGameError):
            newgame.start_with({ids.MISSILE_CONTAINER: most + 1})
        self.assertEqual(starting.problems({ids.MISSILE_CONTAINER: most}, False), [])
        self.assertNotEqual(starting.problems({ids.MISSILE_CONTAINER: most + 1}, False), [])


@unittest.skipIf(CLIENT is None, WHY)
class TestClientReadsTheSlotData(ZexionTestBase):
    """fill_slot_data() of a filled world, for each mode and both goals, through what the
    client reads it with."""

    auto_construct = False
    run_default_tests = False

    def test_each_mode_and_goal(self) -> None:
        # A build whose start table the world's data was measured on, so plan_text uses it.
        build = next(b for b in patterns.KNOWN_BUILDS if b.sha256.startswith(data.DATA_BUILD))
        for mode in ("easy", "standard", "ohko"):
            for goal in ("core", "os_install_disks"):
                with self.subTest(mode=mode, goal=goal):
                    self.options = {"mode": mode, "goal": goal}
                    self.world_setup()
                    distribute_items_restrictive(self.multiworld)
                    self.check(self.world.fill_slot_data(), goal, build.sha256)

    def check(self, slot: dict, goal: str, build_sha256: str) -> None:
        plan = save_patch.plan_from_slot_data(slot)
        self.assertEqual(len(plan.location_items), ids.LOCATIONS)
        self.assertLessEqual(set(plan.location_items), set(ids.GAME_ITEMS))
        self.assertLessEqual(set(plan.start_items), set(ids.WRITABLE_ITEMS))
        self.assertEqual(len(plan.amounts), 6)
        self.assertIsNotNone(plan.menu)
        self.assertEqual(len(slot["locations"]), ids.LOCATIONS)

        # ARCHIPELAGO on the build the menu's start table was measured on, and RANDOMIZER's
        # request, which needs no table.
        self.assertTrue(newgame.plan_text(plan, build_sha256).startswith("ap_newgame\n"))
        self.assertTrue(newgame.plan_text(plan, offer_choice=False).startswith("ap_newgame\n"))
        self.assertIsNotNone(newgame.start_table(plan, build_sha256) or plan.menu.second_target)
        # The new game must have Abridged Finale (setting 32) off: the client's goal is the
        # ending's flag with the finale's clear (client/session.py, ENDING_FLAG).
        abridged = f"expect {newgame.section(save_patch.OPTIONS + 32):#x} 00"
        self.assertIn(abridged, newgame.plan_text(plan, offer_choice=False).splitlines())

        self.assertEqual(aplabels.stand_ins(slot), {ids.STAND_IN, ids.STAND_IN_PROGRESSION})
        kept = frozenset(slot["kept_locations"])
        foreign = aplabels.foreign_locations(slot["location_items"], kept, aplabels.stand_ins(slot))
        self.assertLessEqual(set(foreign), kept)

        if state is not None:  # all of the above together, as the client connects
            connection = state.Connection.from_slot_data(slot, 0, 1)
            self.assertEqual(connection.plan, plan)
            self.assertEqual(connection.foreign, foreign)

        hunt = discs.from_slot_data(slot)
        if goal == "core":
            self.assertIsNone(hunt)
            return
        self.assertIsNotNone(hunt)
        self.assertEqual(hunt.required, self.world.options.os_install_disks_required.value)
        self.assertEqual(hunt.placed, self.world.options.os_install_disks.value)
        self.assertEqual(hunt.start, 0)
        # This world's own disks are stand-ins the game never hands out: labeled as foreign.
        self.assertTrue(hunt.locations)
        self.assertLessEqual(hunt.locations, set(foreign))
        for i in hunt.locations:
            self.assertEqual(plan.location_items[i], ids.STAND_IN_PROGRESSION)
