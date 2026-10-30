"""Custom mode: the game's pool rule, Random Gun and Random Start, the menu's limits, the options'
refusals, and whole Custom seeds (docs/re/randocells.md, "The pool")."""

import random
import unittest
from collections import Counter
from unittest import mock

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
from .bases import FLAGGED, ZexionTestBase
from .test_world import STANDARD_GUNS, RefusalTestCase, drawing, drawn_gun, fake_world, pool_names

FILLER = {46, 47, 48, 49, 50}
ENTRY = {name: i for i, (name, _) in enumerate(custom.MENU_ITEMS)}


def supplies(items: list[int]) -> tuple[int, int, int, int, int]:
    """(Missile Boxes, Bomb Boxes, Mega Bombs, Missile Containers, filler) in a pool."""
    n = Counter(items)
    filler = sum(n[c] for c in FILLER)
    return n[MISSILE_BOX], n[BOMB_BOX], n[MEGA_BOMB], n[MISSILE_CONTAINER], filler


def pool_of(menu: Menu, seed: int = 1) -> list[int]:
    """The item pool from the menu, each step drawn with random.Random(seed)."""
    _, pool = custom.resolve(menu, random.Random(seed))
    return custom.pool_items(menu, pool, random.Random(seed))


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
        # Easy: R = 91, 17 boxes, 29 containers, 18 Mega Bombs, 27 filler; Standard: R = 107,
        # 6 boxes, 62 containers, 12 Mega Bombs (docs/re/randocells.md, "The pool").
        for name, want in (("easy", (17, 0, 18, 29, 27)), ("standard", (6, 0, 12, 62, 27))):
            mode = data.MODES[name]
            menu = Menu.preset(name)
            pool, rng = resolved_as(menu, mode["start_inventory"])
            items = custom.pool_items(menu, pool, rng)
            with self.subTest(name):
                self.assertEqual(len(items), 154)
                self.assertEqual(supplies(items), want)
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


class TestThePresetsDraw(unittest.TestCase):
    """A preset seed's items are drawn from its menu as Custom's are (world.py, generate_early),
    as the game draws them for each new game: Standard's starting gun by Random Gun, and every
    mode's filler. Where nothing is drawn, the items are the fresh save's."""

    def worlds(self, mode: str):
        for seed in range(60):
            world = fake_world(mode=mode)
            world.random = random.Random(seed)
            world.generate_early()
            yield world

    def test_standard_draws_each_of_its_four_guns_and_only_those(self) -> None:
        # The Machine Gun start-only and the Force Blaster, Snake and Lance pool-only in
        # Standard's menu: one of the four is started, the rest pooled. The Arc Plasma, set both
        # ways, is no draw of Random Gun's and always pooled.
        guns = {custom.MENU_CODES[i] for i in custom.GUNS}
        mode = data.MODES["standard"]
        save = Counter(mode["start_inventory"]) + Counter(
            c for c in mode["pool"] if c not in FILLER
        )
        drawn = Counter()
        for world in self.worlds("standard"):
            start = world.start_inventory
            (gun,) = guns & set(start)
            drawn[gun] += 1
            self.assertEqual(start, {ENERGY_TANK: 3, MISSILE_CONTAINER: 3, gun: 1})
            pool = Counter(c for c in world.pool_codes if c not in FILLER)
            self.assertEqual(Counter(start) + pool, save, "the fresh save's items but filler")
        self.assertEqual({NAMES[c] for c in drawn}, set(STANDARD_GUNS))

    def test_easy_and_ohko_start_with_their_fresh_saves_items(self) -> None:
        # Nothing in their starting items is drawn. Easy's pool is its save's but filler; OHKO's
        # holds the save's items but its supplies, which the save's placement compressed once
        # (TestThePresetsPools).
        for name in ("easy", "ohko"):
            mode = data.MODES[name]
            supply = {MISSILE_BOX, BOMB_BOX, MEGA_BOMB, MISSILE_CONTAINER}
            skip = FILLER | (supply if name == "ohko" else set())
            save = Counter(c for c in mode["pool"] if c not in skip)
            fillers = set()
            for world in self.worlds(name):
                with self.subTest(name):
                    self.assertEqual(world.start_inventory, mode["start_inventory"])
                    self.assertEqual(Counter(c for c in world.pool_codes if c not in skip), save)
                fillers.add(tuple(sorted(c for c in world.pool_codes if c in FILLER)))
            self.assertGreater(len(fillers), 1, "the filler is drawn per seed")


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
        self.assertEqual(supplies(pool_of(menu)), (3, 0, 12, 92, 0))

    def test_seed_c_bulk_missiles(self) -> None:
        menu = Menu.preset("standard").with_settings({custom.BULK_MISSILES: 1})
        self.assertEqual(supplies(pool_of(menu)), (12, 0, 12, 2, 81))

    def test_the_menus_counter_for_the_bulk_settings(self) -> None:
        # Read on screen in Standard's state: Bulk Bombs alone, one Bomb Box, 2 Mega Bombs and
        # five Missile Boxes; both, 12 Missile Boxes and one Bomb Box.
        bombs = Menu.preset("standard").with_settings({custom.BULK_BOMBS: 1})
        both = bombs.with_settings({custom.BULK_MISSILES: 1})
        for menu, want in ((bombs, (5, 1, 2)), (both, (12, 1, 2))):
            with self.subTest(want=want):
                self.assertEqual(supplies(pool_of(menu))[:3], want)


