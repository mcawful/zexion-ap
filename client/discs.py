"""The OS Install Disks goal: the Goal option's "os_install_disks" (worlds/zexion/options.py).

The world shuffles OS Install Disks, the game's Data Disc item, into the multiworld, and the
player wins on having enough of them. The game's own Data Disc changes its endings
(docs/re/memory.md, "The Data Disc"), so a disk is never put in the game's inventory: the client
counts them,

    disks owned = the disks the slot starts with
                + the disks received from other worlds
                + this world's own disk locations the player has found

Shown in popups ("OS INSTALL DISK 3 OF 15", "OF 15" once the scanner has said how many).
"Found" is what clients reported from the game's map (storage.own_found), not the server's
checked set, which a release fills; the disc the game gives for its secret boss does not count.
Pure functions; client/ticks.py talks to the helper.
"""

from __future__ import annotations

import struct
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass

from . import save_patch
from .ids import DATA_DISC, SECTION_SIZE

NAME = "OS INSTALL DISK"  # what the game's record calls DATA DISC is called here


@dataclass(frozen=True)
class DiscHunt:
    required: int
    placed: int
    start: int
    locations: frozenset[int]  # this world's location indices holding its own discs


GOALS = ("core", "os_install_disks")  # the Goal option's keys (worlds/zexion/options.py)


def from_slot_data(slot: Mapping[str, object]) -> DiscHunt | None:
    """The hunt a seed's slot data sets up, or None for the core goal."""
    goal = slot["goal"]
    if goal not in GOALS:
        raise ValueError(f"the goal {goal!r} is not one the client knows")
    if goal != "os_install_disks":
        return None
    return DiscHunt(
        required=int(slot["discs_required"]),
        placed=int(slot["discs_placed"]),
        start=int(slot["discs_start"]),
        locations=frozenset(int(i) for i in slot["disc_locations"]),
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

SLOTS_OFF = "disk_slots"  # the file select's count taken back


def seed_slots(
    markers: Sequence[tuple[int, bool]], copies: bytes, plan: save_patch.SeedPlan
) -> list[int]:
    """The save slots (1 to 30) holding this seed: a randomizer save (marker 1) in use, whose
    copy of its randomizer section holds this seed. The copy is not cleared when a slot is
    deleted or given a normal game, hence the marker."""
    out = []
    for n, (marker, used) in enumerate(markers, start=1):
        copy = copies[(n - 1) * SECTION_SIZE : n * SECTION_SIZE]
        if (
            marker == 1
            and used
            and len(copy) == SECTION_SIZE
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


# Told once the player has enough disks: RETURN HOME WITH THE DISKS? is offered only after a
# save at the landing pod, the seed's start.
POD_NOTE = "ENOUGH OS INSTALL DISKS FOUND. SAVE AT THE LANDING POD TO RETURN HOME."

# The randomizer section's start record (docs/re/save-format.md, "The continue point and the
# seed's start"): the start-candidate index less 1, then the start's tile column and row.
START_AT = save_patch.START_RECORD - save_patch.SECTION
# A save stamp (game.Game.save_stamp): the slot, then the slot block's bytes from +0x9354, of
# which the continue point's row is at 6 and its column at 8.
STAMP_ROW_AT = 4 + 6


def _start(blob: bytes) -> tuple[int, int] | None:
    """The start record's (column, row), or None if the blob is too short to hold it."""
    if len(blob) < START_AT + 6:
        return None
    return struct.unpack_from("<2H", blob, START_AT + 2)


def start_cell(blob: bytes) -> tuple[int, int]:
    """The seed's start cell as the player state counts cells (game.PLAYER_CELL): the
    game's (X, Y), which is this repo's map cell (save_patch.cell_of) as (y, x)
    (docs/re/randocells.md, "Grid order"; save-format.md and memory.md for the formulas)."""
    start = _start(blob)
    if start is None:
        return (-1, -1)
    x, y = save_patch.cell_of(*start)
    return (y, x)


def saved_at_start(stamp: bytes, blob: bytes) -> bool:
    """Whether the loaded save's continue point (its last save point) is the seed's start, the
    landing pod."""
    start = _start(blob)
    if len(stamp) < STAMP_ROW_AT + 4 or start is None:
        return False
    row, column = struct.unpack_from("<2H", stamp, STAMP_ROW_AT)
    return (column, row) == start


def popups_handed(queued: int | None, received: int) -> bool:
    """Whether every received item's popup has gone to the helper (``Session.announced``).
    The helper shows its queue in order, so POD_NOTE queued after that comes after the disks'
    own popups (a received item is handed over a poll after it arrives, once its grant is
    confirmed)."""
    return queued is not None and queued >= received


@dataclass
class ReturnHome:
    """Per load of this seed's save: the POD_NOTE reminder once the player has enough disks,
    and RETURN HOME WITH THE DISKS? after each save at the pod. A save shows as a new stamp in
    the same slot; it counts only with the pod as continue point and the player in the start
    cell (the escape saves too, keeping the old continue point), and the offer lapses when the
    player leaves that cell."""

    reminded: bool = False
    stamp: bytes | None = None
    offer: bool = False  # a save at the pod, the prompt not yet queued

    def see(self, stamp: bytes, at_start: bool, enough: bool, in_start_cell: bool) -> None:
        same_slot = self.stamp is not None and stamp[:4] == self.stamp[:4]
        saved = same_slot and stamp != self.stamp
        self.stamp = stamp
        if saved:
            self.offer = enough and at_start and in_start_cell
        if not in_start_cell:
            self.offer = False
