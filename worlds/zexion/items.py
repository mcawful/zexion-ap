"""Zexion's items: names, IDs and classifications.

Item codes are the game's item IDs (``ItemNames.txt``). With Guns First on (every preset), each
weapon and its upgrade are one progressive item, because the game itself counts a second copy of
a weapon as its upgrade (docs/re/randocells.md); with it off (a Custom seed), the game applies
no such redirect, and each of the five upgrades is an item of its own, named as the game names
it, useless without its weapon. Items the randomizer never places (the eggs, Super Fly, Zexion) are
left out. The game's Data Disc, which the randomizer never places either, is the OS Install
Disk of the OS Install Disks goal (the Goal option): an Archipelago-only count, never written
into the game's inventory, since holding one changes the game's endings (docs/re/memory.md, "The
Data Disc").

Two items are not the game's: Assist Unlock (100), never in the pool, sent from the server
console to lift the client's assist lock (an item rule keeps a plando'd one out of its own
player's locations, where it would never be received: worlds/zexion/world.py); and Intel Chip
(101), the Area Map as a hint (Area Map Hints), which the game holds and draws as the Area Map
(DRAWN_AS) and the helper renames INTEL CHIP (docs/re/memory.md, "Intel Chips").
"""

from __future__ import annotations

from collections.abc import Iterable

from BaseClasses import Item, ItemClassification

from .logic import WEAPON_UPGRADES

GAME = "Zexion"
ITEM_ID_BASE = 3392510_000  # Steam app ID x 1000, to keep IDs recognizable

ASSIST_UNLOCK = 100
AREA_MAP, INTEL_CHIP = 49, 101
DRAWN_AS = {INTEL_CHIP: AREA_MAP}
GAME_ITEMS = range(0, 51)  # the codes that are the game's own inventory bytes

# Progressive weapon item code -> its upgrade's code, as the logic counts them with Guns First.
PROGRESSIVE = WEAPON_UPGRADES
# The upgrades, items of their own only in a seed with Guns First off.
UPGRADES = frozenset(WEAPON_UPGRADES.values())
SCRAP_METAL, SCRAP_ENERGY, SCRAP_CHUNK = 46, 47, 48
ENERGY_SHIELD = 50

NAMES = {
    0: "Energy Tank",
    1: "Missile Container",
    2: "Mega Bomb",
    3: "Progressive Machine Gun",
    4: "Progressive Force Blaster",
    5: "Progressive Snake",
    6: "Progressive Lance",
    7: "Progressive Arc Plasma",
    8: "Power Core",
    9: "Pulse Wave",
    10: "Focal Crystal",
    11: "Rocket Slide",
    12: "Plasma Burst",
    13: "Power Cartridge",
    14: "Pentashot",
    15: "Tunneler",
    16: "Slide Mod",
    17: "S-Blade",
    18: "X-Missile",
    19: "Wall Jump Boots",
    20: "Phaser",
    21: "Marine Jets",
    22: "Jump Booster",
    23: "Javelin Missiles",
    24: "Gravity Harness",
    25: "Magnus Armor",
    26: "Zenith Thrusters",
    27: "Alpha Key",
    28: "Beta Key",
    29: "Gamma Key",
    30: "Omega Key",
    32: "Navcore",
    33: "Power Chip",
    34: "G-Diffuser",
    36: "Missile Box",
    37: "Bomb Box",
    44: "OS Install Disk",
    46: "Scrap Metal",
    47: "Scrap Energy",
    48: "Scrap Chunk",
    49: "Area Map",
    50: "Energy Shield",
    ASSIST_UNLOCK: "Assist Unlock",
    INTEL_CHIP: "Intel Chip",
}

# Everything the rules read, directly or through the codes derived from items
# (worlds/zexion/logic.py), plus the keys for the core goal.
PROGRESSION = {0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 13, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26}
PROGRESSION |= {27, 28, 29, 30, 33, 34, 36, 37} | UPGRADES
# Supplies, with many copies in every pool (Energy Tank, Missile Container, Mega Bomb, Missile
# Box, Bomb Box): progression for the rules' counts, but skipped by progression balancing and
# not worth an Area Map's hint (worlds/zexion/world.py).
SUPPLIES = {0, 1, 2, 36, 37}
# Intel Chips are filler like the Area Maps they replace, so excluded locations still have
# filler.
FILLER = {SCRAP_METAL, SCRAP_ENERGY, SCRAP_CHUNK, AREA_MAP, INTEL_CHIP}
DATA_DISC = 44  # the OS Install Disk (the game's Data Disc): progression, never balanced

KEY_CODES = (27, 28, 29, 30)  # Alpha, Beta, Gamma, Omega
KEYS = tuple(NAMES[code] for code in KEY_CODES)
FILLER_NAME = NAMES[SCRAP_METAL]

item_name_to_id = {name: ITEM_ID_BASE + code for code, name in NAMES.items()}
code_of = {name: code for code, name in NAMES.items()}
item_name_groups = {
    "Keys": set(KEYS),
    "Weapons": {NAMES[code] for code in PROGRESSIVE},
    "Weapon Upgrades": {NAMES[code] for code in UPGRADES},
    "Supplies": {NAMES[code] for code in SUPPLIES},
    "Scrap": {NAMES[code] for code in (SCRAP_METAL, SCRAP_ENERGY, SCRAP_CHUNK)},
}


def as_inventory(counts: Iterable[tuple[int, int]], guns_first: bool = True) -> dict[int, int]:
    """Counts by the game's item IDs as the game holds them: with Guns First, a progressive
    weapon as its weapon, and a second copy as its upgrade too, the way the game counts a weapon
    picked up twice; without it, every item as itself."""
    have: dict[int, int] = {}
    for code, n in counts:
        if code in PROGRESSIVE and guns_first:
            have[code] = 1
            if n > 1:
                have[PROGRESSIVE[code]] = 1
        else:
            have[code] = n
    return have


def classification(code: int) -> ItemClassification:
    if code == DATA_DISC or code in SUPPLIES:
        return ItemClassification.progression_skip_balancing
    if code in PROGRESSION:
        return ItemClassification.progression
    if code in FILLER:
        return ItemClassification.filler
    return ItemClassification.useful


class ZexionItem(Item):
    game = GAME