def every_outcome(m: int, b: int, boxes: int) -> set[int]:
    """The entries step 5 can end with, the boxes drawn, over every draw: each draw's 0, its
    top and the bombs left less 1 cover both of the rule's outcomes."""
    states = {(0, m, b)}
    for _ in range(boxes):
        after = set()
        for n, mm, bb in states:
            draws = {0, mm + bb, bb - 1} & set(range(mm + bb + 1)) if mm + bb >= 0 else {0}
            for d in draws:
                if (d >= bb or bb < 10) and mm >= 10:
                    after.add((n + 1, mm - 10, bb))
                else:
                    after.add((n + (bb != 0), mm, bb - 10))
        states = after
    return {n + max(mm, 0) + max(bb, 0) for n, mm, bb in states}


class TestMoreBoxesThanTens(unittest.TestCase):
    """Box Compression's 25 can make more boxes than the missiles and bombs have tens: the game
    makes them all, a box drawn once both are below 0 a Bomb Box (0xF2350 to 0xF23BF;
    docs/re/randocells.md, "The pool", step 5)."""

    def menu(self, missiles: int, bombs: int) -> Menu:
        menu = Menu.preset("standard").with_amount(custom.POOL_MISSILES, missiles)
        return menu.with_amount(custom.POOL_BOMBS, bombs)

    def test_the_box_past_the_bombs_is_a_bomb_box(self) -> None:
        # 40 for 6 places: ceil((40 - 6 + 25) / 9) = 7 boxes, at most 6, for 4 tens. The fifth
        # box finds the bombs at exactly 0 and is not counted; the sixth is drawn from below 0.
        menu = self.menu(20, 20)
        for seed in range(30):
            got = Counter(custom.supplies(menu, 6, random.Random(seed)))
            self.assertEqual(got, Counter({MISSILE_BOX: 2, BOMB_BOX: 3}), seed)

    def test_standard_with_900_mega_bombs_generates(self) -> None:
        # The finding's state: once a crash in the draw. R = 107: 94 boxes for 92 tens.
        menu = self.menu(20, 900)
        self.assertEqual(custom.problems(menu), [])
        self.assertEqual(
            Counter(custom.supplies(menu, 107, random.Random(1))),
            Counter({MISSILE_BOX: 2, BOMB_BOX: 91}),
        )
        for seed in range(20):
            self.assertEqual(len(pool_of(menu, seed)), 154)

    def test_bombs_first_is_the_longest_list(self) -> None:
        rng = random.Random(5)
        for _ in range(300):
            m, b, room = rng.randrange(20, 300), rng.randrange(20, 300), rng.randrange(1, 60)
            if m + b <= room:
                continue
            menu = self.menu(m, b)
            boxes = min(custom._ceil9(m + b - room + 25), room)
            with self.subTest(m=m, b=b, room=room):
                self.assertEqual(
                    len(custom.supplies(menu, room, custom._BombsFirst())),
                    max(every_outcome(m, b, boxes)),
                )


