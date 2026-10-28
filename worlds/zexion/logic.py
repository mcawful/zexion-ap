"""Zexion's randomizer logic: whether a requirement list from RandoCells is met.

Pure Python with no Archipelago imports, so ``tools/randocells.py`` uses it too. The rules are
the game's own, from disassembly (docs/re/randocells.md): a list of (code, tier) entries is
checked against a table of 64 counts, where codes 0-49 are item counts, 50-63 are derived from
them (``derive``), and 64-69 depend on where the check happens, the seed's options and energy
(``cell_code``). Grid positions are this repo's (x, y) order (docs/re/randocells.md, "Grid
order").
"""

from __future__ import annotations

import struct
from collections.abc import Callable, Mapping, MutableMapping, Sequence
from dataclasses import dataclass, field

W = H = 54
FIRST_SPECIAL = 50
Entry = tuple[int, int]  # (code, tier/count byte)
Cell = tuple[int, int]

# Item definition redirects (docs/re/randocells.md): a weapon picked up again counts as its
# upgrade, and an upgrade picked up first counts as its weapon, so each pair is progressive.
WEAPON_UPGRADES = {3: 9, 4: 14, 5: 15, 6: 10, 7: 12}
REDIRECT_IF_MISSING = {upgrade: weapon for weapon, upgrade in WEAPON_UPGRADES.items()}


def collect(have: MutableMapping[int, int], item: int) -> None:
    """Add one picked-up item to the counts, as the generator does (RVA 0xE5680)."""
    upgrade = WEAPON_UPGRADES.get(item)
    if upgrade is not None and have.get(item, 0) > 0:
        have[upgrade] = have.get(upgrade, 0) + 1
        return
    base = REDIRECT_IF_MISSING.get(item)
    if base is not None and have.get(base, 0) == 0:
        have[base] = 1
        return
    have[item] = have.get(item, 0) + 1


@dataclass(frozen=True)
class Settings:
    """The seed's randomizer settings that the logic reads."""

    energy_total: int = 20  # starting Energy + Energy in the pool
    mode69: int = 0  # the menu's Safety; 0 or 3: code 69 always passes; 1 or 2: checks energy
    options: bytes = field(default=bytes(64))


DEFAULT_SETTINGS = Settings()


def f32(v: float) -> float:
    """Round to single precision, as the game computes these figures."""
    return struct.unpack("<f", struct.pack("<f", v))[0]


def combat_tier(c: list[int], settings: Settings) -> int:
    """Code 62, as the game derives it (RVA 0xE3530)."""
    if c[12] or settings.mode69 == 3:  # Plasma Burst, or both mode bits set
        return 4
    energy = c[0] * 2 if c[25] else c[0]  # Magnus Armor doubles Energy
    total = settings.energy_total
    if total <= 0:
        energy = 40
    elif total != 20:  # energy is judged on a 20-point scale of the seed's total
        energy = int(f32(f32(20.0 / total) * f32(energy)))
    missiles = c[36] * 10 + c[1]
    damage = c[33] + c[13] + c[8]  # Power Chip, Power Cartridge, Power Core
    gun = bool(c[3] or c[4])  # Machine Gun or Force Blaster

    def armed(missile_floor: int) -> bool:
        return (gun and damage >= 1) or bool(c[15]) or bool(c[7]) or missiles >= missile_floor

    strong = (gun and damage >= 2) or c[7] or missiles >= 35
    if energy > 10 and strong and c[24] + c[22] + c[19] >= 2:
        return 4
    if missiles >= 25 and c[18]:
        return 4
    if energy > 10 and armed(25):
        return 3
    if missiles >= 20 and c[18]:
        return 3
    if energy >= 8 and armed(25):
        return 2
    if missiles >= 15 and c[37] * 10 + c[2] >= 2:
        return 2
    if (energy >= 5 and armed(10)) or (energy >= 10 and c[60]) or missiles >= 15:
        return 1
    return 0


def derive(c: list[int], settings: Settings) -> None:
    """Fill in codes 50-63 from item counts, in place, as RVA 0xE3530 does."""
    hyper = bool(c[26]) and not c[63]  # Zenith Thrusters stands in, unless listed itself
    gh = c[24]
    c[54] = int(bool((gh and c[22]) or hyper))
    if gh:  # Gravity Harness counts as Jump Booster and G-Diffuser
        c[22] = c[34] = 1
    wj = c[19]
    c[50] = int(bool(wj or gh or c[22] or hyper))
    c[51] = int(bool((wj and (c[34] or gh)) or hyper))
    c[52] = int(bool((wj and c[22]) or hyper))
    c[53] = int(bool((wj and (c[34] or gh)) or c[22] or hyper))
    for base, upgrade in WEAPON_UPGRADES.items():
        if not c[base]:  # a weapon upgrade needs its weapon
            c[upgrade] = 0
    if not c[16]:  # S-Blade and Rocket Slide need Slide Mod
        c[17] = c[11] = 0
    c[57] = int(bool(c[15] or c[6] or c[17] or c[12]))
    c[61] = int(bool(c[12] or c[1]))
    c[60] = int(bool(c[7] or c[6] or c[3] or c[5] or c[4]))
    c[58] = int(bool(c[12] or c[4] or c[3] or c[5]))
    c[59] = int(bool(c[3] or c[5] or c[12]))
    c[62] = combat_tier(c, settings)
    c[56] = 1
    if hyper:
        c[21] = c[22] = c[34] = c[19] = c[24] = 1
    if c[18] and c[1] < 5:  # X-Missile needs five Missile Containers
        c[18] = 0


