from collections import Counter
from dataclasses import replace

from test.bases import WorldTestBase

from .. import custom, data, reach
from ..world import ZexionWorld

# The start table's flagged rooms, by index.
FLAGGED = {i for i, (_, _, flag) in enumerate(data.START_TABLE) if flag}


def preset(name: str) -> reach.Seed:
    """A preset mode's seed: its menu's settings, and its fresh save's items (data.MODES)."""
    mode = data.MODES[name]
    settings = custom.Menu.preset(name).logic_settings()
    return reach.Seed.of(settings, mode["start_inventory"], mode["pool"])


def with_settings(seed: reach.Seed, **changes) -> reach.Seed:
    return replace(seed, settings=replace(seed.settings, **changes))


class ZexionTestBase(WorldTestBase):
    game = "Zexion"
    world: ZexionWorld


class ZexionStartTestBase(ZexionTestBase):
    """With what Archipelago's Main.py does for start_inventory and start_inventory_from_pool,
    which the test base does not run (as of AP 0.6.8): it precollects both options' items, and
    after generate_basic takes the from-pool items out of the item pool, a filler item from the
    world in each one's place. Done here after the test base's steps: no step of this world
    reads the precollected items before fill_slot_data."""

    def world_setup(self, seed: int | None = None) -> None:
        super().world_setup(seed)
        if not hasattr(self, "world"):
            return  # the test base skipped its setup
        options = self.world.options
        for option in (options.start_inventory, options.start_inventory_from_pool):
            for name, n in option.value.items():
                for _ in range(n):
                    self.multiworld.push_precollected(self.world.create_item(name))
        remove = Counter(options.start_inventory_from_pool.value)
        pool = []
        for item in self.multiworld.itempool:
            if item.player == self.player and remove[item.name] > 0:
                remove[item.name] -= 1
                pool.append(self.world.create_filler())
            else:
                pool.append(item)
        self.multiworld.itempool[:] = pool
