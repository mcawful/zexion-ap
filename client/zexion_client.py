"""The Zexion Archipelago client: ``python -m client.zexion_client [archipelago://...]``.

Archipelago comes from .ap/Archipelago (or ``ZEXION_AP_DIR``), or is the Launcher's own when
started as Zexion Client. While this seed's save is loaded it sends the locations the map marks
collected, gives items from other worlds, marks other players' items the server has as
checked collected in the save (client/session.py) and sends the goal when the ending begins;
the side tasks with the in-game helper are in client/ticks.py. This world's own items are in
the save's item table, so it asks the server only for other worlds' (items_handling).
"""

from __future__ import annotations

import asyncio
import contextlib
import os
import sys
import time
from collections.abc import Awaitable, Callable, Iterable
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

from . import aplabels, discs, game, ids, linux_helper, native, session, state, ticks  # noqa: E402
from .ids import ASSIST_UNLOCK, ITEM_ID_BASE, LOCATION_ID_BASE  # noqa: E402
from .storage import (  # noqa: E402
    announced_tick,
    hint_notes,
    hints_tick,
    own_found,
    store_found,
)
from .ticks import (  # noqa: E402
    ASSISTS,
    HELPER_RETRY_SECONDS,
    INTEL,
    LINUX_KEEPALIVE_SECONDS,
    Link,
    attach_helper,
    client_back_tick,
    configure_linux,
    deathlink_tick,
    detail,
    discs_helper_tick,
    feats_tick,
    followups_tick,
    helper_link,
    how_to_play,
    labels_tick,
    language_tick,
    linux_mod_lost,
    linux_seed_tick,
    log,
    mission_tick,
    newgame_link_tick,
    offline_tick,
    other_save_tick,
    replay_tick,
    return_home_tick,
    shields_tick,
    toggle_tick,
)

GAME = "Zexion"
POLL_SECONDS = 0.25
BACK_RETRY_SECONDS = 1.0  # between attaches after a time away (poll)
RELOAD_SECONDS = 3.0  # /reload_mod: the game loop's wait while it unloads, then loaded again
HELPER_LOST = (
    f"The Zexion mod stopped responding; trying again in {HELPER_RETRY_SECONDS:.0f} seconds. "
    "Until then items arrive without popups."
)


