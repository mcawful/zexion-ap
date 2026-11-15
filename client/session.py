"""One poll of the running game: which locations were checked, whether the goal was reached,
the grants that bring the inventory up to what the player should own, and other players' items
the server has as checked marked collected in the save.

No Archipelago imports, so the tests drive it with a fake game. The client calls ``step`` a few
times a second from a worker thread and does the networking.

Safety, in order:

- Nothing happens unless two reads of the game, ``game.SETTLE_SECONDS`` apart, agree on
  everything the poll reads (``Snapshot``). That covers a load in progress and the moment a
  pickup is being recorded.
- The loaded save must be a randomizer save (its marker reads 1: loading a normal save leaves
  the previous seed's section in memory) and hold this seed (the section's list size,
  item table and start, see ``save_patch.section_matches``); otherwise no check is sent and
  nothing is given.
- Checks and the goal (the ending's flag) are read in any state with this seed loaded, which
  covers the ending's cutscenes and credits. Grants are made only in
  play (``game.IN_PLAY``), as compare-and-set writes, and only to make up a difference
  (client/reconcile.py), so a grant undone by a death or reload is made again and none is
  made twice.
- A grant waits for two polls in a row to owe exactly the same, so a pickup the game is half
  way through recording (the map cell set, the inventory byte not yet) is never taken for an
  item owed.
- Other players' items the server has as checked are kept collected in the save (``mark``):
  only in play, only those two polls in a row found still to mark, as compare-and-set writes,
  and never in a room that waits for its item's pickup (``plain``, ids.PICKUP_ROOMS).
  An item object in the loaded rooms is ended by the game once its state is written, and the
  map cell is never written under one still there; a cell whose item may be there as another
  kind of object is left. A read or write of it that fails is said once and never holds back
  the checks, the goal or the grants.

With the helper attached (``step``'s ``helper``) the grants go as one batch the helper applies
between frames, re-checking each byte, with the game's popup for each item not yet
``announced`` (nothing goes through the helper until this connection has read that count). A
helper that fails gives nothing this poll: the step keeps its checks and goal and names the
error (``Step.helper_error``), and the client attaches the helper again before the next step.
"""

from __future__ import annotations

import logging
import time
from collections.abc import Iterable, Sequence
from dataclasses import astuple, dataclass, field

from . import discs, game, ids, native, reconcile, save_patch

logger = logging.getLogger("Zexion")

# The goal: flag 250 with flag 211 clear, the ending begun. The game's own escape skip sets 250
# with 211 still set, so the goal waits for the ending at the ship (docs/re/memory.md, "The
# ending: flag 250" and "The escape skip").
ENDING_FLAG, FINALE_FLAG = 250, 211
# Set once the scanner has shown where the keys are, or said how many OS Install Disks are
# needed (docs/re/memory.md, "The scanner"); also set from the start of a game whose map is
# revealed in full.
SCANNED_FLAG = 39


@dataclass(frozen=True)
class Snapshot:
    state: str
    grid: bytes = b""
    inventory: bytes = b""
    supplies: tuple[int, ...] = ()
    blob: bytes = b""
    marker: int = 0
    ended: int = 0
    finale: int = 0
    scanned: int = 0
    stamp: bytes = b""  # game.Game.save_stamp, b"" if it could not be read
    cell: tuple[int, int] = (-1, -1)  # game.PlayerState.cell
    game_saved: int | None = None  # game.Game.game_saved, None if it could not be read


# The flags a poll reads, in one read from the first.
FLAGS_READ = (SCANNED_FLAG, ENDING_FLAG - SCANNED_FLAG + 1)


