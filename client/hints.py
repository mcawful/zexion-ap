"""Area Maps as hints (the world's Area Map Hints option).

The slot data lists where this player's progression items are ("hints": [player, location id,
item code]), in a random order. Each Area Map the player gets uses the next entry whose item
they lack: the client creates the Archipelago hint and the helper shows it as a notice, in
place of a received item's popup or after the game's own pickup popup. Each Area Map has a key
(``r<k>``: the k-th received; ``l<i>``: the one at location i) under which the server's data
storage keeps its entry, so a reconnect neither hints again nor renumbers. A received Intel
Chip counts as the Area Map the game holds (ids.DRAWN_AS). Pure functions; the client does the
networking.
"""

from __future__ import annotations

import string
from collections.abc import Iterable, Mapping, Sequence

from .ids import AREA_MAP

NONE_LEFT = (-1, -1)  # an Area Map that found no item left to hint
POPUP_MAX = 200  # the helper's longest notice (native/src/requests.cpp, kNoticeMax)
NO_HINT_NOTE = "HINT: NO HINTS LEFT"
# What the helper keeps of a notice's text (native/src/requests.cpp, the notice line's
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
    line shown. Past POPUP_MAX it drops the words between the names
    ("HINT: SLIDE MOD - <LOCATION> - ALICE"), and is cut at a word only past that."""
    item, location = _popup_text(item), _popup_text(location)
    whose = None if player is None else _popup_text(player)
    full = f"HINT: {item} IS AT {location}" + (f" IN {whose}'S WORLD" if whose else "")
    short = f"HINT: {item} - {location}" + (f" - {whose}" if whose else "")
    for text in (full, short):
        if len(text) <= POPUP_MAX:
            return text
    return short[: POPUP_MAX + 1].rsplit(" ", 1)[0]
