"""The traps: how many the pool holds and which, what they take the place of, and how a trap in
this world's own locations reaches the client (world.py, ``add_traps``, ``location_items``,
``trap_locations``)."""

import unittest
from collections import Counter

from BaseClasses import CollectionState, ItemClassification, LocationProgressType
from Fill import distribute_items_restrictive
from Options import OptionError

from ..items import (
    DISGUISES,
    DISORIENT_TRAP,
    DISRUPTION_TRAP,
    NAMES,
    SCRAP,
    TRAPS,
    classification,
    code_of,
)
from ..options import TrapPercentage
from ..world import FOREIGN_PROGRESSION, ZexionWorld
from .test_plando import block, generate, index_of


def pool_of(multiworld, player: int = 1) -> Counter[int]:
    return Counter(code_of[i.name] for i in multiworld.itempool if i.player == player)


def traps_of(pool: Counter[int]) -> int:
    return sum(pool[c] for c in TRAPS)


class TestTheTrapsInThePool(unittest.TestCase):
    def test_a_share_of_the_scrap_becomes_traps(self) -> None:
        # The same seed with no traps and with the default 15: the traps take the Scrap's place,
        # rounded to the nearest whole trap, and nothing else changes.
        for mode in ("easy", "standard", "ohko"):
            with self.subTest(mode=mode):
                none = pool_of(generate([{"mode": mode, "trap_percentage": 0}]))
                some = pool_of(generate([{"mode": mode}]))
                scrap = sum(none[c] for c in SCRAP)
                expected = (scrap * TrapPercentage.default + 50) // 100
                self.assertEqual(traps_of(none), 0)
                self.assertGreater(expected, 0)
                self.assertEqual(traps_of(some), expected)
                self.assertEqual(sum(some[c] for c in SCRAP), scrap - expected)
                others = {c: n for c, n in none.items() if c not in SCRAP}
                self.assertEqual(
                    {c: n for c, n in some.items() if c not in (*SCRAP, *TRAPS)}, others
                )

    def test_no_traps_leaves_the_seed_as_it_was(self) -> None:
        # No trap to add: no random draw either, so the placement is the one before the traps.
        def placed(options):
            multiworld = generate([options])
            distribute_items_restrictive(multiworld)
            return [(loc.name, loc.item.name) for loc in multiworld.get_filled_locations()]

        a = placed({"mode": "easy", "trap_percentage": 0})
        b = placed({"mode": "easy", "disruption_trap_weight": 0, "disorient_trap_weight": 0})
        self.assertEqual(a, b)
        self.assertFalse({name for _, name in a} & {NAMES[c] for c in TRAPS})

    def test_the_weights_pick_the_traps(self) -> None:
        only = pool_of(generate([{"mode": "standard", "disorient_trap_weight": 0}]))
        self.assertGreater(only[DISRUPTION_TRAP], 0)
        self.assertEqual(only[DISORIENT_TRAP], 0)
        only = pool_of(generate([{"mode": "standard", "disruption_trap_weight": 0}]))
        self.assertGreater(only[DISORIENT_TRAP], 0)
        self.assertEqual(only[DISRUPTION_TRAP], 0)
        every = pool_of(generate([{"mode": "standard", "trap_percentage": 100}]))
        self.assertEqual(sum(every[c] for c in SCRAP), 0)
        self.assertGreater(every[DISRUPTION_TRAP], 0)
        self.assertGreater(every[DISORIENT_TRAP], 0)

    def test_traps_take_scrap_left_after_the_disks(self) -> None:
        # The disks take the Scrap first: at 100% the traps are exactly the Scrap the same seed
        # has left without traps (none with 30 disks), and every disk is placed all the same.
        options = {"mode": "standard", "goal": "os_install_disks"}
        for placed in (16, 30):
            for seed in range(1, 6):
                with self.subTest(placed=placed, seed=seed):
                    disks = {**options, "os_install_disks": placed}
                    none = pool_of(generate([{**disks, "trap_percentage": 0}], seed=seed))
                    every = pool_of(generate([{**disks, "trap_percentage": 100}], seed=seed))
                    left = sum(none[c] for c in SCRAP)
                    self.assertEqual(every[code_of["OS Install Disk"]], placed)
                    self.assertEqual(sum(every[c] for c in SCRAP), 0)
                    self.assertEqual(traps_of(every), left)
                    if placed == 30:
                        self.assertEqual(left, 0)

    def test_traps_are_trap_items(self) -> None:
        for code in TRAPS:
            self.assertEqual(classification(code), ItemClassification.trap)
        world_item = generate([{"mode": "easy"}]).worlds[1].create_item("Disruption Trap")
        self.assertTrue(world_item.trap)
        self.assertFalse(world_item.advancement or world_item.useful)

    def test_every_seed_with_traps_is_beatable(self) -> None:
        for seed in range(1, 6):
            for mode in ("easy", "standard", "ohko"):
                with self.subTest(seed=seed, mode=mode):
                    multiworld = generate([{"mode": mode, "trap_percentage": 100}], seed=seed)
                    distribute_items_restrictive(multiworld)
                    self.assertTrue(multiworld.can_beat_game(CollectionState(multiworld)))


