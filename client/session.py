"""One poll of the running game: which locations were checked, whether the goal was reached,
and the grants that bring the inventory up to what the player should own.

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

With the helper attached (``step``'s ``helper``) the grants go as one batch the helper applies
between frames, re-checking each byte, with the game's popup for each item not yet
``announced`` (nothing goes through the helper until this connection has read that count). A
helper that fails gives nothing this poll: the step keeps its checks and goal and names the
error (``Step.helper_error``), and the client attaches the helper again before the next step.
"""

from __future__ import annotations

import logging
import time
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
    ) -> Step:
        """One poll. ``senders`` names who sent each of ``received`` (for popups); ``followups``
        adds a notice straight after the popup of some of them (an Intel Chip's hint,
        client/hints.py; an OS Install Disk's count, client/discs.py). Grants go through
        ``helper`` when one is attached, else directly; a HelperError from it gives nothing and
        is kept in the step (``helper_error``). Without ``give`` (a helper lost earlier in the
        poll, to be attached again first) nothing is given, and the rest is read as usual."""
        snap = read_settled(g, gap)
        if snap is None or snap.state != game.IN_PLAY:
            self._due = None
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
            self.leave()
            if seed is None:
                return Step(left_seed=True)  # a normal save: nothing to say in the game
            return Step(left_seed=True, other_seed=True, state=snap.state)
        if not save_patch.section_matches(snap.blob, self.plan):
            self._due = None
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
        elif not goal:
            self.holding = False  # a load before its ending (one carried over a reconnect too)
        self._goal_before = goal
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

    def hold_ending(self) -> None:
        """The ending began again in this load, on a save past it (RETURN HOME's YES: the
        helper says so, ticks.replay_tick): held as an ending beginning is, which flag 250, set
        already, does not show. A step keeps it, as the goal stays read, until the load ends."""
        self.holding = True
        self._due = None

    def leave(self) -> None:
        """No save of this seed loaded (or the game closed): the next load starts afresh."""
        self._goal_before = None
        self.holding = False

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
            self.announced = len(received)  # direct writes show no popups
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
