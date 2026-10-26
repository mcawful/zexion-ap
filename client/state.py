"""The client's state, grouped by how long each part lasts (client/zexion_client.py keeps one of
each, or None).

- ``Connection``: one per Connected, from the seed's slot data; None while disconnected.
- ``HelperState``: what one attached helper has been told; a new one each time a helper is
  attached, so a helper still loaded from before is told everything again.
- ``SeedState``: what carries over reconnections to the same seed and slot (the Area Map
  hints this client stored, DeathLink's queue); started afresh for another seed or slot.
- ``LoadState``: what the polls found of the loaded save of this seed; mostly started afresh
  when no save of the seed is loaded, or when the game closes.

Also the decisions the game loop makes from that state, as pure functions: the count of
received items' popups shown, kept on the server (``announced_start`` and the rest), the
locations found to record there (``found_to_store``), and ``SayOnce`` for messages that would
repeat each poll. No Archipelago imports, so the tests build them from slot data.
"""

from __future__ import annotations

import enum
from collections.abc import Mapping
from dataclasses import dataclass, field

from . import aplabels, deathlink, discs, save_patch, session


class NewGame(enum.Enum):
    """The AP new game (client/newgame.py): armed in the helper, or what became of it."""

    ARMED = "armed"
    DONE = "done"  # a new game holds the seed; armed again once its save is in play
    UNAVAILABLE = "unavailable"  # the seed's starting items cannot make one: /prepare


@dataclass
class Connection:
    """This connection's seed, from its slot data, and what is tied to the connection."""

    team: int
    slot: int
    plan: save_patch.SeedPlan
    session: session.Session
    kept: frozenset[int]  # the location indices the world created
    # Other players' items (client/aplabels.py): this world's locations holding one.
    foreign: list[int]
    # Area Maps as hints (client/hints.py): where this player's progression items are.
    hint_targets: list[list[int]]
    lock_assists: bool  # the seed disallows assists (until Assist Unlock is received)
    intel_chip: bool  # the seed's Area Maps are Intel Chips
    death_link_on: bool
    hunt: discs.DiscHunt | None  # the OS Install Disks goal, or None for the core goal
    # The server's full item list has come (the packet after Connected may be it).
    items_listed: bool = False
    awaiting_backlog: bool = True
    # The follow-up popups shown for this world's own finds, by location index, until the
    # server's record has them.
    popped_mine: set[int] = field(default_factory=set)
    # Received items' popups (announced_tick): the count of those shown that this client last
    # stored on the server, and the last count known shown; and how many the connection before
    # (a reconnect) handed the same helper, which still holds them (not queued again).
    announced_sent: int | None = None
    announced_shown: int | None = None
    queued_before: int | None = None
    found_sent_at: float = 0.0  # when the locations found were last stored (store_found)
    # Whether this seed's save has had the scanner's message (the disks needed are known).
    discs_told: bool = False
    # Whether the helper should hold the labels: only while this seed's save is loaded
    # (aplabels.labels_wanted), so other saves keep Scrap Energy as the game's own.
    labels_want: bool = False
    goal_sent: bool = False
    choice_refused: bool = False  # the game's start table differed: no ARCHIPELAGO
    newgame: NewGame | None = None  # None: to arm
    newgame_said: bool | None = None  # the offer "Ready" was logged for (once a connection)
    newgame_polled_at: float = 0.0
    newgame_retry_at: float = 0.0  # when to arm again after the helper refused (newgame_tick)
    warned: set[int] = field(default_factory=set)  # received items said to be no Zexion item

    @classmethod
    def from_slot_data(cls, slot_data: Mapping, team: int, slot: int) -> Connection:
        plan = save_patch.plan_from_slot_data(slot_data)
        kept = frozenset(slot_data["kept_locations"])
        return cls(
            team=team,
            slot=slot,
            plan=plan,
            session=session.Session(plan, slot_data["locations"]),
            kept=kept,
            # Only a world that says it gives other players' items the stand-in has them.
            foreign=aplabels.foreign_locations(
                slot_data["location_items"], kept, aplabels.stand_ins(slot_data)
            ),
            # A world from before Area Map hints sends no "hints": Area Maps are filler.
            hint_targets=[list(t) for t in slot_data.get("hints", [])],
            # A seed from before the option has no "allow_assists": nothing is locked.
            lock_assists=slot_data.get("allow_assists", True) is False,
            # A seed from before Intel Chips has no "intel_chip": its Area Maps are Area Maps.
            intel_chip=slot_data.get("intel_chip", False) is True,
            death_link_on=bool(slot_data.get("death_link", False)),
            hunt=discs.from_slot_data(slot_data),
        )

    def key(self, name: str) -> str:
        """This slot's data storage key for ``name`` (per team and slot)."""
        return f"zexion_{name}_{self.team}_{self.slot}"

    # Received Energy Shields counted as given (client/shields.py).
    @property
    def shield_key(self) -> str:
        return self.key("shields")

    # This world's locations found in game, in any save of the seed (own_found): a release,
    # which checks every location on the server, never writes it.
    @property
    def found_key(self) -> str:
        return self.key("found")

    # The follow-up popups shown for this world's own finds, by location index.
    @property
    def popped_key(self) -> str:
        return self.key("popped")

    # How many received items have had their popup (announced_tick).
    @property
    def announced_key(self) -> str:
        return self.key("announced")

    # Which entry each Area Map used (client/hints.py).
    @property
    def hints_key(self) -> str:
        return self.key("hints")

    def helper_lost(self) -> None:
        """The helper is gone, and the popups it had not shown with it: received items past
        the last count known shown are queued again, on the next helper."""
        queued = self.session.announced
        if queued is not None and self.announced_shown is not None:
            self.session.mark_announced(min(queued, self.announced_shown))
        self.queued_before = None


