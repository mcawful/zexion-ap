"""What a seed can start with: the starting items the client can give.

The client gives the starting items (slot data "start_items": the mode's own and any
start_inventory) through a new game, ARCHIPELAGO or RANDOMIZER (``start_with`` in
client/newgame.py), or by writing them into a fresh save (``patch`` in client/save_patch.py,
for ``/prepare``); the server does not send it the start inventory (the client asks only for
other worlds' items). Either refuses what it cannot write, but only at play time, so the world
refuses those items in generation (``problems``, from generate_early), with the limits of the
two:

- an upgrade, the Navcore or another item of the new game's start-with array: one at most (the
  array holds 1 for "start with it"), and a progressive weapon twice at most (the weapon and its
  upgrade);
- Missile Containers: 9 at most, the mode's own included (the new game takes one missile amount,
  containers plus 10 per Missile Box, and the game splits it back into tens and containers);
- keys: the first ones from the Alpha Key on (the new game takes a key count);
- no Mega Bombs or Bomb Boxes (where a save keeps current bombs is not known, so ``/prepare``
  cannot write them);
- no Scrap, Area Map or Energy Shield (no way into a new game; the shield is a pickup's spawn);
- no Intel Chip or Assist Unlock: the client acts on them only as received (a hint, the assist
  lock lifted), and a starting one is never received (the Allow Assists option lifts the lock);
- an OS Install Disk only with the OS Install Disks goal, which counts it (slot data
  "discs_start").
"""

from __future__ import annotations

from collections.abc import Mapping

from .items import ASSIST_UNLOCK, DATA_DISC, INTEL_CHIP, KEY_CODES, NAMES, PROGRESSIVE

MISSILE_CONTAINER = 1
MAX_CONTAINERS = 9
# The new game's start-with array (client/newgame.py, START_WITH_ORDER), less the upgrades,
# which this world hands out as a progressive weapon's second copy.
START_WITH = frozenset({3, 4, 5, 6, 7, 8, 11, 13, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26})
START_WITH |= {32, 33, 34}
NEVER = {
    2: "the client cannot write starting bombs",
    37: "the client cannot write starting bombs",
    46: "a new game cannot start with Scrap",
    47: "a new game cannot start with Scrap",
    48: "a new game cannot start with Scrap",
    49: "a new game cannot start with an Area Map",
    50: "a new game cannot start with an Energy Shield",
    INTEL_CHIP: "a starting Intel Chip is never received, so it would hint nothing",
    ASSIST_UNLOCK: "a starting Assist Unlock is never received; use Allow Assists instead",
}


def problems(counts: Mapping[int, int], disc_hunt: bool) -> list[str]:
    """Why the client could not give these starting items (item code -> count, the mode's own
    and start_inventory together), one line each; empty when it can."""
    out = []
    for code, n in sorted(counts.items()):
        if n <= 0:
            continue
        name = NAMES[code]
        if code in NEVER:
            out.append(f"{name}: {NEVER[code]}")
        elif code == DATA_DISC and not disc_hunt:
            out.append(f"{name}: only the OS Install Disks goal counts them")
        elif code in PROGRESSIVE and n > 2:
            out.append(f"{n} {name}: a weapon and its upgrade are 2")
        elif code in START_WITH and code not in PROGRESSIVE and n > 1:
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
