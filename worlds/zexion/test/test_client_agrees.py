"""The client (this repo's ``client`` package) against the world: the codes it shares
(client/ids.py), and the slot data it reads, for each mode and both goals.

Run from the source checkout (this folder linked into an Archipelago checkout), where the
repo's root is found through the link; skipped where the client is not there to import (an
installed apworld), except where ZEXION_REQUIRE lists ``client`` (CI's world tests).
"""

from __future__ import annotations

import os
import sys
import unittest

from Fill import distribute_items_restrictive

from .. import custom, data, items, locations, logic, reach, starting
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
    from client import aplabels, discs, ids, newgame, patterns, reconcile, save_patch
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
# CI's world tests set ZEXION_REQUIRE=client: there a client that does not import (the world
# copied rather than linked) fails this test, where the tests below would only skip.


def required(value: str) -> set[str]:
    """ZEXION_REQUIRE's entries: comma-separated, spaces around them ignored."""
    return {entry.strip() for entry in value.split(",")} - {""}


REQUIRED = required(os.environ.get("ZEXION_REQUIRE", ""))


class TestTheClientIsThereWhereRequired(unittest.TestCase):
    @unittest.skipUnless("client" in REQUIRED, "ZEXION_REQUIRE does not list client")
    def test_the_client_imports(self) -> None:
        self.assertIsNotNone(CLIENT, WHY)

    def test_zexion_require_ignores_spaces_around_entries(self) -> None:
        self.assertEqual(required("sh, client"), {"client", "sh"})
        self.assertEqual(required(" client ,sh,"), {"client", "sh"})
        self.assertEqual(required(""), set())


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
        self.assertEqual(save_patch.GUNS_FIRST, custom.GUNS_FIRST)
        self.assertEqual(ids.DATA_DISC, items.DATA_DISC)
        self.assertEqual(ids.DATA_DISC, 44)
        self.assertEqual(ids.KEYS, items.KEY_CODES)
        self.assertEqual(ids.SCRAP_METAL, items.SCRAP_METAL)
        self.assertEqual(ids.ENERGY_SHIELD, custom.ENERGY_SHIELD)
        # The client writes every other item's byte as 1 at most.
        self.assertEqual(reconcile.COUNTED, items.SUPPLIES | {items.SCRAP_METAL})
        self.assertEqual(reconcile.COUNTED, starting.COUNTED)  # pool_duplicates counts the same

    def test_the_stand_ins(self) -> None:
        self.assertEqual((ids.STAND_IN, ids.STAND_IN_PROGRESSION), (FOREIGN, FOREIGN_PROGRESSION))
        self.assertEqual((ids.STAND_IN, ids.STAND_IN_PROGRESSION), (47, 48))

    def test_the_locations(self) -> None:
        self.assertEqual(ids.LOCATIONS, len(data.LOCATIONS))
        self.assertEqual(ids.MAX_LOCATIONS, len(reach.ALL_LOCATIONS))
        self.assertEqual(ids.SECRET_SHUFFLE, reach.SECRET_SHUFFLE)
        # The game's list order is grid order: the 154 already in it, the 155th at 134.
        self.assertEqual(save_patch.game_order(data.LOCATIONS), tuple(range(154)))
        self.assertEqual(save_patch.game_order(reach.ALL_LOCATIONS).index(154), 134)

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
        # The most Energy and missiles generation accepts are amounts the client writes (16
        # bits each, plan_text).
        top = {starting.ENERGY_TANK: starting.MAX_ENERGY, starting.MISSILE_BOX: 99}
        top[starting.MISSILE_CONTAINER] = most
        self.assertEqual(starting.problems(top, False), [])
        _, amounts = newgame.start_with(top)
        self.assertEqual((amounts["energy"], amounts["missiles"]), (40, 999))
        self.assertTrue(all(-(2**15) <= v < 2**15 for v in amounts.values()))
        over = {starting.ENERGY_TANK: 41, starting.MISSILE_BOX: 100}
        self.assertEqual(len(starting.problems(over, False)), 2)
        # Without Guns First, the upgrades are started as themselves: one each, in the array.
        one_each = dict.fromkeys(newgame.START_WITH_ORDER, 1)
        self.assertEqual(starting.problems(one_each, False, guns_first=False), [])
        array, _ = newgame.start_with(one_each)
        self.assertEqual(array, bytes([1] * len(newgame.START_WITH_ORDER)))


