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


class DeathLink:
    def __init__(self) -> None:
        self.alive: bool | None = None  # the last of in play or dead seen in this seed's save
        self.owed = 0  # received deaths not dealt yet
        self.asked_at: float | None = None  # the helper was asked to deal one; no reply yet
        self.dealt_at: float | None = None  # the helper dealt one
        # The reply was lost (the Linux helper's connection closed, or its socket timed out) and
        # no in-play or dead state was read since: the window of ``asked_at`` starts at that read
        # (reply_lost), as the next poll first configures the helper again, which can take longer
        # than DEALT_SECONDS before any state is read.
        self.unread = False

    def observe(self, state: str, now: float) -> bool:
        """One poll's settled state, with this seed's save loaded (``game.NO_SAVE`` when it is
        not). True when the player just died a death of their own, to send to the others."""
        if state == game.NO_SAVE:
            self.alive = None  # a save loaded next starts over
            if self.unread:  # the lost reply's death was in the save left
                self.unread, self.asked_at = False, None
            return False
        if self.unread and state in (game.IN_PLAY, game.DEAD):
            # The first read since the reply was lost: a death seen within DEALT_SECONDS of this
            # one is the one the helper may have dealt (a respawn already past it is not told
            # from a death never dealt: the death stays queued).
            self.unread, self.asked_at = False, now
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
            self.asked_at = None  # dealt, and the helper's reply lost
            self.owed = max(0, self.owed - 1)
            return False
        return True

    def receive(self) -> int:
        """A death received from another player, queued. Returns how many are queued."""
        self.owed += 1
        return self.owed

    def dealing(self, now: float) -> None:
        """The helper is being asked to deal the next death: a death seen soon after is that
        one, even if the reply is lost."""
        self.asked_at = now
        self.unread = False

    def reply_lost(self, now: float) -> None:
        """The Linux helper's reply to the request to deal was lost (the connection closed, or
        the socket timed out after its whole timeout): it may have dealt the death. One seen
        within DEALT_SECONDS of the first in-play or dead state read from now on is that one:
        counted as dealt, not sent as the player's own."""
        self.asked_at = now
        self.unread = True

    def refused(self) -> None:
        """The helper did not deal it: it stays queued."""
        self.asked_at = None
        self.unread = False

    def dealt(self, now: float) -> None:
        """The helper killed the player for the next death queued."""
        self.owed = max(0, self.owed - 1)
        self.asked_at = None
        self.unread = False
        self.dealt_at = now