class TestListsLongerThanThePlaces(unittest.TestCase):
    """The menu allows states whose list can be longer than the places, which the game cuts at
    random, keys and items among them (step 7); problems refuses them."""

    def test_bulk_missiles_past_the_places(self) -> None:
        # Bulk Missiles: 46 boxes and 9 containers; the bombs' 53 boxes take them below 0.
        menu = Menu.preset("standard").with_settings({custom.BULK_MISSILES: 1})
        menu = menu.with_amount(custom.POOL_MISSILES, 469).with_amount(custom.POOL_BOMBS, 499)
        self.assertEqual(custom.most_entries(menu), 155)
        found = custom.problems(menu)
        self.assertTrue(any("can come to 155, more than its 154 places" in f for f in found))
        fits = menu.with_amount(custom.POOL_MISSILES, 459)
        self.assertEqual(custom.problems(fits), [])

    def test_the_coin_flips_counted(self) -> None:
        menu = Menu.preset("standard")
        self.assertEqual(custom.pooled_counts(menu), range(26, 27))  # Random Gun's guns
        off = menu.with_settings({custom.RANDOM_GUN: 0})
        self.assertEqual(custom.pooled_counts(off), range(25, 27))  # the Arc Plasma's flip

    def test_whatever_problems_passes_builds(self) -> None:
        rng = random.Random(7)
        for trial in range(400):
            menu = Menu.preset(rng.choice(("easy", "standard", "ohko")))
            for i in rng.sample(range(custom.ARRAY), 6):
                menu = menu.with_item(i, rng.choice(list(custom.STATES)))
            flags = (custom.BULK_MISSILES, custom.BULK_BOMBS, custom.BOX_COMPRESSION)
            menu = menu.with_settings({t: rng.randrange(2) for t in flags})
            menu = menu.with_settings({custom.SECRET_SHUFFLE: rng.randrange(2)})
            menu = menu.with_amount(custom.POOL_MISSILES, rng.randrange(1000))
            menu = menu.with_amount(custom.POOL_BOMBS, rng.randrange(1000))
            if custom.problems(menu):
                continue
            for seed in range(3):
                with self.subTest(trial=trial, seed=seed):
                    self.assertEqual(len(pool_of(menu, seed)), menu.places)


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

    def test_off_the_guns_follow_their_states(self) -> None:
        menu = Menu.preset("standard").with_settings({custom.RANDOM_GUN: 0})
        start, _ = custom.resolve(menu, random.Random(3))
        self.assertEqual(start[ENTRY["Machine Gun"]], 1)
        self.assertEqual(start[ENTRY["Force Blaster"]], 0)