class ZexionCommandProcessor(ClientCommandProcessor):
    ctx: ZexionContext

    def _cmd_reload_mod(self) -> bool:
        """Reload the Zexion mod in the game, if it misbehaves (popups still waiting are lost;
        items are not)."""
        run = self.ctx.run
        if self.ctx.helper is None:
            if run.mismatched:  # ZexionContext.mismatched
                self.output(
                    "The Zexion mod in the game doesn't match this client: close Zexion, put "
                    "the matching mod in its folder, and start it again."
                )
                return False
            if run.reattach is state.Reattach.LET_GO:  # leave_helper_tick
                self.output("Not connected: the client uses the Zexion mod again once it is.")
                return False
            if game.on_linux(self.ctx.game_conn):
                # The Linux helper is preloaded with the game, for its whole run.
                self.output("On Linux the Zexion mod can't be reloaded: restart Zexion instead.")
                return False
            self.output("The Zexion mod isn't loaded; the client loads it as soon as it can.")
            return False
        run.reload_mod_pending = True
        self.output("Reloading the Zexion mod.")
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
        self.seed_state: state.SeedState | None = None  # set with the first conn, then kept
        self.load = state.LoadState()
        self.game_conn: game.Game | None = None
        self.helper: native.Helper | None = None
        # With the game's Linux build: its helper, for new games, grants (traps among them),
        # labels, the seed's own traps, the language, the disks' count, the finds' follow-ups
        # and RETURN HOME (ticks.configure_linux, and the poll's ticks through ticks.helper_link).
        self.linux_mod: linux_helper.LinuxMod | None = None
        # What the helpers' life with this attached game has come to: the holds, the retries and
        # the failures in a row; started afresh when the game closes (game_closed).
        self.run = state.GameRun()
        # The Linux helper connection the last LinuxMod was configured on (its process, its
        # connection's number): one configured on another had a stretch with no client, in which
        # the helper counted this world's own disks itself (ticks.configure_linux). Kept over a
        # game closing, not in GameRun: a new game's configuration is compared with the last one.
        self.linux_configured_on: tuple | None = None
        # The seed files whose whole text (offline_seed) went to the Linux helper in this client
        # run: from then on the slot data's alone (linux_seed) never replaces it (linux_seed_tick).
        self.linux_full_seeds: set[str] = set()
        # What the Linux helper's folder held under each seed file's name when this client run
        # first looked (linux_stored_seed), so a poorer text put keeps what it had of the whole
        # one (offline.keep_richer): None where there was no file, or it could not be read.
        self.linux_stored_seeds: dict[str, str | None] = {}
        # Warnings and failures that would repeat each poll or retry: each said once.
        self.said = state.SayOnce()
        # The offline config file, built once per attach to the game: (the game, its text).
        self.offline_config: tuple[game.Game, str] | None = None

    async def server_auth(self, password_requested: bool = False) -> None:
        if password_requested and not self.password:
            await super().server_auth(password_requested)
        await self.get_username()
        await self.send_connect()

    def on_package(self, cmd: str, args: dict) -> None:
        conn = self.conn
        if conn is not None and conn.awaiting_backlog and cmd != "Connected":
            # Only the packet right after Connected can be the slot's backlog (the server sends
            # none when it is empty).
            conn.awaiting_backlog = False
            conn.items_listed = True
        if cmd == "Connected":
            self.connected(args["slot_data"])

    def connected(self, slot: dict) -> None:
        identity = (self.server_seed_name or self.seed_name, self.team, self.slot)
        seed = self.seed_state
        try:
            conn = state.Connection.from_slot_data(slot, self.team, self.slot)
            if seed is None or seed.identity != identity:  # another seed or slot: afresh
                seed = state.SeedState(identity, session.Session(conn.plan, conn.intel_chip))
        except ValueError as exc:  # state.UnsupportedSeed, or locations the session can't use
            logger.error(
                "This seed was made with another version of the Zexion APWorld than this "
                "client: generate and play with the same release. Disconnecting."
            )
            log.error(f"Cannot play this seed: {exc}.")
            Utils.async_start(self.disconnect(), name="disconnect")
            return
        # A reconnect to the same seed and slot (a drop, or a Disconnect and Connect) carries on
        # with its SeedState: the session (the ending's hold, the popups handed over), the
        # save dressed and the labels the helper holds for it. Only the popup count is read
        # again, from the server (announced_tick), and nothing is given before this
        # connection's first look at the save.
        seed.session.connected()
        self.seed_state, self.conn = seed, conn
        # Wait for this connection's own read of the popup count and the stored hints (a drop
        # keeps the last connection's stored_data), and of its item list, and for its own first
        # look at the load (what the helper holds for the save stays). Both asked for again in
        # one Get, so one reply brings them: the popup count is never read without the hints,
        # whose notices go with the popups.
        reread = [conn.announced_key, conn.held_key, conn.hints_key]
        for key in reread:
            self.stored_data.pop(key, None)
            self.stored_data_notification_keys.discard(key)
        # Kept by the server across clients: shields given, locations found, follow-ups shown,
        # item popups handed over, and the Area Map hints.
        keys = [conn.shield_key, conn.found_key, conn.popped_key, conn.announced_key, conn.held_key]
        self.set_notify(*keys, *([conn.hints_key] if conn.hint_targets else []))
        self.load.seed_loaded = False
        self.load.disks_counted = None  # a disk picked up meanwhile shows no count
        self.load.return_home = discs.ReturnHome()
        # The labels are sent at once on the new connection, not after a refusal's retry delay:
        # for the Windows helper's record and, on Linux, the Linux helper's own.
        for told in (self.told, self.linux_mod.told if self.linux_mod is not None else None):
            if told is not None:
                told.labels_retry_at = 0.0
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
            # What the slot data says, not a total: the OS Install Disks option, and how many
            # of the disks start with the player (those moved out of the pool among them).
            hunt = conn.hunt
            detail(
                f"The goal is {hunt.required} OS Install Disks (the OS Install Disks option is "
                f"{hunt.placed}; {hunt.start} start with you)."
            )
        detail(f"Seed for Zexion's {conn.mode} mode.")

    async def disconnect(self, allow_autoreconnect: bool = False) -> None:
        conn = self.conn
        if conn is not None and conn.newgame is state.NewGame.ARMED:
            self.run.disarm_pending = True
        # Nothing of the seed's is wanted now: the helper ticks, which run while a helper is
        # attached, take back the labels, the ASSIST lock and the INTEL CHIP name.
        if self.seed_state is not None:
            self.seed_state.dressed = False  # nothing held for the next connection either
        self.conn = None
        self.load.seed_loaded = False
        self.load.save_collected = set()
        self.load.assists_want = self.load.intel_want = False
        await super().disconnect(allow_autoreconnect)

    def reset_server_state(self) -> None:
        # A dropped connection comes here (connection_closed), not through disconnect: the
        # received items are gone until the next connection lists them again, so what counts
        # them waits. What the helper holds for the seed (labels, the disk count, the ASSIST
        # lock, the armed new game) stays for the reconnect (state.SeedState).
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
        if self.helper is None and self.linux_mod is None:  # neither helper: on Linux, its own
            logger.warning(
                "DeathLink: the Zexion mod isn't running, so this death waits until it is."
            )

    def lose_helper(self) -> None:
        """The helper failed: said unless the game closed with it (no news for the player, as
        the next read says the game closed), and dropped."""
        with contextlib.suppress(Exception):
            if self.game_conn is not None and self.game_conn.pm.alive():
                logger.warning(HELPER_LOST)
        self.drop_helper()

    def mismatched(self, exc: native.ModMismatch) -> None:
        """The helper turned out not to match the client after attaching (another build of
        this version): said once, and dropped. Attached again it would only fail again, so it
        is left alone until Zexion restarts (game_closed), as the line says to."""
        line = state.helper_failed(exc)
        if line is not None and self.said("mismatch", line):
            logger.warning(line)
        log.warning(f"The in-game helper doesn't match this client ({exc}); not used again.")
        self.drop_helper()
        self.run.mismatched = True

    def drop_helper(self) -> None:
        if self.helper is not None:
            self.helper.close()
        self.helper = None
        self.told = None
        run = self.run
        run.reload_mod_pending = False  # a request for the helper gone, not the next one
        self.load.feats = state.FeatsCheck()  # the next helper is asked about its own guard
        # A disk picked up while the helper has no client is the helper's to count (its own
        # notice): the next helper's first look takes the disks the save holds for counted.
        self.load.disks_counted = None
        run.helper_retry_at = time.monotonic() + HELPER_RETRY_SECONDS
        conn = self.conn
        # Its popups not shown yet still show if it stays loaded, and are lost if it is gone. A
        # plan the lost helper held is armed again in the next one; a new game just made waits
        # until its save is in play (poll), and a seed that cannot make one stays as it is.
        if conn is not None and conn.newgame is state.NewGame.ARMED:
            conn.newgame = None
        run.disarm_pending = False

    def assists_unlocked(self) -> bool:
        """Whether this slot has received Assist Unlock (from the server console, say): the
        assist lock's escape hatch, kept by the server like any received item."""
        return any(item.item - ITEM_ID_BASE == ASSIST_UNLOCK for item in self.items_received)

    def received(self) -> tuple[list[int], list[str]]:
        """The Zexion item code of each received item, and who sent it ("" for this slot's
        own), kept in step: anything that is not a Zexion item is left out of both (and said
        once a connection). A trap is in both: it is given as its popup (session.py)."""
        codes, senders = [], []
        warned = self.conn.warned if self.conn is not None else set()
        for index, item in enumerate(self.items_received):
            code = item.item - ITEM_ID_BASE
            if code == ASSIST_UNLOCK:
                continue  # not for the inventory: see assists_unlocked
            code = ids.DRAWN_AS.get(code, code)  # the Intel Chip: the game holds an Area Map
            if code in ids.GAME_ITEMS or code in ids.TRAPS:
                codes.append(code)
                senders.append(
                    "" if item.player == self.slot else self.player_names.get(item.player, "")
                )
            elif index not in warned:
                warned.add(index)
                name = self.item_names.lookup_in_game(item.item)
                logger.warning(
                    f"Received {name}, which this client doesn't know; ignored. Is the client "
                    "from the same release as the seed?"
                )
        return codes, senders


