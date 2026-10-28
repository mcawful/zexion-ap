"""Custom mode: the game's pool rule, Random Gun and Random Start, the menu's limits, the options'
refusals, and whole Custom seeds (docs/custom-mode.md; docs/re/randocells.md, "The pool")."""

import random
import unittest
from collections import Counter
from typing import Any

from Fill import distribute_items_restrictive
from Options import OptionError

from .. import custom, data, logic, reach
from ..custom import (
    BOMB_BOX,
    ENERGY_TANK,
    MEGA_BOMB,
    MISSILE_BOX,
    MISSILE_CONTAINER,
    SCRAP_METAL,
    Menu,
)
from ..items import NAMES
from ..locations import LOCATION_ID_BASE
from ..options import CustomItems
from .bases import ZexionTestBase
from .test_world import fake_world, pool_names

FILLER = {46, 47, 48, 49, 50}
ENTRY = {name: i for i, (name, _) in enumerate(custom.MENU_ITEMS)}


def supplies(items: list[int]) -> tuple[int, int, int, int, int]:
    """(Missile Boxes, Bomb Boxes, Mega Bombs, Missile Containers, filler) in a pool."""
    n = Counter(items)
    filler = sum(n[c] for c in FILLER)
    return n[MISSILE_BOX], n[BOMB_BOX], n[MEGA_BOMB], n[MISSILE_CONTAINER], filler


def resolved_as(menu: Menu, start_inventory: dict[int, int]) -> tuple[list[int], random.Random]:
    """The pool array for a draw whose starting items are these, and the random numbers after
    it (the guns drawn differ by seed)."""
    for seed in range(100):
        rng = random.Random(seed)
        start, pool = custom.resolve(menu, rng)
        if custom.starting_items(menu, start) == start_inventory:
            return pool, rng
    raise AssertionError("no draw gave those starting items")


class TestThePresetsPools(unittest.TestCase):
    """The rule against the three presets' fresh saves: the saves' items but filler, exactly
    for Easy and Standard; OHKO's save holds one later compression of ten containers."""

    def test_easy_and_standard(self) -> None:
        for name in ("easy", "standard"):
            mode = data.MODES[name]
            menu = Menu.preset(name)
            pool, rng = resolved_as(menu, mode["start_inventory"])
            items = custom.pool_items(menu, pool, rng)
            with self.subTest(name):
                self.assertEqual(len(items), 154)
                self.assertEqual(
                    Counter(c for c in items if c not in FILLER),
                    Counter(c for c in mode["pool"] if c not in FILLER),
                )

    def test_ohko_before_the_placements_compression(self) -> None:
        menu = Menu.preset("ohko")
        pool, rng = resolved_as(menu, data.MODES["ohko"]["start_inventory"])
        items = custom.pool_items(menu, pool, rng)
        self.assertEqual(supplies(items)[:4], (8, 0, 12, 72))
        save = data.MODES["ohko"]["pool"]
        self.assertEqual(supplies(save)[:4], (9, 0, 12, 62))

    def test_the_counts(self) -> None:
        # Easy: R = 91, 17 boxes, 29 containers, 18 Mega Bombs, 27 filler; Standard: R = 107,
        # 6 boxes, 62 containers, 12 Mega Bombs (docs/re/randocells.md, "The pool").
        for name, want in (("easy", (17, 0, 18, 29, 27)), ("standard", (6, 0, 12, 62, 27))):
            menu = Menu.preset(name)
            pool, rng = resolved_as(menu, data.MODES[name]["start_inventory"])
            with self.subTest(name):
                self.assertEqual(supplies(custom.pool_items(menu, pool, rng)), want)


