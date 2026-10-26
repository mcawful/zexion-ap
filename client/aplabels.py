"""Names for other players' items in the game.

The world puts a stand-in at each location holding another player's item: 47 (Scrap Energy),
or 48 (Scrap Chunk) for progression, and with the OS Install Disks goal 48 for this player's
own disks too. The client scouts those locations and sends the helper a label per map cell:
"PROGRESSION ITEM" or "AP ITEM" for another game's item, or ``@N`` for another Zexion player's
item (its own icon and name), which the pickup popup and the map show (native/README.md;
docs/re/memory.md, "Other players' items"). A label says what the item is, never whose it is.
Labels keep only what the game's 8 x 8 font draws, 24 characters a line. Pure functions;
client/ticks.py (labels_tick, foreign_labels) sends them.
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


def label(progression: bool) -> str:
    """The stand-in's name for another game's item: what kind of item it is."""
    return PROGRESSION_ITEM if progression else AP_ITEM


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


def own_label(item: int, name: str = "") -> str:
    """The label for a Zexion item behind a stand-in, another player's or this player's own (an
    OS Install Disk, client/discs.py): ``@N`` asks the helper for item N's own icon and the
    game's name for it, or ``name`` in its place (``@N NAME``, zexion_look; native/README.md)."""
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