class Poll:
    """What one pass of the game loop works from, worked out once: the received items' codes
    and senders (ZexionContext.received), and this world's locations found (own_found)."""

    def __init__(self, ctx: ZexionContext) -> None:
        self.codes, self.senders = ctx.received()
        self.found = own_found(ctx)


def helper_error(ctx: ZexionContext, exc: native.HelperError) -> None:
    """The Windows helper failed in a tick or the step: dropped, and attached again before the
    next step (Reattach.LOST), as a step with no helper would write the items owed with no popup;
    one that doesn't match the client is said so instead, and left alone."""
    if isinstance(exc, native.ModMismatch):
        ctx.mismatched(exc)
        return
    log.warning(f"The in-game helper failed ({exc}); trying it again later.")
    ctx.lose_helper()
    ctx.run.reattach = state.Reattach.LOST


async def side_tick(ctx: ZexionContext, name: str, tick: Callable[[], Awaitable[None]]) -> None:
    """Run a tick that the items, the checks and the goal must not wait on: a helper that fails
    is dropped (the Linux helper, which its own error type says (linux_helper.LinuxHelperError),
    by linux_mod_lost; the Windows helper by helper_error), and any other failure is logged, once
    for each distinct one, and the poll goes on."""
    try:
        await tick()
    except linux_helper.LinuxHelperError as exc:
        linux_mod_lost(ctx, exc)
    except native.HelperError as exc:
        helper_error(ctx, exc)
    except Exception as exc:
        if ctx.said(f"tick {name}", repr(exc)):
            logger.warning(
                f"Something went wrong with the {name} (details in the log); items and checks "
                f"still work. Please report it at {state.REPORT}."
            )
            log.exception(f"The client's {name} failed ({exc!r}); items and checks go on.")


