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
received items' popups handed over, kept on the server (``announced_start``), the server's
count of Energy Shields given (``shield_count``), ``SayOnce`` for messages that would repeat
each poll, and the player's lines when the game or the mod can't be used (``attach_failed``,
``helper_failed``, ``reload_reply``, ``FeatsCheck``). No Archipelago imports, so the tests
build them from slot data.
"""

from __future__ import annotations

import enum
from collections.abc import Mapping
from dataclasses import dataclass, field

from . import aplabels, deathlink, discs, game, native, save_patch, session


class UnsupportedSeed(ValueError):
    """The slot data is not what this client reads: a seed from another version of the world."""


class NewGame(enum.Enum):
    """The AP new game (client/newgame.py): armed in the helper, or what became of it."""

    ARMED = "armed"
    DONE = "done"  # a new game holds the seed; armed again once its save is in play
    UNAVAILABLE = "unavailable"  # the seed's starting items cannot make one


@dataclass
class Connection:
    """This connection's seed, from its slot data, and what is tied to the connection."""

    team: int
    slot: int
    mode: str  # the seed's mode, as the world names it
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
    # Received items' popups (announced_tick): the count last stored, and the session of
    # the connection before (same seed and slot), read for how many it handed over once
    # its last poll has finished.
    announced_sent: int | None = None
    before: session.Session | None = None
    found_sent_at: float = 0.0  # when the locations found were last stored (store_found)
    hints_recreated: bool = False  # the stored hints' CreateHints sent again (hints_tick)
    # Whether this seed's save has had the scanner's message (the disks needed are known).
    discs_told: bool = False
    # Whether the helper should dress this seed's save, the labels and the disk count: only
    # while it is loaded, as the last poll that could tell found it (aplabels.labels_wanted), so
    # other saves keep Scrap Energy and the scanner's message as the game's own. Unlike
    # LoadState.seed_loaded, a reconnect to the same seed carries it over (carry_over).
    dressed: bool = False
    goal_sent: bool = False
    choice_refused: bool = False  # the game's start table differed: no ARCHIPELAGO
    newgame: NewGame | None = None  # None: to arm
    newgame_said: bool | None = None  # whose "To play" line was said (once a connection)
    newgame_polled_at: float = 0.0
    newgame_retry_at: float = 0.0  # when to arm again after the helper refused (newgame_tick)
    warned: set[int] = field(default_factory=set)  # received items said to be no Zexion item

    @classmethod
    def from_slot_data(cls, slot_data: Mapping, team: int, slot: int) -> Connection:
        """The connection for a seed's slot data; UnsupportedSeed if this client cannot read it
        (a key missing or of another form: a world of another version)."""
        try:
            plan = save_patch.plan_from_slot_data(slot_data)
            kept = frozenset(slot_data["kept_locations"])
            return cls(
                team=team,
                slot=slot,
                mode=str(slot_data["mode"]),
                plan=plan,
                session=session.Session(plan, slot_data["locations"]),
                kept=kept,
                foreign=aplabels.foreign_locations(
                    slot_data["location_items"], kept, aplabels.stand_ins(slot_data)
                ),
                hint_targets=[list(t) for t in slot_data["hints"]],  # empty with the hints off
                lock_assists=not slot_data["allow_assists"],
                intel_chip=bool(slot_data["intel_chip"]),
                death_link_on=bool(slot_data["death_link"]),
                hunt=discs.from_slot_data(slot_data),
            )
        except (AttributeError, KeyError, TypeError, ValueError) as exc:
            raise UnsupportedSeed(
                f"this seed's slot data is not what this client reads ({exc!r}): the seed is "
                "from a version of the Zexion world this client does not support"
            ) from exc

    def key(self, name: str) -> str:
        """This slot's data storage key for ``name`` (per team and slot)."""
        return f"zexion_{name}_{self.team}_{self.slot}"

    def __post_init__(self) -> None:
        # This slot's data storage keys: received Energy Shields counted as given (client/
        # shields.py); this world's locations found in game, in any save of the seed
        # (own_found: a release, which checks every location on the server, never writes it);
        # the follow-up popups shown for this world's own finds, by location index; how many
        # received items have had their popup (announced_tick); which entry each Area Map
        # used (client/hints.py).
        self.shield_key = self.key("shields")
        self.found_key = self.key("found")
        self.popped_key = self.key("popped")
        self.announced_key = self.key("announced")
        self.hints_key = self.key("hints")


