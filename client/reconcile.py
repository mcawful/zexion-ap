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
# Items whose byte cannot be written: Zexion (the goal) and the Energy Shield, whose byte is
# not understood (docs/re/save-format.md).
NOT_GRANTABLE = {31, 50}
# Filler the game does not keep in its own byte: a collected Scrap Energy or Area Map left its
# byte at 0 in the owner's randomizer save while Scrap Metal's count rose (docs/re/save-format.md,
# "Inventory"). Scrap Chunk (48) is assumed to behave the same. Never given, never reported.
FILLER_WITHOUT_BYTE = {47, 48, 49}


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
    for item, n in start_items.items():
        total[UPGRADE_OF.get(item, item)] += n
    for i in collected:
        item = location_items[i]
        total[UPGRADE_OF.get(item, item)] += 1
    for item in received:
        total[UPGRADE_OF.get(item, item)] += 1
    return total


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
        if missing <= 0 or item in FILLER_WITHOUT_BYTE:
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
