"""The Zexion Archipelago client.

    .venv/Scripts/python client/zexion_client.py [archipelago://name:password@host:port]

It needs Archipelago's own modules: by default the pinned checkout in .ap/Archipelago
(tools/ap_checkout.py), or the folder ``ZEXION_AP_DIR`` names.

Once connected:

- ``/prepare SLOT`` writes the seed into a new game the player made in that slot
  (client/prepare.py): start a new randomizer game in the seed's mode, save at once, close
  Zexion, then run it.
- While Zexion runs with that slot loaded, the client sends each location the map marks as
  collected, gives the items other worlds send (client/session.py), and reports the goal when
  the ending begins (flag 250 with flag 211 clear, after the final boss and the ship; the
  goal on flag 250 was live-tested on 2026-10-22, the flag 211 condition not yet). With the
  helper loaded, grabbing Zexion should offer the game's SKIP ESCAPE AND FINAL BATTLE? in any
  game, and after YES the goal should come as the player boards the ship (not yet run in
  the game); the client logs whether the helper put the offer in place.
- With the helper loaded, the client also arms it with the seed (client/newgame.py), with no
  /prepare: after the player picks an empty slot, the game-mode choice reads NORMAL /
  ARCHIPELAGO, and ARCHIPELAGO starts a new game holding the AP seed (live-tested on
  2026-10-22 with Standard and Easy seeds). For a seed whose mode
  the choice cannot set up (OHKO), RANDOMIZER stays, and a randomizer game started in the
  seed's mode gets the AP seed written in as the game builds it (run in game for Standard,
  Easy and OHKO). ``--keep-randomizer`` takes that path for every seed, leaving RANDOMIZER on the
  panel. One new game per connection: reconnect to start another. A seed whose starting
  items a new game cannot hold falls back to /prepare.
- When it attaches to Zexion it loads the native helper (native/, client/native.py) into the
  game, unless started with ``--no-helper`` or the helper is not built. Items are then sent
  through it, to arrive between frames with the game's own popup and sound (for example
  "MISSILE BOX FROM ALICE") and a HUD refresh; live-tested on 2026-10-22. Without
  the helper, or if it fails, items are written directly, with no popup, and the client tries
  the helper again every 30 seconds. ``/unload_helper`` unloads it as if it had failed, to
  test that.

The client asks the server only for items from other worlds: this world's own items are in
the save's item table, and the game hands them out itself.
"""

from __future__ import annotations

import asyncio
import dataclasses
import os
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
AP_DIR = Path(os.environ.get("ZEXION_AP_DIR", HERE.parent / ".ap" / "Archipelago"))
if str(AP_DIR) not in sys.path:
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

import game  # noqa: E402
import native  # noqa: E402
import newgame  # noqa: E402
import prepare  # noqa: E402
import save_patch  # noqa: E402
import session  # noqa: E402

GAME = "Zexion"
ITEM_ID_BASE = 3392510_000  # worlds/zexion/items.py
LOCATION_ID_BASE = ITEM_ID_BASE + 500  # worlds/zexion/locations.py
POLL_SECONDS = 0.25
HELPER_RETRY_SECONDS = 30.0
NEWGAME_POLL_SECONDS = 1.0
SKIP_CHECK_SECONDS = 2.0
BACKUPS = "zexion_backups"  # under Archipelago's user folder


