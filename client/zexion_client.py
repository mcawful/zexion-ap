"""The Zexion Archipelago client.

    .venv/Scripts/python -m client.zexion_client [archipelago://name:password@host:port]

(``python client/zexion_client.py`` runs the same: it runs this module of the package.)

It needs Archipelago's own modules: by default the pinned checkout in .ap/Archipelago
(tools/ap_checkout.py), or the folder ``ZEXION_AP_DIR`` names. From Archipelago's Launcher, as
Zexion Client (worlds/zexion/__init__.py, run_client), Archipelago is the Launcher's own.
``--verbose`` also shows what the client sets up in the game for the seed; without it the
window shows only what the player acts on.

While Zexion runs with this seed's save loaded, the client sends the locations the map marks as
collected, gives the items other worlds send (client/session.py) and sends the goal when the
ending begins. With the in-game helper (client/native.py, native/) items arrive with the game's
popup, and the helper also starts new games holding the seed, labels other players' items,
offers the escape skip, locks ASSIST, names Intel Chips, shows the OS Install Disks, deals
DeathLinks and keeps AP saves' look without the client. The client asks the server only for
items from other worlds: this world's own are in the save's item table, and the game hands them
out itself.

What each part does, and when it was tested in the game: README.md ("Features") and
native/README.md; the docstrings of the ticks below and of the modules they use; the state the
client keeps, by how long it lasts: client/state.py.
"""

from __future__ import annotations

import asyncio
import contextlib
import os
import sys
import time
from collections.abc import Awaitable, Callable, Iterator
from pathlib import Path

HERE = Path(__file__).resolve().parent
if __name__ == "__main__" and not __package__:
    # Run as a script (client/zexion_client.py): run it as the package's module instead, with
    # the repo root on the path in place of this folder.
    import runpy

    sys.path[0] = str(HERE.parent)
    runpy.run_module("client.zexion_client", run_name="__main__", alter_sys=True)
    sys.exit()

AP_DIR = Path(os.environ.get("ZEXION_AP_DIR", HERE.parent / ".ap" / "Archipelago"))
if AP_DIR.is_dir() and str(AP_DIR) not in sys.path:  # from the Launcher, its own is in use
    sys.path.insert(0, str(AP_DIR))

import Utils  # noqa: E402
from BaseClasses import ItemClassification  # noqa: E402
from CommonClient import (  # noqa: E402
    ClientCommandProcessor,
    CommonContext,
    get_base_parser,
    gui_enabled,
    handle_url_arg,
    logger,
    server_loop,
)
from NetUtils import ClientStatus  # noqa: E402

from . import (  # noqa: E402
    aplabels,
    discs,
    game,
    hints,
    ids,
    native,
    newgame,
    offline,
    reconcile,
    save_patch,
    session,
    shields,
    state,
)
from .ids import ASSIST_UNLOCK, ITEM_ID_BASE, LOCATION_ID_BASE  # noqa: E402

GAME = "Zexion"
# The client's window shows what the player acts on (the connection, the helper, ARCHIPELAGO,
# warnings); what it sets up in the game for the seed is a detail, shown with --verbose.
VERBOSE = False


def detail(message: str) -> None:
    """A setup note: in the window and the log with --verbose, else at debug level."""
    (logger.info if VERBOSE else logger.debug)(message)


POLL_SECONDS = 0.25
HELPER_RETRY_SECONDS = 30.0
NEWGAME_POLL_SECONDS = 1.0
SKIP_CHECK_SECONDS = 2.0
REFUSED_RETRY_SECONDS = 5.0  # before asking the helper again after it refused a request
SLOTS_READ_SECONDS = 2.0  # between reads of the save slots for the file select's count
OFFLINE_WRITE_SECONDS = 5.0  # between looks at what the helper needs without the client
HINTS_RESEND_SECONDS = 5.0  # before storing the Area Map hints again if the server lacks them
FOUND_RESEND_SECONDS = 5.0  # likewise for the locations found (store_found)
DEATH_RETRY_SECONDS = 1.0  # before asking the helper again to deal a received death


class ZexionCommandProcessor(ClientCommandProcessor):
    ctx: ZexionContext

    def _cmd_unload_helper(self) -> bool:
        """Unload the in-game helper, as if it failed: items then arrive with no popup, and the
        client loads the helper again within 30 seconds. For testing; it changes the game."""
        if self.ctx.helper is None:
            self.output("The helper is not loaded.")
            return False
        self.ctx.unload_helper_pending = True
        self.output("Unloading the helper at the next poll.")
        return True


