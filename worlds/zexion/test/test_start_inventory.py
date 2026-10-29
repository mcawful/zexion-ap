"""Starting items as the game's randomizer has them: a starting item comes out of the pool
(docs/re/randocells.md, "The pool", step 1). start_inventory_from_pool does that; a
start_inventory copy of an item the pool holds too, and the game holds once, is refused
(worlds/zexion/starting.py, ``pool_duplicates``)."""

import unittest

from Options import OptionError

from .. import data, starting
from ..items import NAMES, code_of
from ..options import Goal
from .bases import ZexionStartTestBase
from .test_world import fake_world, pool_names

SLIDE_MOD, SNAKE = code_of["Slide Mod"], code_of["Progressive Snake"]


class TestStartFromPool(ZexionStartTestBase):
    """Standard, starting with a Slide Mod and a Snake from its pool."""

    options = {"start_inventory_from_pool": {"Slide Mod": 1, "Progressive Snake": 1}}

    def test_the_items_leave_the_pool_for_filler(self) -> None:
        pool = pool_names(self)
        self.assertNotIn("Slide Mod", pool)
        self.assertEqual(pool.count("Progressive Snake"), self.world.pool_codes.count(SNAKE) - 1)
        self.assertEqual(len(pool), len(self.world.kept_locations))

    def test_the_clients_start_items_hold_them(self) -> None:
        start = {int(k): v for k, v in self.world.fill_slot_data()["start_items"].items()}
        self.assertEqual((start.get(SLIDE_MOD), start.get(SNAKE)), (1, 1))
        # With the mode's own (Standard's Energy Tanks, Missile Containers and Force Blaster).
        for code, n in data.MODES["standard"]["start_inventory"].items():
            self.assertEqual(start.get(code), n, NAMES[code])

    def test_the_reach_starts_with_them_and_counts_them_once(self) -> None:
        start = dict(self.world.seed_reach.start_items)
        self.assertEqual((start.get(SLIDE_MOD), start.get(SNAKE)), (1, 1))
        every = dict(self.world.seed_reach.every_item)
        self.assertEqual(every.get(SLIDE_MOD), 1)
        # With Guns First, two Snakes are the Snake and its upgrade, the Tunneler.
        self.assertEqual((every.get(SNAKE), every.get(code_of["Tunneler"])), (1, 1))


class TestDiscsFromPool(ZexionStartTestBase):
    options = {
        "goal": "os_install_disks",
        "os_install_disks": 10,
        "os_install_disks_required": 10,
        "start_inventory_from_pool": {"OS Install Disk": 2},
    }

    def test_the_disks_move_from_the_pool_to_the_start(self) -> None:
        self.assertEqual(pool_names(self).count("OS Install Disk"), 8)
        self.assertEqual(self.world.fill_slot_data()["discs_start"], 2)


class TestPoolDuplicatesRefused(unittest.TestCase):
    def refused(self, start_inventory: dict[str, int], **options) -> str:
        world = fake_world(start_inventory=start_inventory, **options)
        with self.assertRaises(OptionError) as caught:
            world.generate_early()
        return str(caught.exception)

    def test_an_item_held_once_that_the_pool_holds(self) -> None:
        for name in ("Slide Mod", "Navcore", "Alpha Key", "Power Core"):
            with self.subTest(name):
                error = self.refused({name: 1})
                self.assertIn(f"{name}: the item pool holds it too", error)
                self.assertIn("start_inventory_from_pool", error)
            fake_world(start_inventory_from_pool={name: 1}).generate_early()

    def test_a_weapon_past_its_weapon_and_upgrade(self) -> None:
        # Standard's pool holds two Snakes, and it starts with a Force Blaster, its pool another.
        for name in ("Progressive Snake", "Progressive Force Blaster"):
            with self.subTest(name):
                self.assertIn(f"{name}: the item pool holds it too", self.refused({name: 1}))
            fake_world(start_inventory_from_pool={name: 1}).generate_early()

    def test_with_guns_first_off_a_gun_or_upgrade_the_pool_holds(self) -> None:
        off = {"mode": "custom", "guns_first": "off", "random_gun": "off"}
        for name in ("Progressive Lance", "Tunneler"):
            with self.subTest(name):
                self.assertIn(f"{name}: the item pool holds it too", self.refused({name: 1}, **off))
            fake_world(start_inventory_from_pool={name: 1}, **off).generate_early()

    def test_counted_items_and_items_the_pool_lacks_pass(self) -> None:
        counted = {"Energy Tank": 2, "Missile Container": 2, "Missile Box": 1}
        fake_world(start_inventory=counted).generate_early()
        # With the OS Install Disks goal the pool has no keys, and the disks are counted.
        disc_hunt = {"goal": Goal.option_os_install_disks}
        start = {"Alpha Key": 1, "OS Install Disk": 2}
        fake_world(start_inventory=start, **disc_hunt).generate_early()
        # The Snake and its upgrade excluded: the pool holds none.
        excluded = {"mode": "custom", "custom_items": {"Snake": "excluded", "Tunneler": "excluded"}}
        fake_world(start_inventory={"Progressive Snake": 2}, **excluded).generate_early()

    def test_from_pool_items_meet_the_new_games_limits(self) -> None:
        world = fake_world(start_inventory_from_pool={"Scrap Metal": 1})
        with self.assertRaises(OptionError) as caught:
            world.generate_early()
        self.assertIn("a new game cannot start with Scrap", str(caught.exception))
        world = fake_world(start_inventory_from_pool={"Progressive Snake": 3})
        with self.assertRaises(OptionError):
            world.generate_early()

    def test_from_pool_disks_past_the_placed_ones_count(self) -> None:
        def world(required: int):
            return fake_world(
                goal=Goal.option_os_install_disks,
                start_inventory_from_pool={"OS Install Disk": 12},
                placed=10,
                required=required,
            )

        world(12).generate_early()  # 10 from the pool, and 2 Archipelago starts with anyway
        with self.assertRaises(OptionError):
            world(13).generate_early()

    def test_the_world_counts_what_the_client_counts(self) -> None:
        # client/reconcile.py's COUNTED, by the world's names (test_client_agrees checks codes).
        names = {"Energy Tank", "Missile Container", "Mega Bomb", "Missile Box", "Bomb Box"}
        self.assertEqual({NAMES[c] for c in starting.COUNTED}, names | {"Scrap Metal"})

    def test_pool_duplicates_counts_the_seeds_own_start(self) -> None:
        def found(start: dict[int, int], guns_first: bool = True) -> list[str]:
            return starting.pool_duplicates({SNAKE: 1}, start, [SNAKE], guns_first)

        self.assertEqual(found({}), [])  # the weapon and its upgrade
        self.assertEqual(len(found({SNAKE: 1})), 1)
        self.assertEqual(len(found({}, guns_first=False)), 1)  # a gun is held once