@unittest.skipIf(CLIENT is None, WHY)
class TestClientReadsTheSlotData(ZexionTestBase):
    """fill_slot_data() of a filled world, for each mode and both goals, through what the
    client reads it with."""

    auto_construct = False
    run_default_tests = False

    def test_each_mode_and_goal(self) -> None:
        # A build whose start table the world's data was measured on, so plan_text uses it.
        build = next(b for b in patterns.KNOWN_BUILDS if b.sha256.startswith(data.DATA_BUILD))
        custom = {"mode": "custom", "x_blocks": "on", "hints": "revealed", "start_keys": 1}
        guns_first_off = {"guns_first": "off", "custom_items": {"Pentashot": "start"}}
        secret = {"secret_shuffle": "on", "start_room": "landing_pod"}
        for options in (
            *({"mode": mode} for mode in ("easy", "standard", "ohko")),
            custom,
            {**custom, "custom_base": "ohko", "safety": "off"},
            {**custom, **guns_first_off},
            {**custom, **secret},
        ):
            for goal in ("core", "os_install_disks"):
                with self.subTest(**options, goal=goal):
                    self.options = {**options, "goal": goal}
                    if goal == "core" and options["mode"] == "custom":
                        self.options["abridged_finale"] = "on"
                    self.world_setup()
                    distribute_items_restrictive(self.multiworld)
                    self.check(self.world.fill_slot_data(), goal, build.sha256)

    def check(self, slot: dict, goal: str, build_sha256: str) -> None:
        plan = save_patch.plan_from_slot_data(slot)
        guns_first = self.world.logic_settings.guns_first
        self.assertEqual(plan.guns_first, guns_first)
        if not guns_first:  # the started Pentashot, as itself
            self.assertEqual(plan.start_items.get(14), 1)
        # 154 locations, or 155 with Secret Shuffle, its 155th last by location index and at
        # list position 134 in the game's (save_patch.game_order); the 154 in the game's order.
        self.assertEqual(plan.check_size(), "")
        if slot["options"]["37"]:
            self.assertEqual(len(plan.location_items), ids.MAX_LOCATIONS)
            self.assertEqual(plan.cells[154], data.SECRET_LOCATION)
            self.assertEqual(plan.order.index(154), 134)
            self.assertEqual(plan.table[134], plan.location_items[154])
        else:
            self.assertEqual(len(plan.location_items), ids.LOCATIONS)
            self.assertEqual(plan.order, tuple(range(ids.LOCATIONS)))
            self.assertEqual(plan.table, bytes(plan.location_items))
        self.assertLessEqual(set(plan.location_items), set(ids.GAME_ITEMS))
        self.assertLessEqual(set(plan.start_items), set(ids.WRITABLE_ITEMS))
        self.assertEqual(len(plan.amounts), 6)
        self.assertIsNotNone(plan.menu)
        self.assertEqual(len(slot["locations"]), plan.list_size)
        table = f"write {newgame.section(save_patch.ITEM_TABLE):#x} {plan.table.hex()}"
        self.assertIn(table, newgame.plan_text(plan, offer_choice=False).splitlines())

        # ARCHIPELAGO on the build the menu's start table was measured on, and RANDOMIZER's
        # request, which needs no table.
        self.assertTrue(newgame.plan_text(plan, build_sha256).startswith("ap_newgame\n"))
        self.assertTrue(newgame.plan_text(plan, offer_choice=False).startswith("ap_newgame\n"))
        self.assertIsNotNone(newgame.start_table(plan, build_sha256) or plan.menu.second_target)
        # The new game must have every setting the logic reads as the seed's, Abridged Finale
        # (setting 32) among them; the client's goal, the ending's flag with the finale's clear,
        # holds with it on or off (client/session.py, ENDING_FLAG).
        lines = newgame.plan_text(plan, offer_choice=False).splitlines()
        for t in (10, 30, 31, 32, 33, 37, 42, 43, 72, 73):
            value = slot["options"][str(t)]
            self.assertIn(f"expect {newgame.section(save_patch.OPTIONS + t):#x} {value:02x}", lines)
        self.assertEqual(slot["options"]["32"], int(slot["mode"] == "custom" and goal == "core"))
        # Starting Location in the menu names the start room.
        location = sum(plan.menu.settings[t - 10] << i for i, t in enumerate(range(22, 28)))
        self.assertEqual(location, slot["start_index"] + 1)

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
