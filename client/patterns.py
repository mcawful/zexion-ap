"""Byte patterns that locate game state in Zexion.exe, and the game builds they were checked on.

Each target has two or more patterns from different code (candidates from tools/make_pattern.py);
every one must match exactly once in .text and all must lead to the same address
(client/game.py). Where a pattern refers to a field inside the target rather than its start,
another refers to the start, so a layout change makes them disagree. Recorded with evidence
in docs/re/memory.md. ``b1_rva`` is what each target resolved to on build B1, for checking
only: code never uses it as an address.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Build:
    name: str
    sha256: str  # of Zexion.exe on disk
    timestamp: int  # PE TimeDateStamp
    size_of_image: int  # PE SizeOfImage


# Builds whose meaning (item IDs, map values, struct layouts) docs/re/ has been checked on.
KNOWN_BUILDS = (
    Build(
        "B1",
        "6d8a94bd4d641ee2125b4f966b099e2d58530ddfb20531d1d619fc54edde469b",
        0x6A3F82DE,
        0x1204000,
    ),
)


@dataclass(frozen=True)
class Pattern:
    text: str  # hex bytes, "??" for wildcards
    disp_at: int  # offset in the match of the 32-bit field leading to the address
    kind: str  # "rip": match + rip_end + field (the only kind the client accepts)
    rip_end: int  # offset of the end of the referring instruction
    addend: int  # added to what the field leads to


@dataclass(frozen=True)
class Target:
    name: str
    description: str
    b1_rva: int
    patterns: tuple[Pattern, ...]


TARGETS = (
    Target(
        "map_grid",
        "the live 54 x 54 map grid (one byte per cell; 0x1B = item collected)",
        0x3E469D,
        (
            # from the instruction at RVA 0x16e30c on B1
            Pattern(
                (
                    "48 8D 05 ?? ?? ?? ?? 41 FF C7 49 03 C2 42 0F B6 "
                    "14 00 80 FA 18 7C 0B 8D 43 01 80 FA 32 0F 4F C3 "
                    "8B D8 48 8D 05 ?? ?? ?? ??"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x1fd3e7 on B1
            Pattern(
                (
                    "48 8D 05 ?? ?? ?? ?? 49 03 C2 48 89 84 24 A8 00 "
                    "00 00 48 8D 05 ?? ?? ?? ?? 49 03 C2 48 89 74 24 "
                    "40"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "inventory",
        "the live inventory, one byte per item ID (inside the player state)",
        0x3ED47E,
        (
            # from the instruction at RVA 0x1e36a9 on B1
            Pattern(
                (
                    "C6 05 ?? ?? ?? ?? 01 44 89 25 ?? ?? ?? ?? 44 88 "
                    "25 ?? ?? ?? ?? C7 05 ?? ?? ?? ?? 00 00 03 00"
                ),
                2,
                "rip",
                7,
                -0x2,
            ),
            # from the instruction at RVA 0x1e5ba1 on B1
            Pattern(
                (
                    "48 8B 05 ?? ?? ?? ?? 48 89 90 30 01 00 00 48 8B "
                    "05 ?? ?? ?? ?? 48 89 88 38 01 00 00 48 8B 05 ?? "
                    "?? ?? ?? 4C 89 80 40 01 00 00"
                ),
                3,
                "rip",
                7,
                0x71E,
            ),
        ),
    ),
    Target(
        "pickup_counter",
        "the pickup counter shown as ITEMS: x/150 (16-bit)",
        0x3D0168,
        (
            # from the instruction at RVA 0x1216f on B1
            Pattern(
                ("66 FF 0D ?? ?? ?? ?? EB 55 66 83 F8 07 75 4F 66 C7 83 2C 0D 00 00 01 00"),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x167db6 on B1
            Pattern(
                ("0F BF 05 ?? ?? ?? ?? F3 0F 11 74 24 40 F3 0F 11 74 24 38 48 8B 4C D1 F8"),
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "rando_blob",
        "the loaded save's randomizer section: location list, item table, settings",
        0xCDD6D0,
        (
            # from the instruction at RVA 0xeb6ec on B1
            Pattern(
                (
                    "48 8D 05 ?? ?? ?? ?? 44 0F B7 74 24 44 88 94 08 "
                    "6C 02 00 00 41 0F BF D1 44 0F B7 C3 66 44 3B CB "
                    "7D 1C"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0xecabd on B1
            Pattern(
                (
                    "4C 8D 3D ?? ?? ?? ?? 44 0F B7 8D 90 00 00 00 0F "
                    "1F 40 00 66 45 85 C9 0F 8E ?? ?? ?? ?? 4D 6B DB "
                    "C5"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0xa0ecf on B1, a field 0x6c4 into the target
            Pattern(
                (
                    "0F BF 05 ?? ?? ?? ?? 83 F8 09 7E 31 8B C8 B8 67 "
                    "66 66 66 F7 E9 C1 FA 02 8B C2 C1 E8 1F 03 D0 0F "
                    "B6 C2 42 88 94 2F 15 93 00 00"
                ),
                3,
                "rip",
                7,
                -0x6C4,
            ),
        ),
    ),
    Target(
        "slot_array",
        "the in-memory copy of every save slot's first block (stride 0x9408)",
        0x230600,
        (
            # from the instruction at RVA 0x1741c4 on B1
            Pattern(
                (
                    "48 8D 05 ?? ?? ?? ?? 48 03 C1 48 8D 4D 67 48 89 "
                    "4C 24 40 48 8D 0D ?? ?? ?? ?? C7 44 24 38 01 00 "
                    "00 00"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x176140 on B1
            Pattern(
                (
                    "48 8D 05 ?? ?? ?? ?? 4C 89 64 24 30 48 8D 0D ?? "
                    "?? ?? ?? 48 89 44 24 28 4C 8B C3 41 8B D5"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
)

BY_NAME = {t.name: t for t in TARGETS}
