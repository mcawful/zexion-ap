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
    kind: str  # "rip": match + rip_end + field; "at": the match itself (a function start)
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
    Target(
        "menu_state",
        "nonzero while a menu is open (pause, map, quit or file-select menus); 0 otherwise",
        0x345F03,
        (
            # from the instruction at RVA 0xa905a on B1 (stores 2)
            Pattern(
                (
                    "C6 05 ?? ?? ?? ?? 02 48 8B 07 33 F6 44 8B C6 48 8B 78 10 48 8B 87 E0 01 "
                    "00 00 48 8B 08 48 8B 91 F0 01 00 00 8B CE 39 8A D8 01 00 00"
                ),
                2,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x15aab4 on B1 (the only store of 0)
            Pattern(
                (
                    "C6 05 ?? ?? ?? ?? 00 C6 80 20 01 00 00 00 75 ?? E8 ?? ?? ?? ?? 33 C9 FF "
                    "15 ?? ?? ?? ??"
                ),
                2,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "save_loaded",
        "the loaded slot's empty marker (file 0x9360): 0xFF when no save is loaded",
        0x3D016A,
        (
            # from the instruction at RVA 0x1633c6 on B1 (turns the player on or off by it)
            Pattern(
                (
                    "80 3D ?? ?? ?? ?? FF 74 0D 48 8B 40 38 C6 80 20 01 00 00 01 EB 0B 48 8B "
                    "40 38 C6 80 20 01 00 00 00 48 8B 87 F0 01 00 00"
                ),
                2,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x9f267 on B1 (the load path sets 0)
            Pattern(
                ("C6 05 ?? ?? ?? ?? 00 80 3E 02 75 ?? C6 06 03 48 0F BE 0D ?? ?? ?? ?? 33 DB"),
                2,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x94849 on B1 (a return to the title sets 0xFF)
            Pattern(
                (
                    "C6 05 ?? ?? ?? ?? FF 66 89 05 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 33 C0 48 "
                    "89 0D ?? ?? ?? ?? 66 89 05 ?? ?? ?? ??"
                ),
                2,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "rando_marker",
        "the loaded save's randomizer marker (file 0x9402, 16-bit): 1 for a randomizer save, 0 "
        "for a normal one; the randomizer section is copied in only when it is 1",
        0x3E2DD8,
        (
            # from the instructions at RVA 0x9c91c-0x9c923 on B1 (the load routine reads the
            # save's 0x9402, block offset 0x9400, and stores it)
            Pattern(
                (
                    "0F B7 83 00 94 00 00 66 89 05 ?? ?? ?? ?? 8B 83 04 94 00 00 89 05 ?? ?? ?? ?? "
                    "66 89 2D ?? ?? ?? ??"
                ),
                10,
                "rip",
                14,
                0x0,
            ),
            # from the instruction at RVA 0xa05e1 on B1 (saving copies it into the slot)
            Pattern(
                "0F B7 05 ?? ?? ?? ?? 4C 8D 9B B1 7B 00 00 66 89 84 1F 00 94 00 00",
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instructions at RVA 0xf130e-0xf1315 on B1 (the generator sets it to 1)
            Pattern(
                (
                    "B8 01 00 00 00 33 DB 66 89 05 ?? ?? ?? ?? 0F 11 05 ?? ?? ?? ?? "
                    "0F 11 0D ?? ?? ?? ??"
                ),
                10,
                "rip",
                14,
                0x0,
            ),
        ),
    ),
    Target(
        "message_fn",
        ("the game message function: shows a popup (memory.md, Game functions the helper calls)"),
        0x182670,
        (
            # from the call at RVA 0x1f90f on B1 (the pickup popup)
            Pattern(
                "E8 ?? ?? ?? ?? 44 38 3D ?? ?? ?? ?? 48 8B D8 66 44 89 BC 24 90 00 00 00",
                1,
                "rip",
                5,
                0x0,
            ),
            # from the call at RVA 0x1fc891 on B1 (another popup)
            Pattern(
                ("E8 ?? ?? ?? ?? 48 85 C0 74 48 80 3D ?? ?? ?? ?? 00 74 3F 48 8B 0D ?? ?? ?? ??"),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                "89 54 24 10 48 89 4C 24 08 57 41 56 48 83 EC 48",
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "sound_fn",
        "the game's play-a-sound function",
        0x75E00,
        (
            # from the call at RVA 0x193178 on B1
            Pattern(
                (
                    "E8 ?? ?? ?? ?? F2 0F 10 93 08 02 00 00 48 8D 35 ?? ?? ?? ?? F2 0F 10 83 10 "
                    "02 00 00 0F 28 CA F2 0F 58 47 18 F2 0F 58 8B 00 02 00 00 66 41 FF C7"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the call at RVA 0xb6b25 on B1
            Pattern(
                (
                    "E8 ?? ?? ?? ?? 8B 86 E0 01 00 00 48 8B 8B 00 03 00 00 89 83 DC 02 00 00 8B "
                    "83 E4 02 00 00 89 41 48 80 BF 5E 93 00 00 FF"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                "4C 8B DC 55 41 54 41 57 49 8D 6B C1",
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "sound_system",
        "the static sound-system object the sound function takes first",
        0x22F370,
        (
            # from the instruction at RVA 0x1f9cd on B1 (the pickup sound)
            Pattern(
                (
                    "F3 0F 10 44 11 5C 0F B7 54 11 5A 48 8D 0D ?? ?? ?? ?? F3 0F 11 44 24 20 E8 "
                    "?? ?? ?? ??"
                ),
                14,
                "rip",
                18,
                0x0,
            ),
            # from the instruction at RVA 0x1cdffc on B1
            Pattern(
                (
                    "48 8D 0D ?? ?? ?? ?? F2 0F 10 97 00 11 00 00 0F 57 C0 66 89 74 24 38 BA 65 "
                    "00 00 00 66 89 6C 24 30 F3 44 0F 11 7C 24 28 66 0F 5A DB 66 0F 5A D2"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "hud_number_fn",
        "sets a HUD number object's value (a double in xmm1)",
        0x1F5B10,
        (
            # from the call at RVA 0x9e325 on B1 (the refresh after a save loads)
            Pattern(
                (
                    "E8 ?? ?? ?? ?? 0F BF 87 BE 0C 00 00 48 8D 8F B8 0B 00 00 66 0F 6E C8 F3 0F "
                    "E6 C9 E8 ?? ?? ?? ?? 48 8B 87 E8 09 00 00"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the call at RVA 0x1fdb3 on B1 (the pickup)
            Pattern(
                (
                    "E8 ?? ?? ?? ?? 0F BF 87 BE 0C 00 00 48 8D 8F B8 0B 00 00 66 0F 6E C8 F3 0F "
                    "E6 C9 E8 ?? ?? ?? ?? 48 8B CF"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                "48 89 6C 24 18 57 48 83 EC 60 0F 29 74 24 50",
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "hud_layout_fn",
        "lays out the missile and bomb HUD (icons and digits) from the player state",
        0x1D5470,
        (
            # from the call at RVA 0x9e308 on B1 (the refresh after a save loads)
            Pattern(
                "E8 ?? ?? ?? ?? 44 0F BF 87 BC 0C 00 00 48 8D 8F 30 0B 00 00",
                1,
                "rip",
                5,
                0x0,
            ),
            # from the call at RVA 0x1a7404 on B1
            Pattern(
                "E8 ?? ?? ?? ?? 45 0F BF 86 BC 0C 00 00 66 41 0F 6E C8",
                1,
                "rip",
                5,
                0x0,
            ),
            # from the call at RVA 0x1d729d on B1 (from the energy HUD)
            Pattern(
                "E8 ?? ?? ?? ?? 0F BF 83 EA 0C 00 00 F2 0F 10 83 F0 0C 00 00",
                1,
                "rip",
                5,
                0x0,
            ),
        ),
    ),
    Target(
        "energy_hud_fn",
        "redraws the energy-tank HUD from the player state",
        0x1D70C0,
        (
            # from the call at RVA 0x9e2f8 on B1 (the refresh after a save loads)
            Pattern(
                (
                    "E8 ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? 44 0F BF 87 "
                    "BC 0C 00 00"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the call at RVA 0x2033e on B1
            Pattern(
                (
                    "E8 ?? ?? ?? ?? F2 0F 10 9F 08 11 00 00 48 8D 0D ?? ?? ?? ?? F2 0F 10 97 00 "
                    "11 00 00 BA 46 00 00 00"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                "40 53 56 41 56 48 81 EC 90 00 00 00",
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "weapon_strip_fn",
        "redraws the weapon-switch icon strip while it is on screen",
        0x1BC7A0,
        (
            # from the call at RVA 0x1fdfb on B1 (the pickup)
            Pattern(
                "E8 ?? ?? ?? ?? 44 38 BF A7 10 00 00 75 06 41 0F B7 D7",
                1,
                "rip",
                5,
                0x0,
            ),
            # from the call at RVA 0x1c4c73 on B1
            Pattern(
                (
                    "E8 ?? ?? ?? ?? 01 2D ?? ?? ?? ?? 0F 88 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? 00 48 "
                    "BA 00 00 00 00 00 00 28 40"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                "48 83 EC 18 45 33 D2 4C 8B C1 44 38 15 ?? ?? ?? ??",
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "suit_color_fn",
        "sets the suit's palette (Magnus Armor's color), through OpenGL",
        0x1EDC00,
        (
            # from the call at RVA 0xb4670 on B1
            Pattern(
                (
                    "E8 ?? ?? ?? ?? 49 8B 84 24 F0 01 00 00 48 8B 88 28 02 00 00 48 8B 01 40 38 "
                    "B8 20 01 00 00 0F 84 ?? ?? ?? ??"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the call at RVA 0x18a0d8 on B1
            Pattern(
                (
                    "E8 ?? ?? ?? ?? BA FF FF FF FF 0F B6 CB E8 ?? ?? ?? ?? FF C3 83 FB 05 7C DF "
                    "E8 ?? ?? ?? ?? 48 B8 01 01 01 01 01 01 01 01"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                (
                    "40 53 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 58 4C 0F BE 35 ?? ?? ?? ?? "
                    "48 8D 1D ?? ?? ?? ??"
                ),
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "item_records",
        "holds a pointer to the item records (100 bytes each, indexed by item ID)",
        0xCDD068,
        (
            # from the instruction at RVA 0x3dbf8 on B1
            Pattern(
                (
                    "48 8B 05 ?? ?? ?? ?? 41 0F 28 C7 0F B6 5C 01 08 48 8B 0D ?? ?? ?? ?? 48 83 "
                    "C1 08 F3 0F 10 5C 24 60"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x1f0ab on B1 (the pickup)
            Pattern(
                "48 8B 15 ?? ?? ?? ?? BB 08 00 00 00 48 0F BF 86 E8 01 00 00",
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "last_message",
        "holds a pointer to the last popup message, with its sequence number at +8",
        0x3E29F0,
        (
            # from the instruction at RVA 0x1826b0 on B1 (the message function)
            Pattern(
                "4C 89 15 ?? ?? ?? ?? EB 40 41 C7 80 EC 01 00 00 01 00 00 00",
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x1d9183 on B1
            Pattern(
                "4C 8B 05 ?? ?? ?? ?? 4D 85 C0 0F 84 ?? ?? ?? ?? 66 83 F9 02 7F 28",
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "weapon_strip_gate",
        "a byte that is set while the weapon strip may be redrawn",
        0xCE02BD,
        (
            # from the instruction at RVA 0x1fde5 on B1 (the pickup)
            Pattern(
                "44 38 3D ?? ?? ?? ?? 74 12 66 44 39 BF 4C 0A 00 00",
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x1d71ca on B1 (the energy HUD)
            Pattern(
                "44 38 35 ?? ?? ?? ?? 75 5A 48 8B 83 E8 09 00 00",
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "transition_fn",
        ("holds the routine the game runs when the transition timer ends (a new game: newgame_fn)"),
        0xCE0F50,
        (
            # from the instruction at RVA 0xf6395 on B1 (START GAME)
            Pattern(
                (
                    "48 89 05 ?? ?? ?? ?? BA 25 00 00 00 B8 01 00 00 00 66 C7 44 24 38 10 00 66 "
                    "89 05 ?? ?? ?? ?? 0F BF 05 ?? ?? ?? ?? 83 C0 70 42 C6 04 37 00"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0xf5210 on B1 (the play-anyway path)
            Pattern(
                (
                    "48 89 05 ?? ?? ?? ?? BA 25 00 00 00 B8 01 00 00 00 66 C7 44 24 38 10 00 66 "
                    "89 05 ?? ?? ?? ?? 0F BF 05 ?? ?? ?? ?? 83 C0 70 C6 04 3B 00"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0xf6bea on B1
            Pattern(
                (
                    "48 39 3D ?? ?? ?? ?? 75 05 E8 ?? ?? ?? ?? 48 8B 83 E0 01 00 00 4C 8D 0D ?? "
                    "?? ?? ?? 48 8B 08 83 81 30 02 00 00 03"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "transition_timer",
        "frames left before the game runs transition_fn (32-bit)",
        0xDF6870,
        (
            # from the instruction at RVA 0xf9e50 on B1
            Pattern(
                (
                    "C7 05 ?? ?? ?? ?? 01 00 00 00 66 0F 6E D0 0F 5B D2 F3 0F 11 4C 24 20 E8 ?? "
                    "?? ?? ?? 8B 8F 30 02 00 00"
                ),
                2,
                "rip",
                10,
                0x0,
            ),
            # from the instruction at RVA 0x94713 on B1
            Pattern(
                (
                    "C7 05 ?? ?? ?? ?? 01 00 00 00 C6 05 ?? ?? ?? ?? 02 48 C7 05 ?? ?? ?? ?? FF "
                    "FF FF FF E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 33 D2"
                ),
                2,
                "rip",
                10,
                0x0,
            ),
        ),
    ),
    Target(
        "flags",
        "the loaded game's flags, one byte each (0x9C4 of them; flag 250: the ending began)",
        0x3CF78D,
        (
            # from the instruction at RVA 0x1fcfd2 on B1
            Pattern(
                "48 8D 0D ?? ?? ?? ?? 48 3B C1 72 1C 33 C9 41 0F B6 84 0E ED 00 00 00",
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x9c9c3 on B1 (the load routine)
            Pattern(
                (
                    "48 8D 0D ?? ?? ?? ?? C6 05 ?? ?? ?? ?? 00 89 2D ?? ?? ?? ?? 40 88 2D ?? ?? "
                    "?? ?? C6 80 20 01 00 00 00"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0xa2786 on B1, which bounds the 0x9C4 bytes
            Pattern(
                "48 8D 0D ?? ?? ?? ?? 48 3B C1 72 17 41 B8 C4 09 00 00",
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "event_table",
        "holds a pointer to the loaded area events, 0x28 bytes each (the AreaEvents file)",
        0xDF6880,
        (
            # from the instruction at RVA 0x2a248 on B1
            Pattern(
                (
                    "4C 8B 05 ?? ?? ?? ?? 33 D2 49 81 C0 E0 23 01 00 41 0F B6 CF 49 0F BE 00 "
                    "FF 94 C6 ?? ?? ?? ?? 0F B6 54 24 7C"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x8355a on B1
            Pattern(
                (
                    "48 8B 05 ?? ?? ?? ?? 48 8D 0C 89 44 0F B6 4C C8 22 4C 8D 14 C8 45 84 C9 "
                    "0F 84 ?? ?? ?? ?? 41 80 F9 01 75 47"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x1d3167 on B1
            Pattern(
                (
                    "48 8B 05 ?? ?? ?? ?? 48 8D 0C 89 44 0F B6 4C C8 22 4C 8D 14 C8 45 84 C9 "
                    "0F 84 ?? ?? ?? ?? 41 80 F9 01 75 45"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "event_count",
        "the number of loaded area events (16-bit)",
        0xEAE9A8,
        (
            # from the instruction at RVA 0x1ddf58 on B1 (the loader: 0x28 bytes per event)
            Pattern(
                (
                    "48 8D 15 ?? ?? ?? ?? 41 B8 02 00 00 00 48 8B CB FF 15 ?? ?? ?? ?? 48 8B "
                    "05 ?? ?? ?? ?? 48 85 C0 75 1A 48 0F BF 0D ?? ?? ?? ?? BA 28 00 00 00"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x4a556 on B1
            Pattern(
                "0F BF 05 ?? ?? ?? ?? 3B C8 0F 8D ?? ?? ?? ?? 48 63 C1 0F B6 93 11 02 00 00",
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "text_table",
        "holds a pointer to the loaded Text table, 32 bytes an entry (the Text file)",
        0x3CF688,
        (
            # from the instruction at RVA 0x10f318 on B1 (in the function at 0x10f110)
            Pattern(
                (
                    "48 03 15 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 87 F0 01 00 00 80 B8 30 02 00 00 "
                    "00 74 32 0F BE 05 ?? ?? ?? ?? 33 C9"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x10f554 on B1 (in the function at 0x10f380)
            Pattern(
                (
                    "48 03 15 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 87 F0 01 00 00 80 B8 30 02 00 00 "
                    "00 74 34"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x10f8e8 on B1 (in the function at 0x10f5c0)
            Pattern(
                (
                    "48 03 15 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 87 F0 01 00 00 80 B8 30 02 00 00 "
                    "00 74 32 0F BE 05 ?? ?? ?? ?? 41 B9 8F 01 00 00"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "equipment_fn",
        "the pause menu's EQUIPMENT callback, which builds the equipment screen",
        0x12BA90,
        (
            # from the instruction at RVA 0x167777 on B1 (the pause menu's builder)
            Pattern(
                (
                    "48 8D 05 ?? ?? ?? ?? 4C 89 6C 24 30 4C 89 6C 24 28 48 89 44 24 20 E8 ?? "
                    "?? ?? ?? F3 0F 10 35 ?? ?? ?? ?? 48 8B C8 48 8B 07 48 89 48 08"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x1671b7 on B1
            Pattern(
                (
                    "48 8D 05 ?? ?? ?? ?? 4C 89 6C 24 30 4C 89 6C 24 28 48 89 44 24 20 E8 ?? "
                    "?? ?? ?? 48 8B C8"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                (
                    "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC C8 00 "
                    "00 00 0F 29 70 A8 0F 29 78 98 44 0F 29 40 88 44 0F 29 90 78 FF FF FF 48 "
                    "8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 C7 48 8B 05 ?? ?? ?? ?? 48 8D 0D ?? "
                    "?? ?? ?? 48 89 48 68"
                ),
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "equip_panel_fn",
        "the equipment screen's panel handler",
        0xB2160,
        (
            # from the instruction at RVA 0x12dfb4 on B1 (the Energy Tank panel)
            Pattern(
                (
                    "4C 8D 05 ?? ?? ?? ?? 33 D2 41 8B CF E8 ?? ?? ?? ?? 48 8B D8 4C 8D 0D ?? "
                    "?? ?? ?? 48 8D 45 A7"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x12c81f on B1
            Pattern(
                ("4C 8D 05 ?? ?? ?? ?? 33 D2 B9 0A 00 00 00 E8 ?? ?? ?? ?? 48 8B F8 41 8B F5"),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                ("40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 B8 EE FF FF"),
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "widget_move_fn",
        "places a widget at its panel's position plus an offset, with its label",
        0xAA8C0,
        (
            # from the call at RVA 0x10561d on B1
            Pattern(
                (
                    "E8 ?? ?? ?? ?? 41 8B FF 4C 89 A3 E8 02 00 00 44 89 A3 C6 02 00 00 44 88 "
                    "BB C3 02 00 00 44 89 BB D8 01 00 00 44 39 BB E8 01 00 00 7E 2B"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the call at RVA 0x1432cf on B1
            Pattern(
                (
                    "E8 ?? ?? ?? ?? 44 89 64 24 40 48 8D 93 60 02 00 00 C7 44 24 38 30 00 00 "
                    "00 41 B9 06 00 00 00 C7 44 24 30 18 00 00 00 41 B8 02 00 00 00"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                ("48 89 5C 24 08 57 48 83 EC 20 48 8B B9 D8 01 00 00 48 8B D9 45 84 C9"),
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "text_place_fn",
        "places a text object's glyphs from (x, y), 8 pixels apart",
        0xA79E0,
        (
            # from the call at RVA 0x12ef28 on B1 (the missile count)
            Pattern(
                ("E8 ?? ?? ?? ?? 48 8B 83 E0 01 00 00 41 BF 01 00 00 00 41 0F BF 96 C0 0C 00 00"),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the call at RVA 0x12f017 on B1
            Pattern(
                ("E8 ?? ?? ?? ?? 48 8B 83 E0 01 00 00 48 8B 48 08 B8 02 00 00 00"),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                ("48 83 EC 58 66 41 0F 6E C0 45 8B D0"),
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "widget_new_fn",
        "makes a menu widget: its label, panel, handler, callback, record and index",
        0xAA610,
        (
            # from the call at RVA 0x17f011 on B1
            Pattern(
                (
                    "E8 ?? ?? ?? ?? 48 8B 8F E0 01 00 00 48 89 01 48 8B 87 E0 01 00 00 48 8B "
                    "08 48 8B 81 F0 01 00 00 C7 80 DC 01 00 00 1D 00 00 00 B8 56 55 55 55"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the call at RVA 0x162569 on B1
            Pattern(
                (
                    "E8 ?? ?? ?? ?? 48 8B 8F E0 01 00 00 45 8B C6 48 89 41 20 48 8B 8F E0 01 "
                    "00 00 48 8B 41 20 4C 8B 88 F0 01 00 00 45 39 B1 D8 01 00 00 7E 54"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                ("40 53 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 28 48 8B E9"),
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "sprite_sheet_fn",
        "gives an object a sprite definition's frames (the item icons for the equipment screen)",
        0x74780,
        (
            # from the call at RVA 0x180ff6 on B1
            Pattern(
                (
                    "E8 ?? ?? ?? ?? 4D 8B 16 49 8B 42 20 48 8B 88 E0 01 00 00 48 8B 01 48 8B "
                    "88 F0 01 00 00 48 8B 81 38 02 00 00 48 8B 08 44 39 B9 BC 01 00 00"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the call at RVA 0x1285b6 on B1
            Pattern(
                (
                    "E8 ?? ?? ?? ?? 49 FF C2 49 83 FA 17 75 CE 48 8B 8F E0 01 00 00 0F 28 DF "
                    "44 89 7C 24 28 41 B8 95 00 00 00 F3 0F 11 7C 24 20 48 8B 49 20"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
        ),
    ),
    Target(
        "panel_link_fn",
        "links a panel's widgets, their labels and glyphs into its menu's draw list",
        0xABB30,
        (
            # from the call at RVA 0x12f7e9 on B1
            Pattern(
                (
                    "E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 45 33 C0 48 8B 93 F0 01 00 00 48 8B "
                    "89 F8 01 00 00 E8 ?? ?? ?? ?? 48 8B 83 F0 01 00 00 48 8B 08 4C 39 29"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the call at RVA 0x136664 on B1
            Pattern(
                (
                    "E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 45 33 C0 48 8B 93 F0 01 00 00 48 8B "
                    "89 D0 02 00 00 E8 ?? ?? ?? ?? 48 8B 83 F0 01 00 00 48 8B 08 4C 39 21"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                ("48 89 5C 24 08 57 48 83 EC 20 33 DB"),
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "text_set_fn",
        "writes a string into a text object's glyphs from one glyph on, blanking the rest",
        0xA61C0,
        (
            # from the call at RVA 0x1119c7 on B1
            Pattern(
                (
                    "E8 ?? ?? ?? ?? 44 38 25 ?? ?? ?? ?? 48 8B 85 E0 01 00 00 48 8B 50 10 74 "
                    "23 C6 44 24 30 01 0F 28 DE 44 89 64 24 28 44 8B C6"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the call at RVA 0x179407 on B1
            Pattern(
                (
                    "E8 ?? ?? ?? ?? 48 8B 8B E0 01 00 00 45 33 C0 48 8B 01 4C 8B 88 F0 01 00 "
                    "00 45 39 81 D8 01 00 00 7E 54"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                ("40 53 55 56 57 48 83 EC 28 48 8B F9"),
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "routine_table",
        "the transition routines: 2 the pause (idle), 4 the ending, 15 the escape skip's prompt",
        0x3CF5F0,
        (
            # from the instruction at RVA 0x9498f on B1 (entry 8)
            Pattern(
                (
                    "48 8B 05 ?? ?? ?? ?? 48 0F 4F 05 ?? ?? ?? ?? 48 89 05 ?? ?? ?? ?? 48 C7 "
                    "05 ?? ?? ?? ?? 00 00 00 00"
                ),
                3,
                "rip",
                7,
                -0x40,
            ),
            # from the instruction at RVA 0x1a355 on B1 (entry 5)
            Pattern(
                ("48 8B 05 ?? ?? ?? ?? 33 F6 48 89 05 ?? ?? ?? ?? 66 89 35 ?? ?? ?? ?? EB 11"),
                3,
                "rip",
                7,
                -0x28,
            ),
        ),
    ),
    Target(
        "keys_branch",
        "the pause screen's jne past its KEYS panel when flag 39 is set (75 2E)",
        0x168C7D,
        (
            # from the instruction at RVA 0x168c76 on B1 (cmp flag 39, then the jne, its opcode
            # wild: the helper makes it a jmp while it runs)
            Pattern(
                (
                    "80 3D ?? ?? ?? ?? 00 ?? 2E 80 BB A9 10 00 00 00 75 25 80 BB AA 10 00 00 "
                    "00 75 1C"
                ),
                0,
                "at",
                0,
                0x7,
            ),
            # the same, with the four key tests (0x168c76 to 0x168ca3 on B1)
            Pattern(
                (
                    "80 3D ?? ?? ?? ?? 00 ?? 2E 80 BB A9 10 00 00 00 75 25 80 BB AA 10 00 00 "
                    "00 75 1C 80 BB AB 10 00 00 00 75 13 80 BB AC 10 00 00 00 75 0A 40 32 FF"
                ),
                0,
                "at",
                0,
                0x7,
            ),
            # from the main loop's reference to the pause-open routine 0x168bd0 (RVA 0x7ac28 on
            # B1), the branch 0xad into it
            Pattern(
                (
                    "4C 8D 0D ?? ?? ?? ?? 0F B6 0D ?? ?? ?? ?? 4C 8B 05 ?? ?? ?? ?? 48 89 B4 24 "
                    "A0 00 00 00"
                ),
                3,
                "rip",
                7,
                0xAD,
            ),
        ),
    ),
    Target(
        "malloc_slot",
        "the import slot of the C runtime's malloc, the game's heap",
        0x2097A0,
        (
            # from the call at RVA 0x89fd2 on B1
            Pattern(
                (
                    "FF 15 ?? ?? ?? ?? 45 33 DB 48 85 C0 0F 84 ?? ?? ?? ?? 4C 8B F8 EB 03 4C "
                    "8B F9 49 8B CF 49 8B C6 BA 03 00 00 00 66 0F 1F 84 00 00 00 00 00"
                ),
                2,
                "rip",
                6,
                0x0,
            ),
            # from the call at RVA 0x8f345 on B1
            Pattern(
                (
                    "FF 15 ?? ?? ?? ?? 48 8B D0 41 B9 FF FF FF FF 48 85 C0 0F 84 ?? ?? ?? ?? "
                    "48 8B CA 48 8B C3 4D 8B C4 66 66 0F 1F 84 00 00 00 00 00"
                ),
                2,
                "rip",
                6,
                0x0,
            ),
        ),
    ),
    Target(
        "free_slot",
        "the import slot of the C runtime's free, which a closing menu frees its arrays with",
        0x209798,
        (
            # from the call at RVA 0x1b4e8c on B1
            Pattern(
                (
                    "FF 15 ?? ?? ?? ?? 48 8B CB FF 15 ?? ?? ?? ?? 49 8B DE 4D 85 F6 74 0D 49 "
                    "8B 7E 18 39 6F 18 0F 84 ?? ?? ?? ?? 48 8B 74 24 68 4C 8B 64 24 40"
                ),
                2,
                "rip",
                6,
                0x0,
            ),
            # from the call at RVA 0x1b4e95 on B1
            Pattern(
                (
                    "FF 15 ?? ?? ?? ?? 49 8B DE 4D 85 F6 74 0D 49 8B 7E 18 39 6F 18 0F 84 ?? "
                    "?? ?? ?? 48 8B 74 24 68 4C 8B 64 24 40"
                ),
                2,
                "rip",
                6,
                0x0,
            ),
        ),
    ),
    Target(
        "start_count",
        "the start table's count (16-bit): the randomizer menu's start rooms, 0 until it opens",
        0xEAF7CC,
        (
            # from the instruction at RVA 0x1121fe on B1
            Pattern(
                "0F BF 0D ?? ?? ?? ?? FF C1 41 83 FE 01 73 11 B8 01 00 00 00",
                3,
                "rip",
                7,
                0x0,
            ),
            # from the start table's builder in the randomizer menu's opening (RVA 0x14bb02 on
            # B1), which stores the count
            Pattern(
                "66 89 1D ?? ?? ?? ?? 66 43 89 BC 87 ?? ?? ?? ?? 41 FF C3 41 83 FB 36",
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "start_cells",
        "the start table's map cells, (x, y) as two 16-bit values each",
        0xEB2070,
        (
            # from the instruction at RVA 0xf387f on B1 (START GAME's first step)
            Pattern(
                (
                    "4C 8B 15 ?? ?? ?? ?? 49 0F BF C1 48 6B C8 36 49 0F BF C2 48 03 C8 C6 85 "
                    "40 01 00 00 00"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the start table's builder in the randomizer menu's opening (RVA 0x14bb02 on
            # B1), which stores the count; this table is 0x28a4 bytes on from it on B1, and the
            # other pattern must agree
            Pattern(
                "66 89 1D ?? ?? ?? ?? 66 43 89 BC 87 ?? ?? ?? ?? 41 FF C3 41 83 FB 36",
                3,
                "rip",
                7,
                0x28A4,
            ),
        ),
    ),
    Target(
        "start_flags",
        "the start table's flags, one byte per room: 1 if it holds object 25",
        0xEAFC20,
        (
            # from the instruction at RVA 0xf4188 on B1 (START GAME's first step)
            Pattern(
                "40 38 35 ?? ?? ?? ?? 89 B5 38 01 00 00 75 17 33 C0",
                3,
                "rip",
                7,
                0x0,
            ),
            # from the start table's builder in the randomizer menu's opening (RVA 0x14bb02 on
            # B1), which stores the count; this table is 0x454 bytes on from it on B1, and the
            # other pattern must agree
            Pattern(
                "66 89 1D ?? ?? ?? ?? 66 43 89 BC 87 ?? ?? ?? ?? 41 FF C3 41 83 FB 36",
                3,
                "rip",
                7,
                0x454,
            ),
        ),
    ),
    Target(
        "sprite_texture",
        "holds a pointer to the sprite data; its +0x40 is the Sprites.png texture object",
        0xCDD060,
        (
            # from the instruction at RVA 0x7918a on B1, which stores the texture just loaded from
            # "Sprites.png" at +0x40
            Pattern(
                "48 8B 0D ?? ?? ?? ?? 32 DB 48 89 41 40 48 8B 05 ?? ?? ?? ??",
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x1a99e5 on B1
            Pattern(
                (
                    "48 8B 05 ?? ?? ?? ?? 0F BF 48 02 48 8B 05 ?? ?? ?? ?? 66 0F 6E C1 F3 0F E6 "
                    "C0 F2 0F 11 80 A8 00 00 00 48 8B 05 ?? ?? ?? ?? 4C 89 A8 B0 00 00 00"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x1f92a5 on B1
            Pattern(
                (
                    "48 8B 05 ?? ?? ?? ?? 0F BF 48 02 48 8B 05 ?? ?? ?? ?? 66 0F 6E C1 F3 0F E6 "
                    "C0 F2 0F 11 80 B8 00 00 00 48 8B 05 ?? ?? ?? ?? 0F BF 08 66 0F 6E C1"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "current_slot",
        "the save slot being played or started, 0-based (32-bit)",
        0xCDD080,
        (
            # from the instruction at RVA 0xe2137 on B1 (the file screen)
            Pattern(
                (
                    "8B 0D ?? ?? ?? ?? F7 E9 8B C2 C1 E8 1F 03 D0 8D 04 52 2B C8 B8 80 00 00 00 "
                    "66 89 86 DA 01 00 00 44 8D 2C 49"
                ),
                2,
                "rip",
                6,
                0x0,
            ),
            # from the instruction at RVA 0x163ec4 on B1
            Pattern(
                (
                    "48 63 05 ?? ?? ?? ?? 48 69 C8 08 94 00 00 66 83 BC 39 00 94 00 00 01 75 4C "
                    "41 B9 01 00 00 00 66 89 74 24 20"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0xa2abe on B1 (the first save of a new game)
            Pattern(
                (
                    "48 63 05 ?? ?? ?? ?? 48 69 D0 08 94 00 00 41 B9 01 00 00 00 41 B8 08 94 00 "
                    "00 48 03 D6 48 8B CB"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "newgame_fn",
        ("the new-game routine: builds a new game from the randomizer section and saves it"),
        0xA0570,
        (
            # from the instruction at RVA 0xe3129 on B1 (NORMAL's new game)
            Pattern(
                (
                    "48 8D 05 ?? ?? ?? ?? F3 0F 10 0D ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 48 89 05 "
                    "?? ?? ?? ?? BA 25 00 00 00 0F BF 05 ?? ?? ?? ??"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0xe2272 on B1 (a normal game, randomizer locked)
            Pattern(
                (
                    "48 8D 05 ?? ?? ?? ?? 48 89 05 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 0F BF 05 ?? "
                    "?? ?? ?? BA 25 00 00 00"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                "4C 8B DC 53 55 56 57 48 81 EC 28 02 00 00",
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "current_menu",
        ("holds the most recently opened menu (its panels at [menu]; the file screen's panel 3)"),
        0xDF82F8,
        (
            # from the instruction at RVA 0x124da2 on B1
            Pattern(
                (
                    "48 8B 05 ?? ?? ?? ?? BA 05 00 00 00 48 8B 08 48 8B 19 48 8B 83 E0 01 00 00 "
                    "4C 8B 00 49 8B C8 41 FF 90 C8 02 00 00 48 8B 83 E0 01 00 00"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x142a69 on B1
            Pattern(
                (
                    "48 8B 05 ?? ?? ?? ?? BA 05 00 00 00 48 8B 08 48 8B 01 48 8B 88 E0 01 00 00 "
                    "48 8B 01 48 8B C8 FF 90 C8 02 00 00 0F B6 05 ?? ?? ?? ??"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "randomizer_menu",
        ("the randomizer menu's state: seed text, item arrays, amounts, settings at fixed offsets"),
        0xDF7340,
        (
            # from the instruction at RVA 0x1478d0 on B1
            Pattern(
                (
                    "48 8D 0D ?? ?? ?? ?? 41 0F 45 D3 0F 44 DF 48 63 C2 44 8D 5A 01 0F BE 8C 08 "
                    "94 00 00 00 44 8D 04 1B 23 CB F7 D9 66 45 1B C9"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x145361 on B1
            Pattern(
                (
                    "48 8D 05 ?? ?? ?? ?? 42 80 7C 00 6C 00 0F 84 ?? ?? ?? ?? 42 80 7C 00 51 00 "
                    "74 37"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "widget_handler_fn",
        "the generic menu-widget input handler (calls a widget's callback on select)",
        0xD0240,
        (
            # from the instruction at RVA 0x100ff4 on B1
            Pattern(
                (
                    "4C 8D 0D ?? ?? ?? ?? 4C 8B C7 BA 18 00 00 00 48 89 41 18 48 8D 45 67 48 89 "
                    "44 24 40 48 8D 0D ?? ?? ?? ?? C7 44 24 38 04 00 00 00 4C 89 6C 24 30"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x159151 on B1
            Pattern(
                (
                    "4C 8D 0D ?? ?? ?? ?? 4C 8B C7 BA 0D 00 00 00 48 89 41 08 48 8D 45 67 48 89 "
                    "44 24 40 48 8D 0D ?? ?? ?? ?? C7 44 24 38 02 00 00 00 4C 89 7C 24 30"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B B1 D8 01 00 00 8B FA",
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "panel_handler_fn",
        "the menu-panel handler",
        0xAE4D0,
        (
            # from the instruction at RVA 0x135c0a on B1
            Pattern(
                (
                    "4C 8D 05 ?? ?? ?? ?? 33 D2 41 8B CF E8 ?? ?? ?? ?? 48 8B D8 C7 45 67 F4 FF "
                    "FC FF 48 8D 45 67 C7 45 6B 28 00 0C 00"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0xf560c on B1
            Pattern(
                (
                    "4C 8D 05 ?? ?? ?? ?? 33 D2 B9 03 00 00 00 48 8B F0 E8 ?? ?? ?? ?? 48 8B D8 "
                    "C7 84 24 80 00 00 00 F4 FF FB FF"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                (
                    "48 89 5C 24 08 57 48 83 EC 40 48 8B 99 D8 01 00 00 48 8B F9 85 D2 0F 85 ?? "
                    "?? ?? ?? 48 8B 89 F0 01 00 00"
                ),
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "rando_menu_fn",
        "the RANDOMIZER choice's callback: opens the randomizer menu",
        0x14B910,
        (
            # from the call at RVA 0x1512aa on B1
            Pattern(
                (
                    "E8 ?? ?? ?? ?? 48 8B 35 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 06 48 8D 3D ?? ?? "
                    "?? ?? BA 0F 00 00 00"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the game-mode panel's RANDOMIZER block at RVA 0x1801fa on B1
            Pattern(
                (
                    "B8 58 00 00 00 4C 8D 0D ?? ?? ?? ?? 66 89 45 6B 48 8D 0D ?? ?? ?? ?? 48 8D "
                    "45 67 41 BD 01 00 00 00 48 89 44 24 40 4C 8B C7 44 89 6C 24 38 48 8D 05 ?? "
                    "?? ?? ?? 4C 89 7C 24 30 BA 0B 00 00 00 4C 89 7C 24 28 48 89 44 24 20 E8 ?? "
                    "?? ?? ??"
                ),
                49,
                "rip",
                53,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                "48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8B EC",
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "placement_fn",
        "the randomizer's placement: the item table, starting items and amounts",
        0xF11A0,
        (
            # from the call at RVA 0xf62ea on B1 (START GAME)
            Pattern(
                (
                    "E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? 48 63 05 ?? ?? ?? ?? "
                    "48 69 F8 08 94 00 00"
                ),
                1,
                "rip",
                5,
                0x0,
            ),
            # from the call at RVA 0xf5059 on B1 (the random-start retry)
            Pattern(
                "E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 45 33 C0 0F B6 CB 84 C0 44 0F 45 C1",
                1,
                "rip",
                5,
                0x0,
            ),
        ),
    ),
    Target(
        "rando_newgame_fn",
        "queues a randomizer new game in the current slot (the play-anyway path)",
        0xF5160,
        (
            # from the instruction at RVA 0xf56af on B1 (the play-anyway button)
            Pattern(
                (
                    "48 8D 05 ?? ?? ?? ?? 48 89 6C 24 30 48 89 6C 24 28 48 89 44 24 20 E8 ?? ?? "
                    "?? ?? 48 8B 8B E0 01 00 00 4C 8D 0D ?? ?? ?? ?? 4C 8B C3 BA 37 00 00 00"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                "48 89 5C 24 08 57 48 81 EC C0 00 00 00 48 8B 05 ?? ?? ?? ??",
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "popup_width_code",
        (
            "the pickup popup's `mov edx, 24` (its line width) on the path items with no icon "
            "take; the helper changes the 24 for other players' items"
        ),
        0x1F8F6,
        (
            # the instruction itself on B1, with the rest of the call's arguments; its value is
            # wild, since the helper changes it while it runs
            Pattern(
                (
                    "BA ?? ?? ?? ?? C7 44 24 30 B4 00 00 00 41 B9 58 00 00 00 41 B8 A0 00 00 00 "
                    "E8 ?? ?? ?? ?? 44 38 3D"
                ),
                0,
                "at",
                0,
                0x0,
            ),
            # from the jump to it at RVA 0x1f7f4 on B1, taken when the record has no icon
            Pattern(
                (
                    "48 0F BF 86 E8 01 00 00 48 6B C8 64 48 8D 69 0A 48 03 EB 44 38 7C 19 08 "
                    "48 8B CD 0F 84 ?? ?? ?? ?? E8 ?? ?? ?? ?? 41"
                ),
                29,
                "rip",
                33,
                0x0,
            ),
            # from the end of the icon path just before it (RVA 0x1f8dd on B1)
            Pattern(
                (
                    "48 8D 8C 24 92 00 00 00 4A 8D 14 40 41 B8 6A 00 00 00 E8 ?? ?? ?? ?? EB ?? "
                    "BA ?? ?? ?? ??"
                ),
                0,
                "at",
                0,
                25,
            ),
        ),
    ),
    Target(
        "item_pool",
        "the pool of 128 live item objects, 0x3B0 bytes each (memory.md, Live item objects)",
        0xB6E030,
        (
            # from the stores of the pool's next-free-slot number, which lies right after the
            # 128 objects (at RVA 0x19ec55 and 0x19ec78 on B1, in the item spawn function)
            Pattern(
                "66 89 15 ?? ?? ?? ?? 0F 1F 40 00 48 0F BF C2 48 69 C8 B0 03 00 00",
                3,
                "rip",
                7,
                -0x1D800,
            ),
            # the read of it at RVA 0x19e9ec on B1, with the 128 slots (0x80) the search runs to
            Pattern(
                "0F B7 15 ?? ?? ?? ?? 45 33 DB 41 B9 80 00 00 00 66 83 FA FF",
                3,
                "rip",
                7,
                -0x1D800,
            ),
        ),
    ),
    Target(
        "palette_table",
        "the palette objects by number, eight bytes each (an object's +0x198 is one of them)",
        0xCDF5C0,
        (
            # from the instruction at RVA 0xfffda on B1, which gives an object palette 0
            Pattern(
                (
                    "48 8B 05 ?? ?? ?? ?? 48 8B 11 48 89 82 98 01 00 00 33 C0 66 89 82 A0 01 00 "
                    "00 48 8B 83 E0 01 00 00 48 8B 96 E8 02 00 00 48 8B 88 80 00 00 00"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x129296 on B1, which reads palette 2
            Pattern(
                (
                    "48 8B 05 ?? ?? ?? ?? 48 89 81 98 01 00 00 66 89 B1 A0 01 00 00 48 8B 83 F0 "
                    "01 00 00 48 8B 48 10 44 39 B9 BC 01 00 00"
                ),
                3,
                "rip",
                7,
                -0x10,
            ),
        ),
    ),
    Target(
        "frame_clock",
        "the 64-bit count item animations follow (the item update at 0x19e2e0 divides it)",
        0x3CF750,
        (
            # from the instruction at RVA 0x1c6333 on B1
            Pattern(
                (
                    "48 8B 0D ?? ?? ?? ?? 48 8B C1 48 F7 D8 48 0F 48 C1 48 99 48 2B C2 48 D1 F8 "
                    "48 99 83 E0 01 48 33 C2 48 2B C2 66 83 C0 45"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the item update's read of it at RVA 0x19e342 on B1
            Pattern(
                "48 8B 05 ?? ?? ?? ?? 48 99 49 0F BF 4C 38 04 48 F7 F9 49 0F BF 4C 38 06",
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "sprite_templates",
        "holds the pointer to the sprite definitions' object templates; +0x110 is the items'",
        0xDF7110,
        (
            # from the item spawn function at RVA 0x19ec92 on B1, which takes template 34
            Pattern(
                (
                    "48 8B 05 ?? ?? ?? ?? 4C 8D 15 ?? ?? ?? ?? 48 8B B0 10 01 00 00 4D 85 C0 "
                    "75 2D B9 D8 01 00 00"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the instruction at RVA 0x1032ac on B1
            Pattern(
                (
                    "48 8B 0D ?? ?? ?? ?? 48 8B 93 F0 01 00 00 48 81 C2 10 01 00 00 48 8B 49 08 "
                    "E8 ?? ?? ?? ?? 48 8B 83 F0 01 00 00 48 8B 88 10 01 00 00 4C 39 29"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "event_handlers",
        "the area-event handlers, a function pointer per event type (the pickup calls one)",
        0xCDD090,
        (
            # from two of the places that run an area event through it, at RVA 0xd679 and
            # 0xd8df on B1: handler[type](cell, cell, record)
            Pattern(
                (
                    "4C 8D 0D ?? ?? ?? ?? 4C 8D 04 C8 40 0F B6 CE 49 0F BE 00 41 FF 14 C1 48 8B "
                    "5C 24 50 48 8B 74 24 58 48 83 C4 40 5F C3 48 89 5C 24 10"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            Pattern(
                (
                    "4C 8D 0D ?? ?? ?? ?? 4C 8D 04 C8 40 0F B6 CD 49 0F BE 00 41 FF 14 C1 48 8B "
                    "5C 24 58"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
    Target(
        "death_fn",
        "the player's death: death_fn(player, 0), called once energy is 0 or below (DeathLink)",
        0x1D9FA0,
        (
            # the touch handler's call after damage, at RVA 0x1f07a on B1
            Pattern(
                "E8 ?? ?? ?? ?? E9 ?? ?? ?? ?? 66 83 F8 03 0F 85 ?? ?? ?? ?? 49 8B B6 40 01 00 00",
                1,
                "rip",
                5,
                0x0,
            ),
            # another hit's call, at RVA 0x1b9d3a on B1
            Pattern(
                "E8 ?? ?? ?? ?? 80 BB FE 0C 00 00 00 0F 28 7C 24 40",
                1,
                "rip",
                5,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                "40 53 48 81 EC 90 00 00 00 80 3D ?? ?? ?? ?? 00",
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "assist_menu_fn",
        "ASSIST's callback in the options menu with a save loaded: opens the assist menu",
        0x153D20,
        (
            # the options menu's construction of ASSIST, at RVA 0x16286c on B1
            Pattern(
                (
                    "48 8D 05 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 48 89 44 24 20 E8 ?? ?? ?? ?? 48 "
                    "8B 8F E0 01 00 00 45 8B C6 48 89 04 D9 4C 8B 97 E0 01 00 00"
                ),
                3,
                "rip",
                7,
                0x0,
            ),
            # from the function's start on B1
            Pattern(
                "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 68",
                0,
                "at",
                0,
                0x0,
            ),
        ),
    ),
    Target(
        "object_pool",
        "the general objects, 1000 of 0x5C8 bytes (items in reversed-gravity rooms among them)",
        0x758440,
        (
            # the store of the pool's next-free-slot number, which lies right after the 1000
            # objects, at RVA 0x44bf1 on B1, in the object spawn function (with the 0x5C8 stride)
            Pattern(
                "66 89 15 ?? ?? ?? ?? 0F 1F 84 00 00 00 00 00 48 0F BF C2 48 69 C8 C8 05 00 00",
                3,
                "rip",
                7,
                -0x169540,
            ),
            # the read of it at RVA 0x44bd7 on B1, with the 1000 slots (0x3E8) the search runs to
            Pattern(
                "0F B7 15 ?? ?? ?? ?? 41 B8 E8 03 00 00 66 41 3B D3",
                3,
                "rip",
                7,
                -0x169540,
            ),
        ),
    ),
    Target(
        "halt_flag",
        "a byte the death routine (and much of the game) returns early on while it is set",
        0x345EF4,
        (
            # from the test at RVA 0x178c85 on B1
            Pattern(
                (
                    "80 3D ?? ?? ?? ?? 00 75 1E 33 C9 FF 15 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 33 "
                    "C9 C6 80 20 01 00 00 00 FF 15 ?? ?? ?? ?? 48 8B 83 E0 01 00 00"
                ),
                2,
                "rip",
                7,
                0x0,
            ),
            # from the test at RVA 0x1ccb93 on B1, in the player's update
            Pattern(
                (
                    "80 3D ?? ?? ?? ?? 00 75 10 66 FF 8B BE 0C 00 00 33 C0 66 89 83 8C 10 00 00 "
                    "FF 05 ?? ?? ?? ?? B9 CC 01 00 00"
                ),
                2,
                "rip",
                7,
                0x0,
            ),
        ),
    ),
)

BY_NAME = {t.name: t for t in TARGETS}