class ZexionContext(CommonContext):
    game = GAME
    items_handling = 0b001  # items from other worlds only
    command_processor = ZexionCommandProcessor

    def make_gui(self):
        ui = super().make_gui()
        ui.base_title = "Archipelago Zexion Client"
        return ui

    def __init__(self, server_address: str | None, password: str | None) -> None:
        super().__init__(server_address, password)
        self.keep_randomizer = False  # arm without ARCHIPELAGO (--keep-randomizer)
        # The state, by how long each part lasts (client/state.py).
        self.conn: state.Connection | None = None
        self.told: state.HelperState | None = None  # set with helper
        self.seed_state = state.SeedState()
        self.load = state.LoadState()
        self.shields = shields.Shields()  # by slot key: kept across connections
        self.game_conn: game.Game | None = None
        self.helper: native.Helper | None = None
        self.helper_retry_at = 0.0
        # /unload_helper: the game loop unloads it but keeps the dead connection, so the
        # next request fails the way a helper failing mid-session does.
        self.unload_helper_pending = False
        # The game loop, the helper's only user, is asked to disarm an armed new game.
        self.disarm_pending = False
        # Warnings and failures that would repeat each poll or retry: each said once.
        self.said = state.SayOnce()

        self.hints_failed = ""  # the last error the hints raised, said once
        self.offline_failed = False  # the last offline file failed, said once
        # The offline config file, built once per attach to the game: (the game, its text).
        self.offline_config: tuple[game.Game, str] | None = None

    @property
    def plan(self) -> save_patch.SeedPlan | None:
        return None if self.conn is None else self.conn.plan

    async def server_auth(self, password_requested: bool = False) -> None:
        if password_requested and not self.password:
            await super().server_auth(password_requested)
        await self.get_username()
        await self.send_connect()

    def on_package(self, cmd: str, args: dict) -> None:
        conn = self.conn
        if conn is not None and conn.awaiting_backlog and cmd != "Connected":
            # The server sends what the slot already holds right after Connected, in the same
            # message, and nothing when it holds nothing: so only the packet after Connected can
            # be that list. Which of them have had their popup is the server's count
            # (announced_tick).
            conn.awaiting_backlog = False
            conn.items_listed = True
        if cmd == "Connected":
            self.connected(args["slot_data"])

    def connected(self, slot: dict) -> None:
        conn = state.Connection.from_slot_data(slot, self.team, self.slot)
        conn.session.helper = self.helper
        old = self.conn  # a dropped connection's, on reconnecting
        identity = (self.server_seed_name or self.seed_name, self.team, self.slot)
        if (
            old is not None
            and identity == self.seed_state.identity
            and self.helper is not None
            and old.session.helper is self.helper
        ):
            # The popups it handed this helper are still the helper's to show: not queued again.
            conn.queued_before = old.session.announced
        self.conn = conn
        # Kept by the server across connections and clients: the Energy Shields given, the
        # locations found, the follow-up popups shown, and how many received items have had
        # their popup (so the items received while no client ran get theirs on connecting).
        self.set_notify(conn.shield_key, conn.found_key, conn.popped_key, conn.announced_key)
        if identity != self.seed_state.identity:  # another seed or slot: nothing carries over
            self.seed_state = state.SeedState(identity)
        # Wait for this connection's own read of the stored hints and its item list.
        self.stored_data.pop(conn.hints_key, None)
        self.load.seed_loaded = False
        self.load.return_home = discs.ReturnHome()
        if self.told is not None:
            self.told.labels_retry_at = 0.0
        if conn.hint_targets:
            self.set_notify(conn.hints_key)
        Utils.async_start(self.update_death_link(conn.death_link_on), name="death link tag")
        if conn.death_link_on:
            detail("DeathLink is on for this seed.")
        if conn.foreign:
            scouts = [LOCATION_ID_BASE + i for i in conn.foreign]
            Utils.async_start(
                self.send_msgs(
                    [{"cmd": "LocationScouts", "locations": scouts, "create_as_hint": 0}]
                ),
                name="scout labels",
            )
        if conn.hunt is not None:
            detail(f"The goal is {conn.hunt.required} of {conn.hunt.placed} OS Install Disks.")
        detail(f"Seed for Zexion's {slot['mode']} mode.")

    async def disconnect(self, allow_autoreconnect: bool = False) -> None:
        conn = self.conn
        if conn is not None and conn.newgame is state.NewGame.ARMED:
            self.disarm_pending = True
        # Nothing of the seed's is wanted now: the helper ticks, which run while a helper is
        # attached, take back the labels, the ASSIST lock and the INTEL CHIP name.
        self.conn = None
        self.load.seed_loaded = False
        self.load.save_collected = set()
        self.load.assists_want = self.load.intel_want = False
        await super().disconnect(allow_autoreconnect)

    def reset_server_state(self) -> None:
        # A dropped connection comes here (connection_closed), not through disconnect: the
        # received items are gone until the next connection lists them again, so what counts
        # them waits. What the helper holds for the seed (labels, the ASSIST lock, the armed
        # new game) stays for the reconnect.
        super().reset_server_state()
        conn = getattr(self, "conn", None)  # an Archipelago that calls it from __init__
        if conn is not None:
            conn.items_listed = False
            conn.awaiting_backlog = False

    def on_deathlink(self, data: dict) -> None:
        super().on_deathlink(data)
        if self.conn is None or not self.conn.death_link_on:
            return
        queued = self.seed_state.deaths.receive()
        if queued > 1:
            detail(f"DeathLink: {queued} deaths are queued, dealt one after another.")
        if self.helper is None:
            logger.warning(
                "DeathLink: the in-game helper is not running, so the death waits for it."
            )

    def drop_helper(self) -> None:
        if self.helper is not None:
            self.helper.close()
        self.helper = None
        self.told = None
        self.helper_retry_at = time.monotonic() + HELPER_RETRY_SECONDS
        conn = self.conn
        if conn is not None:
            conn.session.helper = None
            conn.helper_lost()  # its popups not shown yet are queued again on the next one
            # A plan the lost helper held is armed again in the next one; a new game just made
            # waits until its save is in play (poll), and a seed that cannot make one
            # stays as it is.
            if conn.newgame is state.NewGame.ARMED:
                conn.newgame = None
        self.disarm_pending = False

    def assists_unlocked(self) -> bool:
        """Whether this slot has received Assist Unlock (from the server console, say): the
        assist lock's escape hatch, kept by the server like any received item."""
        return any(item.item - ITEM_ID_BASE == ASSIST_UNLOCK for item in self.items_received)

    def received(self) -> tuple[list[int], list[str]]:
        """The Zexion item code of each received item, and who sent it ("" for this slot's
        own), kept in step: anything that is not a Zexion item is left out of both (and said
        once a connection)."""
        codes, senders = [], []
        warned = self.conn.warned if self.conn is not None else set()
        for index, item in enumerate(self.items_received):
            code = item.item - ITEM_ID_BASE
            if code == ASSIST_UNLOCK:
                continue  # not for the inventory: see assists_unlocked
            code = ids.DRAWN_AS.get(code, code)  # the Intel Chip: the game holds an Area Map
            if code in ids.GAME_ITEMS:
                codes.append(code)
                senders.append(
                    "" if item.player == self.slot else self.player_names.get(item.player, "")
                )
            elif index not in warned:
                warned.add(index)
                logger.warning(f"Received item {item.item}, which is not a Zexion item; ignored.")
        return codes, senders


class Poll:
    """What one pass of the game loop works from, worked out once: the received items' codes
    and senders (ZexionContext.received), and this world's locations found (own_found)."""

    def __init__(self, ctx: ZexionContext) -> None:
        self.codes, self.senders = ctx.received()
        self.found = own_found(ctx)


def server_open(ctx: ZexionContext) -> bool:
    """Whether a message can go to the server now: CommonContext.send_msgs drops one silently
    while the socket is not open (the same test as its own)."""
    server = ctx.server
    return bool(server and server.socket and server.socket.open and not server.socket.closed)


async def store(
    ctx: ZexionContext,
    key: str,
    op: str,
    value: object,
    default: object,
    what: str,
    reply: bool = False,
) -> bool:
    """One operation on a data storage key (``Set``); False, warned with ``what``, when the send
    failed (a closing connection), for the caller to send it again."""
    if not server_open(ctx):
        return False  # send_msgs would drop it without a word
    try:
        await ctx.send_msgs(
            [
                {
                    "cmd": "Set",
                    "key": key,
                    "default": default,
                    "want_reply": reply,
                    "operations": [{"operation": op, "value": value}],
                }
            ]
        )
    except Exception as exc:
        logger.warning(f"{what} failed ({exc}).")
        return False
    return True


def stored_indices(ctx: ZexionContext, key: str | None) -> set[int]:
    """The location indices a data storage dict holds as its keys. The key is open to any
    client in the room: anything but an index is skipped."""
    stored = ctx.stored_data.get(key) if key else None
    if not isinstance(stored, dict):
        return set()
    return {int(k) for k in stored if isinstance(k, str) and k.isdigit() and int(k) < ids.LOCATIONS}


def hints_used(ctx: ZexionContext) -> dict[str, tuple[int, int]] | None:
    """The entry each Area Map used, by its key (client/hints.py): the server's, with this
    client's on top until the server has them; None until the server's have been read."""
    if ctx.conn is None or ctx.conn.hints_key not in ctx.stored_data:
        return None
    return stored_hints(ctx) | (ctx.seed_state.hints_mine or {})


def stored_hints(ctx: ZexionContext) -> dict[str, tuple[int, int]]:
    """The server's stored entries, each a key and two ints; anything else is skipped (the
    key is open to any client in the room)."""
    stored = ctx.stored_data.get(ctx.conn.hints_key) if ctx.conn else None
    if not isinstance(stored, dict):
        return {}
    out = {}
    for k, v in stored.items():
        if (
            isinstance(k, str)
            and isinstance(v, list | tuple)
            and len(v) == 2
            and all(isinstance(x, int) and not isinstance(x, bool) for x in v)
        ):
            out[k] = (v[0], v[1])
    return out


def owned_from_locations(ctx: ZexionContext, p: Poll) -> set[tuple[int, int]]:
    """The (player, location) of each item the player has from a location: received from
    another world, or found in one of this world's locations (own_found)."""
    owned = {(item.player, item.location) for item in ctx.items_received}
    owned |= {(ctx.slot, LOCATION_ID_BASE + i) for i in p.found}
    return owned


