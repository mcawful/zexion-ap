"""The game's randomizer menu and the rules its generator applies to it: the pool, the starting
items and Random Start (docs/re/randocells.md, from "The randomizer menu's settings" on; the
design: docs/custom-mode.md).

A ``Menu`` is the randomizer menu's state: the 27 items' start and pool bytes, the six amounts
and settings t10 to t73. A preset mode is one fixed state; Custom is any state the menu can
hold. The world builds the seed's pool and starting items from it as the game's generator does
(``resolve``, ``starting_items``, ``pool_items``), with the world's own random numbers: the
counts and odds are the game's, the exact draws are not, which nothing needs (the game's own
placement is replaced by Archipelago's fill, and the client writes the world's starting items
over the game's). ``problems`` refuses a state the menu cannot hold, and one the menu allows
but whose pool the game would cut at random (``most_entries``).

Pure Python with no Archipelago imports.
"""

from __future__ import annotations

import random
from collections import Counter
from collections.abc import Callable, Iterable, Mapping, Sequence
from dataclasses import dataclass, replace
from typing import Protocol

from . import data, logic
from .reach import flagged

# The 27 items of the menu's first screen, in its order (the word table at .rdata 0x20DA68):
# their names as the screen shows them and their item codes. Entry i is byte i of the
# start-with and pool arrays.
MENU_ITEMS = (
    ("Slide Mod", 16), ("Wall Jump Boots", 19), ("Marine Jets", 21), ("Navcore", 32),
    ("Machine Gun", 3), ("Force Blaster", 4), ("Snake", 5), ("Lance", 6), ("Arc Plasma", 7),
    ("Rocket Slide", 11), ("Jump Booster", 22), ("Phaser", 20), ("S-Blade", 17),
    ("Pulse Wave", 9), ("Pentashot", 14), ("Tunneler", 15), ("Focal Crystal", 10),
    ("Plasma Burst", 12), ("G-Diffuser", 34), ("Gravity Harness", 24),
    ("Zenith Thrusters", 26), ("Magnus Armor", 25), ("Power Cartridge", 13),
    ("Power Chip", 33), ("Power Core", 8), ("Javelin Missiles", 23), ("X-Missile", 18),
)  # fmt: skip
ARRAY = len(MENU_ITEMS)
MENU_CODES = tuple(code for _, code in MENU_ITEMS)
GUNS = range(4, 9)  # the entries of the five guns
# Each gun upgrade's entry -> its gun's entry (Pulse Wave and the Machine Gun, ...).
UPGRADE_GUN = {13: 4, 14: 5, 15: 6, 16: 7, 17: 8}
# The four states of an item (the first screen's labels): its (start, pool) bytes.
STATES = {"start": (1, 0), "random": (1, 1), "pool": (0, 1), "excluded": (0, 0)}

# Settings bytes t (docs/re/randocells.md, "Where each setting is stored").
FIRST_SETTING, SETTINGS = 10, 64  # the menu holds t10 to t73
GUNS_FIRST, COLORIZER, RANDOM_GUN = 10, 12, 15
BULK_MISSILES, BULK_BOMBS, BOX_COMPRESSION = 16, 17, 28
X_CRYSTALS, X_BLOCKS, ABRIDGED_FINALE, DOUBLE_TELEPORT, SECRET_SHUFFLE = 30, 31, 32, 33, 37
HINTS = (18, 19, 20, 21)
START_LOCATION = (22, 23, 24, 25, 26, 27)
START_KEYS = (34, 35, 36)
SAFETY = (42, 43)
GATES = (72, 73)
GATES_OFF, GATES_OPEN = 0, 3  # Gates' first and last values (Vanilla 1, Unlocked 2)
LIGHT = 0  # Safety's first value
# The settings the logic reads, which a new game must have as the seed's (client/newgame.py,
# the expect lines): Guns First's item redirects, code 68's settings, Double Teleport, Safety
# (code 69's mode) and Gates (Open's gate travel).
CHECKED = (GUNS_FIRST, X_CRYSTALS, X_BLOCKS, ABRIDGED_FINALE, DOUBLE_TELEPORT, SECRET_SHUFFLE)
CHECKED += (*SAFETY, *GATES)

