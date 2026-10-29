"""One poll of the running game: which locations were checked, whether the goal was reached,
and the grants that bring the inventory up to what the player should own.

No Archipelago imports, so the tests drive it with a fake game. The client calls ``step`` a few
times a second from a worker thread and does the networking.

Safety, in order:

- Nothing happens unless two reads of the game, ``SETTLE_SECONDS`` apart, agree on everything
  the poll reads (``Snapshot``). That covers a load in progress and the moment a pickup is
  being recorded.
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

With the helper attached (``Session.helper``) the grants go as one batch the helper applies
between frames, re-checking each byte, with the game's popup for each item not yet
``announced`` (nothing goes through the helper until that count is known). A helper that
fails is dropped and grants go back to direct writes.
"""

from __future__ import annotations

import logging
import time
from dataclasses import astuple, dataclass, field

from . import discs, game, ids, native, reconcile, save_patch

logger = logging.getLogger("Zexion")

SETTLE_SECONDS = game.SETTLE_SECONDS
# The goal: flag 250 with flag 211 clear, the ending begun (docs/re/memory.md, "The ending: flag
# 250"). The ending sets 250 and clears 185-212, at the ship after the final battle or, under
# Abridged Finale, soon after the grab. The game's own SKIP ESCAPE AND FINAL BATTLE?, offered
# after the grab in a game that has reached the ending before, sets 250 on YES with 211 still
# set ("The escape skip"): the goal waits for the ending that follows at the ship.
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
        save_stamp(g),
        player.cell,
    )


def save_stamp(g: game.Game) -> bytes:
    """The loaded slot's save stamp, or b"" if the slot number is out of range."""
    try:
        return g.save_stamp()
    except game.GameNotRunning:
        raise
    except game.GameError:
        return b""


def read_settled(g: game.Game, gap: float = SETTLE_SECONDS) -> Snapshot | None:
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
    # This seed's save: its slot's save stamp (new after each save), and whether its last save
    # point is the seed's start, the landing pod (discs.saved_at_start).
    save_stamp: bytes = b""
    at_start: bool = False
    in_start_cell: bool = False  # the player is in the seed's start cell now