async def link_ticks(
    ctx: ZexionContext, entries: Iterable[tuple[str, Callable[[Link], Awaitable[None]]]]
) -> None:
    """Run the ticks of ``entries`` in turn, each through the helper there is when its turn comes
    (ticks.helper_link), by side_tick. An entry is (name, tick) for either helper, or (name, tick,
    linux): True for a tick of the Linux helper's alone, False for the Windows helper's. None
    there (no helper, one that failed in the tick before, or the Linux helper not on the
    connection it was configured on): the rest are left for the next poll."""
    for name, tick, *only in entries:
        link = helper_link(ctx)
        if link is None:
            break
        if only and only[0] is not link.linux:
            continue
        await side_tick(ctx, name, lambda: tick(link))  # noqa: B023 (run before the next turn)


async def game_loop(ctx: ZexionContext) -> None:
    while not ctx.exit_event.is_set():
        await asyncio.sleep(POLL_SECONDS)
        try:
            await poll(ctx)
        except Exception as exc:  # never the end of the loop: what failed is tried on the next poll
            if ctx.said("loop", repr(exc)):  # said again after a poll that went through
                logger.error(
                    "Something went wrong in the client (details in the log); trying again. "
                    f"Please report it at {state.REPORT}."
                )
                log.exception("The Zexion game loop failed; trying again.")
            await asyncio.sleep(2)
        else:
            ctx.said.clear("loop")


async def keepalive_loop(ctx: ZexionContext) -> None:
    """With the game's Linux build, the Linux helper told the client is still there
    (ticks.linux_keepalive_tick) while the game loop is busy with something else; it does
    nothing with the Windows helper."""
    while not ctx.exit_event.is_set():
        await asyncio.sleep(LINUX_KEEPALIVE_SECONDS)
        with contextlib.suppress(Exception):  # the poll's own requests find what failed
            await ticks.linux_keepalive_tick(ctx)


def shields_due(ctx: ZexionContext, seed: state.SeedState) -> bool:
    """Whether the poll runs shields_tick: with a helper to spawn one (the Windows helper, or the
    Linux helper with the game's Linux build), or with a shield to look for or a count to send
    without one."""
    if ctx.helper is not None or ctx.linux_mod is not None:
        return True
    return seed.shields.pending is not None or seed.shields.given is not None


def game_alive(ctx: ZexionContext) -> bool:
    """Whether the attached game is still running (False if it can't be told)."""
    with contextlib.suppress(Exception):
        return bool(ctx.game_conn.pm.alive())
    return False


def game_closed(ctx: ZexionContext, seed: state.SeedState) -> None:
    """The attached game has exited: nothing of this game's carries into the next one's,
    ``seed``'s load (the ending's hold, and the save dressed: the next game's helper is not to
    dress a save at its title) included."""
    logger.info("Zexion has closed.")
    seed.shields.left()
    seed.shields.restart()  # the next game's spawns are its own
    ctx.load.leave()
    seed.end_load()
    with contextlib.suppress(Exception):
        ctx.game_conn.pm.close_process()  # the exited game's handle
    ctx.game_conn = None
    ctx.linux_mod = None
    ctx.run = state.GameRun()  # the next game's helper is attached with none of this one's
    ctx.drop_helper()  # the helper itself, and its retry (HELPER_RETRY_SECONDS) from now
    # The next game's failures are news, and its language is told again.
    ctx.said.clear("read", "read detail", "mismatch", "language")


async def away_tick(ctx: ZexionContext) -> None:
    """While no connection steps the session (disconnected, or dropped): it still sees its save
    leave, so the ending's hold it keeps for the next connection is one of the load still
    loaded (Session.look_away), and the helper takes back what it held for the save
    (state.SeedState.end_load)."""
    seed = ctx.seed_state
    if seed is None or ctx.game_conn is None:
        return
    try:
        if await asyncio.to_thread(seed.session.look_away, ctx.game_conn):
            seed.end_load()
    except game.GameNotRunning:
        game_closed(ctx, seed)
    except game.GameError as exc:  # looked at again on the next poll
        log.debug(f"Reading Zexion while disconnected failed: {exc}")