def read_once(g: game.Game) -> Snapshot:
    """One read of what a poll uses: the player state and the flags in one read each."""
    player = g.player_state()
    state = g.state(player.energy)
    if state == game.NO_SAVE:
        return Snapshot(state)
    first, count = FLAGS_READ
    flags = g.flag_bytes(first, count)
    stamp = save_stamp(g)
    return Snapshot(
        state,
        g.map_grid(),
        player.inventory,
        player.supplies,
        g.rando_blob(),
        g.rando_marker(),
        flags[ENDING_FLAG - first],
        flags[FINALE_FLAG - first],
        flags[SCANNED_FLAG - first],
        stamp,
        player.cell,
        game_saved(g),  # after the stamp (game.Game.game_saved)
    )


def save_stamp(g: game.Game) -> bytes:
    """The loaded slot's save stamp, or b"" if the slot number is out of range."""
    try:
        return g.save_stamp()
    except game.GameNotRunning:
        raise
    except game.GameError:
        return b""


def game_saved(g: game.Game) -> int | None:
    """game.Game.game_saved, or None if the message could not be read (a pointer the game
    left behind)."""
    try:
        return g.game_saved()
    except game.GameNotRunning:
        raise
    except game.GameError:
        return None


# Two polls agree on what to mark only this close together, and in the same save slot: a
# pause in the polls (the helper attached again, a read that failed) starts over.
MARK_AGREE_SECONDS = 5.0
# The cells this far from the player's, each way, are marked only where an item object of the
# cell is in the loaded rooms, on top of the cells the game says it has loaded
# (game.Game.loaded_cells): the game keeps the cells next to the player's loaded
# (docs/re/memory.md, "What decides whether an item appears"), and one more is the margin
# chosen for the player moving on between the look and the write.
NEAR_CELLS = 2


def near(cell: tuple[int, int], player: tuple[int, int]) -> bool:
    """Whether a map cell (x, y) is among those around the player's cell, which the player
    state gives as the game's (X, Y): this repo's (y, x) (game.PlayerState.cell)."""
    return abs(cell[0] - player[1]) <= NEAR_CELLS and abs(cell[1] - player[0]) <= NEAR_CELLS


def plain(cells: Sequence[tuple[int, int]], gone: Iterable[int]) -> set[int]:
    """Those of ``gone`` (location indices) that may be marked: every one but those in a room
    that waits for its item's pickup (ids.PICKUP_ROOMS), whose item stays to be picked up."""
    return {i for i in gone if 0 <= i < len(cells) and cells[i] not in ids.PICKUP_ROOMS}


def to_mark(
    grid: bytes, states: bytes, cells: Sequence[tuple[int, int]], gone: Iterable[int]
) -> set[int]:
    """Which of ``gone`` (location indices) do not read as a pickup leaves them: the map cell
    not collected, or the item state still there (a mark a load cut in two: the game itself
    never leaves a collected map cell over an item that is there)."""
    out = set()
    for i in gone:
        if not 0 <= i < len(cells):
            continue
        x, y = cells[i]
        at = y * ids.MAP_SIZE + x
        if grid[at] != ids.COLLECTED or states[at] == game.ITEM_THERE:
            out.add(i)
    return out


def same_load(g: game.Game, stamp: bytes) -> bool:
    """Whether the game is in play in the randomizer save a look found, by its slot's stamp
    (new after each save, death and reload): read again right before the marking's writes."""
    return (
        len(stamp) >= 4
        and g.state() == game.IN_PLAY
        and g.rando_marker() == 1
        and g.save_stamp() == stamp
    )


