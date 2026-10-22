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
)

BY_NAME = {t.name: t for t in TARGETS}