async def leave_helper_tick(ctx: ZexionContext) -> None:
    """While no connection steps a session and a save the client has not dressed is loaded, let
    the helper go, as if the client had closed, so the helper dresses an AP save itself from the
    client's file (native/README.md, "AP saves without the client"). Kept: a save dressed before
    a drop (state.SeedState.dressed), an ending, and an armed new game at the title. The next
    connection attaches it again before its first step (poll). With the game's Linux build the
    Linux helper's connection is closed (LinuxProcess.disconnect), which the helper takes for the
    client gone; it is configured again, as after any lost connection, by the next connection's
    first poll (ticks.configure_linux)."""
    seed = ctx.seed_state
    linux = ctx.helper is None and ctx.linux_mod is not None
    if (ctx.helper is None and not linux) or ctx.game_conn is None:
        return
    if seed is not None and (seed.dressed or seed.session.holding):
        return  # the client's look, or its ending (the Feats guard, RETURN HOME): kept as is
    try:
        no_save = await asyncio.to_thread(ctx.game_conn.state) == game.NO_SAVE
    except game.GameNotRunning:
        if seed is not None:
            game_closed(ctx, seed)
        return
    except game.GameError:  # read again on the next poll
        return
    if no_save:
        return
    if linux:
        # What drop_helper puts back for the Windows helper: a plan it held is armed again in the
        # next one. Its popups and counts the helper keeps or drops itself (client_left).
        conn = ctx.conn
        if conn is not None and conn.newgame is state.NewGame.ARMED:
            conn.newgame = None
        ctx.linux_mod = None
        try:
            await asyncio.to_thread(ctx.game_conn.pm.disconnect)
        except Exception as exc:  # it closes in time all the same (a silence of 30 s)
            log.debug(f"Letting go of the Linux helper failed ({exc!r}).")
    else:
        ctx.drop_helper()
    ctx.run.reattach = state.Reattach.LET_GO  # attached again as soon as a connection steps
    detail("Not connected: the mod gives this save its look until the client connects.")