class TestTheGeneratedSeeds(unittest.TestCase):
    """The three seeds generated in the game's menu and read from their first saves, 2026-10-27
    (docs/re/randocells.md, "The pool")."""

    def test_seed_a_everything_started_fits_as_singles(self) -> None:
        menu = Menu.preset("standard")
        for i in range(custom.ARRAY):
            menu = menu.with_item(i, "start")
        menu = menu.with_value(custom.START_KEYS, 4)
        for which, value in ((custom.START_ENERGY, 20), (custom.START_MISSILES, 50)):
            menu = menu.with_amount(which, value)
        self.assertEqual(custom.problems(menu), [])
        start, pool = custom.resolve(menu, random.Random(1))
        items = custom.pool_items(menu, pool, random.Random(1))
        self.assertEqual(supplies(items), (0, 0, 12, 122, 3))
        self.assertEqual(Counter(items)[ENERGY_TANK], 17)
        self.assertFalse(set(items) & {27, 28, 29, 30}, "Start Keys 4: no key in the pool")
        have = custom.starting_items(menu, start)
        self.assertEqual((have[27], have[30], have[ENERGY_TANK], have[MISSILE_BOX]), (1, 1, 20, 5))
        self.assertNotIn(MISSILE_CONTAINER, have)

    def test_seed_b_box_compression_off(self) -> None:
        menu = Menu.preset("standard").with_settings({custom.BOX_COMPRESSION: 0})
        _, pool = custom.resolve(menu, random.Random(1))
        self.assertEqual(
            supplies(custom.pool_items(menu, pool, random.Random(1))), (3, 0, 12, 92, 0)
        )

    def test_seed_c_bulk_missiles(self) -> None:
        menu = Menu.preset("standard").with_settings({custom.BULK_MISSILES: 1})
        _, pool = custom.resolve(menu, random.Random(1))
        items = custom.pool_items(menu, pool, random.Random(1))
        self.assertEqual(supplies(items), (12, 0, 12, 2, 81))

    def test_the_menus_counter_for_the_bulk_settings(self) -> None:
        # Read on screen in Standard's state: Bulk Bombs alone, one Bomb Box, 2 Mega Bombs and
        # five Missile Boxes; both, 12 Missile Boxes and one Bomb Box.
        bombs = Menu.preset("standard").with_settings({custom.BULK_BOMBS: 1})
        both = bombs.with_settings({custom.BULK_MISSILES: 1})
        for menu, want in ((bombs, (5, 1, 2)), (both, (12, 1, 2))):
            _, pool = custom.resolve(menu, random.Random(1))
            with self.subTest(want=want):
                self.assertEqual(
                    supplies(custom.pool_items(menu, pool, random.Random(1)))[:3], want
                )


class TestRandomGun(unittest.TestCase):
    def guns(self, menu: Menu, starts: int, seeds: int = 60) -> tuple[Counter, Counter]:
        """How often each gun is started and pooled. Guns First is off here: with it, a started
        gun whose upgrade is pooled is pooled as well (the upgrade swapped for it)."""
        menu = menu.with_settings({custom.GUNS_FIRST: 0})
        started, pooled = Counter(), Counter()
        for seed in range(seeds):
            start, pool = custom.resolve(menu, random.Random(seed))
            for i in custom.GUNS:
                started[custom.MENU_ITEMS[i][0]] += start[i]
                pooled[custom.MENU_ITEMS[i][0]] += pool[i]
                self.assertFalse(start[i] and pool[i])
            self.assertEqual(sum(start[i] for i in custom.GUNS), starts)
        return started, pooled

    def test_standards_one_coin_flip_gun_is_no_pair(self) -> None:
        # The Arc Plasma goes to the pool; one of the other four is drawn and started.
        started, pooled = self.guns(Menu.preset("standard"), 1)
        self.assertEqual(started["Arc Plasma"], 0)
        self.assertEqual(pooled["Arc Plasma"], 60)
        self.assertEqual(set(+started), {"Machine Gun", "Force Blaster", "Snake", "Lance"})

    def test_two_coin_flip_guns_draw_between_them(self) -> None:
        menu = Menu.preset("standard").with_item(ENTRY["Force Blaster"], "random")
        started, pooled = self.guns(menu, 2)
        # The Machine Gun keeps its start; one of the two coin-flip guns is started too.
        self.assertEqual(started["Machine Gun"], 60)
        self.assertEqual(started["Force Blaster"] + started["Arc Plasma"], 60)
        self.assertEqual((pooled["Snake"], pooled["Lance"]), (60, 60))

    def guns_started(self, menu: Menu) -> int:
        start, _ = custom.resolve(menu, random.Random(3))
        return sum(start[i] for i in custom.GUNS)

    def test_off_the_guns_follow_their_states(self) -> None:
        menu = Menu.preset("standard").with_settings({custom.RANDOM_GUN: 0})
        start, pool = custom.resolve(menu, random.Random(3))
        self.assertEqual(start[ENTRY["Machine Gun"]], 1)
        self.assertEqual(start[ENTRY["Force Blaster"]], 0)


