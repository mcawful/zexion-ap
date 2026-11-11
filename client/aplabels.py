"""Names for other players' items in the game.

The world puts a stand-in at each location holding another player's item: 47 (Scrap Energy),
or 48 (Scrap Chunk) for progression (not another Zexion player's supplies, which stay minor:
world.important_elsewhere), and with the OS Install Disks goal 48 for this player's own disks
too. The client scouts those locations and sends the helper a label per map cell:
"Progression Item" or "AP Item" over "for Alice", ``@N`` for another Zexion player's item
(its own icon and name), or a Zexion trap's name (this player's own, which the world puts a
stand-in for too, or another Zexion player's), which the pickup popup shows (native/README.md;
docs/re/memory.md, "Other players' items"). Labels keep only what the game's 8 x 8 font draws,
24 characters a line, in the case they are written in: the helper shows them in capitals but in
English(HD).
Pure functions; client/ticks.py (labels_tick, foreign_labels) sends them.
"""

from __future__ import annotations

import string
from collections.abc import Iterable, Mapping, Sequence

from . import ids
from .ids import STAND_IN, STAND_IN_PROGRESSION

PER_LINE = 24  # the popup's characters a line
FONT = frozenset(string.ascii_uppercase + string.digits + " .,'!?-:()/&")
AP_ITEM = "AP Item"
PROGRESSION_ITEM = "Progression Item"
# A scouted item's flags: the progression bit (Archipelago's ItemClassification.progression,
# checked against it in worlds/zexion/test/test_client_agrees.py), kept here so the client's
# modules other than zexion_client.py import without Archipelago.
PROGRESSION = 0b00001


def font_text(text: str, chars: frozenset[str]) -> str:
    """``text`` in the characters ``chars`` holds (capital letters, digits, punctuation), each
    letter in its own case: the helper draws capitals but in English(HD). A character outside
    ASCII whose capitals are ASCII is spelled out by them first, in its case ("ß" as "ss", "ı"
    as "i", "ſ" as "s"), as the capitals the client sent before had it ("SS", "I", "S"); any
    other is dropped."""
    out = []
    for c in text:
        if not c.isascii():
            capitals = c.upper()
            if not capitals.isascii():
                continue
            c = capitals if c.isupper() else capitals.lower()
        out.append("".join(ch for ch in c if ch.upper() in chars))
    return "".join(out)


def _font_words(text: str) -> list[str]:
    return font_text(text, FONT).split()


def _fit(words: list[str], width: int) -> str:
    """As many whole words as fit in `width`; a first word longer than that, cut."""
    line = ""
    for word in words:
        if not line:
            line = word[:width]
            if len(word) > width:
                break
        elif len(line) + 1 + len(word) <= width:
            line += " " + word
        else:
            break
    return line


def _for(player_name: str) -> str:
    """A label's second line: whom the item is for, as much of the name as fits."""
    player = _fit(_font_words(player_name), PER_LINE - len("for "))
    return f"for {player}" if player else "for"


def label(progression: bool, player_name: str) -> str:
    """The stand-in's name for one location: what kind of item, a newline, then whom it is
    for."""
    item = PROGRESSION_ITEM if progression else AP_ITEM
    return f"{item}\n{_for(player_name)}"


# Zexion items shown under a name of our own: the Data Disc as OS Install Disk, and the Area
# Map and the Intel Chip, whose record a seed with Area Map Hints renames.
ZEXION_LOOKS = {
    ids.DATA_DISC: (ids.DATA_DISC, "OS Install Disk"),
    ids.AREA_MAP: (ids.AREA_MAP, "Area Map"),
    ids.INTEL_CHIP: (ids.DRAWN_AS[ids.INTEL_CHIP], "Intel Chip"),
}


def trap_label(code: int, player_name: str = "") -> str:
    """The label for a Zexion trap (ids.TRAPS): its name as text, with no icon (it has no record
    of the game's), then whom it is for, or nothing more for this player's own."""
    name = ids.TRAP_NAMES[code]
    return f"{name}\n{_for(player_name)}" if player_name else name


def zexion_look(code: int) -> tuple[int, str] | None:
    """The record and the name another Zexion player's item ``code`` is shown with (an empty
    name: the record's own), or None for a code that is not a Zexion item."""
    if code in ZEXION_LOOKS:
        return ZEXION_LOOKS[code]
    return (code, "") if code in ids.GAME_ITEMS else None


def zexion_label(item: int, player_name: str, name: str = "") -> str:
    """The label for another Zexion player's item: ``@N`` asks the helper for item N's own icon
    and the game's name for it (native/README.md), or ``name`` in its place (``@N NAME``,
    zexion_look), then whom it is for."""
    return f"{own_label(item, name)}\n{_for(player_name)}"


def own_label(item: int, name: str = "") -> str:
    """The label for one of this player's own items the world put a stand-in for (an OS Install
    Disk, client/discs.py): its icon, and ``name`` in place of the game's name for it (the
    helper's "@N NAME", native/README.md), for no one else."""
    kept = " ".join(_font_words(name))[: PER_LINE - 1]
    return f"@{item} {kept}" if kept else f"@{item}"


def labels_wanted(seed_matches: bool, left_seed: bool, wanted: bool) -> bool:
    """Whether the helper should hold the labels after a poll (and the disk count: state.
    SeedState.dressed): while this seed's save is loaded,
    and not while another save (or none) is, where Scrap Energy and Scrap Chunk are the game's
    own (their refill drop included). A poll that could not tell keeps what was wanted."""
    if seed_matches:
        return True
    if left_seed:
        return False
    return wanted


def foreign_locations(location_items: Sequence[int], kept: Iterable[int]) -> list[int]:
    """Indices of this world's locations that hold a stand-in (STAND_IN or STAND_IN_PROGRESSION,
    which the world uses too: worlds/zexion/test/test_client_agrees.py): another player's item,
    or one of this player's own the game must not hand out (an OS Install Disk, client/discs.py)."""
    keep = set(kept)
    return [
        i
        for i, code in enumerate(location_items)
        if code in (STAND_IN, STAND_IN_PROGRESSION) and i in keep
    ]


def labels_request(labels: Mapping[tuple[int, int], str]) -> str:
    """The helper's ``ap_labels`` request: a label per map cell (x, y)."""
    lines = ["ap_labels"]
    for (x, y), text in sorted(labels.items()):
        lines.append(f"label {x} {y} {text.encode('ascii', 'ignore').hex()}")
    return "\n".join(lines)