async def poll(ctx: ZexionContext) -> None:
    """One pass of the game loop."""
    p = Poll(ctx)
    run = ctx.run
    if ctx.helper is not None and run.reload_mod_pending:
        run.reload_mod_pending = False
        reply = await asyncio.to_thread(ctx.helper.unload)  # a refusal as it is; None: failed
        unloaded, line = state.reload_reply(reply)
        (logger.info if unloaded else logger.warning)(line)
        if unloaded:
            ctx.drop_helper()
            await asyncio.sleep(RELOAD_SECONDS)
            await attach_helper(ctx)
            # An attach that failed (no frame while the game is in the background, say) holds
            # the step back as a lost helper does (tried again below), rather than write the
            # items owed with no popup and drop the traps waiting.
            if ctx.helper is None:
                run.reattach = state.Reattach.LOST
            elif run.reattach is state.Reattach.LOST:
                run.reattach = state.Reattach.NONE
    await side_tick(ctx, "Linux helper", lambda: configure_linux(ctx))  # nothing on Windows
    await link_ticks(
        ctx,
        (
            ("new game", lambda link: newgame_link_tick(ctx, link)),
            ("item labels", lambda link: labels_tick(ctx, link.helper, link.told)),
            # The Linux helper's own traps (the Windows helper's are offline_tick's).
            ("own traps", lambda link: linux_seed_tick(ctx, link.helper, p), True),
            (
                "assist lock",
                lambda link: toggle_tick(
                    link.told.assists, ctx.load.assists_want, ASSISTS, link.helper
                ),
            ),
            (
                "Intel Chips",
                lambda link: toggle_tick(link.told.intel, ctx.load.intel_want, INTEL, link.helper),
            ),
            ("disk count", lambda link: discs_helper_tick(ctx, p, link.helper, link.told)),
            ("language", lambda link: language_tick(ctx, link)),
        ),
    )
    if ctx.conn is None or not ctx.slot:
        # A hold cut short by the disconnect starts over at the next connection, with its own
        # HELPER_RETRY_SECONDS (else a reconnect after them would step at once without it).
        run.helper_back_by = 0.0
        await away_tick(ctx)
        await leave_helper_tick(ctx)
        return
    # The helper's files for this seed's saves without the client.
    await side_tick(ctx, "offline files", lambda: offline_tick(ctx, p))
    if ctx.game_conn is None:
        try:
            ctx.game_conn = await asyncio.to_thread(game.Game.attach)
        except game.GameNotRunning:
            return
        except game.GameError as exc:
            if ctx.said("game", str(exc)):  # said again only when it changes
                logger.error(state.attach_failed(exc))
                log.error(f"Cannot use this Zexion: {exc}")
            await asyncio.sleep(10)
            return
        ctx.said.clear("game")
        logger.info("Attached to Zexion.")
        if game.on_linux(ctx.game_conn):
            # The Linux helper is configured from the next poll (configure_linux, which ends the
            # hold): held until then, or for as long as after a lost one if it can't be, so no
            # step meanwhile writes the items owed with no popup or drops a waiting trap.
            run.linux_back_by = time.monotonic() + HELPER_RETRY_SECONDS
        # One that failed but may attach on a later try (an old helper refusing to unload, no
        # frame yet): held for as after a lost helper, so this poll's step writes nothing with
        # no popup and drops no waiting trap.
        if await attach_helper(ctx) and ctx.helper is None:
            run.reattach = state.Reattach.LOST
    elif ctx.helper is None and run.reattach is not state.Reattach.NONE:
        # Let go while no connection stepped (leave_helper_tick), or lost in a side tick:
        # attached again before any step, as a step with no helper writes the items owed with
        # no popup. An attach that fails (no frame comes while the game is in the background,
        # say) is tried again each BACK_RETRY_SECONDS for HELPER_RETRY_SECONDS before the step
        # goes on without it, as after any failed attach; not for a mod that doesn't match, or
        # a closed game.
        now = time.monotonic()
        if ctx.linux_mod is None and game.on_linux(ctx.game_conn):
            # The Linux helper, let go: configured in this poll's first part, or held for as after
            # any lost one, so the step writes nothing with no popup meanwhile.
            run.linux_back_by = now + HELPER_RETRY_SECONDS
        if run.helper_back_by == 0.0:
            run.helper_back_by = now + HELPER_RETRY_SECONDS
        if now < run.attach_again_at:  # refused to unload just now: asked again in a while
            waiting = now < run.helper_back_by
        else:
            waiting = await attach_helper(ctx) and now < run.helper_back_by
        if ctx.helper is None and waiting and game_alive(ctx):
            await asyncio.sleep(BACK_RETRY_SECONDS)
            return
        run.reattach, run.helper_back_by = state.Reattach.NONE, 0.0
    conn = ctx.conn
    if conn is None or not ctx.slot:
        return  # a disconnect or another connection while the above waited
    seed = ctx.seed_state  # set with conn (connected)
    current = seed.session
    p = Poll(ctx)  # again: the items may have come while the above waited
    # A save past its ending playing it again: held before this poll's step gives anything.
    await link_ticks(ctx, (("the ending again", lambda link: replay_tick(ctx, link.helper)),))
    await side_tick(ctx, "popup count", lambda: announced_tick(ctx, conn, current, p))
    # CLIENT CONNECTED before the popups this poll's step hands over.
    await link_ticks(
        ctx, (("client-back notice", lambda link: client_back_tick(ctx, link.helper, link.told)),)
    )
    try:
        followups: dict[int, str] = {}
        try:
            await hints_tick(ctx, p)
            followups = hint_notes(ctx, p.codes)
        except Exception as exc:  # the hints must never stop items or checks
            if ctx.said("hints", str(exc)):
                logger.warning(
                    "Area Map hints aren't working right now (details in the log); items and "
                    "checks still work."
                )
                log.warning(f"Area Map hints failed ({exc!r}); items and checks go on.")
        hunt = conn.hunt
        if hunt is not None:  # a received disk's popup, then the count
            # Disks picked up whose count followups_tick has still to show count there, not
            # here; the rest of the loaded save's own count here.
            have = ctx.load.save_collected
            pending = discs.uncounted(ctx.load.disks_counted, have, hunt.locations)
            followups |= discs.received_notes(hunt, p.codes, have, seed.discs_told, pending)
        if ctx.conn is not conn:
            return  # another connection while the hints were stored: its items are not these
        # How the step gives (state.plan_give): through the Windows helper, or on the game's Linux
        # build through the Linux helper once configured (configure_linux), or directly. One
        # lost (a side tick since the attach above, the ending again, say; linux_mod_lost) holds
        # the step back, once for the Windows helper, a while for the Linux one, rather than
        # write the items with no popup; a Linux helper configured afresh after a failure gets
        # its own try (a direct write would drop the traps since the popup count), and after
        # LINUX_DIRECT_AFTER real errors in a row (not refusals) the next step's items are
        # written directly even if this poll configured a fresh one: one that answers config but
        # fails apply would else hold them back for good.
        plan = state.plan_give(
            run,
            helper=ctx.helper is not None,
            mod=ctx.linux_mod is not None,
            linux_game=game.on_linux(ctx.game_conn),
            now=time.monotonic(),
        )
        run.reattach = plan.reattach
        current.keep_traps = plan.keep_traps
        grants = {
            state.Grants.HELPER: ctx.helper,
            state.Grants.MOD: ctx.linux_mod,
            state.Grants.DIRECT: None,
        }[plan.grants]
        step = await asyncio.to_thread(
            current.step,
            ctx.game_conn,
            p.codes,
            senders=p.senders,
            followups=followups,
            helper=grants,
            give=plan.give,
            # Other players' items the server has as checked stay collected in the save. Not
            # before this connection's item list has come: after a drop the checked locations
            # are the last connection's until the next Connected (a server that came back with
            # an older save has fewer).
            gone=state.others_gone(conn, ctx.checked_locations) if conn.items_listed else (),
        )
    except game.GameNotRunning:
        game_closed(ctx, seed)
        return
    except game.GameError as exc:
        if ctx.said("read", "failing"):  # said again only after a read that worked
            logger.warning("Can't read Zexion right now; trying again.")
        if ctx.said("read detail", str(exc)):
            log.warning(f"Reading Zexion failed: {exc}")
        await asyncio.sleep(2)
        return
    ctx.said.clear("read", "read detail")
    if step.marked:
        n = len(step.marked)
        places = ", ".join(
            ctx.location_names.lookup_in_slot(LOCATION_ID_BASE + i, ctx.slot)
            for i in sorted(step.marked)
        )
        detail(
            f"Marked collected in this save ({n} other players' "
            f"{'item' if n == 1 else 'items'} found or collected already): {places}."
        )
    after = state.after_step(
        plan,
        run,
        error=step.helper_error is not None,  # nothing was given (session.py); checks go on below
        helper=ctx.helper is not None,
        in_play=step.state == game.IN_PLAY,
    )
    if after.failure is state.Failure.LINUX_MOD:
        linux_mod_lost(ctx, step.helper_error)  # configured again next poll
    elif after.failure is state.Failure.HELPER:
        helper_error(ctx, step.helper_error)
        if after.forgive_lost and run.reattach is state.Reattach.LOST:
            run.reattach = state.Reattach.NONE
    run.step_failed, run.linux_failed = after.step_failed, after.linux_failed
    if ctx.helper is None and not run.mismatched and time.monotonic() >= run.helper_retry_at:
        run.helper_retry_at = time.monotonic() + HELPER_RETRY_SECONDS
        await attach_helper(ctx)
    if ctx.helper is not None or ctx.linux_mod is not None:  # either helper sets them off
        ctx.said.clear("own traps")
    elif conn.traps and step.seed_matches and ctx.said("own traps", "no mod"):
        # The helper sets them off at their pickup; nothing makes one up later.
        logger.info("Without the mod, your own traps do nothing when you pick them up.")
    # The popups the step handed the helper, or the items it wrote directly.
    await side_tick(ctx, "popup count", lambda: announced_tick(ctx, conn, current, p))
    load = ctx.load
    if step.left_seed:
        seed.shields.left()
        seed.shields.restart()  # a stop was that load's (not a reconnect's: the load goes on)
        load.assists_want = load.intel_want = False
    if ctx.conn is not conn:
        return  # a poll from before a reconnect says nothing of this seed
    if step.seed_matches and conn.newgame is state.NewGame.DONE:
        # The new game holds the seed and is in play: arm again for another slot.
        conn.newgame = None
        choice = "RANDOMIZER" if ctx.keep_randomizer else "ARCHIPELAGO"
        detail(f"{choice} is armed again: another empty slot can start this seed.")
    looked_before = load.seed_loaded  # this load was seen by the poll before too
    if step.seed_matches:
        if not load.seed_loaded:
            logger.info(state.save_loaded(step.save_stamp))  # again after a reconnect
        load.seed_loaded = True
        load.in_play = step.state == game.IN_PLAY
        seed.discs_told = step.scanned
        load.save_collected = set(step.checked)
        if conn.lock_assists and ctx.assists_unlocked():
            conn.lock_assists = False
            logger.info("Assist Unlock received: the assist menu is no longer locked.")
        load.assists_want = conn.lock_assists
        load.intel_want = conn.intel_chip
    elif step.left_seed:
        if load.seed_loaded:
            logger.info(state.SAVE_CLOSED)
        load.leave()  # reminded again on the next load, and so on
    how_to_play(ctx, step)  # after the loaded or left line, not while this seed's save is loaded
    if not step.other_seed and (step.left_seed or step.seed_matches):  # not an unsettled read
        ctx.said.clear("other save")  # said again for the next such load (other_save_tick)
    seed.dressed = aplabels.labels_wanted(step.seed_matches, step.left_seed, seed.dressed)
    if conn.death_link_on:
        await side_tick(ctx, "DeathLink", lambda: deathlink_tick(ctx, step))
    # Another seed's save: told so in the game.
    await link_ticks(
        ctx,
        (("other save's notice", lambda link: other_save_tick(ctx, step, link.helper, link.told)),),
    )
    if not step.seed_matches or ctx.conn is not conn:
        return  # nothing to send, or the connection changed meanwhile
    if not looked_before:
        # A load's first look: sent from the next poll's, two reads apart, so a load caught half
        # way (the section still the last save's, the map grid the new one's) sends nothing.
        return
    if hunt is not None:
        # From the second look on: the own disks the save holds have had their count, but one
        # picked up since (followups_tick), again after a reload put it back. Another slot of
        # the seed (loaded while the polls paused, so with no look at the title between) starts
        # over: the disks it holds were not picked up now.
        if step.save_stamp[:4] != load.disks_slot:
            load.disks_counted, load.disks_slot = None, step.save_stamp[:4]
        load.disks_counted = discs.counted(load.disks_counted, step.checked, hunt.locations)
    if shields_due(ctx, seed):
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
            logger.warning(
                "Couldn't send the locations found (connection lost?); they're sent on "
                "reconnecting."
            )
            log.warning(f"Sending the locations found failed ({exc}); sent on reconnecting.")
    just_found = {loc - LOCATION_ID_BASE for loc in new}
    await side_tick(ctx, "found record", lambda: store_found(ctx, conn, found, just_found))
    if new:
        p.found = own_found(ctx)  # counted from now on, in this poll too
    await link_ticks(
        ctx,
        (
            # The Linux helper's connection that closed cancels a RETURN HOME prompt asked on it.
            ("RETURN HOME's prompt", lambda link: return_home_tick(ctx, link.helper), True),
            (  # after CLIENT CONNECTED
                "follow-up popups",
                lambda link: followups_tick(ctx, p, step, link.helper, link.told),
            ),
            (
                "Landing Pod's RETURN HOME",
                lambda link: mission_tick(ctx, p, step, link.helper, link.told),
            ),
            ("Feats guard check", lambda link: feats_tick(ctx, step), False),
        ),
    )
    if ctx.conn is not conn:
        return  # the connection changed meanwhile: this ending is not its goal
    if step.goal and not ctx.finished_game:
        have = 0 if hunt is None else discs.owned(hunt, p.codes, load.save_collected)
        if state.goal_reached(step.goal, None if hunt is None else hunt.required, have):
            try:
                await ctx.send_msgs([{"cmd": "StatusUpdate", "status": ClientStatus.CLIENT_GOAL}])
            except Exception as exc:  # a closing connection: sent on a later poll
                logger.warning("Couldn't send the goal (connection lost?); trying again.")
                log.warning(f"Sending the goal failed ({exc}); trying again.")
                return
            ctx.finished_game = True
            logger.info(
                "This save reached the ending: goal sent."
                if hunt is None
                else f"This save reached the ending with {have} of {hunt.required} OS Install "
                "Disks: goal sent."
            )
        elif not load.goal_withheld:
            load.goal_withheld = True  # said once a load
            logger.warning(
                f"This save reached the ending with {have} of {hunt.required} OS Install Disks: "
                "not the goal yet."
            )