def energy_69(have: Mapping[int, int], settings: Settings) -> int:
    """The energy figure code 69 compares (it reads the raw counts, not the derived ones)."""
    energy = have.get(0, 0) * 2 if have.get(25, 0) else have.get(0, 0)
    total = settings.energy_total
    if total <= 0:
        return 40
    if total != 20:
        factor = f32(20.0 / total)
        if factor > 1.0:  # code 69 only scales up, unlike code 62
            energy = int(f32(f32(energy) * factor))
    return energy


def weapons_69(have: Mapping[int, int]) -> bool:
    """Code 69, tier 16: a weapon good enough (RVA 0xE483E)."""

    def n(code: int) -> int:
        return have.get(code, 0)

    missiles = n(36) * 10 + n(1)
    if n(5) and (n(15) or n(8) or missiles >= 20):
        return True
    if n(3) or n(7):
        return True
    if n(4) and n(33) and n(13):
        return True
    return missiles >= 40


def neighbor(code: int, cell: Cell) -> Cell | None:
    """The cell codes 64-67 look at (west, east, north, south), or None at the map edge.

    East steps by grid index, as the game does, so x = 53 looks at the next row's first cell.
    """
    x, y = cell
    index = y * W + x
    if code == 64 and x > 0:
        index -= 1
    elif code == 65 and x < W:  # always: east wraps to the next row
        index += 1
    elif code == 66 and y > 0:
        index -= W
    elif code == 67 and y < H - 1:
        index += W
    else:
        return None
    return (index % W, index // W)


def cell_code(
    code: int,
    tier: int,
    have: Mapping[int, int],
    c: list[int],
    settings: Settings,
    cell: Cell,
    reached: Callable[[Cell], bool],
) -> bool | None:
    """Codes 64-69 (RVA 0xE3C80): True or False decides the whole check; None = not applicable."""
    if 64 <= code <= 67:
        other = neighbor(code, cell)
        return None if other is None else reached(other)
    if code == 68:  # an option, chosen by the tier byte
        return True if tier > 10 and settings.options[tier] else None
    if code == 69:
        if settings.mode69 in (0, 3):
            return True
        if tier <= 10:
            return None
        energy = energy_69(have, settings)
        if tier == 11:
            return energy > 9
        if tier == 12:
            return c[62] >= 1 and energy > 9
        if tier == 13:
            return c[62] >= 2 and energy > 9
        if tier == 14:
            return energy > (19 if settings.mode69 == 1 else 29)
        if tier == 15:
            return c[62] >= 1 and energy > 7
        if tier == 16:
            return energy > 4 and weapons_69(have)
        return None
    return None


def _no_cell(cell: Cell) -> bool:
    raise ValueError("codes 64-67 need to know which cells are reached")


def satisfied(
    entries: Sequence[Entry],
    have: Mapping[int, int],
    settings: Settings = DEFAULT_SETTINGS,
    cell: Cell | None = None,
    reached: Callable[[Cell], bool] = _no_cell,
) -> bool:
    """The game's check (RVA 0xE3C80). ``have`` maps item IDs 0-49 to counts."""
    c = [have.get(i, 0) if i < FIRST_SPECIAL else 0 for i in range(64)]
    for code, tier in entries:
        if code < 64 and tier == 3:  # this item does not count here
            c[code] = 0
        if code == 55:  # "without the Gravity Harness"
            c[24] = 0
        if code == 26:  # Zenith Thrusters listed: it doesn't stand in for other items
            c[63] = 1
    derive(c, settings)

    # First pass, in list order: codes 64-69 decide outright; a missing tier-4 entry fails.
    for code, tier in entries:
        if code >= 64:
            if cell is None:
                raise ValueError("codes 64-69 need to know where the check happens")
            decided = cell_code(code, tier, have, c, settings, cell, reached)
            if decided is not None:
                return decided
        elif tier == 4 and c[code] == 0:
            return False

    group = []
    for code, tier in entries:
        if code >= 64:
            continue
        if tier in (1, 4) and c[code] >= 1:
            return True
        if 11 <= tier <= 40 and c[code] >= tier - 10:
            return True
        if tier == 2:
            group.append((code, 1))
        elif tier > 40:
            group.append((code, tier - 40))
    return bool(group) and all(c[k] >= n for k, n in group)