class TestGunsFirst(unittest.TestCase):
    def test_a_pooled_upgrade_without_its_gun_is_swapped_for_it_only_when_on(self) -> None:
        menu = Menu.preset("standard").with_item(ENTRY["Lance"], "excluded")
        for on, want in ((1, (1, 0)), (0, (0, 1))):
            _, pool = custom.resolve(menu.with_settings({custom.GUNS_FIRST: on}), random.Random(1))
            with self.subTest(guns_first=on):
                self.assertEqual((pool[ENTRY["Lance"]], pool[ENTRY["Focal Crystal"]]), want)

    def test_upgrades_are_their_guns_progressive_items(self) -> None:
        menu = Menu.preset("standard")
        self.assertFalse(set(pool_of(menu)) & {9, 10, 12, 14, 15})
        start, _ = custom.resolve(menu.with_item(ENTRY["Pulse Wave"], "start"), random.Random(1))
        have = custom.starting_items(menu, start)
        self.assertNotIn(9, have)

    def test_off_each_upgrade_is_its_own_item(self) -> None:
        menu = Menu.preset("standard").with_settings({custom.GUNS_FIRST: 0})
        items = Counter(pool_of(menu))
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
        # A started gun and its upgrade count as both (the logic review's finding).
        self.assertEqual(reach.Seed.of(on, {7: 2}, []).start_items, ((7, 1), (12, 1)))
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
        # Bombs take 12, so missiles may be 950. Box Compression off: with it, the 25 make 98
        # Missile Boxes, and with the 12 Mega Bombs the list is longer than the places
        # (TestListsLongerThanThePlaces).
        menu = Menu.preset("standard").with_amount(custom.START_MISSILES, 0)
        menu = menu.with_settings({custom.BOX_COMPRESSION: 0})
        compressed = menu.with_amount(custom.POOL_MISSILES, 950)
        found = custom.problems(compressed.with_settings({custom.BOX_COMPRESSION: 1}))
        self.assertTrue(found and "can come to 157" in found[0], found)
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

    @staticmethod
    def with_free(free: int, keys: int = 0, secret: int = 0) -> Menu:
        """Standard with Box Compression off, these Start Keys and Secret Shuffle, 40 pool
        Energy Tanks, and pool Mega Bombs in boxes (two or more) leaving the missiles ``free``
        places by the menu's count (0xD2110)."""
        menu = Menu.preset("standard").with_settings({custom.BOX_COMPRESSION: 0})
        menu = menu.with_value(custom.START_KEYS, keys)
        menu = menu.with_settings({custom.SECRET_SHUFFLE: secret})
        for which in (custom.START_ENERGY, custom.START_MISSILES, custom.START_BOMBS):
            menu = menu.with_amount(which, 0)
        menu = menu.with_amount(custom.POOL_ENERGY, 40)
        pooled = sum(1 for v in menu.pool if v)
        # The count, written out: the 150 places, the started keys and Secret Shuffle's 155th.
        boxes = 150 + keys + secret - pooled - 40 - free
        return menu.with_amount(custom.POOL_BOMBS, 10 * boxes)

    @staticmethod
    def missiles_refused(menu: Menu, missiles: int) -> bool:
        found = custom.problems(menu.with_amount(custom.POOL_MISSILES, missiles))
        return any(line.startswith("Item Pool Missiles") for line in found)

    def test_ten_per_place_from_two_free_places(self) -> None:
        two, one = self.with_free(2), self.with_free(1)
        self.assertFalse(self.missiles_refused(two, 20))
        self.assertTrue(self.missiles_refused(two, 21))
        self.assertFalse(self.missiles_refused(one, 1))  # one place: one missile
        self.assertTrue(self.missiles_refused(one, 2))

    def test_start_keys_and_secret_shuffle_free_places(self) -> None:
        for keys, secret in ((4, 0), (0, 1), (4, 1)):
            with self.subTest(keys=keys, secret=secret):
                menu = self.with_free(10, keys, secret)
                self.assertFalse(self.missiles_refused(menu, 100))
                self.assertTrue(self.missiles_refused(menu, 101))

    def test_started_keys_take_no_place_in_the_list(self) -> None:
        # Every item pooled, singles to the last place: with Start Keys 4, the four places the
        # keys would take hold missiles.
        base = Menu.preset("standard").with_settings(
            {custom.RANDOM_GUN: 0, custom.BOX_COMPRESSION: 0}
        )
        for i in range(custom.ARRAY):
            base = base.with_item(i, "pool")
        base = base.with_amount(custom.START_ENERGY, 0).with_amount(custom.POOL_BOMBS, 0)
        base = base.with_amount(custom.POOL_ENERGY, 40)
        for keys in (0, 4):
            with self.subTest(keys=keys):
                menu = base.with_value(custom.START_KEYS, keys)
                room = 154 - (4 - keys) - 27 - 40
                full = menu.with_amount(custom.POOL_MISSILES, room)
                self.assertEqual(custom.problems(full), [])
                self.assertEqual(custom.most_entries(full), 154)


