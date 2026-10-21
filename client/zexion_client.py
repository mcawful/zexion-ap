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
  Zexion is grabbed in the core.

The client asks the server only for items from other worlds: this world's own items are in
the save's item table, and the game hands them out itself.
"""

from __future__ import annotations

import asyncio
import os
import sys
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
import prepare  # noqa: E402
import save_patch  # noqa: E402
import session  # noqa: E402

GAME = "Zexion"
ITEM_ID_BASE = 3392510_000  # worlds/zexion/items.py
LOCATION_ID_BASE = ITEM_ID_BASE + 500  # worlds/zexion/locations.py
POLL_SECONDS = 0.25
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
            self.kept = set(slot["kept_locations"])
            self.goal_sent = False
            logger.info(
                f"Seed for Zexion's {slot['mode']} mode. If this slot's game is not prepared "
                "yet: make a new randomizer game in that mode, save, close Zexion, and run "
                "/prepare SLOT."
            )

    async def disconnect(self, allow_autoreconnect: bool = False) -> None:
        self.plan = None
        self.session = None
        await super().disconnect(allow_autoreconnect)

    def received_codes(self) -> list[int]:
        codes = []
        for item in self.items_received:
            code = item.item - ITEM_ID_BASE
            if 0 <= code <= 50:
                codes.append(code)
            else:
                logger.warning(f"Received item {item.item}, which is not a Zexion item; ignored.")
        return codes


async def game_loop(ctx: ZexionContext) -> None:
    while not ctx.exit_event.is_set():
        await asyncio.sleep(POLL_SECONDS)
        if ctx.session is None or not ctx.slot:
            continue
        if ctx.game_conn is None:
            try:
                ctx.game_conn = await asyncio.to_thread(game.Game.attach)
                logger.info("Attached to Zexion.")
            except game.GameNotRunning:
                continue
            except game.GameError as exc:
                logger.error(f"Cannot use this Zexion: {exc}")
                await asyncio.sleep(10)
                continue
        try:
            step = await asyncio.to_thread(ctx.session.step, ctx.game_conn, ctx.received_codes())
        except game.GameNotRunning:
            logger.info("Zexion has closed.")
            ctx.game_conn = None
            continue
        except game.GameError as exc:
            logger.error(f"Reading Zexion failed: {exc}")
            await asyncio.sleep(2)
            continue
        if not step.seed_matches:
            continue
        locations = {LOCATION_ID_BASE + i for i in step.checked & ctx.kept}
        new = locations - ctx.locations_checked
        if new:
            ctx.locations_checked |= new
            await ctx.check_locations(new)
        if step.goal and not ctx.goal_sent and not ctx.finished_game:
            await ctx.send_msgs([{"cmd": "StatusUpdate", "status": ClientStatus.CLIENT_GOAL}])
            ctx.goal_sent = True
            ctx.finished_game = True
            logger.info("Zexion grabbed: goal sent.")


def launch(*args: str) -> None:
    async def main(parsed) -> None:
        ctx = ZexionContext(parsed.connect, parsed.password)
        ctx.auth = parsed.name
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