class TestGunsFirst(unittest.TestCase):
    def test_a_pooled_upgrade_without_its_gun_is_swapped_for_it(self) -> None:
        menu = Menu.preset("standard").with_item(ENTRY["Lance"], "excluded")
        _, pool = custom.resolve(menu, random.Random(1))
        self.assertEqual((pool[ENTRY["Lance"]], pool[ENTRY["Focal Crystal"]]), (1, 0))

    def test_upgrades_are_their_guns_progressive_items(self) -> None:
        menu = Menu.preset("standard")
        _, pool = custom.resolve(menu, random.Random(1))
        items = custom.pool_items(menu, pool, random.Random(1))
        self.assertFalse(set(items) & {9, 10, 12, 14, 15})
        start, _ = custom.resolve(menu.with_item(ENTRY["Pulse Wave"], "start"), random.Random(1))
        have = custom.starting_items(menu, start)
        self.assertNotIn(9, have)

    def test_off_no_swap(self) -> None:
        menu = Menu.preset("standard").with_item(ENTRY["Lance"], "excluded")
        menu = menu.with_settings({custom.GUNS_FIRST: 0})
        _, pool = custom.resolve(menu, random.Random(1))
        self.assertEqual((pool[ENTRY["Lance"]], pool[ENTRY["Focal Crystal"]]), (0, 1))

    def test_off_each_upgrade_is_its_own_item(self) -> None:
        menu = Menu.preset("standard").with_settings({custom.GUNS_FIRST: 0})
        _, pool = custom.resolve(menu, random.Random(1))
        items = Counter(custom.pool_items(menu, pool, random.Random(1)))
        # Standard pools all five upgrades: each is in the pool once, as itself.
        self.assertEqual([items[c] for c in (9, 10, 12, 14, 15)], [1] * 5)
        self.assertTrue(all(items[c] <= 1 for c in (3, 4, 5, 6, 7)))
        start, _ = custom.resolve(menu.with_item(ENTRY["Pulse Wave"], "start"), random.Random(1))
        have = custom.starting_items(menu, start)
        self.assertEqual(have[9], 1)

    def test_off_the_logic_redirects_nothing(self) -> None:
        on = Menu.preset("standard").logic_settings()
        off = Menu.preset("standard").with_settings({custom.GUNS_FIRST: 0}).logic_settings()
        self.assertTrue(on.guns_first)
        self.assertFalse(off.guns_first)
        # A Plasma Burst alone counts as the Arc Plasma only with Guns First (0xE5680).
        self.assertEqual(reach.Seed.of(on, {12: 1}, []).start_items, ((7, 1),))
        self.assertEqual(reach.Seed.of(off, {12: 1}, []).start_items, ((12, 1),))
        # And a gun picked up twice counts as its upgrade only with it.
        self.assertEqual(reach.Seed.of(on, {}, [7, 7]).every_item, ((7, 1), (12, 1)))
        self.assertEqual(reach.Seed.of(off, {}, [7, 7]).every_item, ((7, 2),))
        # Without its gun an upgrade does nothing, by the game's rules (logic.derive): code 57
        # is Tunneler, Lance, S-Blade or Plasma Burst.
        self.assertFalse(logic.satisfied([(57, 1)], {12: 1}, off))
        self.assertTrue(logic.satisfied([(57, 1)], {7: 1, 12: 1}, off))


