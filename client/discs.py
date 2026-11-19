"""The OS Install Disks goal: the Goal option's "os_install_disks" (worlds/zexion/options.py).

The world shuffles OS Install Disks, the game's Data Disc item, into the multiworld, and the
player wins on having enough of them. The game's own Data Disc changes its endings
(docs/re/memory.md, "The Data Disc"), so a disk is never put in the game's inventory: the client
counts them,

    disks owned = the disks the slot starts with
                + the disks received from other worlds
                + this world's own disk locations collected in the loaded save

Shown in a notice after each disk's popup ("OS Install Disk 3 of 15", "of 15" once the scanner
has said how many; in capitals but in English(HD)).
This world's own disks follow the save, as its other items do: after a death or a reload
without saving, one picked up since lies in its room again and no longer counts, and picking it
up again counts it again, with its notice. So the count is a save's own: neither the server's
checked set, which a release fills, nor what another save of the seed has found; the disc the
game gives for its secret boss does not count.
Pure functions; client/ticks.py talks to the helper.
"""

from __future__ import annotations

import struct
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass

from . import save_patch
from .ids import DATA_DISC

NAME = "OS Install Disk"  # what the game's record calls DATA DISC is called here


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


def owned(hunt: DiscHunt, received: Sequence[int], collected: Iterable[int]) -> int:
    """How many discs the player has in a save: ``received`` are the item codes received from
    other worlds, ``collected`` the save's collected location indices (the loaded save's:
    state.LoadState.save_collected; none for the count every save of the seed shares)."""
    return hunt.start + received.count(DATA_DISC) + len(hunt.locations & set(collected))


def counted(
    before: set[int] | None, collected: Iterable[int], locations: Iterable[int]
) -> set[int]:
    """This world's own disks in the loaded save whose count notice is not owed (state.LoadState.
    disks_counted), after a look at the save: at the first (``before`` None), every one the save
    holds, picked up before this load was watched; later, those of ``before`` the save still
    holds, so one a death or a reload put back in its room is owed its notice again once picked
    up again."""
    own = set(collected) & set(locations)
    return own if before is None else before & own


def uncounted(
    before: set[int] | None, collected: Iterable[int], locations: Iterable[int]
) -> set[int]:
    """This world's own disks picked up in this load whose count notice is still to show: none
    before the load's first look (``counted``)."""
    if before is None:
        return set()
    return (set(collected) & set(locations)) - before


def note(count: int, required: int | None = None) -> str:
    """A disk's count notice: the count, and how many are needed once the player has been told
    (the scanner's message; ``required`` None before)."""
    if required is None:
        return f"{NAME} {count}"
    return f"{NAME} {count} of {required}"


def received_notes(
    hunt: DiscHunt,
    received: Sequence[int],
    collected: Iterable[int],
    told: bool = True,
    pending: Iterable[int] = (),
) -> dict[int, str]:
    """The count shown after each received disk's popup, by its position in ``received``: the
    count with that disk, so a batch of them counts up; with how many are needed once ``told``.
    ``collected`` as for ``owned``. Disks picked up whose own count is still to be shown
    (``pending``: ``uncounted``) are left out: they count in that later notice instead
    (ticks.followups_tick), so each count shows once, in the order the helper shows them."""
    count = hunt.start + len(hunt.locations & (set(collected) - set(pending)))
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


def scan_line(required: int) -> str:
    """What the scanner says in place of ENERGY KEYS DETECTED! (docs/re/memory.md, "The
    scanner"): one line of the game's text, at most 31 characters."""
    return f"Collect {required} {NAME}{'' if required == 1 else 's'}!"


# Told once the player has enough disks: RETURN HOME WITH THE DISKS? is offered only after a
# save at the Landing Pod's save point, whichever room the seed starts in.
POD_NOTE = "Enough OS Install Disks found. Save at the landing pod to return home."

# Home: the Landing Pod's save point (start room landing_pod, Kresezoa X16 Y13), tile column
# then row, as a save's continue point and the world's data.START_ROOMS hold it
# (docs/re/save-format.md, "The continue point and the seed's start";
# worlds/zexion/test/test_client_agrees.py checks the two agree).
HOME = (191, 329)
# Its cell as the player state counts cells (game.PLAYER_CELL): the game's (X, Y), which is
# this repo's map cell (save_patch.cell_of) as (y, x) (docs/re/randocells.md, "Grid order").
HOME_CELL = save_patch.cell_of(*HOME)[::-1]
# The helper's request for RETURN HOME WITH THE DISKS?, given home: a request not yet opened
# lapses while the player is in play outside its cell (native/README.md, "The OS Install Disks
# goal").
MISSION_REQUEST = f"mission_prompt {HOME[0]} {HOME[1]}"
# The helper's state of that prompt, and its reply once the helper has started the ending after
# YES, until another save is loaded (native/src/mission.h, kEndingStarted).
MISSION_STATUS = "mission_status"
ENDING_STARTED = "ending started"