# Received items' popups. The server keeps how many have been shown (Connection.announced_key),
# so an item received while no client ran pops up when it is given and a reconnect replays
# none. The session counts those handed to the helper or written directly (Session.announced:
# queued); the helper says how many of its popups are still waiting (Helper.popups_pending), and
# queued minus those is the count shown. The key is open to any client in the room: anything
# but a count in it reads as absent.


def stored_count(raw: object) -> int | None:
    """A count read from data storage, or None when absent (or not a count)."""
    if isinstance(raw, int) and not isinstance(raw, bool) and raw >= 0:
        return raw
    return None


def announced_start(raw: object, held: int, queued_before: int | None = None) -> tuple[int, int]:
    """Where a connection's session starts counting received items' popups, as (queued, shown):
    shown, the server's count (at most the ``held`` items), or, with none stored (a slot played
    before the count, or its data lost), every item held: no flood of old popups. Queued, the
    same, but never below what the connection before handed the same helper
    (``queued_before``), which the helper still holds."""
    stored = stored_count(raw)
    shown = held if stored is None else min(stored, held)
    queued = shown if queued_before is None else max(shown, min(queued_before, held))
    return queued, shown


def popups_unstored(queued: int, raw: object, sent: int | None) -> bool:
    """Whether more popups have been queued than the server holds as shown: only then is the
    helper asked how many are still waiting."""
    return queued > max(stored_count(raw) or 0, sent or 0)


def popups_shown(queued: int, pending: int, before: int) -> int:
    """The received items whose popup has been shown: those queued less the helper's popups
    still waiting, never below the count known before. A lower bound, as the helper's count
    takes in the popups of every ``apply`` (the follow-ups too), not only received items'."""
    return min(queued, max(before, queued - pending))


def announced_to_store(shown: int, raw: object, sent: int | None) -> int | None:
    """The count to store on the server now (as a maximum), or None: ``shown`` when it is above
    both the server's count and what this client last stored; and, while the server has none
    and this client has stored none, ``shown`` as it is, so the key exists. A store that fails
    leaves ``sent`` as it was, so it is sent again on the next poll."""
    stored = stored_count(raw)
    if stored is None and sent is None:
        return shown
    if shown > max(stored or 0, sent or 0):
        return shown
    return None