async def mission_tick(ctx: ZexionContext, p: Poll, step: session.Step) -> None:
    """With the OS Install Disks goal, once the player has enough disks: the reminder to save at
    the landing pod (discs.POD_NOTE), once per load of this seed's save, after the disks'
    own popups; then, after each save at the landing pod (the seed's start), RETURN HOME WITH
    THE DISKS? (native/src/mission.h), whose YES starts the game's ending. Never once that save
    has reached the ending (flag 250)."""
    conn, helper, told = ctx.conn, ctx.helper, ctx.told
    home = ctx.load.return_home
    if (
        conn is None
        or conn.hunt is None
        or helper is None
        or not step.seed_matches
        or step.state != game.IN_PLAY
        or step.ended
        or not conn.items_listed
        or not step.save_stamp
    ):
        return
    hunt = conn.hunt
    enough = discs.owned(hunt, p.codes, p.found) >= hunt.required
    home.see(step.save_stamp, step.at_start, enough, step.in_start_cell)
    if not enough or not told.client_back_sent:
        return
    # After the found disks' count popups (followups_tick), so the reminder comes last.
    done = popped(ctx)
    if done is None or any(i in hunt.locations for i in ctx.load.save_collected - done):
        return
    if home.remind(enough):
        note = [(ids.DATA_DISC, discs.POD_NOTE, True)]
        reply = await asyncio.to_thread(helper.apply, native.batch_text([], note))
        if reply != "applied":
            return
        home.reminded = True
        detail(
            "Enough OS Install Disks: save at the landing pod (the seed's start) to be offered "
            "RETURN HOME WITH THE DISKS?."
        )
    if not home.offer:
        return
    reply = await asyncio.to_thread(helper.request, "mission_prompt")
    if reply != "queued":
        return
    home.offer = False
    detail(
        "Saved at the landing pod with enough OS Install Disks: RETURN HOME WITH THE DISKS? is "
        "offered. YES starts the game's ending, which sends the goal; NO plays on, and it is "
        "offered again after the next save there."
    )


async def announced_tick(ctx: ZexionContext, conn: state.Connection, p: Poll) -> None:
    """Received items' popups, kept across connections (client/state.py): the session starts
    from the server's count of those shown, once it has been read (a slot with none stored
    counts every item it holds as shown, and stores that at once, so the key exists); and the
    count is stored again as the helper shows them (the larger one wins, for two clients of a
    slot). The helper is asked how many are still waiting only while more have been queued than
    the server holds as shown."""
    if conn.announced_key not in ctx.stored_data or not conn.items_listed:
        return
    raw = ctx.stored_data.get(conn.announced_key)
    current = conn.session
    if current.announced is None:
        # Counted as received() counts: Zexion items only.
        queued, conn.announced_shown = state.announced_start(raw, len(p.codes), conn.queued_before)
        current.mark_announced(queued)
    queued, shown = current.announced, conn.announced_shown or 0
    helper = current.helper  # None once the session gave up on it and wrote directly
    if helper is None:
        shown = queued  # nothing waits in a helper: direct writes show no popups
    elif state.popups_unstored(queued, raw, conn.announced_sent):
        pending = await asyncio.to_thread(helper.popups_pending)
        if pending is not None:  # else no frame took it: the count known stays
            shown = state.popups_shown(queued, pending, shown)
    conn.announced_shown = shown
    count = state.announced_to_store(shown, raw, conn.announced_sent)
    if count is None:
        return
    if await store(ctx, conn.announced_key, "max", count, 0, "Recording the item popups shown"):
        conn.announced_sent = count  # else sent again on the next poll


def popped(ctx: ZexionContext) -> set[int] | None:
    """The location indices whose follow-up popup has been shown (the server's record and this
    client's), or None until the server's record has been read."""
    conn = ctx.conn
    if conn is None or conn.popped_key not in ctx.stored_data:
        return None
    return conn.popped_mine | stored_indices(ctx, conn.popped_key)


async def other_save_tick(ctx: ZexionContext, step: session.Step) -> None:
    """Once a load: a randomizer save of another seed in play. The helper tells the player
    nothing is sent or received, and gives the save its look if it knows the seed
    (other_save; client/offline.py)."""
    helper, told = ctx.helper, ctx.told
    if helper is None:
        return
    if not step.other_seed:
        # Asked again for the next such save: the helper says it once a load, and again after
        # a reconnect (Game::other_save).
        told.other_save_sent = False
        return
    if told.other_save_sent or step.state != game.IN_PLAY:
        return
    reply = await asyncio.to_thread(helper.request, "other_save")
    if not reply.startswith("refused"):
        told.other_save_sent = True
        logger.info("Another seed's save is loaded: the game says nothing is sent or received.")


async def client_back_tick(ctx: ZexionContext, step: session.Step) -> None:
    """Once per attach, when this seed's save is first seen in play: if the helper dressed it
    with no client (client/offline.py), it pops up ARCHIPELAGO CLIENT CONNECTED. (client_back)."""
    helper, told = ctx.helper, ctx.told
    if helper is None or told.client_back_sent or not step.seed_matches:
        return
    if step.state != game.IN_PLAY:
        return
    reply = await asyncio.to_thread(helper.request, "client_back")
    if not reply.startswith("refused"):
        told.client_back_sent = True
        if reply == "shown":
            detail("This save was played without the client: told the client is back.")


async def followups_tick(ctx: ZexionContext, p: Poll, step: session.Step) -> None:
    """The popup after the game's own for each of this world's finds that has one: an OS Install
    Disk's count, an Intel Chip's (or Area Map's) hint. Shown once for each, for the loaded
    save's collected locations, whenever it was found: in play, before this connection, or with
    no client at all; the server keeps which were shown (popped_key), so none is skipped and
    none shown twice."""
    conn, helper, told = ctx.conn, ctx.helper, ctx.told
    if helper is None or conn is None or not conn.items_listed or not step.seed_matches:
        return
    if not told.client_back_sent:
        return  # after the helper has been asked to say the client is back (client_back_tick)
    done = popped(ctx)
    if done is None:
        return  # the server's record not read yet
    waiting = sorted(ctx.load.save_collected - done)
    if not waiting:
        return
    hunt, plan = conn.hunt, conn.plan
    disks = [i for i in waiting if hunt is not None and i in hunt.locations]
    maps = [i for i in waiting if plan.location_items[i] == ids.AREA_MAP and conn.hint_targets]
    used = hints_used(ctx) or {}
    maps = [i for i in maps if f"l{i}" in used]  # its hint assigned (hints_tick)
    if not disks and not maps:
        return
    popups: list[tuple[int, str, bool]] = []
    if disks:
        have = discs.owned(hunt, p.codes, p.found)
        # The ones shown now are the latest found: they count up to the count as it is.
        first = have - len(disks) + 1
        told_count = hunt.required if conn.discs_told else None
        popups += [
            (ids.DATA_DISC, discs.note(first + k, told_count), True) for k in range(len(disks))
        ]
    popups += [(ids.AREA_MAP, hint_note(ctx, used[f"l{i}"]), True) for i in maps]
    reply = await asyncio.to_thread(helper.apply, native.batch_text([], popups))
    if reply != "applied":
        return  # not shown: tried again
    shown = set(disks) | set(maps)
    conn.popped_mine |= shown  # this client's record holds them until the server has them
    value = {str(i): 1 for i in shown}
    await store(ctx, conn.popped_key, "update", value, {}, "Recording the popups shown")


