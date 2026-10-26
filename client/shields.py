"""Received Energy Shields: when to spawn one through the helper, and when it counts as given.

An Energy Shield's effect is its pickup's spawn, not an inventory byte (docs/re/memory.md,
"Received Energy Shields"). The client asks the helper to spawn received ones not given yet,
one at a time, and counts each in the server's data storage (per team and slot, so a restart
or a shield received offline is not lost) once the game shows it up in play. One not seen
within SEEN_SECONDS, or whose player died or whose save was unloaded first, is spawned again
(reconcile.shield_outcome); after MISSES in a row the client waits BACKOFF_SECONDS.

Pure logic and the game read; client/zexion_client.py talks to the helper and the server.
"""

from __future__ import annotations

from dataclasses import dataclass

from . import game, reconcile

GAP_SECONDS = 2.0  # between received Energy Shields, or before asking again
SEEN_SECONDS = 3.0  # from a spawn, in play, to giving up on seeing the shield
MISSES = 3  # spawns in a row not seen before backing off
BACKOFF_SECONDS = 60.0

# What a look at a spawned shield led to (Shields.looked).
WAIT, AGAIN, BACKOFF, UP = "wait", "again", "backoff", "up"


@dataclass
class Pending:
    """A received Energy Shield the helper spawned, not counted yet."""

    key: str  # the data storage key of the slot it was spawned for
    at: float  # when (time.monotonic), or when last seen out of play
    left_play: bool = False  # the player died or the save was unloaded since


@dataclass
class Shields:
    pending: Pending | None = None
    misses: int = 0  # spawns in a row not seen
    # Shields counted as given that the server may not hold yet: (key, count), and when the
    # count was last sent.
    given: tuple[str, int] | None = None
    count_sent_at: float = 0.0
    retry_at: float = 0.0
    said: str = ""  # the last reply or failure said, so each is said once

    def given_count(self, key: str, stored: int) -> int:
        """Shields counted as given for this slot: the server's number (``stored``), or this
        client's when the server has not taken it yet."""
        if self.given is not None and self.given[0] == key:
            return max(stored, self.given[1])
        return stored

    def resend_due(self, key: str, stored: int, now: float) -> bool:
        """Whether to send the count again: the server has not taken it, and GAP_SECONDS have
        passed. Forgets the count once the server has it."""
        if self.given is None or self.given[0] != key:
            return False
        if stored >= self.given[1]:
            self.given = None  # the server has it
            return False
        return now >= self.count_sent_at + GAP_SECONDS

    def left(self) -> None:
        """The player died, or the save was unloaded or the game closed: a spawned shield not
        counted yet is spawned again."""
        if self.pending is not None:
            self.pending.left_play = True

    def current(self, key: str) -> Pending | None:
        """The spawned shield waiting to be seen, if it was spawned for this slot."""
        if self.pending is not None and self.pending.key != key:
            self.pending = None  # spawned for another slot's seed: not counted
        return self.pending

    def spawned(self, key: str, now: float) -> None:
        self.pending = Pending(key, now)
        self.said = ""

    def refused(self, reply: str, now: float) -> bool:
        """The helper did not spawn it (``reply``): True if that is worth saying (not said
        just before, and not the usual wait for a settled game)."""
        worth = reply != self.said and reply != "refused not settled"
        self.said = reply
        self.retry_at = now + GAP_SECONDS
        return worth

    def looked(self, pending: Pending, state: str, up: bool, key: str, stored: int, now: float):
        """One look at the game for ``pending``: its settled ``state`` and whether a shield is
        up. WAIT; AGAIN (spawned again after GAP_SECONDS); BACKOFF (MISSES in a row not seen:
        after BACKOFF_SECONDS); or UP, counted as given (``given``, to send to the server)."""
        if state in (game.DEAD, game.NO_SAVE):
            pending.left_play = True
        if state != game.IN_PLAY:
            pending.at = now  # the time to see it runs in play only
        overdue = now >= pending.at + SEEN_SECONDS
        outcome = reconcile.shield_outcome(state == game.IN_PLAY, up, pending.left_play, overdue)
        if outcome == reconcile.WAIT:
            return WAIT
        self.pending = None
        if outcome == reconcile.AGAIN:
            self.misses += 1
            if self.misses >= MISSES:
                self.misses = 0
                self.retry_at = now + BACKOFF_SECONDS
                return BACKOFF
            self.retry_at = now + GAP_SECONDS
            return AGAIN
        self.misses = 0
        self.given = (key, self.given_count(key, stored) + 1)
        self.retry_at = now + GAP_SECONDS
        return UP


def look(game_conn: game.Game, state: str) -> tuple[bool, str]:
    """Whether the player has a shield up, read only in play (``state``, settled); and the
    error if the read failed, which counts as not seen."""
    if state != game.IN_PLAY:
        return False, ""
    try:
        return game_conn.shield_up(), ""
    except game.GameNotRunning:
        raise
    except game.GameError as exc:  # the link leads nowhere readable: not seen
        return False, str(exc)