class TestTheMenusLimits(unittest.TestCase):
    def test_the_presets_hold(self) -> None:
        for name in ("easy", "standard", "ohko"):
            self.assertEqual(custom.problems(Menu.preset(name)), [], name)

    def test_energy_is_40_in_all(self) -> None:
        menu = Menu.preset("standard").with_amount(custom.POOL_ENERGY, 37)
        self.assertEqual(custom.problems(menu), [])
        self.assertIn("more than 40", custom.problems(menu.with_amount(custom.POOL_ENERGY, 38))[0])

    def test_missiles_and_bombs_are_999_in_all(self) -> None:
        # Pool missiles 996 with 3 starting, seen at the menu's top.
        menu = Menu.preset("standard").with_amount(custom.POOL_MISSILES, 996)
        menu = menu.with_amount(custom.POOL_ENERGY, 0).with_amount(custom.POOL_BOMBS, 0)
        self.assertEqual(custom.problems(menu), [])
        self.assertIn("999", custom.problems(menu.with_amount(custom.POOL_MISSILES, 997))[0])

    def test_ten_per_free_place(self) -> None:
        # Standard: 150 places less 17 Energy Tanks and 26 pooled entries leave 107; the 12 Mega
        # Bombs take 12, so missiles may be 950.
        menu = Menu.preset("standard").with_amount(custom.START_MISSILES, 0)
        self.assertEqual(custom.problems(menu.with_amount(custom.POOL_MISSILES, 950)), [])
        found = custom.problems(menu.with_amount(custom.POOL_MISSILES, 951))
        self.assertTrue(found and "950" in found[0], found)
        # Mega Bombs from 20 count as boxes: 30 take 3 places, not 30. With 37 pool Energy
        # Tanks, 150 - 37 - 26 = 87 free, less 3: 840 missiles.
        bombs = menu.with_amount(custom.POOL_BOMBS, 30).with_amount(custom.POOL_ENERGY, 0)
        bombs = bombs.with_amount(custom.START_ENERGY, 0).with_amount(custom.POOL_ENERGY, 37)
        self.assertEqual(custom.problems(bombs.with_amount(custom.POOL_MISSILES, 840)), [])
        found = custom.problems(bombs.with_amount(custom.POOL_MISSILES, 841))
        self.assertTrue(found and "840" in found[0], found)

    def test_more_fixed_items_than_places(self) -> None:
        menu = Menu.preset("standard").with_amount(custom.POOL_ENERGY, 37)
        menu = menu.with_amount(custom.START_ENERGY, 0)
        self.assertEqual(custom.problems(menu), [])  # 4 + 26 + 37 = 67 of 154
        self.assertEqual(custom.most_pooled(menu), 26)

    def test_random_gun_pools_every_gun_but_one_at_most(self) -> None:
        menu = Menu.preset("standard")
        for gun in ("Machine Gun", "Force Blaster", "Snake", "Lance", "Arc Plasma"):
            menu = menu.with_item(ENTRY[gun], "random")
        self.assertEqual(custom.most_pooled(menu), 26)
        self.assertEqual(custom.most_pooled(menu.with_settings({custom.RANDOM_GUN: 0})), 27)


class TestRandomStart(unittest.TestCase):
    rooms = data.START_ROOMS

    def test_flagged_rooms_need_the_score(self) -> None:
        def every(score: int) -> set[int]:
            got = set()
            for seed in range(400):
                room = custom.random_start(
                    self.rooms, score, 0, lambda r: True, random.Random(seed)
                )
                got.add(room["index"])
            return got

        flagged = {i for i, (_, _, f) in enumerate(data.START_TABLE) if f}
        self.assertEqual(flagged, {0, 2, 25, 27, 29, 30, 32, 33})
        self.assertFalse(every(26) & flagged)
        self.assertEqual(every(27), set(range(34)))

    def test_the_first_tower_room_is_kept_one_time_in_three(self) -> None:
        towers = [r for r in self.rooms if custom.tower(r)]
        self.assertEqual([r["index"] for r in towers], [1, 4, 11, 14])
        one = [towers[0], self.rooms[6]]  # Upper Tower and Landing Pod
        picks = Counter()
        for seed in range(3000):
            room = custom.random_start(one, 0, 2, lambda r: True, random.Random(seed))
            picks[room["index"]] += 1
        # Upper Tower first half the time, then kept one in three: about 1/6 in all.
        self.assertAlmostEqual(picks[1] / 3000, 1 / 6, delta=0.03)
        light = Counter(
            custom.random_start(one, 0, 0, lambda r: True, random.Random(s))["index"]
            for s in range(3000)
        )
        self.assertAlmostEqual(light[1] / 3000, 1 / 2, delta=0.04)

    def test_none_when_no_room_passes(self) -> None:
        self.assertIsNone(custom.random_start(self.rooms, 0, 2, lambda r: False, random.Random()))

    def test_the_score(self) -> None:
        self.assertEqual(custom.score(data.MODES["easy"]["start_inventory"]), 5 + 3 + 3)
        self.assertEqual(custom.score({0: 3, 36: 2, 1: 4, 3: 2}), 3 + 24 + 2)


