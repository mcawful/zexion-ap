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
  the ending begins (flag 250 with flag 211 clear, after the final boss and the ship). With the
  helper loaded, grabbing Zexion offers the game's SKIP ESCAPE AND FINAL BATTLE? in any game,
  and after YES the goal comes as the player boards the ship (both live-tested 2026-10-22 and
  2026-10-23); the client logs whether the helper put the offer in place.
- With the helper loaded, the client also arms it with the seed (client/newgame.py), with no
  /prepare: after the player picks an empty slot, the game-mode choice reads NORMAL /
  ARCHIPELAGO, and ARCHIPELAGO starts a new game holding the AP seed (live-tested on
  2026-10-22 with Standard and Easy seeds, and on 2026-10-23 with an OHKO seed). Without a
  menu setup in the slot data, or with ``--keep-randomizer``, RANDOMIZER stays, and a
  randomizer game started in the seed's mode gets the AP seed written in as the game builds
  it (run in game for Standard, Easy and OHKO). One new game per connection: reconnect to
  start another. A seed whose starting items a new game cannot hold falls back to /prepare.
- With the helper loaded, and slot data saying the world gives other players' items the
  stand-ins ("stand_in": 47, and "stand_in_progression": 48 for progression items), the
  client also asks the server what each location holding one holds and sends the helper a
  label per map cell (client/aplabels.py): the item shows an Archipelago logo (with its up
  arrow for progression), and its pickup pops up "PROGRESSION ITEM" or "AP ITEM" (another
  Zexion player's item: its own icon and name) over "FOR ALICE" (working in the game,
  2026-10-23). The helper holds the labels only while this seed's save is loaded; while
  another save (or none) is, and on disconnect, they are taken back, so Scrap Energy and Scrap
  Chunk are the game's own there. A refusal (no frame yet) is retried.
- When it attaches to Zexion it loads the native helper (native/, client/native.py) into the
  game, unless started with ``--no-helper`` or the helper is not built. Items are then sent
  through it, to arrive between frames with the game's own popup and sound (for example
  "MISSILE BOX FROM ALICE") and a HUD refresh; live-tested on 2026-10-22. Without
  the helper, or if it fails, items are written directly, with no popup, and the client tries
  the helper again every 30 seconds. ``/unload_helper`` unloads it as if it had failed, to
  test that.
- For a seed with DeathLink on (slot data "death_link"), the client sends the player's deaths
  (in play, then dead) to the other players, and deals the deaths received from them, queued,
  through the helper, one each time the player is in play (client/deathlink.py): the one thing
  the client does that costs the player progress, as a death goes back to the last save.
  Live-tested 2026-10-23.