class ZexionCommandProcessor(ClientCommandProcessor):
    ctx: ZexionContext

    def _cmd_prepare(self, slot: str = "") -> bool:
        """Write this seed into a new game in a save slot (Zexion must be closed)."""
        if self.ctx.plan is None:
            self.output("Connect to the server first: the seed comes from it.")
            return False
        if not slot.isdigit():
            self.output("Usage: /prepare SLOT, the slot number (1 to 30) of the new game.")
            return False
        try:
            done = prepare.prepare(
                int(slot),
                self.ctx.plan,
                Path(Utils.user_path(BACKUPS)),
                game.game_running,
            )
        except (prepare.PrepareError, game.GameError, OSError) as exc:
            self.output(f"Not prepared: {exc}")
            return False
        if done.changed:
            self.output(f"Slot {done.slot} now holds this seed. Its backup: {done.backup}")
        else:
            self.output(f"Slot {done.slot} already holds this seed; nothing written.")
        return True

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

    def __init__(self, server_address: str | None, password: str | None) -> None:
        super().__init__(server_address, password)
        self.plan: save_patch.SeedPlan | None = None
        self.session: session.Session | None = None
        self.kept: set[int] = set()
        self.game_conn: game.Game | None = None
        self.use_helper = True
        self.keep_randomizer = False  # arm without ARCHIPELAGO (--keep-randomizer)
        self.skip_check_at: float | None = None  # when to ask the helper about the skip
        # /unload_helper: the game loop unloads it but keeps the dead connection, so the
        # next request fails the way a helper failing mid-session does.
        self.unload_helper_pending = False
        self.helper: native.Helper | None = None
        self.helper_retry_at = 0.0
        # The AP new game: None (to arm), "armed", "done" or "unavailable"; disarm_pending asks
        # the game loop, the helper's only user, to disarm it.
        self.newgame_state: str | None = None
        self.newgame_polled_at = 0.0
        self.disarm_pending = False
        self.goal_sent = False

    async def server_auth(self, password_requested: bool = False) -> None:
        if password_requested and not self.password:
            await super().server_auth(password_requested)
        await self.get_username()
        await self.send_connect()

    def on_package(self, cmd: str, args: dict) -> None:
        if cmd == "Connected":
            slot = args["slot_data"]
            self.plan = save_patch.plan_from_slot_data(slot)
            self.session = session.Session(self.plan, slot["locations"])
            self.session.helper = self.helper
            self.newgame_state = None
            self.kept = set(slot["kept_locations"])
            self.goal_sent = False
            logger.info(
                f"Seed for Zexion's {slot['mode']} mode. If this slot's game is not prepared "
                "yet: make a new randomizer game in that mode, save, close Zexion, and run "
                "/prepare SLOT."
            )
        if cmd == "ReceivedItems" and args.get("index") == 0 and self.session is not None:
            # The server's full list: what it holds was received before this connection.
            self.session.mark_announced(len(self.received()[0]))

    async def disconnect(self, allow_autoreconnect: bool = False) -> None:
        self.plan = None
        self.session = None
        if self.newgame_state == "armed":
            self.disarm_pending = True
        self.newgame_state = None
        await super().disconnect(allow_autoreconnect)

    def drop_helper(self) -> None:
        if self.helper is not None:
            self.helper.close()
        self.helper = None
        if self.session is not None:
            self.session.helper = None
        self.helper_retry_at = time.monotonic() + HELPER_RETRY_SECONDS
        # A plan the lost helper held is armed again in the next one; a new game already made
        # (or a seed that cannot make one) stays as it is: one new game per connection.
        if self.newgame_state == "armed":
            self.newgame_state = None
        self.disarm_pending = False

    def received(self) -> tuple[list[int], list[str]]:
        """The Zexion item code of each received item, and who sent it ("" for this slot's
        own), kept in step: anything that is not a Zexion item is left out of both."""
        codes, senders = [], []
        for item in self.items_received:
            code = item.item - ITEM_ID_BASE
            if 0 <= code <= 50:
                codes.append(code)
                senders.append(
                    "" if item.player == self.slot else self.player_names.get(item.player, "")
                )
            else:
                logger.warning(f"Received item {item.item}, which is not a Zexion item; ignored.")
        return codes, senders


async def attach_helper(ctx: ZexionContext) -> None:
    """Load and configure the native helper in the attached game, or carry on without it."""
    if not ctx.use_helper:
        return
    dll = native.default_dll()
    if not dll.is_file():
        logger.info(f"No native helper at {dll}: items will arrive with no popup.")
        return
    try:
        ctx.helper = await asyncio.to_thread(native.attach, ctx.game_conn, dll)
    except native.HelperError as exc:
        logger.warning(f"The in-game helper could not be used ({exc}); items arrive with no popup.")
        ctx.drop_helper()
        return
    if ctx.session is not None:
        ctx.session.helper = ctx.helper
    logger.info("The in-game helper is running: received items are sent with the game's popup.")
    ctx.skip_check_at = time.monotonic() + SKIP_CHECK_SECONDS


