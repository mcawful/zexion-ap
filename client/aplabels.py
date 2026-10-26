"""Names for other players' items in the game (helper phase 4).

The world fills a location holding another player's item with a stand-in: item 47 (Scrap
Energy), or 48 (Scrap Chunk) for a progression item, which the helper draws with Archipelago's up
arrow; and, with the OS Install Disks goal, one holding this player's own disk with 48 too,
labeled ``@44 OS INSTALL DISK`` (own_label): the disk's look, its glyph and that name. The
client asks the server whom each such location's item is for (``LocationScouts``) and sends the
helper a label per map cell, "PROGRESSION ITEM" or "AP ITEM" over "FOR ALICE":
generic names, as many Archipelago games use (Pokemon Red and Blue's "AP ITEM", Castlevania:
Circle of the Moon's icons by classification), since another game's item names can be long. For
another Zexion player's item the popup instead shows the item's own icon and the game's name for
it (or, for an OS Install Disk, an Area Map and an Intel Chip, the name given here: zexion_look),
and the helper shows the item itself in the room, as the Castlevania worlds show their own
game's items.
The helper names each stand-in after the label of the cell its item object carries (an enemy's
drop lands wherever the enemy dies), so the game's own pickup popup shows it, with the
Archipelago logo the helper paints (native/README.md, docs/re/memory.md "Other players' items").

Labels keep only what the game's 8 x 8 font draws: upper-case letters, digits, space and a
little punctuation. A label is two lines joined by a newline, the item and "FOR ALICE" (at most
24 characters, cut at a word); the helper lays them out in the popup, each centered.

Pure functions; the client does the networking and talks to the helper.
"""

from __future__ import annotations

import string
from collections.abc import Iterable, Mapping, Sequence

from . import ids
from .ids import STAND_IN, STAND_IN_PROGRESSION

PER_LINE = 24  # the popup's characters a line
FONT = frozenset(string.ascii_uppercase + string.digits + " .,'!?-:()/&")
AP_ITEM = "AP ITEM"
PROGRESSION_ITEM = "PROGRESSION ITEM"


def _font_words(text: str) -> list[str]:
    kept = "".join(c for c in text.upper() if c in FONT)
    return kept.split()


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


def label(progression: bool, player_name: str) -> str:
    """The stand-in's name for one location: what kind of item, a newline, then whom it is
    for."""
    item = PROGRESSION_ITEM if progression else AP_ITEM
    player = _fit(_font_words(player_name), PER_LINE - len("FOR "))
    who = f"FOR {player}" if player else "FOR"
    return f"{item}\n{who}"


# Another Zexion player's items whose name in this game is not what the game's own record
# says in every seed: the OS Install Disk (the game's DATA DISC), the Area Map, whose record this
# player's own seed may have the helper rename INTEL CHIP (Area Map Hints), and the Intel Chip,
# an item that is not the game's (code 101, worlds/zexion/items.py), drawn as the Area Map.
# Each is its record's look under the name given here.
ZEXION_LOOKS = {
    ids.DATA_DISC: (ids.DATA_DISC, "OS INSTALL DISK"),
    ids.AREA_MAP: (ids.AREA_MAP, "AREA MAP"),
    ids.INTEL_CHIP: (ids.DRAWN_AS[ids.INTEL_CHIP], "INTEL CHIP"),
}


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
    player = _fit(_font_words(player_name), PER_LINE - len("FOR "))
    who = f"FOR {player}" if player else "FOR"
    kept = " ".join(_font_words(name))[: PER_LINE - 1]
    return f"@{item} {kept}\n{who}" if kept else f"@{item}\n{who}"


def own_label(item: int, name: str = "") -> str:
    """The label for one of this player's own items the world put a stand-in for (an OS Install
    Disk, client/discs.py): its icon, and ``name`` in place of the game's name for it (the
    helper's "@N NAME", native/README.md), for no one else."""
    kept = " ".join(_font_words(name))[: PER_LINE - 1]
    return f"@{item} {kept}" if kept else f"@{item}"


def labels_wanted(seed_matches: bool, left_seed: bool, wanted: bool) -> bool:
    """Whether the helper should hold the labels after a poll: while this seed's save is loaded,
    and not while another save (or none) is, where Scrap Energy and Scrap Chunk are the game's
    own (their refill drop included). A poll that could not tell keeps what was wanted."""
    if seed_matches:
        return True
    if left_seed:
        return False
    return wanted


def stand_ins(slot_data: Mapping[str, object]) -> frozenset[int]:
    """The stand-in items the world says it uses; ValueError if they are not the client's."""
    codes = (slot_data["stand_in"], slot_data["stand_in_progression"])
    if codes != (STAND_IN, STAND_IN_PROGRESSION):
        raise ValueError(f"the world's stand-in items {codes} are not the client's")
    return frozenset(codes)


def foreign_locations(
    location_items: Sequence[int], kept: Iterable[int], codes: Iterable[int] = (STAND_IN,)
) -> list[int]:
    """Indices of this world's locations that hold a stand-in: another player's item, or one
    of this player's own the game must not hand out (an OS Install Disk, client/discs.py)."""
    keep, wanted = set(kept), set(codes)
    return [i for i, code in enumerate(location_items) if code in wanted and i in keep]


def labels_request(labels: Mapping[tuple[int, int], str]) -> str:
    """The helper's ``ap_labels`` request: a label per map cell (x, y)."""
    lines = ["ap_labels"]
    for (x, y), text in sorted(labels.items()):
        lines.append(f"label {x} {y} {text.encode('ascii', 'ignore').hex()}")
    return "\n".join(lines)