- For a seed that disallows assists (slot data "allow_assists" false, the world's default),
  the client has the helper grey out the options menu's ASSIST entry, put a padlock after it
  and keep it from opening while this seed's save is loaded, and unlocks it otherwise, or for
  good once the slot receives Assist Unlock (sent from the server console as an escape hatch).
  The lock live-tested 2026-10-23; unlocking in another save not yet.

The client asks the server only for items from other worlds: this world's own items are in
the save's item table, and the game hands them out itself.
"""

from __future__ import annotations

import asyncio
import os
import sys
import time
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
AP_DIR = Path(os.environ.get("ZEXION_AP_DIR", HERE.parent / ".ap" / "Archipelago"))
if str(AP_DIR) not in sys.path:
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

import aplabels  # noqa: E402
import deathlink  # noqa: E402
import game  # noqa: E402
import native  # noqa: E402
import newgame  # noqa: E402
import prepare  # noqa: E402
import reconcile  # noqa: E402
import save_patch  # noqa: E402
import session  # noqa: E402

GAME = "Zexion"
ITEM_ID_BASE = 3392510_000  # worlds/zexion/items.py
ASSIST_UNLOCK = 100  # worlds/zexion/items.py: lifts the assist lock; not an item of the game's
LOCATION_ID_BASE = ITEM_ID_BASE + 500  # worlds/zexion/locations.py
POLL_SECONDS = 0.25
HELPER_RETRY_SECONDS = 30.0
NEWGAME_POLL_SECONDS = 1.0
SKIP_CHECK_SECONDS = 2.0
LABELS_RETRY_SECONDS = 5.0
SHIELD_GAP_SECONDS = 2.0  # between received Energy Shields, or before asking again
DEATH_RETRY_SECONDS = 1.0  # before asking the helper again to deal a received death
SHIELD_SEEN_SECONDS = 3.0  # from a spawn, in play, to giving up on seeing the shield
SHIELD_MISSES = 3  # spawns in a row not seen before backing off
SHIELD_BACKOFF_SECONDS = 60.0


@dataclass
class PendingShield:
    """A received Energy Shield the helper spawned, not counted yet."""

    key: str  # the data storage key of the slot it was spawned for
    at: float  # when (time.monotonic)
    left_play: bool = False  # the player died or the save was unloaded since


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
        self.choice_refused = False  # the game's start table differed: no ARCHIPELAGO
        # Other players' items (client/aplabels.py): this world's locations holding one, and
        # whether the helper has their labels.
        self.foreign: list[int] = []
        self.labels_sent = False  # the helper holds this connection's labels
        self.labels_held = False  # the helper may hold labels (ours, or an earlier client's)
        self.labels_refused = False  # the helper would not take them: not asked again
        self.labels_retry_at = 0.0
        # Whether the helper should hold them: only while this seed's save is loaded
        # (aplabels.labels_wanted), so other saves keep Scrap Energy as the game's own.
        self.labels_want = False
        # Received Energy Shields given so far (counted once seen up), kept in the server's data
        # storage under this key (per team and slot), so a restart or a shield received offline
        # is not lost.
        self.shield_key: str | None = None
        self.shield_retry_at = 0.0
        self.shield_said = ""
        # A shield spawned but not counted yet (shields_tick), and how many spawns in a row
        # were not seen.
        self.shield_pending: PendingShield | None = None
        self.shield_misses = 0
        # Shields counted as given that the server may not hold yet: (key, count), and when
        # the count was last sent.
        self.shield_given: tuple[str, int] | None = None
        self.shield_count_sent_at = 0.0
        # DeathLink, when the seed's slot data turns it on (client/deathlink.py).
        self.death_link_on = False
        self.deaths = deathlink.DeathLink()
        self.death_key: tuple | None = None  # (seed, team, slot) the deaths are for
        self.death_retry_at = 0.0
        self.death_said = ""
        # The assist menu (docs/re/memory.md, "The assist menu"): whether this seed's slot data
        # disallows assists, whether the helper should lock ASSIST now (this seed's save
        # loaded), and what the helper was last told (None: nothing yet, or a new helper).
        self.lock_assists = False
        self.assists_want = False
        self.assists_sent: bool | None = None
        self.assists_retry_at = 0.0
        # Counts connections and disconnects: a helper reply from an older one is dropped.
        self.connection = 0
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
            self.choice_refused = False
            self.kept = set(slot["kept_locations"])
            self.connection += 1
            self.shield_key = f"zexion_shields_{self.team}_{self.slot}"
            self.set_notify(self.shield_key)
            death_key = (self.seed_name, self.team, self.slot)
            if death_key != self.death_key:  # another seed or slot: nothing carries over
                self.death_key = death_key
                self.deaths = deathlink.DeathLink()
            # A seed from before the option has no "allow_assists": nothing is locked.
            self.lock_assists = slot.get("allow_assists", True) is False
            self.death_link_on = bool(slot.get("death_link", False))
            asyncio.create_task(self.update_death_link(self.death_link_on))
            if self.death_link_on:
                logger.info("DeathLink is on for this seed.")
            # Only a world that says it gives other players' items the stand-in has them.
            self.foreign = aplabels.foreign_locations(
                slot["location_items"], self.kept, aplabels.stand_ins(slot)
            )
            self.labels_sent = False
            self.labels_want = False  # until a poll of this connection finds its save
            self.labels_retry_at = 0.0
            if self.foreign:
                scouts = [LOCATION_ID_BASE + i for i in self.foreign]
                asyncio.create_task(
                    self.send_msgs(
                        [{"cmd": "LocationScouts", "locations": scouts, "create_as_hint": 0}]
                    )
                )
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
        self.connection += 1
        self.shield_key = None
        self.lock_assists = False
        self.assists_want = False  # unlocked on the next helper tick
        self.foreign = []
        self.labels_sent = False
        self.labels_want = False  # taken back by labels_tick, which runs while a helper is attached
        if self.newgame_state == "armed":
            self.disarm_pending = True
        self.newgame_state = None
        await super().disconnect(allow_autoreconnect)

    def on_deathlink(self, data: dict) -> None:
        super().on_deathlink(data)
        if not self.death_link_on:
            return
        queued = self.deaths.receive()
        if queued > 1:
            logger.info(f"DeathLink: {queued} deaths are queued, dealt one after another.")
        if self.helper is None:
            logger.warning(
                "DeathLink: the in-game helper is not running, so the death waits for it."
            )

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
        # The next helper (perhaps this one, still loaded) is treated as holding labels.
        self.labels_sent = self.labels_held = self.labels_refused = False

    def assists_unlocked(self) -> bool:
        """Whether this slot has received Assist Unlock (from the server console, say): the
        assist lock's escape hatch, kept by the server like any received item."""
        return any(item.item - ITEM_ID_BASE == ASSIST_UNLOCK for item in self.items_received)

    def received(self) -> tuple[list[int], list[str]]:
        """The Zexion item code of each received item, and who sent it ("" for this slot's
        own), kept in step: anything that is not a Zexion item is left out of both."""
        codes, senders = [], []
        for item in self.items_received:
            code = item.item - ITEM_ID_BASE
            if code == ASSIST_UNLOCK:
                continue  # not for the inventory: see assists_unlocked
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
    ctx.assists_sent = None  # told again: a helper still loaded from before may hold a lock
    # Likewise it may hold an earlier client's labels: taken back unless this seed's save is
    # loaded, where this seed's are sent over them.
    ctx.labels_held = True
    ctx.labels_sent = ctx.labels_refused = False
    logger.info("The in-game helper is running: received items are sent with the game's popup.")
    ctx.skip_check_at = time.monotonic() + SKIP_CHECK_SECONDS


async def assists_tick(ctx: ZexionContext) -> None:
    """Tell the helper to lock the options menu's ASSIST entry while this seed's save is loaded
    and the seed disallows assists, and to unlock it otherwise (docs/re/memory.md, "The assist
    menu"). Sent when that changes, or to a new helper."""
    helper = ctx.helper
    want = ctx.assists_want
    if helper is None or want == ctx.assists_sent or time.monotonic() < ctx.assists_retry_at:
        return
    reply = await asyncio.to_thread(helper.request, "assists lock" if want else "assists free")
    if helper is not ctx.helper:
        return
    if reply == ("locked" if want else "free"):
        ctx.assists_sent = want
        logger.info(
            "The assist menu is locked for this seed." if want else "The assist menu is unlocked."
        )
    else:
        ctx.assists_retry_at = time.monotonic() + LABELS_RETRY_SECONDS


async def labels_tick(ctx: ZexionContext) -> None:
    """Once the server has said what each location holding another player's item holds, send
    the helper a label per map cell (client/aplabels.py), while this seed's save is loaded. While
    another save (or none) is loaded, and after a disconnect, take them back."""
    helper = ctx.helper
    if helper is None:
        return
    if not ctx.labels_want:
        if not ctx.labels_held or time.monotonic() < ctx.labels_retry_at:
            return
        ours = ctx.labels_sent
        reply = await asyncio.to_thread(helper.request, "ap_labels")  # none: Scrap Energy again
        if helper is not ctx.helper:
            return
        if reply.startswith("labeled"):
            ctx.labels_sent = ctx.labels_held = False
            if ours:
                logger.info("Not this seed's save: other players' items are Scrap Energy again.")
        else:
            ctx.labels_retry_at = time.monotonic() + LABELS_RETRY_SECONDS  # asked again later
        return
    if (
        ctx.labels_sent
        or ctx.labels_refused
        or not ctx.foreign
        or ctx.session is None
        or time.monotonic() < ctx.labels_retry_at
    ):
        return
    infos = [ctx.locations_info.get(LOCATION_ID_BASE + i) for i in ctx.foreign]
    if any(info is None for info in infos):
        return  # the server has not answered yet
    labels = {}
    for i, info in zip(ctx.foreign, infos, strict=True):
        player = ctx.player_names.get(info.player, "")
        slot = ctx.slot_info.get(info.player)
        code = info.item - ITEM_ID_BASE
        if slot is not None and slot.game == GAME and 0 <= code <= 50:
            text = aplabels.zexion_label(code, player)  # another Zexion player's own item
        else:
            text = aplabels.label(bool(info.flags & ItemClassification.progression), player)
        labels[tuple(ctx.session.cells[i])] = text
    connection = ctx.connection
    reply = await asyncio.to_thread(helper.request, aplabels.labels_request(labels))
    if connection != ctx.connection or helper is not ctx.helper:
        return  # a reconnect or a lost helper meanwhile: the next tick sends what is current
    if reply.startswith("labeled"):
        ctx.labels_sent = ctx.labels_held = True
        logger.info(
            f"Other players' items show as Archipelago items, named at pickup ({len(labels)})."
        )
    elif reply.startswith("refused"):
        ctx.labels_retry_at = time.monotonic() + LABELS_RETRY_SECONDS  # no frame came yet
    else:
        logger.warning(f"The helper did not take the item labels: {reply}")
        ctx.labels_refused = True  # not asked again until a helper is attached again


async def shields_tick(ctx: ZexionContext, received: list[int]) -> None:
    """Spawn received Energy Shields not given yet, one at a time, through the helper, and
    count each in the server's data storage once the game shows it up in play; spawn it again
    if it is not seen within SHIELD_SEEN_SECONDS, or the player died or the save was unloaded
    first (reconcile.shield_outcome). An Energy Shield's effect is its pickup's spawn, not a
    byte: docs/re/memory.md, "Received Energy Shields"."""
    key, game_conn = ctx.shield_key, ctx.game_conn
    if key is None or key not in ctx.stored_data or game_conn is None:
        return  # no game, or the count not read from the server yet
    given = ctx.shield_given
    if given is not None and given[0] == key:
        if (ctx.stored_data.get(key) or 0) >= given[1]:
            ctx.shield_given = None  # the server has it
        elif time.monotonic() >= ctx.shield_count_sent_at + SHIELD_GAP_SECONDS:
            await send_shield_count(ctx)
    pending = ctx.shield_pending
    if pending is not None and pending.key != key:
        ctx.shield_pending = pending = None  # spawned for another slot's seed: not counted
    if pending is not None:
        await confirm_shield(ctx, game_conn, pending, key)
        return
    helper = ctx.helper
    if helper is None or time.monotonic() < ctx.shield_retry_at:
        return
    if reconcile.shields_owed(received, shields_given(ctx, key)) <= 0:
        return
    reply = await asyncio.to_thread(helper.request, "shield")
    if ctx.shield_key not in (None, key):
        return  # another slot now: the spawn is that game's, not this seed's
    if reply == "spawned":
        ctx.shield_pending = PendingShield(key, time.monotonic())
        ctx.shield_said = ""
    else:
        if reply != ctx.shield_said and reply != "refused not settled":
            logger.info(f"A received Energy Shield is waiting: {reply}")
        ctx.shield_said = reply
        ctx.shield_retry_at = time.monotonic() + SHIELD_GAP_SECONDS


async def confirm_shield(
    ctx: ZexionContext, game_conn: game.Game, pending: PendingShield, key: str
) -> None:
    """One look at the game for a spawned shield: count it, wait, or let it be spawned again."""

    def look() -> tuple[str, bool]:
        state = game_conn.settled_state()
        if state != game.IN_PLAY:
            return state, False
        try:
            return state, game_conn.shield_up()
        except game.GameNotRunning:
            raise
        except game.GameError as exc:  # the link leads nowhere readable: not seen
            said = f"check failed: {exc}"
            if said != ctx.shield_said:
                logger.warning(f"Reading the player's Energy Shield failed ({exc}).")
            ctx.shield_said = said
            return state, False

    try:
        state, up = await asyncio.to_thread(look)
    except game.GameError:  # the state could not be read: look again; the loop sees a close
        state, up = game.CHANGING, False
    if ctx.shield_pending is not pending:
        return
    if state in (game.DEAD, game.NO_SAVE):
        pending.left_play = True
    if state != game.IN_PLAY:
        pending.at = time.monotonic()  # the time to see it runs in play only
    overdue = time.monotonic() >= pending.at + SHIELD_SEEN_SECONDS
    outcome = reconcile.shield_outcome(state == game.IN_PLAY, up, pending.left_play, overdue)
    if outcome == reconcile.WAIT:
        return
    ctx.shield_pending = None
    if outcome == reconcile.AGAIN:
        ctx.shield_misses += 1
        if ctx.shield_misses >= SHIELD_MISSES:
            logger.warning(
                f"{ctx.shield_misses} received Energy Shields in a row were not seen in the "
                f"game; trying again in {SHIELD_BACKOFF_SECONDS:.0f} seconds."
            )
            ctx.shield_misses = 0
            ctx.shield_retry_at = time.monotonic() + SHIELD_BACKOFF_SECONDS
        else:
            logger.info("A received Energy Shield was not seen in the game; spawning it again.")
            ctx.shield_retry_at = time.monotonic() + SHIELD_GAP_SECONDS
        return
    ctx.shield_misses = 0
    ctx.shield_given = (key, shields_given(ctx, key) + 1)
    logger.info("A received Energy Shield is up.")
    ctx.shield_retry_at = time.monotonic() + SHIELD_GAP_SECONDS
    await send_shield_count(ctx)


def shields_given(ctx: ZexionContext, key: str) -> int:
    """Shields counted as given for this slot: the server's number, or this client's when the
    server has not taken it yet."""
    stored = ctx.stored_data.get(key) or 0
    if ctx.shield_given is not None and ctx.shield_given[0] == key:
        return max(stored, ctx.shield_given[1])
    return stored


async def send_shield_count(ctx: ZexionContext) -> None:
    """Send the shields counted as given to the server as a maximum, so sending it twice does
    no harm; shields_tick sends it again until the server's number reaches it."""
    if ctx.shield_given is None:
        return
    key, count = ctx.shield_given
    ctx.shield_count_sent_at = time.monotonic()
    try:
        await ctx.send_msgs(
            [
                {
                    "cmd": "Set",
                    "key": key,
                    "default": 0,
                    "want_reply": True,
                    "operations": [{"operation": "max", "value": count}],
                }
            ]
        )
    except Exception as exc:  # a closing connection: sent again once connected
        logger.warning(f"Sending the Energy Shield count failed ({exc}); sending it again.")


async def deathlink_tick(ctx: ZexionContext, step: session.Step) -> None:
    """Send a death of the player's own to the other players, and deal the ones received from
    them, queued, through the helper, one each time the player is in play (client/deathlink.py)."""
    now = time.monotonic()
    state = game.NO_SAVE if step.left_seed else step.state
    if ctx.deaths.observe(state, now):
        try:
            await ctx.send_death(f"{ctx.player_names.get(ctx.slot, 'A player')} died in Zexion.")
        except Exception as exc:  # a closing connection: this death is not sent
            logger.warning(f"DeathLink: sending the death failed ({exc}).")
    if step.goal and ctx.deaths.owed:
        ctx.deaths.ending()
        logger.info("DeathLink: the ending began, so the received deaths queued are dropped.")
    helper = ctx.helper
    if not ctx.deaths.owed or helper is None or state != game.IN_PLAY or now < ctx.death_retry_at:
        return
    ctx.deaths.dealing(now)  # a death seen after a lost reply is this one
    reply = await asyncio.to_thread(helper.request, "die", native.APPLY_SECONDS)
    if reply == "died":
        ctx.deaths.dealt(time.monotonic())
        ctx.death_said = ""
        more = f" ({ctx.deaths.owed} more queued)" if ctx.deaths.owed else ""
        logger.info(f"DeathLink: the received death was dealt{more}.")
        return
    ctx.deaths.refused()
    if reply != ctx.death_said and reply != "refused not settled":
        logger.info(f"DeathLink: the received death is waiting: {reply}")
    ctx.death_said = reply
    ctx.death_retry_at = time.monotonic() + DEATH_RETRY_SECONDS


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
    if ctx.plan is None or ctx.newgame_state in ("done", "unavailable"):
        return
    if ctx.newgame_state is None:
        plan = ctx.plan
        build = ctx.game_conn.build.sha256 if ctx.game_conn is not None else ""
        offer = plan.menu is not None and not ctx.keep_randomizer and not ctx.choice_refused
        try:
            text = newgame.plan_text(plan, build, offer)
        except newgame.NewGameError as exc:
            if not offer:
                ctx.newgame_state = "unavailable"
                logger.info(f"This seed cannot start as a new game ({exc}); use /prepare instead.")
                return
            logger.info(f"ARCHIPELAGO cannot set up this seed ({exc}); RANDOMIZER stays.")
            offer = False
            try:
                text = newgame.plan_text(plan, build, offer)
            except newgame.NewGameError as again:
                ctx.newgame_state = "unavailable"
                logger.info(f"This seed cannot start as a new game ({again}); use /prepare.")
                return
        reply = await asyncio.to_thread(helper.request, text)
        if reply == "armed":
            ctx.newgame_state = "armed"
            ctx.newgame_polled_at = time.monotonic()
            if offer:
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
        if "start table" in status:
            ctx.choice_refused = True  # the game's own table differs: RANDOMIZER from now on
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
                await labels_tick(ctx)
                await assists_tick(ctx)
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
            if ctx.shield_pending is not None:
                ctx.shield_pending.left_play = True
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
        if step.left_seed and ctx.shield_pending is not None:
            ctx.shield_pending.left_play = True
        if step.seed_matches:
            if ctx.lock_assists and ctx.assists_unlocked():
                ctx.lock_assists = False
                logger.info("Assist Unlock received: the assist menu is no longer locked.")
            ctx.assists_want = ctx.lock_assists
        elif step.left_seed:
            ctx.assists_want = False
        if ctx.session is current:  # a poll from before a reconnect says nothing of this seed
            ctx.labels_want = aplabels.labels_wanted(
                step.seed_matches, step.left_seed, ctx.labels_want
            )
        if ctx.death_link_on and ctx.session is current:
            try:
                await deathlink_tick(ctx, step)
            except native.HelperError as exc:
                logger.warning(f"The in-game helper failed ({exc}); trying it again later.")
                ctx.drop_helper()
        if not step.seed_matches or ctx.session is not current:
            continue  # nothing to send, or the connection changed while the game was read
        shield_work = ctx.shield_pending is not None or ctx.shield_given is not None
        if ctx.helper is not None or shield_work:
            try:
                await shields_tick(ctx, codes)
            except native.HelperError as exc:
                logger.warning(f"The in-game helper failed ({exc}); trying it again later.")
                ctx.drop_helper()
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