async def skip_tick(ctx: ZexionContext) -> None:
    """Once per helper, a moment after it attaches: say whether it offers the escape skip (it
    patches the game's event within 30 frames, or leaves an event it does not recognize)."""
    if ctx.helper is None or ctx.skip_check_at is None or time.monotonic() < ctx.skip_check_at:
        return
    ctx.skip_check_at = None
    hello = await asyncio.to_thread(ctx.helper.hello)
    if hello.get("configured") != "1":
        ctx.skip_check_at = time.monotonic() + SKIP_CHECK_SECONDS  # no frame came: ask again
        return
    if hello.get("skip") == "1":
        logger.info(
            "Grabbing Zexion offers SKIP ESCAPE AND FINAL BATTLE? while the helper runs; the goal "
            "is sent when the ending begins, either way."
        )
    else:
        logger.warning(
            "The helper could not offer the escape skip (the game's event is not as expected): "
            "a first playthrough must finish the escape, which needs the Zenith Thrusters."
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
    if ctx.plan is None or ctx.newgame_state in ("done", "unavailable"):
        return
    if ctx.newgame_state is None:
        plan = ctx.plan
        if ctx.keep_randomizer and plan.menu is not None:
            plan = dataclasses.replace(plan, menu=None)
        try:
            text = newgame.plan_text(plan)
        except newgame.NewGameError as exc:
            if plan.menu is None:
                ctx.newgame_state = "unavailable"
                logger.info(f"This seed cannot start as a new game ({exc}); use /prepare instead.")
                return
            logger.info(f"ARCHIPELAGO cannot set up this seed ({exc}); RANDOMIZER stays.")
            plan = dataclasses.replace(plan, menu=None)
            try:
                text = newgame.plan_text(plan)
            except newgame.NewGameError as again:
                ctx.newgame_state = "unavailable"
                logger.info(f"This seed cannot start as a new game ({again}); use /prepare.")
                return
        reply = await asyncio.to_thread(helper.request, text)
        if reply == "armed":
            ctx.newgame_state = "armed"
            ctx.newgame_polled_at = time.monotonic()
            if plan.menu is not None:
                logger.info(
                    "Ready for a new game: pick an empty slot and choose ARCHIPELAGO (in "
                    "place of RANDOMIZER). It starts the AP seed."
                )
            else:
                logger.info(
                    "Ready for a new game: in an empty slot choose RANDOMIZER, pick this seed's "
                    "mode with no options changed, and START GAME. The AP seed is written in "
                    "as the game starts."
                )
        return
    if time.monotonic() - ctx.newgame_polled_at < NEWGAME_POLL_SECONDS:
        return
    ctx.newgame_polled_at = time.monotonic()
    status = await asyncio.to_thread(helper.request, "newgame_status")
    if status.startswith("applied"):
        ctx.newgame_state = "done"
        logger.info(f"The new game holds the AP seed ({status}).")
    elif status.startswith("refused"):
        ctx.newgame_state = None  # arm again for the next try
        logger.warning(f"No new game with the AP seed: {status[len('refused ') :]}.")
    elif status == "idle":
        ctx.newgame_state = None  # the helper lost the plan (reloaded): arm again


async def game_loop(ctx: ZexionContext) -> None:
    while not ctx.exit_event.is_set():
        await asyncio.sleep(POLL_SECONDS)
        if ctx.helper is not None and ctx.unload_helper_pending:
            ctx.unload_helper_pending = False
            try:
                reply = await asyncio.to_thread(ctx.helper.request, "unload")
                logger.info(f"The helper was asked to unload: {reply}")
            except native.HelperError as exc:
                logger.warning(f"Unloading the helper failed: {exc}")
        if ctx.helper is not None:
            try:
                await newgame_tick(ctx)
                await skip_tick(ctx)
            except native.HelperError as exc:
                logger.warning(f"The in-game helper failed ({exc}); trying it again later.")
                ctx.drop_helper()
        if ctx.session is None or not ctx.slot:
            continue
        if ctx.game_conn is None:
            try:
                ctx.game_conn = await asyncio.to_thread(game.Game.attach)
                logger.info("Attached to Zexion.")
                await attach_helper(ctx)
            except game.GameNotRunning:
                continue
            except game.GameError as exc:
                logger.error(f"Cannot use this Zexion: {exc}")
                await asyncio.sleep(10)
                continue
        current = ctx.session
        try:
            codes, senders = ctx.received()
            step = await asyncio.to_thread(current.step, ctx.game_conn, codes, senders=senders)
        except game.GameNotRunning:
            logger.info("Zexion has closed.")
            ctx.game_conn = None
            ctx.drop_helper()
            continue
        except game.GameError as exc:
            logger.error(f"Reading Zexion failed: {exc}")
            await asyncio.sleep(2)
            continue
        if ctx.helper is not None and current.helper is None:
            ctx.drop_helper()  # the session gave up on it; try again later
        elif ctx.helper is None and ctx.use_helper and time.monotonic() >= ctx.helper_retry_at:
            ctx.helper_retry_at = time.monotonic() + HELPER_RETRY_SECONDS
            await attach_helper(ctx)
        if not step.seed_matches or ctx.session is not current:
            continue  # nothing to send, or the connection changed while the game was read
        locations = {LOCATION_ID_BASE + i for i in step.checked & ctx.kept}
        new = locations - ctx.locations_checked
        if new:
            ctx.locations_checked |= new
            await ctx.check_locations(new)
        if step.goal and not ctx.goal_sent and not ctx.finished_game:
            await ctx.send_msgs([{"cmd": "StatusUpdate", "status": ClientStatus.CLIENT_GOAL}])
            ctx.goal_sent = True
            ctx.finished_game = True
            logger.info("The ending began: goal sent.")


def launch(*args: str) -> None:
    async def main(parsed) -> None:
        ctx = ZexionContext(parsed.connect, parsed.password)
        ctx.auth = parsed.name
        ctx.use_helper = not parsed.no_helper
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
        "--no-helper",
        action="store_true",
        help="Do not load the native helper into the game: items arrive with no popup.",
    )
    parser.add_argument(
        "--keep-randomizer",
        action="store_true",
        help="Do not offer ARCHIPELAGO: start the seed from RANDOMIZER, its mode and START GAME.",
    )
    parser.add_argument("url", nargs="?", help="Archipelago connection url")
    parsed = handle_url_arg(parser.parse_args(args), parser=parser)
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