def left_connection(conn: Connection | None, left: Connection | None) -> Connection | None:
    """The connection to keep for the next one (carry_over) when the client disconnects: the
    one just ended, or, with none (Archipelago's connect disconnects first, after a Disconnect
    already ended it), the one kept before. The same pick names the connection a reconnect
    carries from (connected), and the one kept meanwhile (away_tick: a dropped one is still
    ``conn``)."""
    return conn if conn is not None else left


def carry_over(
    conn: Connection, old: Connection | None, same_seed: bool, told: HelperState | None = None
) -> None:
    """A new connection to the same seed and slot as ``old`` (one that dropped, or one a
    disconnect ended): the ending's hold carries over (Session.keep_ending), and ``old``'s
    session (``before``), for how many popups it handed over in case its last count never
    reached the server. Read at the first count (announced_tick), not now: a poll of ``old``
    may still be handing some over.

    So does what the helper (``told``) holds for the save: whether it is dressed, the labels
    ``old`` sent (the same seed's and slot's) and whether the scanner's message was had, so a
    dropped connection takes nothing away to send it again once reconnected. A disconnect has
    taken them back already (``old.dressed`` False)."""
    if old is None or not same_seed:
        return
    conn.session.keep_ending(old.session)
    conn.before = old.session
    conn.dressed, conn.discs_told = old.dressed, old.discs_told
    if told is not None and told.labels_for is old:
        told.labels_for = conn


def end_kept_load(ended: session.Session, conn: Connection | None) -> None:
    """``ended``'s load has ended (its save left, or the game closed), found by a read in a
    thread (Session.look_away, or a step raising GameNotRunning). A reconnect may have carried
    its ending over meanwhile (carry_over): the current connection's session too, then, if it
    has not stepped since (polls run one at a time, so it cannot have yet)."""
    ended.leave()
    if conn is not None and conn.session.carried_from is ended:
        conn.session.leave()


def left_while_away(kept: Connection, conn: Connection | None) -> None:
    """While no connection stepped (a dropped one, or one a disconnect ended), ``kept``'s save
    was seen to leave (Session.look_away): its load ends (end_kept_load), and the save is
    undressed, so the helper takes back the labels and the disk count from another save loaded
    meanwhile; so is the current connection's, if it carried them over (carry_over)."""
    carried = conn is not None and conn.session.carried_from is kept.session
    end_kept_load(kept.session, conn)
    kept.dressed = False
    if carried:
        conn.dressed = False


def goal_reached(ended: bool, required: int | None, have: int) -> bool:
    """Whether the loaded save of this seed holds the goal: its ending (``ended``: the
    session's goal), and with the OS Install Disks goal (``required``) only with enough disks
    (``have``). Whether or not the client saw that ending begin: one it missed (closed during
    it, or the save played without it) would never come again, as a save past its ending is not
    offered RETURN HOME again."""
    return ended and (required is None or have >= required)


def stored_count(raw: object) -> int | None:
    """A count read from data storage, or None when absent (or not a count).

    For received items' popups the server keeps how many have been handed over
    (announced_key: Session.announced), so items received offline still pop up and a reconnect
    or a restart replays none. The helper shows what it was handed even if the client goes
    away; a game closed or the helper unloaded with popups still queued loses those popups (the
    items are given), and so does a client closed between handing some over and storing the
    count (the next one hands them over again: shown twice). The key is open to any client in
    the room: anything but a count reads as absent."""
    if isinstance(raw, int) and not isinstance(raw, bool) and raw >= 0:
        return raw
    return None


def shield_count(raw: object) -> int | None:
    """The server's count of Energy Shields given (client/shields.py): 0 when none is stored,
    None when the key holds something other than a count. The key is open to any client in the
    room, and a bad value must not read as 0, which would give every received shield again: the
    shields wait instead."""
    if raw is None:
        return 0
    return stored_count(raw)


def announced_start(raw: object, held: int, handed_before: int | None = None) -> int:
    """How many received items a connection's session counts as handed over at its start: the
    server's count (at most the ``held`` items), or, with none stored (or its data lost),
    every item held: no flood of old popups. Never below what the connection before handed
    over (``handed_before``), whose count may not have reached the server."""
    stored = stored_count(raw)
    start = held if stored is None else min(stored, held)
    if handed_before is not None:
        start = max(start, min(handed_before, held))
    return start