class Session:
    def __init__(self, plan: save_patch.SeedPlan, cells: list):
        if len(cells) != plan.list_size or plan.check_size():
            raise ValueError(f"the slot data does not list {plan.list_size} locations")
        self.plan = plan
        self.cells = [tuple(c) for c in cells]  # by location index
        # As the game's list holds them (save_patch.game_order).
        self.listed = [self.cells[i] for i in save_patch.game_order(self.cells)]
        self._said: set[str] = set()
        self._due: dict[int, int] | None = None  # what the previous poll found owed
        self.helper: native.Helper | None = None
        # How many received items have been handed over, to the helper for their popup or
        # written directly; None until the server's count is read (client/storage.py,
        # announced_tick).
        self.announced: int | None = None
        # The ending: the goal as the previous poll of this load read it (None: not read yet),
        # and whether received items wait because the ending began during this load (its
        # cutscenes read as play). Both cleared when no save of this seed is loaded.
        self._goal_before: bool | None = None
        self.holding = False
        # The session keep_ending carried those from, until this one's first step
        # (state.end_kept_load).
        self.carried_from: Session | None = None

    def keep_ending(self, before: Session) -> None:
        """A reconnect to the same seed and slot: the ending's hold carries over, as a new
        session would read the goal as reached already and never hold. Only a hold of the load
        still loaded: ``before`` looked for its save leaving while no connection stepped it
        (look_away)."""
        self._goal_before, self.holding = before._goal_before, before.holding
        self.carried_from = before

    def look_away(self, g: game.Game, gap: float = SETTLE_SECONDS) -> bool:
        """While no step runs (the client disconnected, this session kept for the next
        connection's keep_ending): whether its load has ended, no save loaded (the title, the
        file select, MISSION COMPLETE onward) in two reads, as a step would see. Otherwise a
        carried hold, or the goal as read before the ending, would land on a later load (an
        ended save loaded again). GameNotRunning (the game closed) is raised. It reads nothing,
        and says False, with nothing to carry. It changes nothing: it runs in a thread while a
        reconnect may carry this session's ending over, so the caller ends the load
        (state.end_kept_load)."""
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
        gap: float = SETTLE_SECONDS,
        senders: list[str] | None = None,
        notes: dict[int, str] | None = None,
    ) -> Step:
        """One poll. ``senders`` names who sent each of ``received`` (for popups); ``notes``
        replaces the popup of some of them with a notice, its text alone (an Area Map's hint,
        client/hints.py)."""
        self.carried_from = None  # stepped: its own reads decide from here
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
        collected = reconcile.collected_locations(snap.grid, self.cells)
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
            at_start=discs.saved_at_start(snap.stamp, snap.blob),
            in_start_cell=snap.cell == discs.start_cell(snap.blob),
        )
        # The ending beginning in this load: what is received from now (as a release or collect
        # on the goal) waits for the next load, not popping up over the ending's cutscenes.
        if self._goal_before is False and goal:
            self.holding = True
            self._due = None
        elif not goal:
            self.holding = False  # a load before its ending (one carried over a reconnect too)
        self._goal_before = goal
        if snap.state == game.IN_PLAY and not self.holding:
            step.granted = self.give(g, snap, collected, received, gap, senders, notes)
        return step

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
        notes: dict[int, str] | None = None,
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
        if self.helper is not None and self.announced is None:
            return {}  # which items have had their popup is not known yet
        if self.helper is not None:
            try:
                return self.give_through_helper(snap, result.inventory, received, senders, notes)
            except native.HelperError as exc:
                logger.warning(f"The in-game helper failed ({exc}); giving items directly.")
                self.helper.close()
                self.helper = None
        if self.announced is not None:
            self.announced = len(received)  # direct writes show no popups
        granted: dict[int, int] = {}
        try:
            for item, value in result.inventory.items():
                g.set_item(item, snap.inventory[item], value)
                granted[item] = value
            # Bring max (and current) energy, missiles and bombs up to what the counts give,
            # as the pickup routine does. Only ever raises them.
            inventory = g.inventory()
            before = reconcile.Supplies(*g.supplies())
            after = reconcile.supplies_after(inventory, {}, before)
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
        snap: Snapshot,
        owed: dict[int, int],
        received: list[int],
        senders: list[str] | None,
        notes: dict[int, str] | None = None,
    ) -> dict[int, int]:
        popups: list[tuple[int, str] | tuple[int, str, bool]] = []
        start = self.announced if self.announced is not None else len(received)
        # With Guns First, a progressive weapon received while its weapon is owned arrives as
        # the upgrade, so its popup names the upgrade; an upgrade received as itself (sent from
        # the server console) is a copy of its weapon too (reconcile.should_own). Without it,
        # each item is itself.
        guns_first = self.plan.guns_first
        weapons = {w for w in ids.PROGRESSIVE if snap.inventory[w]}
        for index in range(start, len(received)):
            item = received[index]
            if guns_first:
                item = reconcile.UPGRADE_OF.get(item, item)
            if item in ids.PROGRESSIVE and guns_first:
                weapon = item
                if weapon in weapons:
                    item = ids.PROGRESSIVE[weapon]
                # The weapon is owned from now on, whichever byte this one set.
                weapons.add(weapon)
            sender = senders[index] if senders and index < len(senders) else ""
            note = (notes or {}).get(index)
            if note:
                popups.append((item, note, True))  # a notice: the hint alone
            else:
                popups.append((item, f"FROM {sender}" if sender else ""))
        if not owed and not popups:
            before = reconcile.Supplies(*snap.supplies)
            if reconcile.supplies_after(snap.inventory, {}, before) != before:
                self.helper.apply(native.batch_text([], [], refresh=True))
            return {}
        cas = [(item, snap.inventory[item], value) for item, value in sorted(owed.items())]
        reply = self.helper.apply(native.batch_text(cas, popups))
        if reply != "applied":
            logger.info(f"Not given this time: {reply}")
            return {}
        if self.announced is not None:
            self.announced = len(received)
        return dict(owed)