class TestCompressionIsReachedAgain(unittest.TestCase):
    def test_the_kept_locations_are_the_compressed_pools(self) -> None:
        # Without the Lance and the Arc Plasma (and their upgrades, which Guns First would make
        # them), location 91 needs the X-Missile, which needs five Missile Containers. The pool's
        # 10 and the 3 started make 13; the locations left out need filler, and with none the
        # ten become a Missile Box (no Missile Container in the logic), so 91 and one more are
        # left out too.
        out = ("Lance", "Arc Plasma", "Focal Crystal", "Plasma Burst")
        world = fake_world(
            mode="custom",
            custom_items=dict.fromkeys(out, "excluded"),
            box_compression="off",
            pool_missiles=10,
            pool_mega_bombs=100,  # with the 17 pool Energy Tanks, no filler
        )
        world.generate_early()
        self.assertEqual(Counter(world.pool_codes)[MISSILE_BOX], 1)
        every = world.seed_reach.every_from(world.start, world.second)
        self.assertEqual(world.kept_locations, set(every.locations))
        self.assertEqual(len(world.kept_locations), 124)
        self.assertNotIn(91, world.kept_locations)


class TestTheCoinFlips(unittest.TestCase):
    def test_an_item_set_both_ways_ends_in_one_place(self) -> None:
        # Not a gun, which Random Gun draws again from the menu's states: the flip alone.
        menu = Menu.preset("standard").with_item(ENTRY["Slide Mod"], "random")
        i, seen = ENTRY["Slide Mod"], set()
        for seed in range(40):
            start, pool = custom.resolve(menu, random.Random(seed))
            self.assertEqual(start[i] + pool[i], 1, seed)
            seen.add(start[i])
        self.assertEqual(seen, {0, 1})


class TestFillerAndBoxes(unittest.TestCase):
    def test_the_filler_odds(self) -> None:
        # One draw in 16 per place (0xF25AC): Scrap metal 8, Scrap energy and Scrap chunk 1 each,
        # Area Map and Energy Shield 3 each.
        self.assertEqual(Counter(custom.FILLER_ODDS), {SCRAP_METAL: 8, 47: 1, 48: 1, 49: 3, 50: 3})

    def test_compression_takes_missile_containers_first_and_ten_are_enough(self) -> None:
        items = [MISSILE_CONTAINER] * 10 + [MEGA_BOMB] * 10
        out = custom.compress(items, 9, FILLER)
        self.assertEqual(Counter(out)[MISSILE_BOX], 1)
        self.assertEqual(Counter(out)[MEGA_BOMB], 10)
        self.assertEqual(Counter(out)[SCRAP_METAL], 9)
        with self.assertRaises(custom.PoolError):
            custom.compress([MISSILE_CONTAINER] * 9, 9, FILLER)

    def test_a_box_draw_of_the_bombs_left_is_a_missile_box(self) -> None:
        # Boxes drawn (no bulk, 20 or more of each): a draw at or above the bombs left is a
        # Missile Box (0xF2079 to 0xF25A0). 100 + 30 in 121 places: one box.
        menu = Menu.preset("standard").with_settings({custom.BOX_COMPRESSION: 0})
        menu = menu.with_amount(custom.POOL_MISSILES, 100).with_amount(custom.POOL_BOMBS, 30)

        class Draw:
            @staticmethod
            def randrange(n: int) -> int:
                return 30

        self.assertEqual(supplies(custom.supplies(menu, 121, Draw()))[:2], (1, 0))


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

        self.assertEqual(FLAGGED, {0, 2, 25, 27, 29, 30, 32, 33})
        self.assertFalse(every(26) & FLAGGED)
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

    def test_the_tower_check_is_once_per_start_game(self) -> None:
        # Two tower rooms only: once the first drawn is put back, the second is kept.
        towers = [r for r in self.rooms if custom.tower(r)][:2]
        for seed in range(200):
            self.assertIsNotNone(
                custom.random_start(towers, 0, 2, lambda r: True, random.Random(seed)), seed
            )

    def test_the_world_draws_with_its_safety_and_its_own_score(self) -> None:
        # The seed's Safety, and the score of the mode's own starting items (Standard: 3 Energy
        # Tanks, 3 missiles and its drawn gun), not start_inventory's.
        world = fake_world(
            start_room="random_start", safety="early", start_inventory={"Energy Tank": 30}
        )
        calls = []
        draw = custom.random_start

        def random_start(rooms, score, safety, passes, rng):
            calls.append((score, safety))
            return draw(rooms, score, safety, passes, rng)

        with mock.patch.object(custom, "random_start", random_start):
            world.generate_early()
        self.assertEqual(calls, [(7, 1)])

    def test_none_when_no_room_passes(self) -> None:
        self.assertIsNone(custom.random_start(self.rooms, 0, 2, lambda r: False, random.Random()))

    def test_the_score(self) -> None:
        self.assertEqual(custom.score(data.MODES["easy"]["start_inventory"]), 5 + 3 + 3)
        self.assertEqual(custom.score({0: 3, 36: 2, 1: 4, 3: 2}), 3 + 24 + 2)