class TestTrapsInExcludedLocations(unittest.TestCase):
    def test_traps_count_as_filler_for_excluded_locations(self) -> None:
        # As many excluded locations as the seed has filler, traps among it: the fill puts only
        # filler and traps there, and the world does not refuse it (stage_pre_fill).
        world = generate([{"mode": "standard", "trap_percentage": 0}]).worlds[1]
        filler = sum(world.pool_codes.count(c) for c in (*SCRAP, 49))
        names = sorted(world.location_name_to_id)[:filler]
        options = {"mode": "standard", "trap_percentage": 50, "exclude_locations": names}
        multiworld = generate([options])
        ZexionWorld.stage_pre_fill(multiworld)
        distribute_items_restrictive(multiworld)
        kinds = {
            loc.item.classification
            for loc in multiworld.get_locations(1)
            if loc.progress_type == LocationProgressType.EXCLUDED
        }
        self.assertLessEqual(kinds, {ItemClassification.filler, ItemClassification.trap})


class TestOwnTraps(unittest.TestCase):
    def test_an_own_trap_is_a_stand_in_the_client_hears_of(self) -> None:
        # Plando'd into this world's own location: the game holds a minor stand-in there, and
        # the slot data names the trap for the client to set off once it is found.
        options = {
            "mode": "easy",
            "plando_items": [block({"Disorient Trap": 1}, ["Kresezoa 25,25"])],
        }
        world = generate([options]).worlds[1]
        i = index_of("Kresezoa 25,25")
        self.assertEqual(world.location_items()[i], FOREIGN_PROGRESSION)
        self.assertIn([i, DISORIENT_TRAP], world.trap_locations())
        self.assertEqual(
            {c for _, c in world.trap_locations()} - set(TRAPS), set(), "only traps listed"
        )

    def test_each_trap_has_a_look_the_player_does_not_start_with(self) -> None:
        # Easy starts with the Power Cartridge (13), one of the disguises: never drawn there.
        traps = block({"Disorient Trap": 30}, ["Machinus"], from_pool=False)
        world = generate([{"mode": "easy", "plando_items": [traps]}]).worlds[1]
        looks = dict(world.trap_looks())
        self.assertEqual(set(looks), {i for i, _ in world.trap_locations()})
        self.assertNotIn(13, looks.values())
        self.assertLessEqual(set(looks.values()), set(DISGUISES))
        self.assertGreater(len(set(looks.values())), 1, "drawn, not one for all")
        self.assertEqual(world.trap_looks(), world.trap_looks(), "the same draw each time")

    def test_another_players_trap_is_a_stand_in_and_not_listed(self) -> None:
        trap = block({"Disruption Trap": 1}, ["Machinus 24,29"], world="Tester2")
        multiworld = generate([{"mode": "easy", "plando_items": [trap]}, {"mode": "easy"}])
        beta = multiworld.worlds[2]
        self.assertEqual(beta.location_items()[index_of("Machinus 24,29")], FOREIGN_PROGRESSION)
        self.assertNotIn(index_of("Machinus 24,29"), {i for i, _ in beta.trap_locations()})


class TestTrapsAreNeverHeld(unittest.TestCase):
    def test_plando_may_make_any_number(self) -> None:
        # Never held: plando past the pool's is not refused (plando.MANY).
        trap = block({"Disruption Trap": 5}, ["Machinus"], from_pool=False)
        generate([{"mode": "easy", "trap_percentage": 0, "plando_items": [trap]}])

    def test_a_starting_trap_is_refused(self) -> None:
        for option in ("start_inventory", "start_inventory_from_pool"):
            with self.subTest(option=option), self.assertRaises(OptionError) as caught:
                generate([{"mode": "easy", option: {"Disorient Trap": 1}}])
            self.assertIn("a starting trap is never received", str(caught.exception))