# The amounts, in the menu's and the save's order.
START_ENERGY, POOL_ENERGY, START_MISSILES, POOL_MISSILES, START_BOMBS, POOL_BOMBS = range(6)
MAX_ENERGY, MAX_AMMO = 40, 999
LOCATIONS = len(data.LOCATIONS)  # 154; Secret Shuffle adds a 155th
MENU_PLACES = LOCATIONS - 4  # the menu's count of places: the locations less the four keys
ENERGY_TANK, MISSILE_CONTAINER, MEGA_BOMB, MISSILE_BOX, BOMB_BOX = 0, 1, 2, 36, 37
KEY_CODES = (27, 28, 29, 30)  # Alpha, Beta, Gamma, Omega
SCRAP_METAL, SCRAP_ENERGY, SCRAP_CHUNK, AREA_MAP, ENERGY_SHIELD = 46, 47, 48, 49, 50
# Filler, one draw per place: Scrap metal 8 in 16, Scrap energy and Scrap chunk 1 each, Area
# Map and Energy Shield 3 each (0xF25AC).
FILLER_ODDS = (SCRAP_METAL,) * 8 + (SCRAP_ENERGY, SCRAP_CHUNK) + (AREA_MAP, ENERGY_SHIELD) * 3
# Random Start draws from every room only from this starting score on (0xF2F30).
FLAGGED_SCORE = 27


@dataclass(frozen=True)
class Menu:
    """The randomizer menu's state."""

    start: tuple[int, ...]  # the start-with array, 27 entries of 0 or 1
    pool: tuple[int, ...]  # the pool array
    amounts: tuple[int, ...]  # starting and pool Energy, missiles, Mega Bombs
    settings: tuple[int, ...]  # t10 to t73

    @classmethod
    def preset(cls, name: str) -> Menu:
        """A preset mode's state (``data.MODES``, read from the exe and checked against the
        mode's fresh save by tools/gen_world_data.py)."""
        mode = data.MODES[name]
        menu = mode["menu"]
        return cls(
            tuple(menu["start"]), tuple(menu["pool"]), tuple(mode["amounts"]),
            tuple(menu["settings"]),
        )  # fmt: skip

    def setting(self, t: int) -> int:
        return self.settings[t - FIRST_SETTING]

    def value(self, bits: Sequence[int]) -> int:
        """A setting stored over several bytes, one bit each, lowest first."""
        return sum(self.setting(t) << i for i, t in enumerate(bits))

    def with_settings(self, values: dict[int, int]) -> Menu:
        settings = list(self.settings)
        for t, v in values.items():
            settings[t - FIRST_SETTING] = v
        return replace(self, settings=tuple(settings))

    def with_value(self, bits: Sequence[int], value: int) -> Menu:
        return self.with_settings({t: (value >> i) & 1 for i, t in enumerate(bits)})

    def with_item(self, entry: int, state: str) -> Menu:
        start, pool = list(self.start), list(self.pool)
        start[entry], pool[entry] = STATES[state]
        return replace(self, start=tuple(start), pool=tuple(pool))

    def with_amount(self, which: int, value: int) -> Menu:
        amounts = list(self.amounts)
        amounts[which] = value
        return replace(self, amounts=tuple(amounts))

    @property
    def start_keys(self) -> int:
        k = self.value(START_KEYS)
        return k if k <= 4 else 0  # a value above 4 counts as 0 (0xE5450, 0xF1DAB)

    @property
    def places(self) -> int:
        """N: the locations the game's list holds."""
        return LOCATIONS + self.setting(SECRET_SHUFFLE)

    def logic_settings(self) -> logic.Settings:
        """What the logic reads: the Energy total, Safety (code 69's mode), the settings
        code 68 reads (bytes t0 to t63; t0 to t9 are no settings), Gates Open and Guns First
        (the item redirects)."""
        options = bytes(FIRST_SETTING) + bytes(self.settings[: SETTINGS - FIRST_SETTING])
        energy = self.amounts[START_ENERGY] + self.amounts[POOL_ENERGY]
        gates_open = self.value(GATES) == GATES_OPEN
        guns_first = bool(self.setting(GUNS_FIRST))
        return logic.Settings(energy, self.value(SAFETY), options, gates_open, guns_first)

    def checked(self) -> dict[int, int]:
        return {t: self.setting(t) for t in CHECKED}