def own_found(ctx: ZexionContext) -> list[int]:
    """This world's locations the player has found, as indices: those this client has reported
    from the game's map, and those any client of this slot has recorded as found in the
    server's data storage (store_found), so another save of the seed counts them too. Not every
    location the server has as checked, which after a release (on the goal) is all of them: a
    release gives no OS Install Disk, and no Area Map hint, the player did not find."""
    found = {loc - LOCATION_ID_BASE for loc in ctx.locations_checked if loc >= LOCATION_ID_BASE}
    key = ctx.conn.found_key if ctx.conn else None
    return sorted(found | stored_indices(ctx, key))


async def store_found(
    ctx: ZexionContext, conn: state.Connection, in_game: set[int], new: set[int]
) -> None:
    """Record this world's locations found in the game (indices: the loaded save's, ``new``
    just found) in the server's data storage (own_found). Once the server's record has been
    read, every one found that it lacks is sent, again every FOUND_RESEND_SECONDS while it
    lacks them: a store lost with a closing connection heals without a restart (the server's
    checked locations, which a reconnect starts from, are never reported again)."""
    key = conn.found_key
    stored = stored_indices(ctx, key) if key in ctx.stored_data else None
    due = time.monotonic() >= conn.found_sent_at + FOUND_RESEND_SECONDS
    locations = state.found_to_store(in_game, stored, new, due)
    if not locations:
        return
    conn.found_sent_at = time.monotonic()
    value = {str(i): 1 for i in sorted(locations)}
    if not await store(ctx, key, "update", value, {}, "Recording the locations found"):
        conn.found_sent_at = 0.0  # sent again on the next poll


async def hints_tick(ctx: ZexionContext, p: Poll) -> None:
    """Give each Area Map the player has and has not used yet the next item to hint
    (hints.assign), create the Archipelago hints, and store which entry each used. The hint of
    one found in this world pops up from followups_tick."""
    conn, seed = ctx.conn, ctx.seed_state
    if conn is None or not conn.hint_targets or not conn.items_listed:
        return  # no hints, or the server's item list not here yet (what the player owns)
    used = hints_used(ctx)
    if used is None:
        return  # the server's not read yet
    keys = hints.area_maps(p.codes, conn.plan.location_items, p.found)
    new = hints.assign(conn.hint_targets, used, keys, owned_from_locations(ctx, p))
    stored = stored_hints(ctx)
    if not new:
        behind = seed.hints_mine and any(k not in stored for k in seed.hints_mine)
        if behind and time.monotonic() >= seed.hints_sent_at + HINTS_RESEND_SECONDS:
            await store_hints(ctx, conn)  # the server has not taken the last ones yet
        return
    by_player: dict[int, list[int]] = {}
    for player, location in new.values():
        if (player, location) != hints.NONE_LEFT:
            by_player.setdefault(player, []).append(location)
    try:
        for player, locations in by_player.items():
            await ctx.send_msgs([{"cmd": "CreateHints", "player": player, "locations": locations}])
    except Exception as exc:  # a closing connection: tried again once connected
        logger.warning(f"Creating the Area Map hints failed ({exc}); trying again.")
        return
    seed.hints_mine = (seed.hints_mine or {}) | new
    await store_hints(ctx, conn)


async def store_hints(ctx: ZexionContext, conn: state.Connection) -> None:
    seed = ctx.seed_state
    seed.hints_sent_at = time.monotonic()
    value = {k: list(v) for k, v in (seed.hints_mine or {}).items()}
    # Sent again until the server echoes them back (hints_tick).
    await store(ctx, conn.hints_key, "update", value, {}, "Storing the Area Map hints", reply=True)


def hint_note(ctx: ZexionContext, entry: tuple[int, int]) -> str:
    """An entry's popup text (hints.popup_note), or NO HINTS LEFT."""
    code_at = {(p, loc): code for p, loc, code in ctx.conn.hint_targets}
    if tuple(entry) == hints.NONE_LEFT or tuple(entry) not in code_at:
        return hints.NO_HINT_NOTE
    player, location = entry
    item = ctx.item_names.lookup_in_slot(ITEM_ID_BASE + code_at[(player, location)], ctx.slot)
    place = ctx.location_names.lookup_in_slot(location, player)
    whose = None if player == ctx.slot else ctx.player_names.get(player, "ANOTHER PLAYER")
    return hints.popup_note(item, place, whose)


def hint_notes(ctx: ZexionContext, codes: list[int]) -> dict[int, str]:
    """The popup text of each received Area Map whose hint is known, by its position among
    ``codes``."""
    used = hints_used(ctx) if ctx.conn and ctx.conn.hint_targets else None
    if not used:
        return {}
    return {
        index: hint_note(ctx, used[key])
        for index, key in hints.received_area_maps(codes).items()
        if key in used
    }


async def attach_helper(ctx: ZexionContext) -> None:
    """Load and configure the native helper in the attached game, or carry on without it."""
    dll = native.default_dll()  # used when the game has none loaded (no proxy in its folder)
    notes: list[str] = []
    try:
        helper = await asyncio.to_thread(native.attach, ctx.game_conn, dll, notes)
    except native.HelperError as exc:
        if ctx.said("helper", str(exc)):  # tried every 30 s: said again only when it changes
            logger.warning(
                f"The in-game helper could not be used ({exc}); items arrive with no popup."
            )
        ctx.drop_helper()
        return
    ctx.said.clear("helper")
    # A helper still loaded from before may hold a lock, labels (taken back unless this seed's
    # save is loaded, where this seed's are sent over them) and so on: it is told everything
    # again, and told the client is back if the save was played offline.
    ctx.helper = helper
    ctx.told = state.HelperState(skip_check_at=time.monotonic() + SKIP_CHECK_SECONDS)
    if ctx.conn is not None:
        ctx.conn.session.helper = helper
    ctx.load.return_home = discs.ReturnHome()  # and any reminder or prompt of the one before
    for note in notes:
        # Where the helper came from is a detail; a copy to update is not.
        (detail if note.startswith("The helper was loaded with the game") else logger.info)(note)
    logger.info(f"The in-game helper {native.VERSION} is running.")


# The settings the helper is told to turn on or off while this seed's save is loaded: its
# requests and the replies that say it took them (off, on), and what is said when it does.
ASSISTS = (
    ("assists free", "assists lock"),
    ("free", "locked"),
    ("The assist menu is unlocked.", "The assist menu is locked for this seed."),
)
INTEL = (
    ("intel off", "intel on"),
    ("intel off", "intel on"),
    (None, "The Area Maps are Intel Chips for this seed."),
)


async def toggle_tick(ctx: ZexionContext, toggle: state.Toggle, want: bool, spec: tuple) -> None:
    """Tell the helper to turn a setting on or off (ASSISTS: the options menu's ASSIST entry
    locked, docs/re/memory.md "The assist menu"; INTEL: the game's Area Map named INTEL CHIP,
    its map reveal off, "Intel Chips"). Sent when that changes, or to a new helper."""
    helper = ctx.helper
    if helper is None or want == toggle.sent or time.monotonic() < toggle.retry_at:
        return
    requests, replies, notes = spec
    reply = await asyncio.to_thread(helper.request, requests[want])
    if reply == replies[want]:
        toggle.sent = want
        if notes[want]:
            detail(notes[want])
    else:
        toggle.retry_at = time.monotonic() + REFUSED_RETRY_SECONDS


