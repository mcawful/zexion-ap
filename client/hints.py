"""Area Maps as hints (the world's Area Map Hints option, on by default).

The world lists where each of this player's progression items is, in any world, in a random
order (slot data "hints": [player, location id, item code] each). Each Area Map the player
gets uses the next entry whose item the player does not have yet: the client creates an
Archipelago hint for it (``CreateHints``), which every player sees in the hint list, and a
notice in the game names it, worded as the hint: a popup of its text alone, left-aligned
("HINT: SLIDE MOD IS AT GUARDIA 22,7"). An Area Map comes either from another world,
received from the server (the notice in place of the received item's popup), or from one of
this world's own locations,
which the game hands out itself (the server sends this client no items of its own world): the
notice follows the game's own pickup popup.

Each Area Map is named by a key, ``r<k>`` for the k-th received one and ``l<i>`` for the one
at location i, and the entry it used is kept under that key in the server's data storage, so a
reconnect neither hints again nor names them differently.

Pure functions; the client does the networking.
"""

from __future__ import annotations

import string
from collections.abc import Iterable, Mapping, Sequence

from ids import AREA_MAP

# With the world's Area Map Hints on, the Area Maps are Intel Chips (ids.INTEL_CHIP), an item
# that is not the game's: the client counts a received one as the Area Map the game holds
# (ids.DRAWN_AS, ZexionContext.received), and the helper names the Area Map's record so
# (`intel on`).
NONE_LEFT = (-1, -1)  # an Area Map that found no item left to hint
POPUP_MAX = 200  # the helper's longest notice (native/src/game_calls.cpp, kNoticeMax)
NO_HINT_NOTE = "HINT: NO HINTS LEFT"
# What the helper keeps of a notice's text (native/src/game_calls.cpp, the notice line's
# parser): anything else would be dropped there, and counted here.
POPUP_CHARS = frozenset(string.ascii_uppercase + string.digits + " .,'!?-:")


def area_maps(
    codes: Sequence[int], location_items: Sequence[int], checked: Iterable[int]
) -> list[str]:
    """The key of every Area Map the player has: ``r<k>`` for the k-th among the received
    items ``codes``, ``l<i>`` for each checked location i of this world holding one."""
    received = [f"r{k}" for k, _ in enumerate(i for i, c in enumerate(codes) if c == AREA_MAP)]
    found = [
        f"l{i}"
        for i in sorted(set(checked))
        if 0 <= i < len(location_items) and location_items[i] == AREA_MAP
    ]
    return received + found


def received_area_maps(codes: Sequence[int]) -> dict[int, str]:
    """Each received Area Map's position among ``codes``, and its key."""
    positions = [i for i, code in enumerate(codes) if code == AREA_MAP]
    return {index: f"r{k}" for k, index in enumerate(positions)}


def next_targets(
    targets: Sequence[Sequence[int]],
    used: Iterable[Sequence[int]],
    owned: Iterable[tuple[int, int]],
    count: int,
) -> list[tuple[int, int]]:
    """The entries the next ``count`` Area Maps use: in the world's order, each (player,
    location) not used yet whose item the player does not have (``owned`` holds the (player,
    location) of each item the player has from a location), NONE_LEFT once there are none."""
    taken = {tuple(entry) for entry in used} | set(owned)
    out: list[tuple[int, int]] = []
    for player, location, _code in targets:
        if len(out) == count:
            break
        if (player, location) not in taken:
            out.append((player, location))
            taken.add((player, location))
    out += [NONE_LEFT] * (count - len(out))
    return out


def assign(
    targets: Sequence[Sequence[int]],
    stored: Mapping[str, Sequence[int]],
    keys: Sequence[str],
    owned: Iterable[tuple[int, int]],
) -> dict[str, tuple[int, int]]:
    """The entries for the keys not in ``stored`` yet, in the order of ``keys``."""
    missing = [key for key in keys if key not in stored]
    picked = next_targets(targets, stored.values(), owned, len(missing))
    return dict(zip(missing, picked, strict=True))


def _popup_text(text: str) -> str:
    kept = "".join(c for c in text.upper() if c in POPUP_CHARS)
    return " ".join(kept.split())


def popup_note(item: str, location: str, player: str | None = None) -> str:
    """A hint's notice, worded as the server's hint: "HINT: SLIDE MOD IS AT GUARDIA 22,7" for an
    item in this world, "HINT: SLIDE MOD IS AT <LOCATION> IN ALICE'S WORLD" for one in
    ``player``'s, in the characters the helper keeps (POPUP_CHARS). The helper wraps it, every
    line shown. Past POPUP_MAX (no real name comes near) it drops the words between the names
    ("HINT: SLIDE MOD - <LOCATION> - ALICE"), and is cut at a word only past that."""
    item, location = _popup_text(item), _popup_text(location)
    whose = None if player is None else _popup_text(player)
    full = f"HINT: {item} IS AT {location}" + (f" IN {whose}'S WORLD" if whose else "")
    short = f"HINT: {item} - {location}" + (f" - {whose}" if whose else "")
    for text in (full, short):
        if len(text) <= POPUP_MAX:
            return text
    return short[: POPUP_MAX + 1].rsplit(" ", 1)[0]
