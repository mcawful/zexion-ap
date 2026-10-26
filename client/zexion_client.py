"""The Zexion Archipelago client: ``python -m client.zexion_client [archipelago://...]``.

Archipelago comes from .ap/Archipelago (or ``ZEXION_AP_DIR``), or is the Launcher's own when
started as Zexion Client. While this seed's save is loaded it sends the locations the map marks
collected, gives items from other worlds (client/session.py) and sends the goal when the ending
begins; the side tasks with the in-game helper are in client/ticks.py. This world's own items
are in the save's item table, so it asks the server only for other worlds' (items_handling).
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

from . import aplabels, discs, game, ids, native, save_patch, shields, state, ticks  # noqa: E402
from .ids import ASSIST_UNLOCK, ITEM_ID_BASE, LOCATION_ID_BASE  # noqa: E402
from .storage import announced_tick, hint_notes, hints_tick, own_found, store_found  # noqa: E402
from .ticks import (  # noqa: E402
    ASSISTS,
    INTEL,
    attach_helper,
    client_back_tick,
    deathlink_tick,
    detail,
    discs_helper_tick,
    followups_tick,
    labels_tick,
    log,
    mission_tick,
    newgame_tick,
    offline_tick,
    other_save_tick,
    shields_tick,
    skip_tick,
    slots_tick,
    toggle_tick,
)

GAME = "Zexion"
POLL_SECONDS = 0.25
HELPER_RETRY_SECONDS = 30.0
HELPER_LOST = (
    f"The Zexion mod stopped responding; trying again in {HELPER_RETRY_SECONDS:.0f} seconds. "
    "Until then items arrive without popups."
)


class ZexionCommandProcessor(ClientCommandProcessor):
    ctx: ZexionContext
    HIDDEN = ("unload_helper",)  # for testing the client: they work, but /help leaves them out

    def get_help_text(self) -> str:
        self.commands = {k: v for k, v in type(self).commands.items() if k not in self.HIDDEN}
        try:
            return super().get_help_text()
        finally:
            del self.commands  # the class's again

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
            # Only the packet right after Connected can be the slot's backlog (the server sends
            # none when it is empty).
            conn.awaiting_backlog = False
            conn.items_listed = True
        if cmd == "Connected":
            self.connected(args["slot_data"])

    def connected(self, slot: dict) -> None:
        try:
            conn = state.Connection.from_slot_data(slot, self.team, self.slot)
        except state.UnsupportedSeed as exc:
            logger.error(
                "This seed was made with another version of the Zexion APWorld than this "
                "client: generate and play with the same release. Disconnecting."
            )
            log.error(f"Cannot play this seed: {exc}.")
            Utils.async_start(self.disconnect(), name="disconnect")
            return
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
        # Kept by the server across clients: shields given, locations found, follow-ups and
        # item popups shown.
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
        detail(f"Seed for Zexion's {conn.mode} mode.")

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
                "DeathLink: the Zexion mod isn't running, so this death waits until it is."
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


def game_alive(ctx: ZexionContext) -> bool:
    """Whether the attached game is still running: a helper lost with its game is no news for
    the player, as the next read says the game closed."""
    try:
        return ctx.game_conn is not None and ctx.game_conn.pm.alive()
    except Exception:
        return False


@contextlib.contextmanager
def helper_failure(ctx: ZexionContext) -> Iterator[None]:
    """Around requests to the helper: one that fails drops it, to be tried again later."""
    try:
        yield
    except native.HelperError as exc:
        if game_alive(ctx):
            logger.warning(HELPER_LOST)
        log.warning(f"The in-game helper failed ({exc}); trying it again later.")
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
            logger.warning(
                f"Something went wrong with the {name} (details in the log); items and checks "
                f"still work. Please report it at {state.REPORT}."
            )
            log.exception(f"The client's {name} failed ({exc!r}); items and checks go on.")


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
            logger.warning(ticks.OFFLINE_FAILED)
            log.warning(f"Writing the helper's offline files failed: {exc!r}")
        ctx.offline_failed = True
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
                logger.warning(
                    "Area Map hints aren't working right now (details in the log); items and "
                    "checks still work."
                )
                log.warning(f"Area Map hints failed ({exc!r}); items and checks go on.")
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
        ctx.said.clear("read", "read detail")  # the next game's failures are news
        return
    except game.GameError as exc:
        if ctx.said("read", "failing"):  # said again only after a read that worked
            logger.warning("Can't read Zexion right now; trying again.")
        if ctx.said("read detail", str(exc)):
            log.warning(f"Reading Zexion failed: {exc}")
        await asyncio.sleep(2)
        return
    ctx.said.clear("read", "read detail")
    if ctx.helper is not None and current.helper is None:
        # The session gave up on it (try again later): dropped before the count below, so the
        # popups it had not shown are not counted as shown.
        if game_alive(ctx):
            logger.warning(HELPER_LOST)
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
        if not load.seed_loaded:
            logger.info(state.save_loaded(step.save_stamp))  # again after a reconnect
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
        if load.seed_loaded:
            logger.info(state.SAVE_CLOSED)
        load.leave()  # reminded again on the next load, and so on
    if step.other_seed:
        if ctx.said("other save", "loaded"):  # once a load
            logger.info(state.OTHER_SAVE)
    elif step.left_seed or step.seed_matches:  # not an unsettled read
        ctx.said.clear("other save")
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
            logger.warning(
                "Couldn't send the locations found (connection lost?); they're sent on "
                "reconnecting."
            )
            log.warning(f"Sending the locations found failed ({exc}); sent on reconnecting.")
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
                logger.warning("Couldn't send the goal (connection lost?); trying again.")
                log.warning(f"Sending the goal failed ({exc}); trying again.")
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
