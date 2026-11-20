"""DeathLink: when a death in Zexion goes out to the other players, and which deaths received
from them are still to be dealt.

A death is the settled state going from in play to dead (current energy at 0 or below;
docs/re/memory.md, "Telling play from menus, the title screen and death"), seen while this
seed's save is loaded. Received deaths queue: each is dealt by the native helper (its ``die``
request, docs/re/memory.md, "DeathLink") in turn, once the player is in play, so two received
together kill the player twice, the second after the respawn. A death the helper deals is not
sent back; a death of the player's own is sent and leaves the queue as it was.

Pure logic; client/ticks.py (deathlink_tick) sends deaths and has the helper deal them.
"""

from __future__ import annotations

from . import game

# A death seen this soon after the helper was asked to deal one is that one.
DEALT_SECONDS = 5.0
# A lost reply (the Linux helper's connection closed, or its socket timed out) is looked into only
# if the first state read after it comes this soon: past it, a death seen (or counted) is the
# player's own as likely as the dealt one. The poll holds the step for 30 s while the helper is
# configured again (ticks.LINUX_HOLD_SECONDS, the socket's 5 s after the ask).
LOST_SECONDS = 45.0


class DeathLink:
    def __init__(self) -> None:
        self.alive: bool | None = None  # the last of in play or dead seen in this seed's save
        self.owed = 0  # received deaths not dealt yet
        self.asked_at: float | None = None  # the helper was asked to deal one; no reply yet
        self.dealt_at: float | None = None  # the helper dealt one
        # The save's death count (discs.deaths) when the helper was last asked: a death dealt
        # whose reply was lost shows in it even once the player has respawned unseen.
        self.deaths_at_ask: int | None = None
        # When the reply to the request to deal was lost (the Linux helper's connection closed, or
        # its socket timed out), until that death is settled: dealt (seen dead, or the count
        # up), or not (in play past the window). While it is set no other is asked for.
        self.lost_at: float | None = None
        # No in-play or dead state read since the reply was lost: the window of ``asked_at``
        # starts at that read, as the next poll first configures the helper again, which can take
        # longer than DEALT_SECONDS before any state is read.
        self.unread = False

    @property
    def watching(self) -> bool:
        """A death the helper may have dealt, whose reply was lost, is not settled yet."""
        return self.lost_at is not None

    def observe(self, state: str, now: float, deaths: int | None = None) -> bool:
        """One poll's settled state, with this seed's save loaded (``game.NO_SAVE`` when it is
        not), and the save's death count (discs.deaths) if it was read. True when the player just
        died a death of their own, to send to the others."""
        if state == game.NO_SAVE:
            self.alive = None  # a save loaded next starts over
            if self.lost_at is not None:  # the lost reply's death was in the save left
                self.lost_at, self.asked_at, self.unread = None, None, False
            return False
        if self.lost_at is not None and state in (game.IN_PLAY, game.DEAD):
            if self.unread:
                # The first read since the reply was lost: a death seen within DEALT_SECONDS of
                # this one is the one the helper may have dealt, if it comes soon enough to be
                # told from the player's own.
                self.unread = False
                if now - self.lost_at > LOST_SECONDS:
                    self.lost_at = self.asked_at = None  # asked for again: nothing is known
                else:
                    self.asked_at = now
            counted = deaths is not None and self.deaths_at_ask is not None
            if self.lost_at is not None and counted and deaths > self.deaths_at_ask:
                # Dealt, and the player already respawned, or the death is on its way: it is
                # counted, and not dealt again nor sent as the player's own.
                self.lost_at = self.asked_at = None
                self.owed = max(0, self.owed - 1)
                self.alive = state == game.IN_PLAY
                return False
            if self.lost_at is not None and now - (self.asked_at or now) > DEALT_SECONDS:
                self.lost_at = self.asked_at = None  # no death came: it was not dealt
        if state == game.IN_PLAY:
            self.alive = True
            return False
        if state != game.DEAD:
            return False  # a menu, or changing: neither
        was_alive, self.alive = self.alive, False
        if was_alive is not True:
            return False
        if self.dealt_at is not None and now - self.dealt_at <= DEALT_SECONDS:
            self.dealt_at = None
            return False  # the received death, dealt
        if self.asked_at is not None and now - self.asked_at <= DEALT_SECONDS:
            self.asked_at = self.lost_at = None  # dealt, and the helper's reply lost
            self.owed = max(0, self.owed - 1)
            return False
        return True

    def receive(self) -> int:
        """A death received from another player, queued. Returns how many are queued."""
        self.owed += 1
        return self.owed

    def dealing(self, now: float, deaths: int | None = None) -> None:
        """The helper is being asked to deal the next death (``deaths``: the save's death count):
        a death seen soon after is that one, even if the reply is lost."""
        self.asked_at = now
        self.deaths_at_ask = deaths
        self.lost_at, self.unread = None, False

    def reply_lost(self, now: float) -> None:
        """The Linux helper's reply to the request to deal was lost (the connection closed, or
        the socket timed out after its whole timeout): it may have dealt the death. One seen
        within DEALT_SECONDS of the first in-play or dead state read from now on is that one,
        counted as dealt and not sent as the player's own; so is one the death count shows, if
        the player has respawned by then. No other is asked for until it is settled
        (``watching``)."""
        self.asked_at = now
        self.lost_at = now
        self.unread = True

    def refused(self) -> None:
        """The helper did not deal it: it stays queued."""
        self.asked_at = self.lost_at = None
        self.unread = False

    def dealt(self, now: float) -> None:
        """The helper killed the player for the next death queued."""
        self.owed = max(0, self.owed - 1)
        self.asked_at = self.lost_at = None
        self.unread = False
        self.dealt_at = now