class TestTheOptionsRefuse(unittest.TestCase):
    def refused(self, **options: Any) -> str:
        with self.assertRaises(OptionError) as caught:
            fake_world(**options).generate_early()
        return str(caught.exception)

    def test_custom_settings_with_a_preset(self) -> None:
        for name, value in (
            ("x_blocks", "on"),
            ("hints", "revealed"),
            ("start_keys", 2),
            ("pool_missiles", 50),
            ("custom_items", {"Navcore": "start"}),
            ("random_gun", "off"),
        ):
            with self.subTest(name):
                self.assertIn(name, self.refused(mode="standard", **{name: value}))

    def test_start_room_and_safety_go_with_any_preset(self) -> None:
        for safety in ("light", "early", "normal", "off"):
            world = fake_world(mode="easy", safety=safety, start_room="landing_pod")
            world.generate_early()
            self.assertEqual(
                world.menu.value(custom.SAFETY), ["light", "early", "normal", "off"].index(safety)
            )
        fake_world(mode="ohko", safety="off").generate_early()
        self.assertIn("OHKO", self.refused(mode="ohko", safety="normal"))

    def test_abridged_finale_with_the_disk_goal(self) -> None:
        self.assertIn("OS Install Disks", self.refused(mode="custom", abridged_finale="on", goal=1))

    def test_what_is_not_supported_yet(self) -> None:
        self.assertIn("secret_shuffle", self.refused(mode="custom", secret_shuffle="on"))
        self.assertIn("starting_mega_bombs", self.refused(mode="custom", starting_mega_bombs=5))

    def test_what_the_menu_cannot_hold(self) -> None:
        self.assertIn(
            "more than 40",
            self.refused(mode="custom", starting_energy_tanks=20, pool_energy_tanks=21),
        )
        self.assertIn("950", self.refused(mode="custom", starting_missiles=0, pool_missiles=951))

    def test_an_unknown_state(self) -> None:
        self.assertIn("'maybe'", self.refused(mode="custom", custom_items={"Navcore": "maybe"}))
        self.assertEqual(len(CustomItems.valid_keys), 27)


class TestCustomAsItsBase(unittest.TestCase):
    def test_nothing_set_is_custom_base(self) -> None:
        for base in ("easy", "standard", "ohko"):
            world = fake_world(mode="custom", custom_base=base)
            world.generate_early()
            with self.subTest(base):
                self.assertEqual(world.menu, Menu.preset(base))
                self.assertEqual(len(world.pool_codes), 154)

    def test_each_setting_lands_in_its_byte(self) -> None:
        world = fake_world(
            mode="custom",
            hints="revealed",
            gates="open",
            safety="early",
            start_keys=3,
            x_crystals="on",
            x_blocks="on",
            abridged_finale="on",
            obscure_minors="off",
            obscure_class="on",
            show_obtainable="off",
            bulk_bombs="on",
            box_compression="off",
            double_teleport="off",
        )
        world.generate_early()
        menu = world.menu
        self.assertEqual(menu.value(custom.HINTS), 7)
        self.assertEqual(menu.value(custom.GATES), 3)
        self.assertEqual(menu.value(custom.SAFETY), 1)
        self.assertEqual(menu.start_keys, 3)
        for t, v in ((30, 1), (31, 1), (32, 1), (38, 0), (41, 1), (11, 0), (17, 1), (28, 0)):
            self.assertEqual(menu.setting(t), v, t)
        self.assertEqual(world.logic_settings.mode69, 1)
        self.assertEqual(world.logic_settings.options[31], 1)
        self.assertEqual(world.start_inventory.get(29), 1)  # the Gamma Key: Alpha onward
        self.assertNotIn(30, world.start_inventory)


