"""The OS Install Disks goal: the Goal option's "os_install_disks" (worlds/zexion/options.py).

The world shuffles OS Install Disks, the game's Data Disc item, into the multiworld, and the
player wins on having enough of them. The game's own Data Disc changes its endings
(docs/re/memory.md, "The Data Disc"), so a disk is never put in the game's inventory: the client
counts them,

    disks owned = the disks the slot starts with
                + the disks received from other worlds
                + this world's own disk locations the player has found

and shows the count in popups ("OS INSTALL DISK 3 OF 15", "OF 15" once the scanner has said
how many are needed). This world's own are stand-ins in the save's item table, listed by
location in the slot data, so the game hands out none. "Found" is what the client has reported
from the game's map, kept in the server's data storage so every save of the seed counts it
(zexion_client.own_found), not what the server has as checked, which after a release is every
location; the real disc the game gives for beating its secret boss does not count.

Pure functions; the client does the networking and talks to the helper.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass

import save_patch

DATA_DISC = 44  # the game's item ID, its Data Disc (worlds/zexion/items.py)
NAME = "OS INSTALL DISK"  # what the game's record calls DATA DISC is called here


@dataclass(frozen=True)
class DiscHunt:
    required: int
    placed: int
    start: int
    locations: frozenset[int]  # this world's location indices holding its own discs


def from_slot_data(slot: Mapping[str, object]) -> DiscHunt | None:
    """The hunt a seed's slot data sets up, or None for the core goal (or a world from before
    the option)."""
    if slot.get("goal") != "os_install_disks":
        return None
    return DiscHunt(
        required=int(slot["discs_required"]),
        placed=int(slot["discs_placed"]),
        start=int(slot.get("discs_start", 0)),
        locations=frozenset(int(i) for i in slot.get("disc_locations", [])),
    )


def owned(hunt: DiscHunt, received: Sequence[int], checked: Iterable[int]) -> int:
    """How many discs the player has: ``received`` are the item codes received from other
    worlds, ``checked`` this world's checked location indices."""
    return hunt.start + received.count(DATA_DISC) + len(hunt.locations & set(checked))


def note(count: int, required: int | None = None) -> str:
    """A disk's popup text: the count, and how many are needed once the player has been told
    (the scanner's message; ``required`` None before)."""
    if required is None:
        return f"OS INSTALL DISK {count}"
    return f"OS INSTALL DISK {count} OF {required}"


def received_notes(
    hunt: DiscHunt, received: Sequence[int], checked: Iterable[int], told: bool = True
) -> dict[int, str]:
    """The popup text of each received disk, by its position in ``received``: the count with
    that disk, so a batch of them counts up; with how many are needed once ``told``."""
    count = hunt.start + len(hunt.locations & set(checked))
    out = {}
    for index, code in enumerate(received):
        if code == DATA_DISC:
            count += 1
            out[index] = note(count, hunt.required if told else None)
    return out


def request(count: int, required: int, told: bool = True) -> str:
    """The helper's ``discs`` request while this seed's save is loaded: the count, the number
    that wins, the scanner's line in hex, and whether the player has been told how many are
    needed (native/README.md)."""
    line = scan_line(required).encode("ascii").hex()
    return f"discs {count} {required} {line} {int(told)}"


OFF = "discs"  # the request taking it all back

SLOT_COPY = 0x908  # a slot's copy of its randomizer section (client/game.py)
SLOTS_OFF = "disk_slots"  # the file select's count taken back


def seed_slots(
    markers: Sequence[tuple[int, bool]], copies: bytes, plan: save_patch.SeedPlan
) -> list[int]:
    """The save slots (1 to 30) holding this seed: a randomizer save (marker 1) in use, whose
    copy of its randomizer section holds this seed. The copy is not cleared when a slot is
    deleted or given a normal game, hence the marker."""
    out = []
    for n, (marker, used) in enumerate(markers, start=1):
        copy = copies[(n - 1) * SLOT_COPY : n * SLOT_COPY]
        if (
            marker == 1
            and used
            and len(copy) == SLOT_COPY
            and save_patch.section_matches(copy, plan)
        ):
            out.append(n)
    return out


def slots_request(count: int, required: int, slots: Sequence[int]) -> str:
    """The helper's ``disk_slots`` request: the count, the number needed, and the slots holding
    the seed, whose file select entries show the disks in place of the keys."""
    listed = ",".join(str(n) for n in slots) if slots else "-"
    return f"disk_slots {count} {required} {listed}"


def scan_line(required: int) -> str:
    """What the scanner says in place of ENERGY KEYS DETECTED! (docs/re/memory.md, "The
    scanner"): one line of the game's text, at most 31 characters."""
    return f"COLLECT {required} OS INSTALL DISK{'' if required == 1 else 'S'}!"
