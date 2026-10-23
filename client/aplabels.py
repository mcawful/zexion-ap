"""Names for other players' items in the game (helper phase 4).

The world fills a location holding another player's item with a stand-in: item 47 (Scrap
Energy), or 48 (Scrap Chunk) for a progression item, which the helper draws with Archipelago's up
arrow. The client asks the server what each such location holds (``LocationScouts``) and
sends the helper a label per map cell, "MASTER SWORD FOR ALICE"; while the player stands in that
cell the helper names the stand-in after it, so the game's own pickup popup shows it, with the
Archipelago logo the helper paints (native/README.md, docs/re/memory.md "Other players'
items").

Labels keep only what the game's 8 x 8 font draws: upper-case letters, digits, space and a
little punctuation, at most 49 bytes (the item record's name field).

Pure functions; the client does the networking and talks to the helper.
"""

from __future__ import annotations

import string
from collections.abc import Iterable, Mapping, Sequence

STAND_IN = 47  # Scrap Energy: other players' items
STAND_IN_PROGRESSION = 48  # Scrap Chunk: other players' progression items
NAME_MAX = 49
FONT = frozenset(string.ascii_uppercase + string.digits + " .,'!?-:()/&")
GENERAL = "ARCHIPELAGO ITEM"


def label(item_name: str, player_name: str) -> str:
    """The stand-in's name for one location: the item, then whom it is for."""
    text = f"{item_name} FOR {player_name}".upper()
    kept = "".join(c for c in text if c in FONT)
    kept = " ".join(kept.split())  # no runs of spaces where characters were dropped
    return kept[:NAME_MAX].rstrip() or GENERAL


def stand_ins(slot_data: Mapping[str, object]) -> frozenset[int]:
    """The stand-in items the world says it uses (none for a world from before them)."""
    codes = set()
    if slot_data.get("stand_in") == STAND_IN:
        codes.add(STAND_IN)
        if slot_data.get("stand_in_progression") == STAND_IN_PROGRESSION:
            codes.add(STAND_IN_PROGRESSION)
    return frozenset(codes)


def foreign_locations(
    location_items: Sequence[int], kept: Iterable[int], codes: Iterable[int] = (STAND_IN,)
) -> list[int]:
    """Indices of this world's locations that hold another player's item."""
    keep, wanted = set(kept), set(codes)
    return [i for i, code in enumerate(location_items) if code in wanted and i in keep]


def labels_request(labels: Mapping[tuple[int, int], str]) -> str:
    """The helper's ``ap_labels`` request: a label per map cell (x, y)."""
    lines = ["ap_labels"]
    for (x, y), text in sorted(labels.items()):
        lines.append(f"label {x} {y} {text.encode('ascii', 'ignore').hex()}")
    return "\n".join(lines)