def announced_to_store(handed: int, raw: object, sent: int | None) -> int | None:
    """The count to store on the server now (as a maximum), or None: ``handed`` when it is above
    both the server's count and what this client last stored; and, while the server has none
    and this client has stored none, ``handed`` as it is, so the key exists. A store that fails
    leaves ``sent`` as it was, so it is sent again on the next poll."""
    stored = stored_count(raw)
    if stored is None and sent is None:
        return handed
    if handed > max(stored or 0, sent or 0):
        return handed
    return None


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

    def clear(self, *topics: str) -> None:
        """The next message for each topic is said, whatever it is (it went well meanwhile)."""
        for topic in topics:
            self._last.pop(topic, None)


@dataclass
class Toggle:
    """A request the helper is kept told (ticks.tell): what it last took (None: nothing yet,
    and a helper may hold an earlier client's), and when to ask again after a refusal."""

    sent: object = None
    retry_at: float = 0.0


@dataclass
class HelperState:
    """What one attached helper has been told."""

    assists: Toggle = field(default_factory=Toggle)  # the ASSIST lock
    intel: Toggle = field(default_factory=Toggle)  # the Area Map named INTEL CHIP
    # The OS Install Disks: the discs request (discs.request or discs.OFF); what the equipment
    # screen last showed, and when it is asked.
    discs: Toggle = field(default_factory=Toggle)
    discs_status: str = ""
    discs_status_at: float = 0.0
    # The file select's count (discs.slots_request), and when the slots are read again.
    slots: Toggle = field(default_factory=Toggle)
    slots_read_at: float = 0.0
    # The labels: whether it may hold some (this client's, or an earlier client's), the
    # connection whose labels it holds, whether it refused them (not asked again).
    labels_held: bool = True
    labels_for: Connection | None = None
    labels_refused: bool = False
    labels_retry_at: float = 0.0
    client_back_sent: bool = False  # asked once (client_back_tick)
    other_save_sent: bool = False  # told of another seed's save, this load
    other_save_seen: bool = False  # the poll before saw it too (other_save_tick)
    offline_sent: dict[str, str] = field(default_factory=dict)  # what it last kept, by kind
    offline_at: float = 0.0


@dataclass
class SeedState:
    """What carries over reconnections to one seed and slot."""

    identity: tuple | None = None  # (seed name, team, slot)
    # The entry each Area Map used that this client stored (ahead of the server's until it is
    # echoed back), and when it was last stored.
    hints_mine: dict[str, tuple[int, int]] = field(default_factory=dict)
    hints_sent_at: float = 0.0
    # The follow-up popups this client showed for this world's own finds, by location index
    # (ahead of the server's record until it has them), and when they were last stored.
    popped_mine: set[int] = field(default_factory=set)
    popped_sent_at: float = 0.0
    deaths: deathlink.DeathLink = field(default_factory=deathlink.DeathLink)
    death_retry_at: float = 0.0


FEATS_ASK_SECONDS = 5.0  # between asks whether the Feats guard is on for this load
# This seed's save in play this long with the guard still off: it is not coming (the helper
# turns it on two frame calls into an AP save's load).
FEATS_OFF_SECONDS = 10.0


@dataclass
class FeatsCheck:
    """Whether the helper keeps Feats and Steam achievements out of the loaded save of this
    seed (its hello's ``feats``; native/README.md, "Feats and achievements in AP saves"), for
    one load and one helper (ticks.feats_tick asks; this decides)."""

    known: bool = False  # on, or on with why not all of it: not asked again
    ask_at: float = 0.0  # when to ask next
    off_since: float | None = None  # first read off with the save in play

    def due(self, now: float) -> bool:
        """Whether to ask the helper now (and if so, not again for FEATS_ASK_SECONDS)."""
        if self.known or now < self.ask_at:
            return False
        self.ask_at = now + FEATS_ASK_SECONDS
        return True

    def seen(self, feats: str, in_play: bool, now: float, said: SayOnce) -> tuple[str, str] | None:
        """The helper's ``feats`` read now (``in_play``: this seed's save in play): the
        player's line and the log's detail to say, or None. Each warning is said once, and
        again only after a load where the guard was all in place."""
        if feats == "on":
            self.known = True
            said.clear("feats")
            return None
        if feats.startswith("on:"):  # on, and why not all of it, its spaces as underscores
            self.known = True
            line, detail = FEATS_UNGUARDED, f"not all in place: {feats.removeprefix('on:')}"
        else:  # off: not yet, or not for this save (the helper does not take it for an AP save)
            if not in_play:
                return None
            if self.off_since is None:
                self.off_since = now
            if now - self.off_since < FEATS_OFF_SECONDS:
                return None
            line, detail = (
                FEATS_OFF,
                f"still {feats or 'not reported'} after {now - self.off_since:.0f} s in play",
            )
        return (line, detail) if said("feats", line) else None