def mark_collected(
    g: game.Game,
    grid: bytes,
    cells: Sequence[tuple[int, int]],
    indices: Iterable[int],
    stamp: bytes,
    gap: float = 0.0,
    ended: set[int] | None = None,
) -> set[int]:
    """Mark these locations (``to_mark``'s) collected, as Session.mark describes; the ones
    marked. Right before the first write the save is checked to be the look's, in play
    (``same_load``), and the item states, the loaded cells, the item objects and the player's
    cell are read again. First each cell's item state, which stops the game making the item
    from then on and, for an item object of the cell in the loaded rooms, has the game end it
    in its next update (docs/re/memory.md, "What decides whether an item appears": the item's
    update ends its own object once the state reads collected); then, after ``gap`` for those
    updates and the save checked again, the map cell of each with no item object left. A cell
    the game has loaded, or near the player's, with no item object of its own is left out: an
    object of another kind may be its item, and writing the state would not end that. Such a
    cell is marked once it had an item object here (``ended`` keeps those across polls of one
    load, so the map cell follows even if the object goes between two polls). One whose map
    cell could not be written this time (the game changed it since the look) is left for a
    later poll. Nothing counts as marked if the game is not in play after the last write (a
    load began: the next polls look again). GameNotRunning is raised; another GameError leaves
    that location for a later poll."""
    if not same_load(g, stamp):
        return set()
    ended = set() if ended is None else ended
    states, loaded = g.cell_items(), g.loaded_cells()
    live = g.live_item_cells()
    player = g.player_state().cell
    todo, waits = [], False
    for i in indices:
        x, y = cells[i]
        state = states[y * ids.MAP_SIZE + x]
        if state not in (game.ITEM_THERE, game.ITEM_COLLECTED):
            continue  # an object of the cell holds its item, or has dropped it
        if (x, y) in live:
            ended.add(i)  # the game ends that object once the state reads collected
            waits = True
        elif i not in ended and (loaded[y * ids.MAP_SIZE + x] or near((x, y), player)):
            continue  # its item may be in the loaded rooms as an object of another kind
        try:
            if state == game.ITEM_THERE:
                g.set_item_collected((x, y))
        except game.GameNotRunning:
            raise
        except game.GameError as exc:
            logger.info(f"Not marked collected this time: {exc}")
            continue
        todo.append(i)
    if not todo:
        return set()
    if waits:
        time.sleep(gap)  # the game's next updates end the item objects of the cells written
        if not same_load(g, stamp):
            return set()
    live, loaded = g.live_item_cells(), g.loaded_cells()
    marked = set()
    for i in todo:
        x, y = cells[i]
        if (x, y) in live or (loaded[y * ids.MAP_SIZE + x] and i not in ended):
            continue  # an item object still there, or a room loaded since the look
        try:
            if grid[y * ids.MAP_SIZE + x] != ids.COLLECTED:
                g.set_map_collected((x, y), grid[y * ids.MAP_SIZE + x])
        except game.GameNotRunning:
            raise
        except game.GameError as exc:
            logger.info(f"Not marked collected this time: {exc}")
            continue
        marked.add(i)
    if marked and g.state() != game.IN_PLAY:
        logger.info("A load began during the marking; the next polls look again.")
        return set()
    return marked


def read_settled(g: game.Game, gap: float = game.SETTLE_SECONDS) -> Snapshot | None:
    """A snapshot two reads agree on, or None while something is changing."""
    first = read_once(g)
    time.sleep(gap)
    second = read_once(g)
    return first if first == second else None


@dataclass
class Step:
    checked: set[int] = field(default_factory=set)  # location indices collected in game
    goal: bool = False
    granted: dict[int, int] = field(default_factory=dict)  # item -> new byte value
    seed_matches: bool = False
    # No save of this seed is loaded: the title or file-select screen, or another save. What
    # the game held in play is gone unless its save had it.
    left_seed: bool = False
    state: str = ""  # the settled game.state() of this seed's save; "" when not read
    scanned: bool = False  # this seed's save has had the scanner's message (flag 39)
    ended: bool = False  # this seed's save has reached the game's ending (flag 250)
    # A randomizer save of another seed is loaded (left_seed too), in play (state).
    other_seed: bool = False
    # This seed's save: its slot's save stamp (new after each save, and after a death or a
    # reload: discs.ReturnHome), and whether its last save point is home, the Landing Pod's
    # (discs.saved_at_home), whichever room the seed starts in.
    save_stamp: bytes = b""
    at_home: bool = False
    in_home_cell: bool = False  # the player is in the Landing Pod's cell now
    game_saved: int | None = None  # read after save_stamp (game.Game.game_saved)
    # The helper failed with this poll's grants: nothing given (zexion_client's helper_error).
    helper_error: native.HelperError | None = None
    # Other players' items marked collected by this poll (Session.mark), by location index.
    marked: set[int] = field(default_factory=set)


