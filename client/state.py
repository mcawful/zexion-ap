"""The client's state, grouped by how long each part lasts (client/zexion_client.py keeps one of
each, or None).

- ``Connection``: one per Connected, from the seed's slot data; None while disconnected.
- ``HelperState``: what one attached helper has been told; a new one each time a helper is
  attached, so a helper still loaded from before is told everything again.
- ``SeedState``: what carries over reconnections to the same seed and slot (the Area Map
  hints this client stored, DeathLink's queue); started afresh for another seed or slot.
- ``LoadState``: what the polls found of the loaded save of this seed; mostly started afresh
  when no save of the seed is loaded.

No Archipelago imports, so the tests build them from slot data.
"""

from __future__ import annotations

import enum
from collections.abc import Mapping
from dataclasses import dataclass, field

import aplabels
import deathlink
import discs
import save_patch
import session


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
    # server's record has them; and the popup count last stored on the server.
    popped_mine: set[int] = field(default_factory=set)
    announced_sent: int | None = None
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

    def leave(self) -> None:
        """No save of this seed is loaded: the next load starts afresh."""
        self.seed_loaded = False
        self.save_collected = set()
        self.return_home = discs.ReturnHome()
        self.ending_unseen = False
        self.assists_want = self.intel_want = False
