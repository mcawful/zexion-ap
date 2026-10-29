"""Work out which items to give the player, from what the game shows and what AP sent.

The client never counts its own grants. It compares, item by item, what the player should own
with what the live inventory holds, and gives only the difference:

    should own = the seed's starting items
               + the items the game itself handed out at the locations collected so far
               + the items received from other worlds

The collected locations come from the live map grid (a cell reading COLLECTED), and the game
hands out what the seed's item table holds there (client/newgame.py). Both the map and the
inventory go back to the last save when the player dies or reloads, so the difference stays
right: an item the save lost is given again, and one the game handed out is never given twice.
An inventory above what the player should own is left alone.

Only the supplies and Scrap Metal are counts (``COUNTED``: Energy Tanks, Missile Containers,
Mega Bombs, Missile Boxes, Bomb Boxes and Scrap Metal, docs/re/save-format.md, "Inventory: one
byte per item ID at 0x92F3"). Every other item is held once: its byte is 0 or 1, and the game's
pools hold one of each at most (docs/re/randocells.md, "The pool": one entry per item set in the
pool array, one per key). So no other byte is written above 1 (with Guns First, a weapon and its
upgrade are a byte each): a second copy, which only a plando or a server console's ``/send``
can make (the world refuses a start_inventory copy of an item the pool holds:
worlds/zexion/starting.py, ``pool_duplicates``), is not written. The pickup routine itself only
increments the byte (docs/re/memory.md, "Granting each kind of item, and what keeps it"), and
what the game does with a 2 in such a byte is not measured: the cap keeps the bytes as the
game's own seeds leave them.

With Guns First on (setting t10, every preset), a weapon and its upgrade are one progressive
item: the game turns a second copy into the upgrade (docs/re/randocells.md, "The evaluator works
on a table of 64 counts": on the item's update by the save's Guns First, at pickup by the
randomizer menu's; seen for the Machine Gun only); should it not, the upgrade is still owed and
given. With it off the game
redirects nothing in the seed's own items, and each upgrade is an item, and a byte, of its own
(docs/re/randocells.md, "The evaluator works on a table of 64 counts"): every item is counted as
itself, a gun and its upgrade each at most 1 like any other item held once.

Pure functions; client/game.py does the reading and writing, in a settled in-play state.
"""

from __future__ import annotations

from collections import Counter
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass

from . import ids
from .ids import BOMB_BOX, ENERGY_TANK, MEGA_BOMB, MISSILE_BOX, MISSILE_CONTAINER, PROGRESSIVE

UPGRADE_OF = {u: w for w, u in PROGRESSIVE.items()}

# The items the game holds as a count (the module's docstring); every other byte is 0 or 1.
COUNTED = frozenset(
    {ENERGY_TANK, MISSILE_CONTAINER, MEGA_BOMB, MISSILE_BOX, BOMB_BOX, ids.SCRAP_METAL}
)

# Items whose byte cannot be written: Zexion (the goal).
NOT_GRANTABLE = {ids.ZEXION}
# Items never put in the inventory: the Data Disc, which the client counts instead
# (client/discs.py), as holding one changes the game's endings.
NOT_IN_INVENTORY = {ids.DATA_DISC}
# Records 46-50 all name Scrap Metal's byte as the one their pickup increments, so each of
# these counts as one Scrap Metal, collected or received (docs/re/memory.md, "Other players'
# items"; docs/re/save-format.md, "Inventory").
COUNTED_AS_SCRAP = {ids.STAND_IN, ids.STAND_IN_PROGRESSION, ids.AREA_MAP, ids.ENERGY_SHIELD}


def collected_locations(grid: bytes, cells: Sequence[tuple[int, int]]) -> set[int]:
    """Indices of the locations whose map cell reads collected."""
    return {i for i, (x, y) in enumerate(cells) if grid[y * ids.MAP_SIZE + x] == ids.COLLECTED}


def should_own(
    start_items: Mapping[int, int],
    location_items: Sequence[int],
    collected: Iterable[int],
    received: Iterable[int],
    guns_first: bool = True,
) -> Counter[int]:
    """How many of each item the player should own; with Guns First, progressive weapons under
    the weapon."""
    total: Counter[int] = Counter()
    merged = UPGRADE_OF if guns_first else {}

    def count(item: int, n: int = 1) -> None:
        if item in NOT_IN_INVENTORY:
            return
        item = ids.SCRAP_METAL if item in COUNTED_AS_SCRAP else item
        total[merged.get(item, item)] += n

    for item, n in start_items.items():
        count(item, n)
    for i in collected:
        count(location_items[i])
    for item in received:
        count(item)
    return total


def shields_owed(received: Iterable[int], spawned: int) -> int:
    """How many received Energy Shields have not been given yet (``spawned``: those counted
    as given). Its effect is not a byte: its pickup spawns the shield (docs/re/memory.md,
    "Received Energy Shields"), which the helper does for a received one."""
    return max(0, sum(1 for item in received if item == ids.ENERGY_SHIELD) - max(0, spawned))


def owned(inventory: bytes, guns_first: bool = True) -> Counter[int]:
    """What the inventory holds; with Guns First, progressive weapons under the weapon."""
    have: Counter[int] = Counter()
    for item in range(min(len(inventory), len(ids.GAME_ITEMS))):
        if not inventory[item]:
            continue
        if guns_first and (item in PROGRESSIVE or item in UPGRADE_OF):
            have[UPGRADE_OF.get(item, item)] += 1
        else:
            have[item] += inventory[item]
    return have


@dataclass(frozen=True)
class Grants:
    inventory: dict[int, int]  # item -> new byte value
    not_grantable: dict[int, int]  # item -> how many are owed but cannot be written


def grants(inventory: bytes, target: Counter[int], guns_first: bool = True) -> Grants:
    """The inventory bytes to raise so the player owns `target` (from ``should_own`` with the
    same ``guns_first``)."""
    have = owned(inventory, guns_first)
    new: dict[int, int] = {}
    stuck: dict[int, int] = {}
    for item, want in sorted(target.items()):
        if item not in COUNTED and not (guns_first and item in PROGRESSIVE):
            want = min(want, 1)  # held once: a byte of 1 (the module's docstring)
        missing = want - have[item]
        if missing <= 0:
            continue
        if item in NOT_GRANTABLE:
            stuck[item] = missing
        elif item in PROGRESSIVE and guns_first:
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