class TestTheOptionsRefuse(RefusalTestCase):
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
        self.assertIn("starting_mega_bombs", self.refused(mode="custom", starting_mega_bombs=5))

    def test_an_unknown_state_or_one_that_is_not_text(self) -> None:
        self.assertEqual(len(CustomItems.valid_keys), 27)
        for state in ("maybe", ["start"], 1, None):
            with self.subTest(state=state):
                found = self.refused(mode="custom", custom_items={"Slide Mod": state})
                self.assertIn(f"{state!r} is not one of", found)

    def test_a_list_longer_than_the_places(self) -> None:
        found = self.refused(
            mode="custom", bulk_missiles="on", pool_missiles=469, pool_mega_bombs=499
        )
        self.assertIn("the game's randomizer cannot hold or build this", found)
        self.assertIn("more than its 154 places", found)

    def test_too_little_filler_for_what_is_left_out(self) -> None:
        # Bulk Missiles with 949: the Mega Bombs' formula makes them all boxes, so a location
        # needing Mega Bombs is left out, and the pool has no filler and no ten singles.
        found = self.refused(mode="custom", bulk_missiles="on", pool_missiles=949)
        self.assertIn("too little filler for the 1 locations", found)
        self.assertIn("Bulk Missiles and Bulk Bombs off", found)


class TestCustomAsItsBase(unittest.TestCase):
    def test_nothing_set_is_custom_base(self) -> None:
        for base in ("easy", "standard", "ohko"):
            world = fake_world(mode="custom", custom_base=base)
            world.generate_early()
            with self.subTest(base):
                self.assertEqual(world.menu, Menu.preset(base))
                self.assertEqual(len(world.pool_codes), 154)

    def test_each_setting_lands_in_its_byte(self) -> None:
        # Values whose bits differ when two are swapped, read back by the byte numbers
        # docs/re/randocells.md gives ("Where each setting is stored"), lowest bit first.
        world = fake_world(
            mode="custom",
            start_room="open_cave",  # Starting Location 18: 010010
            hints="large",  # 5: 0101
            gates="vanilla",  # 1: 01
            safety="early",  # 1: 01
            start_keys=1,  # 001
            x_crystals="on",
            x_blocks="on",
            abridged_finale="on",
            obscure_majors="off",
            obscure_minors="off",
            obscure_class="on",
            show_obtainable="off",
            bulk_bombs="on",
            box_compression="off",
            double_teleport="off",
        )
        world.generate_early()
        menu = world.menu
        self.assertEqual(menu.value(custom.HINTS), 5)
        self.assertEqual(menu.value(custom.GATES), 1)
        self.assertEqual(menu.value(custom.SAFETY), 1)
        self.assertEqual(menu.start_keys, 1)
        written = {18: 1, 19: 0, 20: 1, 21: 0, 72: 1, 73: 0, 42: 1, 43: 0, 34: 1, 35: 0, 36: 0}
        written |= {30: 1, 31: 1, 32: 1, 29: 0, 38: 0, 41: 1, 11: 0, 17: 1, 28: 0}
        for t, v in written.items():
            self.assertEqual(menu.setting(t), v, t)
        self.assertEqual(world.logic_settings.mode69, 1)
        self.assertFalse(world.logic_settings.gates_open)
        self.assertEqual(world.logic_settings.options[31], 1)
        self.assertEqual(world.start_inventory.get(27), 1)  # the Alpha Key
        self.assertNotIn(28, world.start_inventory)
        # Starting Location, in the menu the client sets up: room 17, value 18.
        world.multiworld.seed_name = "1"
        settings = world.menu_setup()["settings"]
        location = {t: settings[t - 10] for t in range(22, 28)}
        self.assertEqual(location, {22: 0, 23: 1, 24: 0, 25: 0, 26: 1, 27: 0})


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
        location = sum(menu["settings"][t - 10] << i for i, t in enumerate(range(22, 28)))
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
        # The Arc Plasma is a coin flip in Standard: pooled here, so the test is the same each run.
        "custom_items": {"Pulse Wave": "start", "Snake": "excluded", "Arc Plasma": "pool"},
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