class CustomSeed(ZexionTestBase):
    """A whole Custom seed: generated and filled by the test base, which also checks that every
    location can be reached and the goal won."""

    options = {
        "mode": "custom",
        "custom_items": {
            "Machine Gun": "random",
            "Force Blaster": "random",
            "Navcore": "start",
            "Power Core": "excluded",
            "Pulse Wave": "start",
        },
        "starting_energy_tanks": 6,
        "pool_energy_tanks": 20,
        "starting_missiles": 25,
        "pool_missiles": 200,
        "pool_mega_bombs": 30,
        "box_compression": "off",
        "start_keys": 2,
        "x_crystals": "on",
        "x_blocks": "on",
        "abridged_finale": "on",
        "hints": "large",
        "safety": "early",
    }

    def test_the_starting_items(self) -> None:
        have = Counter(item.name for item in self.multiworld.precollected_items[self.player])
        self.assertEqual(have["Energy Tank"], 6)
        self.assertEqual((have["Missile Box"], have["Missile Container"]), (2, 5))
        self.assertEqual((have["Alpha Key"], have["Beta Key"], have["Gamma Key"]), (1, 1, 0))
        self.assertEqual(have["Navcore"], 1)
        # The Pulse Wave and one of the two coin-flip guns: guns as progressive items.
        guns = sum(have[NAMES[c]] for c in (3, 4, 5, 6, 7))
        self.assertEqual(guns, 2)

    def test_the_pool(self) -> None:
        pool = pool_names(self)
        self.assertEqual(len(pool), len(self.world.kept_locations))
        self.assertEqual(pool.count("Energy Tank"), 20)
        self.assertEqual((pool.count("Gamma Key"), pool.count("Omega Key")), (1, 1))
        self.assertNotIn("Power Core", pool)
        self.assertNotIn("Alpha Key", pool)

    def test_the_slot_data_carries_the_settings(self) -> None:
        slot = self.world.fill_slot_data()
        self.assertEqual(slot["mode"], "custom")
        self.assertEqual(slot["amounts"], [6, 20, 25, 200, 0, 30])
        options = {int(t): v for t, v in slot["options"].items()}
        self.assertEqual(
            (options[30], options[31], options[32], options[42], options[43]), (1, 1, 1, 1, 0)
        )
        menu = slot["menu"]
        self.assertEqual(menu["start"][ENTRY["Machine Gun"]], 1)
        self.assertEqual(menu["pool"][ENTRY["Machine Gun"]], 1)  # the menu holds the coin flip
        self.assertEqual(menu["pool"][ENTRY["Power Core"]], 0)
        location = sum(menu["settings"][t - 10] << i for i, t in enumerate(custom.START_LOCATION))
        self.assertEqual(location, self.world.start["index"] + 1)
        self.assertEqual(menu["settings"][custom.BOX_COMPRESSION - 10], 0)
        keys = {int(k): v for k, v in slot["start_items"].items()}
        self.assertEqual((keys.get(27), keys.get(28), keys.get(29)), (1, 1, None))


class CustomSeedGunsFirstOff(ZexionTestBase):
    """Guns First off: each upgrade is an item of its own, named as the game names it, and the
    game's item table holds it as itself."""

    options = {
        "mode": "custom",
        "guns_first": "off",
        "random_gun": "off",
        "custom_items": {"Pulse Wave": "start", "Snake": "excluded"},
    }

    def test_the_items(self) -> None:
        have = Counter(item.name for item in self.multiworld.precollected_items[self.player])
        self.assertEqual((have["Progressive Machine Gun"], have["Pulse Wave"]), (1, 1))
        pool = pool_names(self)
        for name in ("Pentashot", "Focal Crystal", "Plasma Burst", "Tunneler"):
            self.assertEqual(pool.count(name), 1, name)
        for name in ("Progressive Force Blaster", "Progressive Lance", "Progressive Arc Plasma"):
            self.assertEqual(pool.count(name), 1, name)
        self.assertNotIn("Progressive Snake", pool)  # excluded, and no swap for its Tunneler
        self.assertNotIn("Progressive Machine Gun", pool)

    def test_the_slot_data(self) -> None:
        slot = self.world.fill_slot_data()
        self.assertEqual(slot["options"]["10"], 0)
        start = {int(k): v for k, v in slot["start_items"].items()}
        self.assertEqual((start[3], start[9]), (1, 1))
        table = slot["location_items"]
        names = {NAMES[code]: code for code in (14, 10, 12, 15)}
        held = 0
        for location in self.multiworld.get_locations(self.player):
            item = location.item
            if item is None or item.player != self.player or item.name not in names:
                continue
            # Each upgrade the world placed here is held as itself.
            self.assertEqual(table[location.address - LOCATION_ID_BASE], names[item.name])
            held += 1
        self.assertLessEqual(held, 4)