def launch(*args: str) -> None:
    async def main(parsed) -> None:
        ctx = ZexionContext(parsed.connect, parsed.password)
        ctx.auth = parsed.name
        ctx.keep_randomizer = parsed.keep_randomizer
        ctx.server_task = asyncio.create_task(server_loop(ctx), name="server loop")
        loop_task = asyncio.create_task(game_loop(ctx), name="Zexion game loop")
        keepalive_task = asyncio.create_task(keepalive_loop(ctx), name="Zexion keep-alive")
        if gui_enabled:
            ctx.run_gui()
        ctx.run_cli()
        await ctx.exit_event.wait()
        loop_task.cancel()
        keepalive_task.cancel()
        await ctx.shutdown()

    import colorama

    parser = get_base_parser(description="Zexion Archipelago client.")
    parser.add_argument("--name", default=None, help="Slot name to connect as.")
    parser.add_argument(
        "--keep-randomizer",
        action="store_true",
        help="Do not offer ARCHIPELAGO: start the seed from RANDOMIZER, typing in its seed text, "
        "and START GAME.",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Also show what the client sets up in the game for the seed (for testing).",
    )
    parser.add_argument("url", nargs="?", help="Archipelago connection url")
    parsed = handle_url_arg(parser.parse_args(args), parser=parser)
    ticks.VERBOSE = parsed.verbose
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