class CustomSeedSecretShuffle(ZexionTestBase):
    """Secret Shuffle on: the Bomb Box at X48 Y20 is a 155th location, location index 154, which
    the game's list holds at position 134 (docs/re/randocells.md, "The pool")."""

    options = {"mode": "custom", "secret_shuffle": "on", "start_room": "landing_pod"}

    def test_the_155th_location(self) -> None:
        self.assertEqual(len(self.world.location_cells), 155)
        self.assertEqual(self.world.location_cells[154], data.SECRET_LOCATION)
        self.assertIn(154, self.world.kept_locations)
        location = self.multiworld.get_location("??? 48,20", self.player)
        self.assertEqual(location.address, LOCATION_ID_BASE + 154)
        self.assertEqual(len(pool_names(self)), len(self.world.kept_locations))

    def test_the_slot_data(self) -> None:
        slot = self.world.fill_slot_data()
        self.assertEqual(slot["options"]["37"], 1)
        self.assertEqual(len(slot["locations"]), 155)
        self.assertEqual(slot["locations"][154], list(data.SECRET_LOCATION))
        self.assertEqual(len(slot["location_items"]), 155)


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
    """Random Start with Double Teleport in Custom: the default tests fill and beat it (the two
    starts' own checks: test_world.py, TestOHKO and TestStartRooms)."""

    options = {
        "mode": "custom",
        "custom_base": "ohko",
        "start_room": "random_start",
        "x_crystals": "on",
    }


class EveryStartFills(ZexionTestBase):
    """Every room the world takes for Standard, with each gun it draws, and for OHKO fills and
    can be beaten, but those from which the starting items reach fewer than 3 locations (in
    OHKO, with the second teleport target drawn, counted as the logic counts it): Archipelago's
    fill can fail there, as the owner accepted (docs/re/locations.md, "Which start rooms the
    world offers"), so a test of them would fail at random. Each generation's seed is fixed and
    named in its subtest: the fill can fail, rarely, from a room that reaches more (the same
    section), and a failure must be one that can be run again."""

    auto_construct = False
    run_default_tests = False

    def seed_drawing(self, gun: str) -> int:
        """A generation seed whose Standard world draws ``gun``: the draw comes first in
        generate_early, so the start room does not change it."""
        for seed in range(1, 200):
            self.options = {"mode": "standard"}
            self.world_setup(seed)
            if drawn_gun(self.world) == gun:
                return seed
        raise AssertionError(f"no seed drew the {gun}")

    def reaches_three(self) -> bool:
        w = self.world
        return len(w.seed_reach.sphere0_from(w.start, w.second).locations) >= 3

    def test_every_room(self) -> None:
        cases = [("standard", gun) for gun in STANDARD_GUNS] + [("ohko", None)]
        for mode, gun in cases:
            probe = drawing(gun) if gun else fake_world(mode=mode)
            if not gun:
                probe.generate_early()
            rooms = [r for r in data.START_ROOMS if probe.passes(r)]
            # OHKO's second target is drawn: the first of these seeds whose pair reaches 3
            # locations or more.
            seeds = [self.seed_drawing(gun)] if gun else range(1, 11)
            for room in rooms:
                for seed in seeds:
                    self.options = {"mode": mode, "start_room": room["index"] + 1}
                    self.world_setup(seed)
                    if self.reaches_three():
                        break
                else:
                    continue
                with self.subTest(mode=mode, gun=gun, room=room["index"], seed=seed):
                    if gun:
                        self.assertEqual(drawn_gun(self.world), gun)
                    distribute_items_restrictive(self.multiworld)
                    self.assertTrue(self.multiworld.can_beat_game(self.multiworld.state))


class TestTheLogicReviewsFindings(unittest.TestCase):
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