async def discs_helper_tick(ctx: ZexionContext, p: Poll) -> None:
    """With the OS Install Disks goal, give the helper the count and the scanner's line while this
    seed's save is loaded (docs/re/memory.md, "The scanner"), and take them back otherwise.
    Sent when that changes, or to a new helper."""
    helper, told, conn = ctx.helper, ctx.told, ctx.conn
    if helper is None or time.monotonic() < told.discs_retry_at:
        return
    hunt = conn.hunt if conn is not None else None
    if hunt is not None and ctx.load.seed_loaded and not conn.items_listed:
        return  # the items not listed yet, or a dropped connection: the count waits for them
    if hunt is not None and ctx.load.seed_loaded:
        have = discs.owned(hunt, p.codes, p.found)
        want = discs.request(have, hunt.required, conn.discs_told)
    else:
        want = discs.OFF
    if want == told.discs_sent:
        if want != discs.OFF and time.monotonic() >= told.discs_status_at:
            # Whether the equipment screen showed the count when last opened (logged on change).
            told.discs_status_at = time.monotonic() + REFUSED_RETRY_SECONDS
            status = await asyncio.to_thread(helper.request, "discs_status")
            if status != told.discs_status:
                told.discs_status = status
                detail(f"The equipment screen's OS Install Disk count: {status}.")
        return
    reply = await asyncio.to_thread(helper.request, want)
    if reply == ("discs off" if want == discs.OFF else "discs on"):
        if want == discs.OFF and told.discs_sent not in (None, discs.OFF):
            detail("Not this seed's save: the scanner's message is the game's again.")
        elif want != discs.OFF and told.discs_sent in (None, discs.OFF):
            detail("The scanner's message names the OS Install Disks needed.")
        told.discs_sent = want
    else:
        told.discs_retry_at = time.monotonic() + REFUSED_RETRY_SECONDS


async def slots_tick(ctx: ZexionContext, p: Poll) -> None:
    """With the OS Install Disks goal, give the helper the disk count for the file select's
    entries of the slots holding this seed (read from the game every few seconds), and take it
    back otherwise."""
    helper, told, conn = ctx.helper, ctx.told, ctx.conn
    if helper is None or time.monotonic() < told.slots_read_at:
        return
    told.slots_read_at = time.monotonic() + SLOTS_READ_SECONDS
    hunt = conn.hunt if conn is not None else None
    g = ctx.game_conn
    want = discs.SLOTS_OFF
    if hunt is not None and not conn.items_listed:
        return  # the items not listed yet, or a dropped connection: the count waits for them
    if hunt is not None and g is not None:
        try:
            markers = await asyncio.to_thread(g.slot_markers)
            copies = await asyncio.to_thread(g.rando_slot_copies)
        except game.GameError:
            return  # read again in a moment
        slots = discs.seed_slots(markers, copies, conn.plan)
        if slots:
            have = discs.owned(hunt, p.codes, p.found)
            want = discs.slots_request(have, hunt.required, slots)
    if want == told.slots_sent:
        return
    reply = await asyncio.to_thread(helper.request, want)
    if reply == ("disk_slots off" if want == discs.SLOTS_OFF else "disk_slots on"):
        told.slots_sent = want


def foreign_labels(ctx: ZexionContext) -> tuple[dict[tuple[int, int], str], int] | None:
    """The label of each map cell holding another player's item (or this player's own disk),
    and how many are this player's own disks; None until the server has said what each holds."""
    conn = ctx.conn
    if conn is None:
        return None
    infos = [ctx.locations_info.get(LOCATION_ID_BASE + i) for i in conn.foreign]
    if any(info is None for info in infos):
        return None
    labels: dict[tuple[int, int], str] = {}
    own = 0
    for i, info in zip(conn.foreign, infos, strict=True):
        player = ctx.player_names.get(info.player, "")
        slot = ctx.slot_info.get(info.player)
        code = info.item - ITEM_ID_BASE
        if info.player == ctx.slot and code in ids.GAME_ITEMS:
            text = aplabels.own_label(code, discs.NAME)  # this player's own OS Install Disk
            own += 1
        elif slot is not None and slot.game == GAME and aplabels.zexion_look(code) is not None:
            # Another Zexion player's own item, whatever that player's settings.
            item, name = aplabels.zexion_look(code)
            text = aplabels.zexion_label(item, player, name)
        else:
            text = aplabels.label(bool(info.flags & ItemClassification.progression), player)
        labels[tuple(conn.session.cells[i])] = text
    return labels, own


def config_file(g: game.Game) -> str:
    """The config file for the attached game's build (client/offline.py): the addresses, and
    the first bytes of each function among them, read from the game."""
    keys = [k for k in native.CONFIG_KEYS if k not in offline.NOT_CHECKED]
    return offline.config_text(
        native.config_text(dict(g.rva)), g.code_checks(keys, offline.CHECK_BYTES)
    )


async def offline_config(ctx: ZexionContext) -> str | None:
    """The config file, built once per attach to the game (in a thread: it reads ~66 places
    in the game); None while no game is attached, or if building it failed (tried again)."""
    g = ctx.game_conn
    if g is None:
        return None
    if ctx.offline_config is not None and ctx.offline_config[0] is g:
        return ctx.offline_config[1]
    try:
        text = await asyncio.to_thread(config_file, g)
    except (native.HelperError, game.GameError):
        return None
    ctx.offline_config = (g, text)
    return text


def offline_seed(ctx: ZexionContext, p: Poll) -> str | None:
    """This seed's file for the helper to dress its saves without the client (client/
    offline.py), or None while it is not known yet (the items or the labels not listed yet)."""
    seed = None
    conn = ctx.conn
    if conn is not None and conn.items_listed:
        built = foreign_labels(ctx) if conn.foreign else ({}, 0)
        if built is not None:
            labels = aplabels.labels_request(built[0]) if built[0] else None
            hunt = conn.hunt
            disc_info = None
            if hunt is not None:
                have = discs.owned(hunt, p.codes, p.found)
                unfound = offline.grid_cells(conn.session.cells, hunt.locations - set(p.found))
                disc_info = (have, hunt.required, discs.scan_line(hunt.required), unfound)
            seed = offline.seed_text(
                conn.plan.location_items,
                conn.plan.start,
                conn.intel_chip,
                conn.lock_assists,
                disc_info,
                labels,
            )
    return seed


async def offline_tick(ctx: ZexionContext, p: Poll) -> None:
    """Every few seconds, send the helper what it needs without the client (client/offline.py)
    when it has changed: the addresses for this build, and this seed's file. The helper writes
    them (``offline_put``), from inside the game, so they land where the game reads them."""
    helper, told = ctx.helper, ctx.told
    if helper is None or time.monotonic() < told.offline_at:
        return
    told.offline_at = time.monotonic() + OFFLINE_WRITE_SECONDS
    seed_name = ctx.server_seed_name or ctx.seed_name  # the server's (RoomInfo)
    if not seed_name or not ctx.slot:
        return
    config, seed = await offline_config(ctx), offline_seed(ctx, p)
    name = offline.seed_file_name(seed_name, ctx.team, ctx.slot)
    for kind, text, file in (("config", config, ""), ("seed", seed, name)):
        if text is None or told.offline_sent.get(kind) == text:
            continue
        if len(text) > offline.MAX_FILE:
            reply = f"error larger than {offline.MAX_FILE} bytes"
        else:
            try:
                reply = await asyncio.to_thread(
                    helper.request, offline.put_request(kind, text, file)
                )
            except native.HelperError as exc:
                reply = f"error {exc}"
        if reply in ("stored", "unchanged"):
            told.offline_sent[kind] = text
            ctx.offline_failed = False
        elif not ctx.offline_failed:
            logger.warning(f"The helper did not keep the offline {kind} file: {reply}")
            ctx.offline_failed = True