@dataclass
class LoadState:
    """What the polls found of the loaded save of this seed."""

    seed_loaded: bool = False  # the last poll found this seed's save loaded
    save_collected: set[int] = field(default_factory=set)  # its collected locations
    # The landing-pod reminder and RETURN HOME WITH THE DISKS? for this load (mission_tick).
    return_home: discs.ReturnHome = field(default_factory=discs.ReturnHome)
    # Whether the helper should lock ASSIST, and name the Area Map INTEL CHIP, now.
    assists_want: bool = False
    intel_want: bool = False
    # An ending without enough OS Install Disks (yet), said once a load.
    goal_withheld: bool = False
    # The helper's Feats guard for this load (ticks.feats_tick).
    feats: FeatsCheck = field(default_factory=FeatsCheck)

    def leave(self) -> None:
        """No save of this seed is loaded: the next load starts afresh (every field as new)."""
        self.__init__()


REPORT = "https://github.com/mcawful/zexion-ap/issues"
SAVE_CLOSED = "You left this seed's save. Items sent to you wait until you load it again."
OTHER_SAVE = (
    "This save is from another seed: its checks aren't sent, and items sent to you wait until "
    "you load this seed's save."
)


def attach_failed(exc: game.GameError) -> str:
    """The client's line when attaching to the game failed (the details go to the log)."""
    if isinstance(exc, game.UnknownBuild):
        return (
            "This version of Zexion isn't supported yet (a game update?): wait for a new "
            "release of the Zexion client and mod."
        )
    if isinstance(exc, game.PatternError):
        return (
            "Can't find what the client needs in this version of Zexion: please report it at "
            f"{REPORT}."
        )
    if isinstance(exc, game.AccessDenied):
        return (
            "Zexion is running as administrator, so the client can't reach it: run Zexion "
            "normally, or run the Archipelago Launcher as administrator too."
        )
    return "Can't reach the running game; trying again (details in the log)."


FEATS_UNGUARDED = (
    "The Zexion mod couldn't fully turn off Feats and Steam achievements for this Archipelago "
    "save (details in the log): achievements may be earned in this save."
)
FEATS_OFF = (
    "The Zexion mod doesn't recognize this save as an Archipelago save, so Feats and Steam "
    "achievements aren't turned off (details in the log): achievements may be earned in this "
    "save."
)


def reload_reply(reply: str | None) -> tuple[bool, str]:
    """/reload_mod: whether the mod is to be loaded again, from its ``unload`` reply
    (native.Helper.unload; None: the request failed, its pipe broken, so it unloads itself),
    and the client's line. A refusal (``error <why>; not unloading``) leaves it loaded and
    working."""
    if reply is None:
        return True, "The Zexion mod didn't answer; loading it again."
    if reply == "bye":
        return True, "The Zexion mod was unloaded; loading it again."
    why = reply.removeprefix("error ").removesuffix("; not unloading")
    if why == "a menu is open":
        return False, "The Zexion mod can't be reloaded with a menu open: close it and try again."
    return False, (
        f"The Zexion mod can't be reloaded right now ({why}). Try /reload_mod again in a "
        "moment, out of any menu; if it keeps refusing, restart Zexion."
    )


def helper_failed(exc: native.HelperError) -> str | None:
    """The client's line when the mod could not be used (the details go to the log), or None
    when the game has just closed (nothing for the player to do). A cause the player can fix
    (native.ModFixable) is said as it is; any other gets one plain line."""
    if isinstance(exc, native.GameExited):
        return None
    if isinstance(exc, native.ModDisabled):
        return str(exc)
    meanwhile = "new games of this seed can't be started, and items arrive without popups."
    if isinstance(exc, native.ModFixable):
        return f"The Zexion mod isn't working: {exc}. Until it is, {meanwhile}"
    return (
        "The Zexion mod didn't start (details in the log); trying again. If it keeps happening, "
        "restart Zexion, or close it and copy this release's zexion_ap.dll and version.dll "
        f"into its folder again. Until it starts, {meanwhile}"
    )


def save_loaded(stamp: bytes) -> str:
    """The client's line when a save of this seed is loaded, with its slot as the game numbers
    it (the save stamp starts with current_slot, from 0: client/game.py, save_stamp)."""
    slot = int.from_bytes(stamp[:4], "little") + 1 if len(stamp) >= 4 else 0
    where = f" (slot {slot})" if 1 <= slot <= 30 else ""
    return f"This seed's save is loaded{where}: items and checks go through."
