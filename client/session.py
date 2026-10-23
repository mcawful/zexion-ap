"""One poll of the running game: which locations were checked, whether the goal was reached,
and the grants that bring the inventory up to what the player should own.

No Archipelago imports, so the tests drive it with a fake game. The client calls ``step`` a few
times a second from a worker thread and does the networking.

Safety, in order:

- Nothing happens unless two reads of the game, ``SETTLE_SECONDS`` apart, agree on everything
  used (state, map, inventory, supplies, randomizer section). That covers a load in progress
  and the moment a pickup is being recorded.
- The loaded save must be a randomizer save (its marker reads 1: loading a normal save leaves
  the previous seed's section in memory) and hold this seed (the section's item table and
  start, see ``save_patch.section_matches``); otherwise no check is sent and nothing is
  given.
- Checks and the goal (the ending's flag) are read in any state with this seed loaded, which
  covers the ending's cutscenes and credits. Grants are made only in
  play (``game.IN_PLAY``), as compare-and-set writes, and only to make up a difference
  (client/reconcile.py), so a grant undone by a death or reload is made again and none is
  made twice.
- A grant waits for two polls in a row to owe exactly the same, so a pickup the game is half
  way through recording (the map cell set, the inventory byte not yet) is never taken for an
  item owed.

With the native helper attached (``Session.helper``), the grants go to the game as one batch
the helper applies between frames (client/native.py): it checks again that the game is in
play and that every byte still holds what this poll read, writes all or none, sets missiles,
bombs and energy as a pickup would, refreshes the HUD, and shows the game's popup for each
item received since the last batch (not for those the server already held when the client
connected: ``mark_announced``). When nothing is owed but max missiles or bombs lag behind the
counts, it sends a refresh that sets them. A helper that fails is dropped, and grants go back
to direct writes. Both ran in the game on 2026-10-22 (native/README.md).
"""

from __future__ import annotations

import logging
import time
from dataclasses import astuple, dataclass, field

import game
import native
import reconcile
import save_patch

logger = logging.getLogger("Zexion")

SETTLE_SECONDS = game.SETTLE_SECONDS
# The goal: flag 250, set when the first ending cutscene starts, after the final boss and the
# ship (or the game's skip to the ship after the grab), and saved with the game
# (docs/re/memory.md, "The ending").
ENDING_FLAG = 250


@dataclass(frozen=True)
class Snapshot:
    state: str
    grid: bytes = b""
    inventory: bytes = b""
    supplies: tuple[int, ...] = ()
    blob: bytes = b""
    marker: int = 0
    ended: int = 0


def read_once(g: game.Game) -> Snapshot:
    state = g.state()
    if state == game.NO_SAVE:
        return Snapshot(state)
    return Snapshot(
        state,
        g.map_grid(),
        g.inventory(),
        g.supplies(),
        g.rando_blob(),
        g.rando_marker(),
        g.flag(ENDING_FLAG),
    )


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


class Session:
    def __init__(self, plan: save_patch.SeedPlan, cells: list):
        if len(cells) != save_patch.LOCATIONS or len(plan.location_items) != len(cells):
            raise ValueError("the slot data does not list 154 locations")
        self.plan = plan
        self.cells = [tuple(c) for c in cells]
        self._said: set[str] = set()
        self._due: dict[int, int] | None = None  # what the previous poll found owed
        self.helper: native.Helper | None = None
        # How many received items have had their popup. None until the server's first full
        # list arrives (mark_announced): what came before this connection is not announced.
        self._announced: int | None = None

    def mark_announced(self, count: int) -> None:
        """Received items up to ``count`` need no popup (the backlog at connection)."""
        self._announced = count

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
    ) -> Step:
        """One poll. ``senders`` names who sent each of ``received`` (for popups)."""
        snap = read_settled(g, gap)
        if snap is None or snap.state != game.IN_PLAY:
            self._due = None
        if snap is None or snap.state == game.NO_SAVE:
            return Step()
        try:
            seed = game.parse_seed(snap.blob) if snap.marker == 1 else None
        except game.GameError:
            seed = None
        if seed is None or list(seed.locations) != self.cells:
            self.say_once("no seed", "The loaded save is not a randomizer save of this seed.")
            self._due = None
            return Step()
        if not save_patch.section_matches(snap.blob, self.plan):
            self.say_once(
                "other seed",
                "The loaded save holds a different seed: nothing is sent or given. "
                "Load the slot you prepared for this one.",
            )
            self._due = None
            return Step()
        self._said.discard("other seed")
        collected = reconcile.collected_locations(snap.grid, self.cells)
        step = Step(collected, snap.ended >= 1, {}, True)
        if snap.state == game.IN_PLAY:
            step.granted = self.give(g, snap, collected, received, gap, senders)
        return step

    def give(
        self,
        g: game.Game,
        snap: Snapshot,
        collected: set[int],
        received: list[int],
        gap: float,
        senders: list[str] | None = None,
    ) -> dict[int, int]:
        plan = self.plan
        target = reconcile.should_own(plan.start_items, plan.location_items, collected, received)
        result = reconcile.grants(snap.inventory, target)
        for item, missing in result.not_grantable.items():
            self.say_once(
                f"stuck {item} {missing}",
                f"Item {item} is owed {missing} more time(s) but cannot be given by the client.",
            )
        due, self._due = self._due, result.inventory
        if result.inventory != due:
            return {}  # owed for the first time: wait for the next poll to agree
        if self.helper is not None:
            try:
                return self.give_through_helper(snap, result.inventory, received, senders)
            except native.HelperError as exc:
                logger.warning(f"The in-game helper failed ({exc}); giving items directly.")
                self.helper.close()
                self.helper = None
        if self._announced is not None:
            self._announced = len(received)  # direct writes show no popups
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
    ) -> dict[int, int]:
        popups: list[tuple[int, str]] = []
        start = self._announced if self._announced is not None else len(received)
        # A progressive weapon received while its weapon is owned arrives as the upgrade, so
        # its popup names the upgrade.
        weapons = {w for w in reconcile.PROGRESSIVE if snap.inventory[w]}
        for index in range(start, len(received)):
            item = received[index]
            if item in reconcile.PROGRESSIVE:
                if item in weapons:
                    item = reconcile.PROGRESSIVE[item]
                weapons.add(received[index])
            sender = senders[index] if senders and index < len(senders) else ""
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
        if self._announced is not None:
            self._announced = len(received)
        return dict(owed)
