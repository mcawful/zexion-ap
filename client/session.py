"""One poll of the running game: which locations were checked, whether the goal was reached,
and the grants that bring the inventory up to what the player should own.

No Archipelago imports, so the tests drive it with a fake game. The client calls ``step`` a few
times a second from a worker thread and does the networking.

Safety, in order:

- Nothing happens unless two reads of the game, ``SETTLE_SECONDS`` apart, agree on everything
  used (state, map, inventory, supplies, randomizer section). That covers a load in progress
  and the moment a pickup is being recorded.
- The loaded save must hold this seed (its randomizer section's item table and start, see
  ``save_patch.section_matches``); otherwise no check is sent and nothing is given.
- Checks and the goal are read in any state with this seed loaded. Grants are made only in
  play (``game.IN_PLAY``), as compare-and-set writes, and only to make up a difference
  (client/reconcile.py), so a grant undone by a death or reload is made again and none is
  made twice.
"""

from __future__ import annotations

import logging
import time
from dataclasses import astuple, dataclass, field

import game
import reconcile
import save_patch

logger = logging.getLogger("Zexion")

SETTLE_SECONDS = game.SETTLE_SECONDS
GOAL_ITEM = 31  # Zexion: its byte is 1 from the grab in the core until the ship (memory.md)


@dataclass(frozen=True)
class Snapshot:
    state: str
    grid: bytes = b""
    inventory: bytes = b""
    supplies: tuple[int, ...] = ()
    blob: bytes = b""


def read_once(g: game.Game) -> Snapshot:
    state = g.state()
    if state == game.NO_SAVE:
        return Snapshot(state)
    return Snapshot(state, g.map_grid(), g.inventory(), g.supplies(), g.rando_blob())


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

    def say_once(self, key: str, message: str) -> None:
        if key not in self._said:
            self._said.add(key)
            logger.warning(message)

    def step(self, g: game.Game, received: list[int], gap: float = SETTLE_SECONDS) -> Step:
        snap = read_settled(g, gap)
        if snap is None or snap.state == game.NO_SAVE:
            return Step()
        try:
            seed = game.parse_seed(snap.blob)
        except game.GameError:
            seed = None
        if seed is None or list(seed.locations) != self.cells:
            self.say_once("no seed", "The loaded save is not a randomizer save of this seed.")
            return Step()
        if not save_patch.section_matches(snap.blob, self.plan):
            self.say_once(
                "other seed",
                "The loaded save holds a different seed: nothing is sent or given. "
                "Load the slot you prepared for this one.",
            )
            return Step()
        self._said.discard("other seed")
        collected = reconcile.collected_locations(snap.grid, self.cells)
        step = Step(collected, snap.inventory[GOAL_ITEM] >= 1, {}, True)
        if snap.state == game.IN_PLAY:
            step.granted = self.give(g, snap, collected, received, gap)
        return step

    def give(
        self,
        g: game.Game,
        snap: Snapshot,
        collected: set[int],
        received: list[int],
        gap: float,
    ) -> dict[int, int]:
        plan = self.plan
        target = reconcile.should_own(plan.start_items, plan.location_items, collected, received)
        result = reconcile.grants(snap.inventory, target)
        for item, missing in result.not_grantable.items():
            self.say_once(
                f"stuck {item} {missing}",
                f"Item {item} is owed {missing} more time(s) but cannot be given by the client.",
            )
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
