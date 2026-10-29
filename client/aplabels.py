"""Names for other players' items in the game.

The world puts a stand-in at each location holding another player's item: 47 (Scrap Energy),
or 48 (Scrap Chunk) for progression (not another Zexion player's supplies, which stay minor:
world.important_elsewhere), and with the OS Install Disks goal 48 for this player's own disks
too. The client scouts those locations and sends the helper a label per map cell:
"PROGRESSION ITEM" or "AP ITEM" over "FOR ALICE", or ``@N`` for another Zexion player's item
(its own icon and name), which the pickup popup shows (native/README.md; docs/re/memory.md,
"Other players' items"). Labels keep only what the game's 8 x 8 font draws, 24 characters a
line. Pure functions; client/ticks.py (labels_tick, foreign_labels) sends them.
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
# A scouted item's flags: the progression bit (Archipelago's ItemClassification.progression,
# checked against it in worlds/zexion/test/test_client_agrees.py), kept here so the client's
# modules other than zexion_client.py import without Archipelago.
PROGRESSION = 0b00001


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


def _for(player_name: str) -> str:
    """A label's second line: whom the item is for, as much of the name as fits."""
    player = _fit(_font_words(player_name), PER_LINE - len("FOR "))
    return f"FOR {player}" if player else "FOR"


def label(progression: bool, player_name: str) -> str:
    """The stand-in's name for one location: what kind of item, a newline, then whom it is
    for."""
    item = PROGRESSION_ITEM if progression else AP_ITEM
    return f"{item}\n{_for(player_name)}"


# Zexion items shown under a name of our own: the Data Disc as OS INSTALL DISK, and the Area
# Map and the Intel Chip, whose record a seed with Area Map Hints renames.
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
    return f"{own_label(item, name)}\n{_for(player_name)}"


def own_label(item: int, name: str = "") -> str:
    """The label for one of this player's own items the world put a stand-in for (an OS Install
    Disk, client/discs.py): its icon, and ``name`` in place of the game's name for it (the
    helper's "@N NAME", native/README.md), for no one else."""
    kept = " ".join(_font_words(name))[: PER_LINE - 1]
    return f"@{item} {kept}" if kept else f"@{item}"


def labels_wanted(seed_matches: bool, left_seed: bool, wanted: bool) -> bool:
    """Whether the helper should hold the labels after a poll (and the disk count: state.
    Connection.dressed): while this seed's save is loaded,
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
