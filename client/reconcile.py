"""Work out which items to give the player, from what the game shows and what AP sent.

The client never counts its own grants. It compares, item by item, what the player should own
with what the live inventory holds, and gives only the difference:

    should own = the seed's starting items
               + the items the game itself handed out at the locations collected so far
               + the items received from other worlds

The collected locations come from the live map grid (a cell reading COLLECTED), and the game
hands out what the patched item table holds there (client/save_patch.py). Both the map and the
inventory go back to the last save when the player dies or reloads, so the difference stays
right: an item the save lost is given again, and one the game handed out is never given twice.
An inventory above what the player should own is left alone.

A weapon and its upgrade are one progressive item, counted as one for each byte that is set.
The game turns a second copy of a weapon into its upgrade, in the generator and in play
(docs/re/randocells.md; seen for the Machine Gun only). Should another weapon not, its byte would
read 2 and the upgrade would still be owed, so the client would give it.

Pure functions; client/game.py does the reading and writing, in a settled in-play state.
"""

from __future__ import annotations

from collections import Counter
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass

MAP_W = 54
COLLECTED = 0x1B  # a location cell whose item was collected (docs/re/memory.md)
ITEMS = 51  # inventory bytes 0 to 50

# Progressive weapon -> its upgrade (worlds/zexion/items.py).
PROGRESSIVE = {3: 9, 4: 14, 5: 15, 6: 10, 7: 12}
UPGRADE_OF = {u: w for w, u in PROGRESSIVE.items()}

ENERGY_TANK, MISSILE_CONTAINER, MEGA_BOMB, MISSILE_BOX, BOMB_BOX = 0, 1, 2, 36, 37
# Items whose byte cannot be written: Zexion (the goal).
NOT_GRANTABLE = {31}
# Items the game counts in Scrap Metal's byte: every record from 46 to 50 names inventory byte
# 46 as the one its pickup increments (docs/re/memory.md, "Other players' items"), and a
# collected Scrap Energy or Area Map left its own byte at 0 while Scrap Metal's count rose
# (docs/re/save-format.md, "Inventory"). So each counts as one Scrap Metal, collected (other
# players' items are Scrap Energy in this world's locations) or received.
SCRAP_METAL = 46
COUNTED_AS_SCRAP = {47, 48, 49, 50}
# The Energy Shield's effect is not a byte: its pickup spawns the shield (docs/re/memory.md,
# "Received Energy Shields"), which the helper does for a received one.
ENERGY_SHIELD = 50


def collected_locations(grid: bytes, cells: Sequence[tuple[int, int]]) -> set[int]:
    """Indices of the locations whose map cell reads collected."""
    return {i for i, (x, y) in enumerate(cells) if grid[y * MAP_W + x] == COLLECTED}


def should_own(
    start_items: Mapping[int, int],
    location_items: Sequence[int],
    collected: Iterable[int],
    received: Iterable[int],
) -> Counter[int]:
    """How many of each item the player should own, progressive weapons under the weapon."""
    total: Counter[int] = Counter()

    def count(item: int, n: int = 1) -> None:
        item = SCRAP_METAL if item in COUNTED_AS_SCRAP else item
        total[UPGRADE_OF.get(item, item)] += n

    for item, n in start_items.items():
        count(item, n)
    for i in collected:
        count(location_items[i])
    for item in received:
        count(item)
    return total


def shields_owed(received: Iterable[int], spawned: int) -> int:
    """How many received Energy Shields have not been spawned yet."""
    return max(0, sum(1 for item in received if item == ENERGY_SHIELD) - max(0, spawned))


def owned(inventory: bytes) -> Counter[int]:
    """What the inventory holds, progressive weapons under the weapon."""
    have: Counter[int] = Counter()
    for item in range(min(len(inventory), ITEMS)):
        if not inventory[item]:
            continue
        if item in PROGRESSIVE or item in UPGRADE_OF:
            have[UPGRADE_OF.get(item, item)] += 1
        else:
            have[item] += inventory[item]
    return have


@dataclass(frozen=True)
class Grants:
    inventory: dict[int, int]  # item -> new byte value
    not_grantable: dict[int, int]  # item -> how many are owed but cannot be written


def grants(inventory: bytes, target: Counter[int]) -> Grants:
    """The inventory bytes to raise so the player owns `target`."""
    have = owned(inventory)
    new: dict[int, int] = {}
    stuck: dict[int, int] = {}
    for item, want in sorted(target.items()):
        missing = want - have[item]
        if missing <= 0:
            continue
        if item in NOT_GRANTABLE:
            stuck[item] = missing
        elif item in PROGRESSIVE:
            upgrade = PROGRESSIVE[item]
            for byte in (item, upgrade):
                if missing and inventory[byte] == 0:
                    new[byte] = 1
                    missing -= 1
            if missing:
                stuck[item] = missing  # more copies than a weapon and its upgrade
        elif inventory[item] + missing > 0xFF:
            stuck[item] = missing
        else:
            new[item] = inventory[item] + missing
    return Grants(new, stuck)


@dataclass(frozen=True)
class Supplies:
    """The player state's energy, missile and bomb numbers (docs/re/memory.md)."""

    energy: int
    energy_max: int
    missiles: int
    missiles_max: int
    bombs: int
    bombs_max: int


def supplies_after(inventory: bytes, new: Mapping[int, int], before: Supplies) -> Supplies:
    """The supplies after the grant, by the pickup routine's rules: max missiles is Missile
    Containers + 10 x Missile Boxes and current missiles rise by the same amount; bombs
    likewise from Mega Bombs and Bomb Boxes; and if Energy Tanks x 16 is above max energy,
    max and current energy both become it."""

    def count(item: int) -> int:
        return new.get(item, inventory[item])

    missiles_max = count(MISSILE_CONTAINER) + 10 * count(MISSILE_BOX)
    bombs_max = count(MEGA_BOMB) + 10 * count(BOMB_BOX)
    energy, energy_max = before.energy, before.energy_max
    if count(ENERGY_TANK) * 16 > energy_max:
        energy = energy_max = count(ENERGY_TANK) * 16
    return Supplies(
        energy=energy,
        energy_max=energy_max,
        missiles=before.missiles + max(0, missiles_max - before.missiles_max),
        missiles_max=max(missiles_max, before.missiles_max),
        bombs=before.bombs + max(0, bombs_max - before.bombs_max),
        bombs_max=max(bombs_max, before.bombs_max),
    )