# A save stamp (game.Game.save_stamp): the slot, then the slot block's bytes from +0x9354, of
# which the continue point's row is at 6 and its column at 8, then the block's death count.
STAMP_ROW_AT = 4 + 6
STAMP_DEATHS_AT = 4 + 10


def saved_at_home(stamp: bytes) -> bool:
    """Whether the loaded save's continue point (its last save point) is home, the Landing
    Pod's save point."""
    if len(stamp) < STAMP_ROW_AT + 4:
        return False
    row, column = struct.unpack_from("<2H", stamp, STAMP_ROW_AT)
    return (column, row) == HOME


def deaths(stamp: bytes) -> int | None:
    """The loaded save's death count as its block holds it (None in a stamp without one): a
    death raises it as it rewrites the block's play time, so the stamp changes with no save
    (docs/re/memory.md, "A save at the Landing Pod")."""
    if len(stamp) < STAMP_DEATHS_AT + 4:
        return None
    return struct.unpack_from("<I", stamp, STAMP_DEATHS_AT)[0]


def popups_handed(queued: int, read: bool, received: int) -> bool:
    """Whether every received item's popup has gone to the helper (``Session.announced``, and
    ``read``: that count read on this connection, ``Session.announced_read``). The helper shows
    its queue in order, so POD_NOTE queued after that comes after the disks' own popups and
    counts (a received item is handed over once any grant it makes is confirmed)."""
    return read and queued >= received


@dataclass
class ReturnHome:
    """Per load of this seed's save: the POD_NOTE reminder once the player has enough disks,
    and RETURN HOME WITH THE DISKS? after each save at the Landing Pod. A save shows as a new
    stamp in the same slot; it counts only with the pod as continue point and the player in the
    pod's cell (the escape saves too, keeping the old continue point), and the offer lapses
    when the player leaves that cell in play, or dies.

    The game also rewrites the stamp without a save: a death (raising the death count),
    RELOAD FROM SAVE and RELOAD FROM CHECKPOINT (raising the count) behind the pause menu,
    leaving to the title, some story events and the ending (docs/re/memory.md, "A save at the
    Landing Pod"). So a new stamp is a save, with the death count unchanged, when it came with
    a GAME SAVED message not seen with an earlier stamp, which only a save shows, or, without
    one (unread, or another message straight after it), between two looks in play."""

    reminded: bool = False
    stamp: bytes | None = None
    in_play: bool = False  # the last look was in play
    shown: int | None = None  # the GAME SAVED message last seen with a new stamp
    offer: bool = False  # a save at the pod, the prompt not yet queued
    # Queued in this load and still on its way (not shown, answered, lapsed or ended): a Linux
    # helper connected to again drops it, and it is offered again (again: ticks.linux_finds_tick).
    queued: bool = False

    def see(
        self,
        stamp: bytes,
        in_play: bool,
        at_home: bool,
        enough: bool,
        in_home_cell: bool,
        game_saved: int | None = None,
    ) -> None:
        """One look at this seed's save, in play or not (dead, a menu). ``game_saved`` is
        game.Game.game_saved, read after the stamp."""
        before, played, shown = self.stamp, self.in_play, self.shown
        self.stamp, self.in_play = stamp, in_play
        if before is None or stamp[:4] != before[:4]:
            self.offer = self.queued = False  # a first look, or another slot: no save
            self.shown = game_saved  # a message from before is no save of this one
            return
        died = (deaths(before) or 0) < (deaths(stamp) or 0)
        if died:
            self.offer = self.queued = False
        new = stamp != before
        if new and game_saved is not None:
            self.shown = game_saved
        message = game_saved is not None and game_saved != shown
        saved = new and not died and (message or (in_play and played))
        if saved:
            self.offer = enough and at_home and in_home_cell
        if in_play and not in_home_cell:
            self.offer = self.queued = False

    def again(self) -> None:
        """The prompt queued in this load was dropped before it showed (the Linux helper's
        connection closed, its status ``not asked``): offered again, as after a save at the
        pod."""
        if self.queued:
            self.offer, self.queued = True, False