def _boxed(amount: int) -> int:
    """The places an amount takes once in boxes of ten, as the menu counts it."""
    return amount - 9 * (amount // 10) if amount >= 20 else amount


def _cap(free: int) -> int:
    """The menu's limit for one pool amount, with ``free`` places left for it (0xD2110): ten
    per place once two or more are free."""
    return 10 * free if free >= 2 else max(free, 0)


def most_pooled(menu: Menu) -> int:
    """The most entries the pool array can put in the pool after the coin flips and Random
    Gun ("The pool", steps 1 to 3; Guns First's swap keeps the count)."""
    entries = [i for i in range(ARRAY) if menu.pool[i]]
    if not menu.setting(RANDOM_GUN):
        return len(entries)
    others = sum(1 for i in entries if i not in GUNS)
    both = [i for i in GUNS if menu.start[i] and menu.pool[i]]
    pool_only = sum(1 for i in GUNS if menu.pool[i] and not menu.start[i])
    # Two or more coin-flip guns: one is started and the rest pooled; otherwise every gun set
    # either way is pooled and as many as were start-only are drawn back out.
    guns = len(both) - 1 + pool_only if len(both) >= 2 else len(both) + pool_only
    return others + guns


def pooled_counts(menu: Menu) -> range:
    """Every count of pooled entries the coin flips can give: ``most_pooled``, less one for each
    coin-flip entry that went to the start (Random Gun's guns are no coin flips there)."""
    random_gun = menu.setting(RANDOM_GUN)
    flips = sum(
        1 for i in range(ARRAY) if menu.start[i] and menu.pool[i] and not (random_gun and i in GUNS)
    )
    most = most_pooled(menu)
    return range(most - flips, most + 1)


class _BombsFirst:
    """Draws that always come out 0: each box a Bomb Box wherever the rule draws, which leaves
    the most entries (``most_entries``)."""

    @staticmethod
    def randrange(n: int) -> int:
        return 0


def most_entries(menu: Menu) -> int:
    """The longest list steps 4 and 5 can build ("The pool"), over every coin flip and draw:
    the keys, the pooled entries and the pool Energy Tanks, then the supplies for the places
    left. Where the boxes are drawn, the list is longest when every draw that can give a Bomb
    Box does: a Missile Box takes ten from the missiles and leaves no more entries than a Bomb
    Box would, and the bombs run out sooner. Supplies cannot take places away, so this is
    above the places only when the supplies are."""
    longest = 0
    for pooled in pooled_counts(menu):
        fixed = 4 - menu.start_keys + pooled + menu.amounts[POOL_ENERGY]
        room = menu.places - fixed
        longest = max(longest, fixed + len(supplies(menu, room, _BombsFirst())))
    return longest


def problems(menu: Menu) -> list[str]:
    """What the menu could not hold, or what the game would build a broken pool from, one
    line each; empty when neither (docs/re/randocells.md, "The randomizer menu's first screen:
    items and amounts" and "The pool"). The menu's limits: Energy 40 in all, missiles and bombs
    999 in all, and the pool missiles and bombs ten per free place. The pool: no more keys,
    items and Energy Tanks than places, and, for every coin flip and draw, no longer list once
    the supplies are in (``most_entries``), which the menu allows: the game drops the extra
    entries at random, keys and items among them, and past 155 writes beyond its list. What it
    does not check is the filler for the locations a seed leaves out, which depends on what the
    items reach from the start (world.py, ``fit_pool``)."""
    a = menu.amounts
    out = []
    if a[START_ENERGY] + a[POOL_ENERGY] > MAX_ENERGY:
        out.append(
            f"Starting Energy Tanks ({a[START_ENERGY]}) and Item Pool Energy Tanks "
            f"({a[POOL_ENERGY]}) are more than {MAX_ENERGY} together"
        )
    for kind, start, pool in (
        ("Missiles", START_MISSILES, POOL_MISSILES),
        ("Mega Bombs", START_BOMBS, POOL_BOMBS),
    ):
        if a[start] + a[pool] > MAX_AMMO:
            out.append(
                f"Starting {kind} ({a[start]}) and Item Pool {kind} ({a[pool]}) are more than "
                f"{MAX_AMMO} together"
            )
    fixed = 4 - menu.start_keys + most_pooled(menu) + a[POOL_ENERGY]
    if fixed > menu.places:
        out.append(
            f"the item pool's keys, items and Energy Tanks ({fixed}) are more than its "
            f"{menu.places} places"
        )
    # The menu's own count: its places, plus the keys started, plus Secret Shuffle's, less the
    # pool Energy and every entry set in the pool array (0xD2110).
    free = MENU_PLACES + menu.start_keys + menu.setting(SECRET_SHUFFLE)
    free -= a[POOL_ENERGY] + sum(1 for v in menu.pool if v)
    for kind, pool, other in (
        ("Missiles", POOL_MISSILES, POOL_BOMBS),
        ("Mega Bombs", POOL_BOMBS, POOL_MISSILES),
    ):
        cap = _cap(free - _boxed(a[other]))
        if a[pool] > cap:
            out.append(
                f"Item Pool {kind} ({a[pool]}) are more than the {cap} the menu allows with "
                f"these items and amounts (ten per free place)"
            )
    if fixed <= menu.places and (longest := most_entries(menu)) > menu.places:
        out.append(
            f"the item pool's keys, items, Energy Tanks, missiles and Mega Bombs can come to "
            f"{longest}, more than its {menu.places} places, and the game would drop some at "
            f"random, keys and items among them (fewer Item Pool Missiles or Mega Bombs, or "
            f"Bulk Missiles and Bulk Bombs off, leave them room)"
        )
    return out


def coin(rng: random.Random) -> bool:
    return rng.random() < 0.5


def resolve(menu: Menu, rng: random.Random) -> tuple[list[int], list[int]]:
    """The start-with and pool arrays after the game's steps 1 to 3 ("The pool"): a coin flip
    for each item set both ways, Random Gun's draw over the menu's guns, and Guns First's swap
    of a pooled upgrade whose gun is not pooled (0xF15B0, 0xF16ED to 0xF1C66, 0xF1C78)."""
    start, pool = list(menu.start), list(menu.pool)
    for i in range(ARRAY):
        if start[i] and pool[i]:
            if coin(rng):
                pool[i] = 0
            else:
                start[i] = 0
    if menu.setting(RANDOM_GUN):
        ms, mp = menu.start, menu.pool  # Random Gun reads the menu's states, not the flips
        both = [i for i in GUNS if ms[i] and mp[i]]
        if len(both) >= 2:
            for i in GUNS:
                start[i], pool[i] = (0, 1) if i in both else (ms[i], mp[i])
            pick = rng.choice(both)
            start[pick], pool[pick] = 1, 0
        else:
            one_way = [i for i in GUNS if ms[i] != mp[i]]
            draws = sum(1 for i in GUNS if ms[i] and not mp[i])
            for i in GUNS:
                start[i], pool[i] = 0, int(bool(ms[i] or mp[i]))
            for _ in range(min(draws, len(one_way))):
                pick = one_way.pop(rng.randrange(len(one_way)))
                start[pick], pool[pick] = 1, 0
    if menu.setting(GUNS_FIRST):
        for upgrade, gun in UPGRADE_GUN.items():
            if pool[upgrade] and not pool[gun]:
                pool[upgrade], pool[gun] = 0, 1
    return start, pool


def as_progressive(code: int, guns_first: bool = True) -> int:
    """With Guns First, an upgrade as its gun's progressive item: the world holds each gun and
    its upgrade as two copies of one item (worlds/zexion/items.py). Without it, each upgrade is
    an item of its own."""
    return logic.REDIRECT_IF_MISSING.get(code, code) if guns_first else code


def starting_items(menu: Menu, start: Sequence[int]) -> dict[int, int]:
    """The starting items, by item code, as the world holds them: the started entries (with
    Guns First an upgrade as a copy of its gun's progressive item), the starting Energy as
    Energy Tanks, the starting missiles and Mega Bombs as the new game splits them (tens as
    boxes, the rest singly: client/newgame.py), and the first Start Keys keys from the Alpha
    Key."""
    guns_first = bool(menu.setting(GUNS_FIRST))
    have: Counter[int] = Counter()
    for i, on in enumerate(start):
        if on:
            have[as_progressive(MENU_CODES[i], guns_first)] += 1
    a = menu.amounts
    have[ENERGY_TANK] += a[START_ENERGY]
    have[MISSILE_BOX] += a[START_MISSILES] // 10
    have[MISSILE_CONTAINER] += a[START_MISSILES] % 10
    have[BOMB_BOX] += a[START_BOMBS] // 10
    have[MEGA_BOMB] += a[START_BOMBS] % 10
    for code in KEY_CODES[: menu.start_keys]:
        have[code] = 1
    return {code: n for code, n in sorted(have.items()) if n}


def score(items: Mapping[int, int]) -> int:
    """Random Start's starting score (0xF2F30): starting Energy + missiles + Mega Bombs + the
    started entries of the menu's 27 items (a progressive gun twice: the gun and its upgrade;
    without Guns First, a gun and its upgrade one each),
    from the starting items as ``starting_items`` gives them."""
    energy = items.get(ENERGY_TANK, 0)
    missiles = items.get(MISSILE_CONTAINER, 0) + 10 * items.get(MISSILE_BOX, 0)
    bombs = items.get(MEGA_BOMB, 0) + 10 * items.get(BOMB_BOX, 0)
    entries = sum(n for code, n in items.items() if code in MENU_CODES)
    return energy + missiles + bombs + entries


def _ceil9(v: int) -> int:
    return -(-max(v, 0) // 9)


class PoolError(ValueError):
    """The game's pool rule gives more entries than places, or too little filler is left."""


class Draws(Protocol):
    def randrange(self, n: int, /) -> int: ...


def supplies(menu: Menu, room: int, rng: Draws) -> list[int]:
    """Step 5: the pool missiles and Mega Bombs as boxes, Mega Bombs and Missile Containers,
    with ``room`` places left after the keys, items and Energy Tanks (0xF2079 to 0xF25A0).
    Where the boxes are drawn, each box's draw is below the missiles and bombs left plus one;
    once they are below 0 (more boxes than tens, which Box Compression's 25 can make) the game
    takes 0 without drawing (0xF2362), so the box is a Bomb Box, counted unless the bombs left
    are exactly 0, and the pool holds more bombs than were set."""
    m, b = menu.amounts[POOL_MISSILES], menu.amounts[POOL_BOMBS]
    bulk_m, bulk_b = menu.setting(BULK_MISSILES), menu.setting(BULK_BOMBS)
    extra = 25 if menu.setting(BOX_COMPRESSION) else 0  # "Other items will also appear."
    missile_boxes = bomb_boxes = 0
    if m + b <= room:
        pass
    elif not bulk_m and not bulk_b:
        boxes = min(_ceil9(m + b - room + extra), room)
        if b < 20:
            missile_boxes, m = boxes, m - 10 * boxes
        elif m < 20:
            bomb_boxes, b = boxes, b - 10 * boxes
        else:
            for _ in range(boxes):
                d = rng.randrange(m + b + 1) if m + b >= 0 else 0
                if (d >= b or b < 10) and m >= 10:
                    missile_boxes, m = missile_boxes + 1, m - 10
                else:
                    if b != 0:
                        bomb_boxes += 1
                    b -= 10
    else:
        if bulk_m:
            missile_boxes = m // 10
            m -= 10 * missile_boxes
        if bulk_b:
            bomb_boxes = b // 10
            b -= 10 * bomb_boxes
        if not bulk_m:
            missile_boxes = _ceil9(m + b - room + missile_boxes + bomb_boxes + extra)
            m -= 10 * missile_boxes
        if not bulk_b:
            bomb_boxes = _ceil9(b - room + m + missile_boxes + bomb_boxes + extra)
            b -= 10 * bomb_boxes
    out = [MISSILE_BOX] * missile_boxes + [BOMB_BOX] * bomb_boxes
    return out + [MEGA_BOMB] * max(b, 0) + [MISSILE_CONTAINER] * max(m, 0)


def pool_items(menu: Menu, pool: Sequence[int], rng: random.Random) -> list[int]:
    """The item pool, by item code, from the resolved pool array ("The pool", steps 4 to 6 and
    8): the keys not started (Omega first), the pooled entries, the pool Energy Tanks, the
    missiles and Mega Bombs (``supplies``), then filler to the places with the game's odds;
    with Guns First, each upgrade is its gun's progressive item (the game's step 8 makes it
    the gun), and without it each upgrade is its own item."""
    guns_first = bool(menu.setting(GUNS_FIRST))
    items = [KEY_CODES[3 - i] for i in range(4 - menu.start_keys)]
    items += [MENU_CODES[i] for i in range(ARRAY) if pool[i]]
    items += [ENERGY_TANK] * menu.amounts[POOL_ENERGY]
    items += supplies(menu, menu.places - len(items), rng)
    if len(items) > menu.places:  # problems refuses every state that can come to this
        raise PoolError(
            f"the pool's {len(items)} keys, items, Energy Tanks, missiles and Mega Bombs are "
            f"more than its {menu.places} places, and the game would drop some at random"
        )
    items += [rng.choice(FILLER_ODDS) for _ in range(menu.places - len(items))]
    return [as_progressive(code, guns_first) for code in items]


def compress(items: list[int], needed: int, filler: Iterable[int]) -> list[int]:
    """The pool with at least ``needed`` items of ``filler``, made as the game makes filler when
    it runs short (0xE5CB0): ten Missile Containers become a Missile Box and nine Scrap metal,
    or ten Mega Bombs a Bomb Box and nine Scrap metal. Raises PoolError when no ten are left."""
    kinds = set(filler)
    out = list(items)
    while sum(1 for code in out if code in kinds) < needed:
        for single, box in ((MISSILE_CONTAINER, MISSILE_BOX), (MEGA_BOMB, BOMB_BOX)):
            if out.count(single) >= 10:
                for _ in range(10):
                    out.remove(single)
                out += [box] + [SCRAP_METAL] * 9
                break
        else:
            raise PoolError("not enough filler, and no ten Missile Containers or Mega Bombs left")
    return out


def tower(room: dict) -> bool:
    """Rows 41 to 43: Upper Tower, Tower Gauntlet, Guardia Tower and Lower Tower."""
    return 41 <= room["cell"][1] <= 43


def random_start(
    rooms: Iterable[dict],
    score: int,
    safety: int,
    passes: Callable[[dict], bool],
    rng: random.Random,
) -> dict | None:
    """Random Start's rule (0xF2F30, the loop at 0xF4661): the unflagged rooms unless the
    starting score is FLAGGED_SCORE or more; rooms drawn at random until one passes; with
    Safety other than Light, the first passing tower room is kept one time in three. None when
    no room passes."""
    left = [room for room in rooms if score >= FLAGGED_SCORE or not flagged(room)]
    towers_checked = safety == LIGHT
    while left:
        room = left.pop(rng.randrange(len(left)))
        if not passes(room):
            continue
        if not towers_checked and tower(room):
            towers_checked = True  # once per START GAME
            if rng.randrange(3):
                continue
        return room
    return None
