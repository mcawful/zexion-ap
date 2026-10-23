"""Names for other players' items in the game (helper phase 4).

The world fills a location holding another player's item with a stand-in: item 47 (Scrap
Energy), or 48 (Scrap Chunk) for a progression item, which the helper draws with Archipelago's up
arrow. The client asks the server what each such location holds (``LocationScouts``) and
sends the helper a label per map cell, "MASTER SWORD FOR ALICE"; while the player stands in that
cell the helper names the stand-in after it, so the game's own pickup popup shows it, with the
Archipelago logo the helper paints (native/README.md, docs/re/memory.md "Other players'
items").

Labels keep only what the game's 8 x 8 font draws: upper-case letters, digits, space and a
little punctuation. A label is two lines, the item (at most 23 characters: the logo glyph comes
first) and "FOR ALICE" (at most 24), each cut at a word, joined by a newline; the helper lays
them out in the popup, 24 characters a line, each centered (native/README.md).

Pure functions; the client does the networking and talks to the helper.
"""

from __future__ import annotations

import string
from collections.abc import Iterable, Mapping, Sequence

STAND_IN = 47  # Scrap Energy: other players' items
STAND_IN_PROGRESSION = 48  # Scrap Chunk: other players' progression items
PER_LINE = 24  # the popup's characters a line
FONT = frozenset(string.ascii_uppercase + string.digits + " .,'!?-:()/&")
GENERAL = "ARCHIPELAGO ITEM"


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


def label(item_name: str, player_name: str) -> str:
    """The stand-in's name for one location: the item, a newline, then whom it is for."""
    item = _fit(_font_words(item_name), PER_LINE - 1)  # the logo glyph comes first
    player = _fit(_font_words(player_name), PER_LINE - len("FOR "))
    who = f"FOR {player}" if player else "FOR"
    if not item:
        return GENERAL
    return f"{item}\n{who}"


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
