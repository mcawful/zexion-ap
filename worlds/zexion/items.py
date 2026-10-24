"""Zexion's items: names, IDs and classifications.

Item codes are the game's item IDs (``ItemNames.txt``). Each weapon and its upgrade are one
progressive item, because the game itself counts a second copy of a weapon as its upgrade
(docs/re/randocells.md). Items the randomizer never places (the eggs, Super Fly, Zexion) are
left out. The game's Data Disc, which the randomizer never places either, is the OS Install
Disk of the OS Install Disks goal (the Goal option): an Archipelago-only count, never written
into the game's inventory, since holding one changes the game's endings (docs/re/memory.md, "The
Data Disc").

One item is not the game's: Assist Unlock (code 100), never in the pool. Sent from the server
console (``/send PLAYER Assist Unlock``) as an escape hatch, it makes the client stop locking the
assist menu for a seed generated with Allow Assists off (client/zexion_client.py; live-tested
2026-10-23).
"""

from __future__ import annotations

from BaseClasses import Item, ItemClassification

GAME = "Zexion"
ITEM_ID_BASE = 3392510_000  # Steam app ID x 1000, to keep IDs recognizable

# Not an item of the game's: the client's escape hatch from the assist lock.
ASSIST_UNLOCK = 100
GAME_ITEMS = range(0, 51)  # the codes that are the game's own inventory bytes

# Progressive weapon item code -> its upgrade's code.
PROGRESSIVE = {3: 9, 4: 14, 5: 15, 6: 10, 7: 12}

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
}

# Everything the rules read, directly or through the codes derived from items
# (worlds/zexion/logic.py), plus the keys for the core goal.
PROGRESSION = {0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 13, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26}
PROGRESSION |= {27, 28, 29, 30, 33, 34, 36, 37}
FILLER = {46, 47, 48, 49}
DATA_DISC = 44  # the OS Install Disk (the game's Data Disc): progression, never balanced

KEYS = ("Alpha Key", "Beta Key", "Gamma Key", "Omega Key")
FILLER_NAME = "Scrap Metal"

item_name_to_id = {name: ITEM_ID_BASE + code for code, name in NAMES.items()}
code_of = {name: code for code, name in NAMES.items()}


def classification(code: int) -> ItemClassification:
    if code == DATA_DISC:
        return ItemClassification.progression_skip_balancing
    if code in PROGRESSION:
        return ItemClassification.progression
    if code in FILLER:
        return ItemClassification.filler
    return ItemClassification.useful


class ZexionItem(Item):
    game = GAME