async def labels_tick(ctx: ZexionContext) -> None:
    """Once the server has said what each location holding another player's item holds, send
    the helper a label per map cell (client/aplabels.py), while this seed's save is loaded. While
    another save (or none) is loaded, and after a disconnect, take them back."""
    helper, told, conn = ctx.helper, ctx.told, ctx.conn
    if helper is None:
        return
    if conn is None or not conn.labels_want:
        if not told.labels_held or time.monotonic() < told.labels_retry_at:
            return
        ours = conn is not None and told.labels_for is conn
        reply = await asyncio.to_thread(helper.request, "ap_labels")  # none: Scrap Energy again
        if reply.startswith("labeled"):
            told.labels_for, told.labels_held = None, False
            if ours:
                detail("Not this seed's save: other players' items are Scrap Energy again.")
        else:
            told.labels_retry_at = time.monotonic() + REFUSED_RETRY_SECONDS  # asked again later
        return
    if (
        told.labels_for is conn
        or told.labels_refused
        or not conn.foreign
        or time.monotonic() < told.labels_retry_at
    ):
        return
    built = foreign_labels(ctx)
    if built is None:
        return  # the server has not answered yet
    labels, own = built
    reply = await asyncio.to_thread(helper.request, aplabels.labels_request(labels))
    if conn is not ctx.conn:
        return  # a reconnect meanwhile: the next tick sends what is current
    if reply.startswith("labeled"):
        told.labels_for, told.labels_held = conn, True
        detail(
            f"Other players' items show as Archipelago items, named at pickup "
            f"({len(labels) - own})"
            + (f"; this world's own OS Install Disks as themselves ({own})." if own else ".")
        )
    elif reply.startswith("refused"):
        told.labels_retry_at = time.monotonic() + REFUSED_RETRY_SECONDS  # no frame came yet
    else:
        logger.warning(f"The helper did not take the item labels: {reply}")
        told.labels_refused = True  # not asked again until a helper is attached again


async def shields_tick(ctx: ZexionContext, received: list[int]) -> None:
    """Spawn received Energy Shields not given yet, one at a time, through the helper, and
    count each in the server's data storage once the game shows it up in play (client/
    shields.py)."""
    conn, game_conn, sh = ctx.conn, ctx.game_conn, ctx.shields
    if conn is None or conn.shield_key not in ctx.stored_data or game_conn is None:
        return  # no game, or the count not read from the server yet
    key = conn.shield_key
    if sh.resend_due(key, ctx.stored_data.get(key) or 0, time.monotonic()):
        await send_shield_count(ctx)
    pending = sh.current(key)
    if pending is not None:
        await confirm_shield(ctx, game_conn, pending, key)
        return
    helper = ctx.helper
    if helper is None or time.monotonic() < sh.retry_at:
        return
    if conn.session.holding:
        return  # the ending began in this load: on the next load, as other items (session.py)
    given = sh.given_count(key, ctx.stored_data.get(key) or 0)
    if reconcile.shields_owed(received, given) <= 0:
        return
    reply = await asyncio.to_thread(helper.request, "shield")
    if ctx.conn is not None and ctx.conn.shield_key != key:
        return  # another slot now: the spawn is that game's, not this seed's
    if reply == "spawned":
        sh.spawned(key, time.monotonic())
    elif sh.refused(reply, time.monotonic()):
        detail(f"A received Energy Shield is waiting: {reply}")


async def confirm_shield(
    ctx: ZexionContext, game_conn: game.Game, pending: shields.Pending, key: str
) -> None:
    """One look at the game for a spawned shield: count it, wait, or let it be spawned again.
    The state is settled again right before the shield is read (not the step's, which helper
    requests since may have outlasted: a death or reload then would count a lost shield)."""
    sh = ctx.shields

    def look() -> tuple[str, bool, str]:
        state_now = game_conn.settled_state()
        return (state_now, *shields.look(game_conn, state_now))

    try:
        state_now, up, failed = await asyncio.to_thread(look)
    except game.GameError:  # the state could not be read: look again; the loop sees a close
        state_now, up, failed = game.CHANGING, False, ""

    if failed:
        said = f"check failed: {failed}"
        if said != sh.said:
            logger.warning(f"Reading the player's Energy Shield failed ({failed}).")
        sh.said = said
    stored = ctx.stored_data.get(key) or 0
    outcome = sh.looked(pending, state_now, up, key, stored, time.monotonic())
    if outcome == shields.BACKOFF:
        logger.warning(
            f"{shields.MISSES} received Energy Shields in a row were not seen in the game; "
            f"trying again in {shields.BACKOFF_SECONDS:.0f} seconds."
        )
    elif outcome == shields.AGAIN:
        detail("A received Energy Shield was not seen in the game; spawning it again.")
    elif outcome == shields.UP:
        detail("A received Energy Shield is up.")
        await send_shield_count(ctx)


async def send_shield_count(ctx: ZexionContext) -> None:
    """Send the shields counted as given to the server as a maximum, so sending it twice does
    no harm; shields_tick sends it again until the server's number reaches it."""
    sh = ctx.shields
    if sh.given is None:
        return
    key, count = sh.given
    sh.count_sent_at = time.monotonic()
    await store(ctx, key, "max", count, 0, "Sending the Energy Shield count", reply=True)


async def deathlink_tick(ctx: ZexionContext, step: session.Step) -> None:
    """Send a death of the player's own to the other players, and deal the ones received from
    them, queued, through the helper, one each time the player is in play (client/deathlink.py)."""
    seed = ctx.seed_state
    deaths = seed.deaths
    now = time.monotonic()
    state_now = game.NO_SAVE if step.left_seed else step.state
    if deaths.observe(state_now, now):
        try:
            await ctx.send_death(f"{ctx.player_names.get(ctx.slot, 'A player')} died in Zexion.")
        except Exception as exc:  # a closing connection: this death is not sent
            logger.warning(f"DeathLink: sending the death failed ({exc}).")
    if step.goal and deaths.owed:
        deaths.ending()
        detail("DeathLink: the ending began, so the received deaths queued are dropped.")
    helper = ctx.helper
    if not deaths.owed or helper is None or state_now != game.IN_PLAY or now < seed.death_retry_at:
        return
    deaths.dealing(now)  # a death seen after a lost reply is this one
    reply = await asyncio.to_thread(helper.request, "die", native.APPLY_SECONDS)
    if reply == "died":
        deaths.dealt(time.monotonic())
        seed.death_said = ""
        more = f" ({deaths.owed} more queued)" if deaths.owed else ""
        detail(f"DeathLink: the received death was dealt{more}.")
        return
    deaths.refused()
    if reply != seed.death_said and reply != "refused not settled":
        detail(f"DeathLink: the received death is waiting: {reply}")
    seed.death_said = reply
    seed.death_retry_at = time.monotonic() + DEATH_RETRY_SECONDS