def found_to_store(
    in_game: set[int], stored: set[int] | None, new: set[int], due: bool
) -> set[int]:
    """The locations found (indices) to record in the server's data storage now: those just
    found (``new``) until the server's record (``stored``) has been read; then every one found
    in the game that the record lacks, when one was just found or a resend is ``due`` (one sent
    before may have been dropped with a closing connection)."""
    if stored is None:
        return set(new)
    missing = in_game - stored
    return missing if missing and (new & missing or due) else set()


class SayOnce:
    """Messages said once each until they change, by topic."""

    def __init__(self) -> None:
        self._last: dict[str, str] = {}

    def __call__(self, topic: str, message: str) -> bool:
        """True if ``message`` is not what was last said for ``topic`` (and so is to be said)."""
        if self._last.get(topic) == message:
            return False
        self._last[topic] = message
        return True

    def clear(self, topic: str) -> None:
        """The next message for ``topic`` is said, whatever it is (it went well meanwhile)."""
        self._last.pop(topic, None)


@dataclass
class Toggle:
    """A setting the helper is told to turn on or off: what it was last told (None: nothing
    yet, and a helper may hold an earlier client's), and when to ask again after a refusal."""

    sent: bool | None = None
    retry_at: float = 0.0


@dataclass
class HelperState:
    """What one attached helper has been told."""

    assists: Toggle = field(default_factory=Toggle)  # the ASSIST lock
    intel: Toggle = field(default_factory=Toggle)  # the Area Map named INTEL CHIP
    # The OS Install Disks: the discs request last taken (discs.request or discs.OFF), and when
    # to ask again; what the equipment screen last showed, and when it is asked.
    discs_sent: str | None = None
    discs_retry_at: float = 0.0
    discs_status: str = ""
    discs_status_at: float = 0.0
    # The file select's count (discs.slots_request), and when the slots are read again.
    slots_sent: str | None = None
    slots_read_at: float = 0.0
    # The labels: whether it may hold some (this client's, or an earlier client's), the
    # connection whose labels it holds, whether it refused them (not asked again).
    labels_held: bool = True
    labels_for: Connection | None = None
    labels_refused: bool = False
    labels_retry_at: float = 0.0
    client_back_sent: bool = False  # asked once (client_back_tick)
    other_save_sent: bool = False  # told of another seed's save, this load
    offline_sent: dict[str, str] = field(default_factory=dict)  # what it last kept, by kind
    offline_at: float = 0.0
    skip_check_at: float | None = None  # when to ask whether it offers the escape skip


@dataclass
class SeedState:
    """What carries over reconnections to one seed and slot."""

    identity: tuple | None = None  # (seed name, team, slot)
    # The entry each Area Map used that this client stored (ahead of the server's until it is
    # echoed back), and when it was last stored.
    hints_mine: dict[str, tuple[int, int]] | None = None
    hints_sent_at: float = 0.0
    deaths: deathlink.DeathLink = field(default_factory=deathlink.DeathLink)
    death_retry_at: float = 0.0
    death_said: str = ""


@dataclass
class LoadState:
    """What the polls found of the loaded save of this seed."""

    seed_loaded: bool = False  # the last poll found this seed's save loaded
    save_collected: set[int] = field(default_factory=set)  # its collected locations
    # This load was seen before its ending (flag 250 clear): with the OS Install Disks goal,
    # only an ending that begins in it is the goal, not one a save held.
    ending_unseen: bool = False
    # The landing-pod reminder and RETURN HOME WITH THE DISKS? for this load (mission_tick).
    return_home: discs.ReturnHome = field(default_factory=discs.ReturnHome)
    # Whether the helper should lock ASSIST, and name the Area Map INTEL CHIP, now.
    assists_want: bool = False
    intel_want: bool = False
    # An ending without enough OS Install Disks, said once a load.
    goal_withheld: bool = False

    def leave(self) -> None:
        """No save of this seed is loaded: the next load starts afresh."""
        self.seed_loaded = False
        self.save_collected = set()
        self.return_home = discs.ReturnHome()
        self.ending_unseen = False
        self.assists_want = self.intel_want = False
        self.goal_withheld = False