class CustomSeedShortOfFiller(ZexionTestBase):
    """No filler at all (Box Compression off, everything fits as singles) and the disk goal:
    the game's compression makes Scrap for the disks and the locations left out."""

    options = {
        "mode": "custom",
        "box_compression": "off",
        "pool_missiles": 107 - 12,
        "goal": "os_install_disks",
        "os_install_disks": 20,
        "os_install_disks_required": 10,
    }

    def test_the_disks_found_room(self) -> None:
        pool = pool_names(self)
        self.assertEqual(pool.count("OS Install Disk"), 20)
        self.assertEqual(len(pool), len(self.world.kept_locations))
        self.assertGreaterEqual(pool.count("Missile Box"), 1)
        self.assertIn(SCRAP_METAL, self.world.pool_codes)


class CustomDoubleTeleportRandomStart(ZexionTestBase):
    options = {
        "mode": "custom",
        "custom_base": "ohko",
        "start_room": "random_start",
        "x_crystals": "on",
    }

    def test_two_starts(self) -> None:
        self.assertIsNotNone(self.world.second)
        self.assertNotEqual(self.world.start, self.world.second)
        menu = self.world.fill_slot_data()["menu"]
        self.assertIsNone(menu["second_target"])
        self.assertEqual(len(menu["start_table"]), 34)


class EveryStartFills(ZexionTestBase):
    """Every room the world takes for Standard and OHKO fills and can be beaten, but those
    from which the starting items reach fewer than 3 locations (in OHKO, with the second
    teleport target drawn, counted as the logic counts it): Archipelago's fill can fail there,
    as the owner accepted (docs/re/locations.md, "Which start rooms the world offers"), so a
    test of them would fail at random."""

    auto_construct = False
    run_default_tests = False

    def test_every_room(self) -> None:
        for mode in ("standard", "ohko"):
            probe = fake_world(mode=mode)
            probe.generate_early()
            rooms = [r for r in data.START_ROOMS if probe.passes(r)]
            for room in rooms:
                with self.subTest(mode=mode, room=room["index"]):
                    self.options = {"mode": mode, "start_room": room["index"] + 1}
                    self.world_setup()
                    w = self.world
                    if len(w.seed_reach.sphere0_from(w.start, w.second).locations) < 3:
                        continue
                    distribute_items_restrictive(self.multiworld)
                    self.assertTrue(self.multiworld.can_beat_game(self.multiworld.state))


class TestTheLogicReviewsFindings(unittest.TestCase):
    def test_a_started_gun_and_its_upgrade_count_as_both(self) -> None:
        seed = reach.Seed.of(Menu.preset("standard").logic_settings(), {7: 2}, [])
        self.assertEqual(seed.start_items, ((7, 1), (12, 1)))
        self.assertEqual(seed.every_item, ((7, 1), (12, 1)))

    def test_the_cores_own_lists_are_judged_with_the_start(self) -> None:
        # Into the core: code 51 (Wall Jump Boots with G-Diffuser or Gravity Harness, or the
        # Zenith Thrusters) or the Jump Booster (data.CORE).
        out = ("Jump Booster", "Zenith Thrusters", "G-Diffuser", "Gravity Harness")
        items = dict.fromkeys(out, "excluded")
        with self.assertRaisesRegex(OptionError, "no start room|does not suit"):
            fake_world(mode="custom", custom_items=items).generate_early()


class TestTheSpoilerNamesPreset(unittest.TestCase):
    def test_an_amount_left_preset_reads_preset(self) -> None:
        from ..options import StartingMissiles, StartKeys

        self.assertEqual(StartingMissiles(-1).current_option_name, "Preset")
        self.assertEqual(StartingMissiles(25).current_option_name, "25")
        self.assertEqual(StartKeys(-1).current_option_name, "Preset")