async def skip_tick(ctx: ZexionContext) -> None:
    """Once per helper, a moment after it attaches: say whether it offers the escape skip (it
    patches the game's event within 31 frames, or leaves an event it does not recognize)."""
    helper, told = ctx.helper, ctx.told
    if helper is None or told.skip_check_at is None or time.monotonic() < told.skip_check_at:
        return
    told.skip_check_at = None
    hello = await asyncio.to_thread(helper.hello)
    if hello.get("configured") != "1":
        told.skip_check_at = time.monotonic() + SKIP_CHECK_SECONDS  # no frame came: ask again
        return
    if hello.get("skip") == "1":
        goal = (
            "the goal is the OS Install Disks"
            if ctx.conn is not None and ctx.conn.hunt is not None
            else "the goal is sent when the ending begins, either way"
        )
        detail(
            f"Grabbing Zexion offers SKIP ESCAPE AND FINAL BATTLE? while the helper runs; {goal}."
        )
    else:
        logger.warning(
            "The helper could not offer the escape skip (the game's event is not as expected): "
            "a first playthrough must finish the escape, which needs items the seed's logic "
            "does not ask for (for example the Zenith Thrusters)."
        )


async def newgame_tick(ctx: ZexionContext) -> None:
    """Arm the helper with the AP seed for the next randomizer new game, and report what
    became of it. Runs in the game loop, the helper's only user."""
    helper = ctx.helper
    if helper is None:
        return
    if ctx.disarm_pending:
        ctx.disarm_pending = False
        await asyncio.to_thread(helper.request, "disarm")
    conn = ctx.conn
    if conn is None or conn.newgame in (state.NewGame.DONE, state.NewGame.UNAVAILABLE):
        return
    if conn.newgame is None:
        if time.monotonic() < conn.newgame_retry_at:
            return  # refused a moment ago
        plan = conn.plan
        build = ctx.game_conn.build.sha256 if ctx.game_conn is not None else ""
        choice = plan.menu is not None and not ctx.keep_randomizer and not conn.choice_refused
        for offer in (True, False) if choice else (False,):  # ARCHIPELAGO, else RANDOMIZER
            try:
                text = newgame.plan_text(plan, build, offer)
                break
            except newgame.NewGameError as exc:
                if offer:
                    logger.info(f"ARCHIPELAGO cannot set up this seed ({exc}); RANDOMIZER stays.")
                    continue
                conn.newgame = state.NewGame.UNAVAILABLE
                logger.warning(
                    f"This seed cannot start as a new game ({exc}): please report it, with "
                    "the seed's settings."
                )
                return
        reply = await asyncio.to_thread(helper.request, text)
        if reply != "armed":  # refused (no frame yet, say): asked again in a moment
            conn.newgame_retry_at = time.monotonic() + REFUSED_RETRY_SECONDS
            if ctx.said("arm", reply):
                detail(f"The helper did not take the new game's plan yet ({reply}); asking again.")
            return
        ctx.said.clear("arm")
        conn.newgame = state.NewGame.ARMED
        conn.newgame_polled_at = time.monotonic()
        if conn.newgame_said == offer:
            return  # armed again after a new game: said once per connection
        conn.newgame_said = offer
        if offer:
            logger.info(
                "To play, load this seed's save, or start a new game: pick an empty slot "
                "and choose ARCHIPELAGO."
            )
        else:
            logger.info(
                "To play, load this seed's save, or start a new game: in an empty slot "
                "choose RANDOMIZER, pick this seed's mode with no options changed, and "
                "START GAME (the AP seed is written in as the game starts)."
            )
        return
    if time.monotonic() - conn.newgame_polled_at < NEWGAME_POLL_SECONDS:
        return
    conn.newgame_polled_at = time.monotonic()
    status = await asyncio.to_thread(helper.request, "newgame_status")
    if status.startswith("applied"):
        conn.newgame = state.NewGame.DONE
        detail(f"The new game holds the AP seed ({status}).")
    elif status.startswith("refused"):
        conn.newgame = None  # arm again for the next try
        logger.warning(f"No new game with the AP seed: {status[len('refused ') :]}.")
        if "start table" in status:
            conn.choice_refused = True  # the game's own table differs: RANDOMIZER from now on
    elif status == "idle":
        conn.newgame = None  # the helper lost the plan (reloaded): arm again


@contextlib.contextmanager
def helper_failure(ctx: ZexionContext) -> Iterator[None]:
    """Around requests to the helper: one that fails drops it, to be tried again later."""
    try:
        yield
    except native.HelperError as exc:
        logger.warning(f"The in-game helper failed ({exc}); trying it again later.")
        ctx.drop_helper()


async def side_tick(ctx: ZexionContext, name: str, tick: Callable[[], Awaitable[None]]) -> None:
    """Run a tick that the items, the checks and the goal must not wait on: a helper that fails
    is dropped (helper_failure), and any other failure is logged, once for each distinct one,
    and the poll goes on."""
    try:
        with helper_failure(ctx):
            await tick()
    except Exception as exc:
        if ctx.said(f"tick {name}", repr(exc)):
            logger.exception(f"The client's {name} failed ({exc!r}); items and checks go on.")


async def game_loop(ctx: ZexionContext) -> None:
    while not ctx.exit_event.is_set():
        await asyncio.sleep(POLL_SECONDS)
        try:
            await poll(ctx)
        except Exception:  # never the end of the loop: what failed is tried on the next poll
            logger.exception("The Zexion game loop failed; trying again.")
            await asyncio.sleep(2)


