"""What a seed can start with: the starting items the client can give.

The client gives the starting items (slot data "start_items": the mode's own and any
start_inventory or start_inventory_from_pool) through a new game, ARCHIPELAGO or RANDOMIZER
(``start_with`` in client/newgame.py); the server does not send it the start inventory (the
client asks only for other worlds' items). The client refuses what it cannot write, but only at
play time, so the world refuses those items in generation (``problems``, from generate_early),
with its limits:

- the Navcore or another item of the new game's start-with array: one at most (the array holds
  1 for "start with it"); with Guns First on, a progressive weapon twice at most (the weapon and
  its upgrade) and no upgrade as an item of its own (the game's Guns First seeds hold none:
  docs/re/randocells.md, "The pool", step 8); with it off, a weapon and an upgrade once each;
- Missile Containers: 9 at most, the mode's own included (the new game takes one missile amount,
  containers plus 10 per Missile Box, and the game splits it back into tens and containers);
- keys: the first ones from the Alpha Key on (the new game takes a key count);
- no Mega Bombs or Bomb Boxes, not supported yet: client/newgame.py writes them as the new
  game's bomb amount, but the game's use of that amount is from disassembly only (docs/re/
  memory.md, "Starting a new game"), and client/save_patch.py, the reference the new game's
  edits are tested against, has no place for current bombs;
- no Scrap, Area Map or Energy Shield (no way into a new game; the shield is a pickup's spawn);
- no Intel Chip or Assist Unlock: the client acts on them only as received (a hint, the assist
  lock lifted), and a starting one is never received (the Enable Assist Options option lifts
  the lock);
- an OS Install Disk only with the OS Install Disks goal, which counts it (slot data
  "discs_start").

And as the game's randomizer takes a starting item out of the pool (docs/re/randocells.md,
"The randomizer menu's first screen" and "The pool", step 1), the world refuses a
start_inventory item, held once, that the pool holds too (``pool_duplicates``): the client
writes no second copy of such an item (client/reconcile.py, ``COUNTED``), so the pool's would be
a location that gives nothing. start_inventory_from_pool takes it from the pool instead, as
Archipelago does after create_items (its Main.py), a filler item in its place.
"""

from __future__ import annotations

from collections import Counter
from collections.abc import Iterable, Mapping

from .items import (
    ASSIST_UNLOCK,
    DATA_DISC,
    INTEL_CHIP,
    KEY_CODES,
    NAMES,
    PROGRESSIVE,
    SCRAP_METAL,
    SUPPLIES,
    UPGRADES,
)

MISSILE_CONTAINER = 1
MAX_CONTAINERS = 9
# The new game's start-with array (client/newgame.py, START_WITH_ORDER), less the upgrades,
# which with Guns First this world hands out as a progressive weapon's second copy.
START_WITH = frozenset({3, 4, 5, 6, 7, 8, 11, 13, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26})
START_WITH |= {32, 33, 34}
# The items the game holds as a count (client/reconcile.py, COUNTED). Every other is held once,
# its inventory byte 0 or 1; with Guns First a progressive weapon is held twice, as the weapon
# and its upgrade, a byte each. The OS Install Disk is counted by the client, never held.
COUNTED = frozenset(SUPPLIES | {SCRAP_METAL})
NEVER = {
    **dict.fromkeys((2, 37), "starting bombs are not supported yet"),
    **dict.fromkeys((46, 47, 48), "a new game cannot start with Scrap"),
    49: "a new game cannot start with an Area Map",
    50: "a new game cannot start with an Energy Shield",
    INTEL_CHIP: "a starting Intel Chip is never received, so it would hint nothing",
    ASSIST_UNLOCK: (
        "a starting Assist Unlock is never received; use Enable Assist Options (allow_assists) "
        "instead"
    ),
}


def problems(counts: Mapping[int, int], disc_hunt: bool, guns_first: bool = True) -> list[str]:
    """Why the client could not give these starting items (item code -> count, the mode's own,
    start_inventory's and start_inventory_from_pool's together), one line each; empty when it
    can. ``guns_first``: the seed's Guns First setting (t10)."""
    out = []
    single = (START_WITH - PROGRESSIVE.keys()) if guns_first else (START_WITH | UPGRADES)
    for code, n in sorted(counts.items()):
        if n <= 0:
            continue
        name = NAMES[code]
        if code in NEVER:
            out.append(f"{name}: {NEVER[code]}")
        elif code == DATA_DISC and not disc_hunt:
            out.append(f"{name}: only the OS Install Disks goal counts them")
        elif code in UPGRADES and guns_first:
            weapon = NAMES[next(w for w, u in PROGRESSIVE.items() if u == code)]
            out.append(
                f"{name}: with Guns First on, the upgrade is a second {weapon} (the game's "
                f"Guns First seeds hold no upgrade of its own)"
            )
        elif code in PROGRESSIVE and guns_first and n > 2:
            out.append(f"{n} {name}: a weapon and its upgrade are 2")
        elif code in single and n > 1:
            out.append(f"{n} {name}: a new game starts with one at most")
        elif code == MISSILE_CONTAINER and n > MAX_CONTAINERS:
            out.append(
                f"{n} Missile Containers, the mode's own included: a new game starts with "
                f"{MAX_CONTAINERS} at most"
            )
    keys = [k for k in KEY_CODES if counts.get(k, 0) > 0]
    if keys != list(KEY_CODES[: len(keys)]):
        out.append("keys: a new game starts with the first ones from the Alpha Key on")
    out += [f"{counts[k]} {NAMES[k]}: one at most" for k in keys if counts[k] > 1]
    return out


def pool_duplicates(
    added: Mapping[int, int], start: Mapping[int, int], pool: Iterable[int], guns_first: bool
) -> list[str]:
    """Why these start_inventory items (item code -> count; not start_inventory_from_pool's)
    would leave a location that gives nothing, one line each: an item held once that the pool
    holds too, with the seed's own starting items (``start``) more copies than the game holds
    (one; with Guns First a progressive weapon two). ``pool``: the pool's item codes as
    create_items puts them in, before Archipelago takes out start_inventory_from_pool's."""
    in_pool = Counter(pool)
    out = []
    for code, n in sorted(added.items()):
        if n <= 0 or not in_pool[code] or code in COUNTED or code == DATA_DISC or code in NEVER:
            continue
        weapon = guns_first and code in PROGRESSIVE
        if start.get(code, 0) + n + in_pool[code] <= (2 if weapon else 1):
            continue
        held = "a weapon and its upgrade" if weapon else "one"
        out.append(
            f"{NAMES[code]}: the item pool holds it too, and the game holds {held} at most, so the "
            f"pool's would give nothing (start_inventory_from_pool takes it from the pool)"
        )
    return out