class Session:
    """The polls of one seed and slot's game, kept over reconnections to it (state.SeedState)."""

    def __init__(self, plan: save_patch.SeedPlan, intel_chips: bool = False):
        if plan.check_size():
            raise ValueError(f"the slot data does not list {plan.list_size} locations")
        self.plan = plan
        self.intel_chips = intel_chips  # its Area Maps are Intel Chips (their popups say so)
        self.listed = [plan.cells[i] for i in plan.order]  # as the game's list holds them
        self._said: set[str] = set()
        self._due: dict[int, int] | None = None  # what the previous poll found owed
        # What the previous poll found to mark (mark), when, and in which save slot.
        self._to_mark: set[int] = set()
        self._to_mark_at = 0.0
        self._to_mark_slot = b""
        # Locations seen with an item object in the loaded rooms while being marked, in the
        # load with this stamp (mark_collected's ``ended``).
        self._ended: set[int] = set()
        self._ended_stamp = b""
        # How many received items have been handed over, to the helper for their popup or
        # written directly, and whether this connection has read the server's count since
        # Connected (client/storage.py, announced_tick; the client clears it at each). Until it
        # has, nothing goes through the helper, and direct writes leave the count as it is.
        self.announced = 0
        self.announced_read = False
        # The traps among the items a slot held at its first connection (no count stored on the
        # server: state.held_traps), by index: counted as handed over with the rest, so no flood
        # of old popups, but each still gets its popup, and so its effect, once, through the
        # helper (give_through_helper); dropped with direct writes, as received traps are.
        self.held_traps: list[int] = []
        # Whether this session has taken the server's record of the waiting held traps: only its
        # first read does (a client restarted keeps them); later reads keep the session's own,
        # which a lost emptying store would otherwise bring back (storage.announced_tick).
        self.held_read = False
        # The ending: the goal as the previous poll of this load read it (None: not read yet),
        # and whether received items wait because the ending began during this load (its
        # cutscenes read as play). Both cleared when no save of this seed is loaded, and kept
        # over a reconnect, as a first read would take the goal as reached already and not hold.
        self._goal_before: bool | None = None
        self.holding = False

    def look_away(self, g: game.Game, gap: float = game.SETTLE_SECONDS) -> bool:
        """While no step runs (the client disconnected, or dropped): whether its load has ended,
        no save loaded (the title, the file select, MISSION COMPLETE onward) in two reads, as a
        step would see. Otherwise a hold, or the goal as read before the ending, would land on a
        later load (an ended save loaded again) once a connection steps again. GameNotRunning
        (the game closed) is raised. It reads nothing, and says False, with nothing to keep. It
        changes nothing: it runs in a thread, and the caller ends the load
        (state.SeedState.end_load)."""
        if self._goal_before is None and not self.holding:
            return False
        first = g.state()
        time.sleep(gap)
        return first == game.NO_SAVE and g.state() == game.NO_SAVE

    def say_once(self, key: str, message: str) -> None:
        if key not in self._said:
            self._said.add(key)
            logger.warning(message)

    def step(
        self,
        g: game.Game,
        received: list[int],
        gap: float = game.SETTLE_SECONDS,
        senders: list[str] | None = None,
        followups: dict[int, str] | None = None,
        helper: native.Helper | None = None,
        give: bool = True,
        gone: Iterable[int] = (),
    ) -> Step:
        """One poll. ``senders`` names who sent each of ``received`` (for popups); ``followups``
        adds a notice straight after the popup of some of them (an Intel Chip's hint,
        client/hints.py; an OS Install Disk's count, client/discs.py). Grants go through
        ``helper`` when one is attached, else directly; a HelperError from it gives nothing and
        is kept in the step (``helper_error``). Without ``give`` (a helper lost earlier in the
        poll, to be attached again first) nothing is given, and the rest is read as usual.
        ``gone`` are the locations to keep collected in the save (``mark``): other players'
        items the server has as checked."""
        snap = read_settled(g, gap)
        if snap is None or snap.state != game.IN_PLAY:
            self._due = None
            self._to_mark = set()
        if snap is None:
            return Step()
        if snap.state == game.NO_SAVE:
            self.leave()
            return Step(left_seed=True)
        try:
            seed = game.parse_seed(snap.blob) if snap.marker == 1 else None
        except game.GameError:
            seed = None
        # Another seed's save is said once a load by ticks.other_save_tick (state.OTHER_SAVE),
        # only when the helper knows its seed (an AP save): nothing for a plain randomizer game
        # ("unknown"), and nothing with no helper, as without it the two can't be told apart.
        if seed is None or list(seed.locations) != self.listed:
            self._due = None
            self._to_mark = set()
            self.leave()
            if seed is None:
                return Step(left_seed=True)  # a normal save: nothing to say in the game
            return Step(left_seed=True, other_seed=True, state=snap.state)
        if not save_patch.section_matches(snap.blob, self.plan):
            self._due = None
            self._to_mark = set()
            self.leave()
            return Step(left_seed=True, other_seed=True, state=snap.state)
        collected = reconcile.collected_locations(snap.grid, self.plan.cells)
        goal = snap.ended >= 1 and snap.finale == 0
        step = Step(
            collected,
            goal,
            {},
            True,
            state=snap.state,
            scanned=snap.scanned != 0,
            ended=snap.ended != 0,
            save_stamp=snap.stamp,
            at_home=discs.saved_at_home(snap.stamp),
            in_home_cell=snap.cell == discs.HOME_CELL,
            game_saved=snap.game_saved,
        )
        # The ending beginning in this load: what is received from now (as a release or collect
        # on the goal) waits for the next load, not popping up over the ending's cutscenes. A
        # load first seen during its ending reads as an ended save loaded again, and is not held
        # (docs/re/memory.md, "The ending: flag 250": nothing measured tells them apart yet).
        if self._goal_before is False and goal:
            self.holding = True
            self._due = None
            self._to_mark = set()
        elif not goal:
            self.holding = False  # a load before its ending (one carried over a reconnect too)
        self._goal_before = goal
        if snap.state == game.IN_PLAY and not self.holding:
            try:
                step.marked = self.mark(g, snap, gone, gap)
            except game.GameNotRunning:
                raise
            except game.GameError as exc:
                # The marking must never stop the checks, the goal or the grants.
                self._to_mark = set()
                self.say_once(
                    "mark",
                    "Can't mark other players' items collected right now (details in the "
                    f"log); items and checks still work. ({exc})",
                )
        if snap.state == game.IN_PLAY and not self.holding and give:
            try:
                step.granted = self.give(
                    g, snap, collected, received, gap, senders, followups, helper
                )
            except native.HelperError as exc:
                step.helper_error = exc
        return step

    def connected(self) -> None:
        """A new connection to this seed (zexion_client's connected): the popup count is read
        from the server again (announced_tick), and nothing is given before a step of this
        connection has looked at the save, so the poll knows which found disks' counts are still
        to show (discs.received_notes) before a received disk's count is worked out."""
        self.announced_read = False
        self._due = None
        self._to_mark = set()

    def hold_ending(self) -> None:
        """The ending began again in this load, on a save past it (RETURN HOME's YES: the
        helper says so, ticks.replay_tick): held as an ending beginning is, which flag 250, set
        already, does not show. A step keeps it, as the goal stays read, until the load ends."""
        self.holding = True
        self._due = None
        self._to_mark = set()

    def leave(self) -> None:
        """No save of this seed loaded (or the game closed): the next load starts afresh."""
        self._goal_before = None
        self.holding = False
        self._to_mark = set()

    def mark(self, g: game.Game, snap: Snapshot, gone: Iterable[int], gap: float = 0.0) -> set[int]:
        """Keep ``gone`` collected in the loaded save (other players' items the server has as
        checked: found in another save of the seed or before a death or reload, collected by
        their owner, or released), so they are not there to pick up again: each one's map cell
        reads collected, as a pickup leaves it, and the game makes no item there when it loads
        the room (docs/re/memory.md, "What decides whether an item appears"). The locations
        marked.

        Only one the poll before found still to mark too and did not mark, in play, in the same
        save slot and no more than MARK_AGREE_SECONDS ago, so a load caught half way marks
        nothing. An item object of the cell in the loaded rooms is ended by the game itself
        once the state is written (a reload at a save point beside it, its owner collecting
        while the player stands by it), and the map cell follows. One whose item may be in the
        loaded rooms as something else is left as it is, to pick up as usual or to mark once
        its room is left: one an object of the cell holds or has dropped, and, with no item
        object of its own, one whose cell the game has loaded (game.Game.loaded_cells) or
        within NEAR_CELLS of the player's cell (an object of another kind may be the item, in
        a reversed-gravity room, say). All of it is read again right before the writes, with
        the save and the state (mark_collected). The map cell is never written under an item
        object still there: the save would count a pickup of it twice. The Scrap
        Metal a stand-in's pickup gives comes with the next polls' grants (reconcile.should_own
        counts a marked cell as collected), so a save reads the same marked or picked up, but
        for the game's pickup counter (ITEMS on the pause screen), which marking leaves
        alone."""
        gone = set(gone)
        if not gone:
            self._to_mark = set()
            return set()
        cells = self.plan.cells
        gone = plain(cells, gone)
        if not gone:
            self._to_mark = set()
            return set()
        states = g.cell_items()
        want = to_mark(snap.grid, states, cells, gone)
        now, slot = time.monotonic(), snap.stamp[:4]
        if len(slot) < 4:
            self._to_mark = set()  # the slot's stamp did not read: no save to tell apart
            return set()
        agreed = slot == self._to_mark_slot and now - self._to_mark_at <= MARK_AGREE_SECONDS
        ready = want & self._to_mark if agreed else set()
        self._to_mark, self._to_mark_at, self._to_mark_slot = want, now, slot
        if not ready:
            return set()
        if snap.stamp != self._ended_stamp:
            self._ended, self._ended_stamp = set(), snap.stamp  # a save, a death or a reload
        marked = mark_collected(g, snap.grid, cells, sorted(ready), snap.stamp, gap, self._ended)
        self._to_mark -= marked  # one a reload brings back waits for two polls again
        return marked

    def give(
        self,
        g: game.Game,
        snap: Snapshot,
        collected: set[int],
        received: list[int],
        gap: float,
        senders: list[str] | None = None,
        followups: dict[int, str] | None = None,
        helper: native.Helper | None = None,
    ) -> dict[int, int]:
        plan = self.plan
        guns_first = plan.guns_first
        target = reconcile.should_own(
            plan.start_items, plan.location_items, collected, received, guns_first
        )
        result = reconcile.grants(snap.inventory, target, guns_first)
        for item, missing in result.not_grantable.items():
            self.say_once(
                f"stuck {item} {missing}",
                f"Item {item} is owed {missing} more time(s) but cannot be given by the client.",
            )
        due, self._due = self._due, result.inventory
        if result.inventory != due:
            return {}  # owed for the first time: wait for the next poll to agree
        if helper is not None:
            if not self.announced_read:
                return {}  # which items have had their popup is not known yet
            try:
                return self.give_through_helper(
                    helper, snap, result.inventory, received, senders, followups
                )
            except native.ReplyLost:
                # The batch may have been applied and its popups shown: none shown again.
                self.announced = len(received)
                self.held_traps = []
                raise
        if self.announced_read:
            # Direct writes show no popups, and a trap rides on its popup: those waiting do
            # nothing (said, so a lost trap is not a mystery).
            waiting = [received[i] for i in self.held_traps if i < len(received)]
            waiting += [c for c in received[self.announced :] if c in ids.TRAPS]
            if waiting:
                names = ", ".join(ids.TRAP_NAMES[c] for c in waiting)
                logger.info(f"Without the mod, these traps sent to you do nothing: {names}.")
            self.announced = len(received)
            self.held_traps = []
        granted: dict[int, int] = {}
        try:
            for item, value in result.inventory.items():
                g.set_item(item, snap.inventory[item], value)
                granted[item] = value
            # Bring max (and current) energy, missiles and bombs up to what the counts give,
            # as the pickup routine does. Only ever raises them.
            inventory = g.inventory()
            before = reconcile.Supplies(*g.supplies())
            after = reconcile.supplies_after(inventory, before)
            if after != before:
                g.set_supplies(astuple(before), astuple(after))
        except game.GameNotRunning:
            raise
        except game.GameError as exc:
            # A changed byte, a read-back the game overwrote: the next poll looks again.
            logger.info(f"Not given this time: {exc}")
        if granted:
            time.sleep(gap)
            if g.state() != game.IN_PLAY or any(
                g.inventory()[item] != value for item, value in granted.items()
            ):
                logger.info("A load undid a grant; it will be given again.")
        return granted

    def give_through_helper(
        self,
        helper: native.Helper,
        snap: Snapshot,
        owed: dict[int, int],
        received: list[int],
        senders: list[str] | None,
        followups: dict[int, str] | None = None,
    ) -> dict[int, int]:
        # The traps held at the slot's first connection first: they came before the rest.
        popups: list[tuple[int, str] | tuple[int, str, bool]] = [
            (
                received[i],
                f"from {senders[i]}" if senders and i < len(senders) and senders[i] else "",
            )
            for i in self.held_traps
            if i < len(received)
        ]
        start = self.announced
        # With Guns First, a progressive weapon received while its weapon is owned arrives as
        # the upgrade, so its popup names the upgrade; an upgrade received as itself (sent from
        # the server console) is a copy of its weapon too (reconcile.should_own). Without it,
        # each item is itself.
        guns_first = self.plan.guns_first
        weapons = {w for w in ids.PROGRESSIVE if snap.inventory[w]}
        for index in range(start, len(received)):
            item = received[index]
            weapon = reconcile.UPGRADE_OF.get(item, item)
            if guns_first and weapon in ids.PROGRESSIVE:
                item = ids.PROGRESSIVE[weapon] if weapon in weapons else weapon
                # The weapon is owned from now on, whichever byte this one set.
                weapons.add(weapon)
            sender = senders[index] if senders and index < len(senders) else ""
            popups.append((item, f"from {sender}" if sender else ""))
            followup = (followups or {}).get(index)
            if followup:
                popups.append((item, followup, True))  # a notice after it: a hint, a count
        if not owed and not popups:
            before = reconcile.Supplies(*snap.supplies)
            if reconcile.supplies_after(snap.inventory, before) != before:
                helper.apply(native.batch_text([], [], refresh=True))
            return {}
        cas = [(item, snap.inventory[item], value) for item, value in sorted(owed.items())]
        reply = helper.apply(native.batch_text(cas, popups, intel=self.intel_chips))
        if reply != "applied":
            logger.info(f"Not given this time: {reply}")
            return {}
        self.announced = len(received)
        self.held_traps = []
        return dict(owed)