async def poll(ctx: ZexionContext) -> None:
    """One pass of the game loop."""
    p = Poll(ctx)
    if ctx.helper is not None and ctx.unload_helper_pending:
        ctx.unload_helper_pending = False
        try:
            reply = await asyncio.to_thread(ctx.helper.request, "unload")
            logger.info(f"The helper was asked to unload: {reply}")
        except native.HelperError as exc:
            logger.warning(f"Unloading the helper failed: {exc}")
    for name, tick in (
        ("new game", lambda: newgame_tick(ctx)),
        ("escape skip check", lambda: skip_tick(ctx)),
        ("item labels", lambda: labels_tick(ctx)),
        ("assist lock", lambda: toggle_tick(ctx, ctx.told.assists, ctx.load.assists_want, ASSISTS)),
        ("Intel Chips", lambda: toggle_tick(ctx, ctx.told.intel, ctx.load.intel_want, INTEL)),
        ("disk count", lambda: discs_helper_tick(ctx, p)),
        ("file select's disk count", lambda: slots_tick(ctx, p)),
    ):
        if ctx.helper is None:
            break  # none, or it failed in the tick before
        await side_tick(ctx, name, tick)
    if ctx.conn is None or not ctx.slot:
        return
    try:
        await offline_tick(ctx, p)  # the helper's files for this seed's saves without the client
    except Exception as exc:  # never in the way of items and checks
        if not ctx.offline_failed:
            logger.warning(f"Writing the helper's offline files failed: {exc!r}")
        ctx.offline_failed = True
    if ctx.game_conn is None:
        try:
            ctx.game_conn = await asyncio.to_thread(game.Game.attach)
        except game.GameNotRunning:
            return
        except game.GameError as exc:
            if ctx.said("game", str(exc)):  # said again only when it changes
                logger.error(f"Cannot use this Zexion: {exc}")
            await asyncio.sleep(10)
            return
        ctx.said.clear("game")
        logger.info("Attached to Zexion.")
        await attach_helper(ctx)
    conn = ctx.conn
    if conn is None or not ctx.slot:
        return  # a disconnect or another connection while the above waited
    current = conn.session
    p = Poll(ctx)  # again: the items may have come while the above waited
    await side_tick(ctx, "popup count", lambda: announced_tick(ctx, conn, p))
    try:
        notes: dict[int, str] = {}
        try:
            await hints_tick(ctx, p)
            notes = hint_notes(ctx, p.codes)
        except Exception as exc:  # the hints must never stop items or checks
            if str(exc) != ctx.hints_failed:
                ctx.hints_failed = str(exc)
                logger.warning(f"Area Map hints failed ({exc!r}); items and checks go on.")
        hunt = conn.hunt
        if hunt is not None:  # a received disc pops up with the count
            notes |= discs.received_notes(hunt, p.codes, p.found, conn.discs_told)
        if ctx.conn is not conn:
            return  # another connection while the hints were stored: its items are not these
        step = await asyncio.to_thread(
            current.step, ctx.game_conn, p.codes, senders=p.senders, notes=notes
        )
    except game.GameNotRunning:
        logger.info("Zexion has closed.")
        ctx.shields.left()
        ctx.load.leave()  # nothing of this game's carries into the next one's
        with contextlib.suppress(Exception):
            ctx.game_conn.pm.close_process()  # the exited game's handle
        ctx.game_conn = None
        ctx.drop_helper()
        return
    except game.GameError as exc:
        logger.error(f"Reading Zexion failed: {exc}")
        await asyncio.sleep(2)
        return
    if ctx.helper is not None and current.helper is None:
        # The session gave up on it (try again later): dropped before the count below, so the
        # popups it had not shown are not counted as shown.
        ctx.drop_helper()
    elif ctx.helper is None and time.monotonic() >= ctx.helper_retry_at:
        ctx.helper_retry_at = time.monotonic() + HELPER_RETRY_SECONDS
        await attach_helper(ctx)
    # The popups the step handed the helper, or the items it wrote directly.
    await side_tick(ctx, "popup count", lambda: announced_tick(ctx, conn, p))
    load = ctx.load
    if step.left_seed:
        ctx.shields.left()
        load.assists_want = load.intel_want = False
    if ctx.conn is not conn:
        return  # a poll from before a reconnect says nothing of this seed
    if step.seed_matches and conn.newgame is state.NewGame.DONE:
        # The new game holds the seed and is in play: arm again for another slot.
        conn.newgame = None
        choice = "RANDOMIZER" if ctx.keep_randomizer else "ARCHIPELAGO"
        detail(f"{choice} is armed again: another empty slot can start this seed.")
    if step.seed_matches:
        load.seed_loaded = True
        conn.discs_told = step.scanned
        load.save_collected = set(step.checked)
        if not step.ended:
            load.ending_unseen = True  # this load is before the ending: one can begin
        if conn.lock_assists and ctx.assists_unlocked():
            conn.lock_assists = False
            logger.info("Assist Unlock received: the assist menu is no longer locked.")
        load.assists_want = conn.lock_assists
        load.intel_want = conn.intel_chip
    elif step.left_seed:
        load.leave()  # reminded again on the next load, and so on
    conn.labels_want = aplabels.labels_wanted(step.seed_matches, step.left_seed, conn.labels_want)
    if conn.death_link_on:
        await side_tick(ctx, "DeathLink", lambda: deathlink_tick(ctx, step))
    if ctx.helper is not None:
        # Another seed's save: told so in the game.
        await side_tick(ctx, "other save's notice", lambda: other_save_tick(ctx, step))
    if not step.seed_matches or ctx.conn is not conn:
        return  # nothing to send, or the connection changed meanwhile
    shield_work = ctx.shields.pending is not None or ctx.shields.given is not None
    if ctx.helper is not None or shield_work:
        await side_tick(ctx, "Energy Shields", lambda: shields_tick(ctx, p.codes))
    if ctx.conn is not conn:
        return  # the connection changed meanwhile: these checks are not its
    found = step.checked & conn.kept
    new = {LOCATION_ID_BASE + i for i in found} - ctx.locations_checked
    if new:
        ctx.locations_checked |= new  # sent again on connecting if this send fails
        try:
            await ctx.check_locations(new)
        except Exception as exc:  # a closing connection
            logger.warning(f"Sending the locations found failed ({exc}); sent on reconnecting.")
    just_found = {loc - LOCATION_ID_BASE for loc in new}
    await side_tick(ctx, "found record", lambda: store_found(ctx, conn, found, just_found))
    if new:
        p.found = own_found(ctx)  # counted from now on, in this poll too
    for name, tick in (
        ("client-back notice", lambda: client_back_tick(ctx, step)),  # CLIENT CONNECTED first
        ("follow-up popups", lambda: followups_tick(ctx, p, step)),
        ("landing pod's RETURN HOME", lambda: mission_tick(ctx, p, step)),
    ):
        if ctx.helper is None:
            break  # none, or it failed in the tick before
        await side_tick(ctx, name, tick)
    if ctx.conn is not conn:
        return  # the connection changed meanwhile: this ending is not its goal
    if step.goal and not conn.goal_sent and not ctx.finished_game:
        # The goal is the ending; with the OS Install Disks goal, only with enough disks.
        have = None if hunt is None else discs.owned(hunt, p.codes, p.found)
        if hunt is not None and not load.ending_unseen:
            pass  # a save that had reached its ending before: not this ending
        elif hunt is None or have >= hunt.required:
            try:
                await ctx.send_msgs([{"cmd": "StatusUpdate", "status": ClientStatus.CLIENT_GOAL}])
            except Exception as exc:  # a closing connection: sent on a later poll
                logger.warning(f"Sending the goal failed ({exc}); trying again.")
                return
            conn.goal_sent = True
            ctx.finished_game = True
            logger.info(
                "The ending began: goal sent."
                if hunt is None
                else f"The ending began with {have} of {hunt.required} OS Install Disks: goal sent."
            )
        elif not load.goal_withheld:
            load.goal_withheld = True  # said once a load
            logger.warning(
                f"The ending began with {have} of {hunt.required} OS Install Disks: not the goal."
            )


def launch(*args: str) -> None:
    async def main(parsed) -> None:
        ctx = ZexionContext(parsed.connect, parsed.password)
        ctx.auth = parsed.name
        ctx.keep_randomizer = parsed.keep_randomizer
        ctx.server_task = asyncio.create_task(server_loop(ctx), name="server loop")
        loop_task = asyncio.create_task(game_loop(ctx), name="Zexion game loop")
        if gui_enabled:
            ctx.run_gui()
        ctx.run_cli()
        await ctx.exit_event.wait()
        loop_task.cancel()
        await ctx.shutdown()

    import colorama

    parser = get_base_parser(description="Zexion Archipelago client.")
    parser.add_argument("--name", default=None, help="Slot name to connect as.")
    parser.add_argument(
        "--keep-randomizer",
        action="store_true",
        help="Do not offer ARCHIPELAGO: start the seed from RANDOMIZER, its mode and START GAME.",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Also show what the client sets up in the game for the seed (for testing).",
    )
    parser.add_argument("url", nargs="?", help="Archipelago connection url")
    parsed = handle_url_arg(parser.parse_args(args), parser=parser)
    global VERBOSE
    VERBOSE = parsed.verbose
    if parsed.url:
        # The name goes in the Connect packet; websockets refuses a URL with a name and no
        # password, so connect to the host and port alone.
        parsed.connect = f"{parsed.url.hostname}:{parsed.url.port or 38281}"
    Utils.init_logging("ZexionClient", exception_logger="Client")
    colorama.just_fix_windows_console()
    asyncio.run(main(parsed))
    colorama.deinit()


if __name__ == "__main__":
    launch(*sys.argv[1:])
