"""Zexion's items: names, IDs and classifications.

Item codes are the game's item IDs (``ItemNames.txt``). Each weapon and its upgrade are one
progressive item, because the game itself counts a second copy of a weapon as its upgrade
(docs/re/randocells.md). Items the randomizer never places (the eggs, Super Fly, Zexion) are
left out. The game's Data Disc, which the randomizer never places either, is the OS Install
Disk of the OS Install Disks goal (the Goal option): an Archipelago-only count, never written
into the game's inventory, since holding one changes the game's endings (docs/re/memory.md, "The
Data Disc").

Two items are not the game's. Assist Unlock (code 100), never in the pool: sent from the server
console (``/send PLAYER Assist Unlock``) as an escape hatch, it makes the client stop locking the
assist menu for a seed generated with Allow Assists off (client/zexion_client.py; live-tested
2026-10-23). Intel Chip (code 101): the Area Map under the name its use has with Area Map Hints
on, which puts it in the pool in the Area Maps' places. The game holds and draws it as the Area
Map (DRAWN_AS), whose record the client's helper renames INTEL CHIP, its map reveal turned off,
while the seed's save is loaded (docs/re/memory.md, "Intel Chips").
"""

from __future__ import annotations

from BaseClasses import Item, ItemClassification

from .logic import WEAPON_UPGRADES

GAME = "Zexion"
ITEM_ID_BASE = 3392510_000  # Steam app ID x 1000, to keep IDs recognizable

# Not items of the game's: the client's escape hatch from the assist lock, and the Area Map as a
# hint (the Area Map Hints option), which the game holds and draws as the Area Map.
ASSIST_UNLOCK = 100
AREA_MAP, INTEL_CHIP = 49, 101
DRAWN_AS = {INTEL_CHIP: AREA_MAP}
GAME_ITEMS = range(0, 51)  # the codes that are the game's own inventory bytes

# Progressive weapon item code -> its upgrade's code, as the logic counts them.
PROGRESSIVE = WEAPON_UPGRADES
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
    11: "Rocket Slide",
    13: "Power Cartridge",
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
PROGRESSION |= {27, 28, 29, 30, 33, 34, 36, 37}
# Supplies, with many copies in every pool (Energy Tank, Missile Container, Mega Bomb, Missile
# Box, Bomb Box): progression for the rules' counts, but skipped by progression balancing (the
# owner's call) and not worth an Area Map's hint (worlds/zexion/world.py).
SUPPLIES = {0, 1, 2, 36, 37}
# Intel Chip as the Area Map was: filler, so excluded locations still have filler to take.
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
    "Supplies": {NAMES[code] for code in SUPPLIES},
    "Scrap": {NAMES[code] for code in (SCRAP_METAL, SCRAP_ENERGY, SCRAP_CHUNK)},
}


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
