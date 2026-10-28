"""The game loop's side tasks with the in-game helper: attaching it, and telling it what this
seed's save needs (the labels, the ASSIST lock, INTEL CHIP, the OS Install Disks and their
popups, the new game, received Energy Shields and DeathLinks, the offline files, and the
notices for another seed's save, the client coming back, or the Feats guard not in place).

Each tick asks the helper only when something changed or a retry is due, and keeps what it
told in client/state.py (``HelperState``, mostly). client/zexion_client.py runs them (its
``poll``, through ``side_tick``, so a failing one never stops items or checks), only while a
helper is attached, except offline_tick, shields_tick and deathlink_tick, which check.
"""

from __future__ import annotations

import asyncio
import logging
import time
from typing import TYPE_CHECKING

from BaseClasses import ItemClassification

from . import aplabels, discs, game, ids, native, newgame, offline, reconcile, shields, state
from .ids import ITEM_ID_BASE, LOCATION_ID_BASE
from .storage import hint_note, hints_used, popped, store, store_popped

if TYPE_CHECKING:
    from . import session
    from .zexion_client import Poll, ZexionContext

logger = logging.getLogger("Client")  # CommonClient's: the client's window shows it
# Technical detail behind a window message (exception text, addresses, replies): the log file
# and the console, not the window (Archipelago's window shows the "Client" logger only).
log = logging.getLogger("Zexion")
OFFLINE_FAILED = (
    "Couldn't save this seed for playing without the client; playing with it is unaffected."
)

# The client's window shows what the player acts on (the connection, the mod, ARCHIPELAGO,
# warnings); what it sets up in the game for the seed is a detail, shown with --verbose.
VERBOSE = False


def detail(message: str) -> None:
    """A setup note: in the window and the log with --verbose, else at debug level."""
    (logger.info if VERBOSE else logger.debug)(message)


NEWGAME_POLL_SECONDS = 1.0
REFUSED_RETRY_SECONDS = 5.0  # before asking the helper again after it refused a request
SLOTS_READ_SECONDS = 2.0  # between reads of the save slots for the file select's count
OFFLINE_WRITE_SECONDS = 5.0  # between looks at what the helper needs without the client
DEATH_RETRY_SECONDS = 1.0  # before asking the helper again to deal a received death


async def attach_helper(ctx: ZexionContext) -> None:
    """Load and configure the native helper in the attached game, or carry on without it."""
    dll = native.default_dll()  # used when the game has none loaded (no proxy in its folder)
    notes: list[str] = []
    try:
        helper = await asyncio.to_thread(native.attach, ctx.game_conn, dll, notes)
    except native.HelperError as exc:
        # Tried every 30 s: each line said again only when it changes.
        line = state.helper_failed(exc)
        if line is not None and ctx.said("helper", line):
            logger.warning(line)
        if ctx.said("helper detail", str(exc)):
            log.warning(f"The helper could not be used: {exc!r}")
        ctx.drop_helper()
        return
    ctx.said.clear("helper", "helper detail")
    # A helper still loaded from before may hold a lock, labels (taken back unless this seed's
    # save is loaded, where this seed's are sent over them) and so on: it is told everything
    # again, and told the client is back if the save was played offline.
    ctx.helper = helper
    ctx.told = state.HelperState()
    if ctx.conn is not None:
        ctx.conn.session.helper = helper
    ctx.load.return_home = discs.ReturnHome()  # and any reminder or prompt of the one before
    for note in notes:
        # Where the helper came from is a detail; a copy to update is not.
        (detail if note.startswith("The helper was loaded with the game") else logger.info)(note)
    logger.info(f"The Zexion mod {native.VERSION} is running.")


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


async def tell(
    helper: native.Helper, told: state.Toggle, want: object, request: str, ok: str
) -> bool:
    """Send ``request`` (for ``want``) unless the helper last took that very ``want`` (``told``),
    or a refusal's wait is on; True once it takes it now (its reply ``ok``)."""
    if want == told.sent or time.monotonic() < told.retry_at:
        return False
    if await asyncio.to_thread(helper.request, request) != ok:
        told.retry_at = time.monotonic() + REFUSED_RETRY_SECONDS
        return False
    told.sent = want
    return True


async def toggle_tick(ctx: ZexionContext, toggle: state.Toggle, want: bool, spec: tuple) -> None:
    """Tell the helper to turn a setting on or off (ASSISTS: the options menu's ASSIST entry
    locked, docs/re/memory.md "The assist menu"; INTEL: the game's Area Map named INTEL CHIP,
    its map reveal off, "Intel Chips"). Sent when that changes, or to a new helper."""
    requests, replies, notes = spec
    if await tell(ctx.helper, toggle, want, requests[want], replies[want]) and notes[want]:
        detail(notes[want])


async def discs_helper_tick(ctx: ZexionContext, p: Poll) -> None:
    """With the OS Install Disks goal, give the helper the count and the scanner's line while this
    seed's save is loaded (docs/re/memory.md, "The scanner"), and take them back otherwise.
    Sent when that changes, or to a new helper."""
    helper, told, conn = ctx.helper, ctx.told, ctx.conn
    hunt = conn.hunt if conn is not None else None
    if hunt is not None and ctx.load.seed_loaded and not conn.items_listed:
        return  # the items not listed yet, or a dropped connection: the count waits for them
    if hunt is not None and ctx.load.seed_loaded:
        have = discs.owned(hunt, p.codes, p.found)
        want = discs.request(have, hunt.required, conn.discs_told)
    else:
        want = discs.OFF
    before = told.discs.sent
    if await tell(helper, told.discs, want, want, "discs off" if want == discs.OFF else "discs on"):
        if want == discs.OFF and before not in (None, discs.OFF):
            detail("Not this seed's save: the scanner's message is the game's again.")
        elif want != discs.OFF and before in (None, discs.OFF):
            detail("The scanner's message names the OS Install Disks needed.")
    elif want == before and want != discs.OFF and time.monotonic() >= told.discs_status_at:
        # Whether the equipment screen showed the count when last opened (logged on change).
        told.discs_status_at = time.monotonic() + REFUSED_RETRY_SECONDS
        status = await asyncio.to_thread(helper.request, "discs_status")
        if status != told.discs_status:
            told.discs_status = status
            detail(f"The equipment screen's OS Install Disk count: {status}.")


async def slots_tick(ctx: ZexionContext, p: Poll) -> None:
    """With the OS Install Disks goal, give the helper the disk count for the file select's
    entries of the slots holding this seed (read from the game every few seconds), and take it
    back otherwise."""
    told, conn, g = ctx.told, ctx.conn, ctx.game_conn
    if time.monotonic() < told.slots_read_at:
        return
    told.slots_read_at = time.monotonic() + SLOTS_READ_SECONDS
    hunt = conn.hunt if conn is not None else None
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
    ok = "disk_slots off" if want == discs.SLOTS_OFF else "disk_slots on"
    await tell(ctx.helper, told.slots, want, want, ok)


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
        elif slot is not None and slot.game == ctx.game and (look := aplabels.zexion_look(code)):
            # Another Zexion player's own item, whatever that player's settings.
            item, name = look
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
    """The config file, built once per attach to the game (in a thread: it reads dozens of
    places in the game); None while no game is attached, or if building it failed (tried again)."""
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
    conn = ctx.conn
    if conn is None or not conn.items_listed:
        return None
    built = foreign_labels(ctx) if conn.foreign else ({}, 0)
    if built is None:
        return None
    labels = aplabels.labels_request(built[0]) if built[0] else None
    hunt = conn.hunt
    disc_info = None
    if hunt is not None:
        have = discs.owned(hunt, p.codes, p.found)
        unfound = offline.grid_cells(conn.session.cells, hunt.locations - set(p.found))
        disc_info = (have, hunt.required, discs.scan_line(hunt.required), unfound)
    return offline.seed_text(
        conn.plan.location_items,
        conn.plan.start,
        conn.intel_chip,
        conn.lock_assists,
        disc_info,
        labels,
    )


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
            ctx.said.clear("offline")
        elif ctx.said("offline", "failed"):  # said once until a file is kept again
            logger.warning(OFFLINE_FAILED)
            log.warning(f"The helper did not keep the offline {kind} file: {reply}")


async def labels_tick(ctx: ZexionContext) -> None:
    """Once the server has said what each location holding another player's item holds, send
    the helper a label per map cell (client/aplabels.py), while this seed's save is loaded. While
    another save (or none) is loaded, and after a disconnect, take them back."""
    helper, told, conn = ctx.helper, ctx.told, ctx.conn
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
        logger.warning(
            "Other players' items can't be shown as Archipelago items in the game; checks "
            "still work."
        )
        log.warning(f"The helper did not take the item labels: {reply}")
        told.labels_refused = True  # not asked again until a helper is attached again


def stored_shields(ctx: ZexionContext, key: str) -> int | None:
    """The server's count of Energy Shields given, read afresh (state.shield_count): None, and
    said once, when the key holds something other than a count."""
    count = state.shield_count(ctx.stored_data.get(key))
    if count is None:
        if ctx.said("shields", "bad count"):
            logger.warning("The server's Energy Shield count is not a number: shields wait.")
        return None
    ctx.said.clear("shields")
    return count


async def shields_tick(ctx: ZexionContext, received: list[int]) -> None:
    """Spawn received Energy Shields not given yet, one at a time, through the helper, and
    count each in the server's data storage once the game shows it up in play (client/
    shields.py)."""
    conn, game_conn, sh = ctx.conn, ctx.game_conn, ctx.shields
    if conn is None or conn.shield_key not in ctx.stored_data or game_conn is None:
        return  # no game, or the count not read from the server yet
    key = conn.shield_key
    stored = stored_shields(ctx, key)
    if stored is None:
        return
    if sh.resend_due(key, stored, time.monotonic()):
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
    stored = stored_shields(ctx, key)  # read again: the server may have answered meanwhile
    if stored is None:
        return
    given = sh.given_count(key, stored)
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
    """One look for a spawned shield: count it, wait, or spawn it again. The state is settled
    again here: the step's may be stale after the helper requests, and a death since would
    count a lost shield."""
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
            logger.warning("Can't tell whether a received Energy Shield is up; trying again.")
            log.warning(f"Reading the player's Energy Shield failed ({failed}).")
        sh.said = said
    stored = stored_shields(ctx, key)
    if stored is None:
        return  # looked at again once the count is a number
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
            logger.warning("DeathLink: your death couldn't be sent (connection lost?).")
            log.warning(f"DeathLink: sending the death failed ({exc}).")
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
        ctx.said.clear("death")
        more = f" ({deaths.owed} more queued)" if deaths.owed else ""
        detail(f"DeathLink: the received death was dealt{more}.")
        return
    deaths.refused()
    if ctx.said("death", reply) and reply != "refused not settled":
        detail(f"DeathLink: the received death is waiting: {reply}")
    seed.death_retry_at = time.monotonic() + DEATH_RETRY_SECONDS


async def newgame_tick(ctx: ZexionContext) -> None:
    """Arm the helper with the AP seed for the next randomizer new game, and report what
    became of it. Runs in the game loop, the helper's only user."""
    helper = ctx.helper
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
        choice = not ctx.keep_randomizer and not conn.choice_refused
        for offer in (True, False) if choice else (False,):  # ARCHIPELAGO, else RANDOMIZER
            try:
                text = newgame.plan_text(plan, build, offer)
                break
            except newgame.NewGameError as exc:
                log.info(f"No new game plan (ARCHIPELAGO: {offer}): {exc}")
                if offer:
                    logger.info(
                        "This seed can't be started with ARCHIPELAGO, so start it from "
                        "RANDOMIZER (see the next message)."
                    )
                    continue
                conn.newgame = state.NewGame.UNAVAILABLE
                logger.warning(
                    "This seed can't be started as a new game: please report it at "
                    f"{state.REPORT}, with the seed's settings."
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
            for line in newgame.randomizer_message(plan, conn.mode):
                logger.info(line)
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
        why = status[len("refused ") :]
        log.warning(f"No new game with the AP seed: {why}.")
        if why.startswith("the new game is not the seed's mode"):  # RANDOMIZER's START GAME
            logger.warning(
                "That new game isn't this seed (other amounts or settings), so it plays "
                "as an ordinary randomizer game. Start another in an empty slot."
            )
        elif "start table" in why:
            conn.choice_refused = True  # the game's own table differs: RANDOMIZER from now on
            logger.warning(
                "ARCHIPELAGO couldn't start this seed, so start it from RANDOMIZER (see the next "
                "message)."
            )
        else:
            logger.warning("ARCHIPELAGO couldn't start this seed; try again in an empty slot.")
    elif status == "idle":
        conn.newgame = None  # the helper lost the plan (reloaded): arm again


async def other_save_tick(ctx: ZexionContext, step: session.Step) -> None:
    """Once a load: a randomizer save of another seed in play. If it is an AP save (a seed the
    helper has a file for), the helper and the window tell the player items wait, and the save
    gets its look (other_save; client/offline.py); a plain randomizer game is left alone."""
    helper, told = ctx.helper, ctx.told
    if not step.other_seed:
        # Asked again for the next such save: the helper says it once a load, and again after
        # a reconnect (Game::other_save).
        told.other_save_sent = told.other_save_seen = False
        return
    if told.other_save_sent or step.state != game.IN_PLAY:
        return
    if not told.other_save_seen:
        # Asked from the next poll's look: a load caught half way is not taken for another seed.
        told.other_save_seen = True
        return
    reply = await asyncio.to_thread(helper.request, "other_save")
    if reply.startswith("refused"):
        return
    told.other_save_sent = True
    if reply == "unknown":
        return  # a plain randomizer game (no seed the mod has a file for): left alone
    if ctx.said("other save", "loaded"):  # once a load
        logger.info(state.OTHER_SAVE)


async def feats_tick(ctx: ZexionContext, step: session.Step) -> None:
    """While this seed's save is loaded: whether the helper keeps Feats and Steam achievements
    out of it (its hello's ``feats``; native/README.md, "Feats and achievements in AP saves").
    Asked every few seconds until it is on for this load; the player is warned when it is on
    with why not all of it, or still off with the save in play (state.FeatsCheck)."""
    helper, check = ctx.helper, ctx.load.feats
    if not step.seed_matches or not check.due(time.monotonic()):
        return
    feats = (await asyncio.to_thread(helper.hello)).get("feats", "")
    said = check.seen(feats, step.state == game.IN_PLAY, time.monotonic(), ctx.said)
    if said is not None:
        line, detail = said
        logger.warning(line)
        log.warning(f"The mod's Feats and achievements guard: {detail}")


async def client_back_tick(ctx: ZexionContext, step: session.Step) -> None:
    """Once per attach, when this seed's save is first seen in play: if the helper dressed it
    with no client (client/offline.py), it pops up ARCHIPELAGO CLIENT CONNECTED (client_back)."""
    helper, told = ctx.helper, ctx.told
    if told.client_back_sent or not step.seed_matches or step.state != game.IN_PLAY:
        return
    reply = await asyncio.to_thread(helper.request, "client_back")
    if not reply.startswith("refused"):
        told.client_back_sent = True
        if reply == "shown":
            detail("This save was played without the client: told the client is back.")


async def followups_tick(ctx: ZexionContext, p: Poll, step: session.Step) -> None:
    """The follow-up popup (an OS Install Disk's count, an Intel Chip's hint) for each of this
    world's finds in the loaded save, found with or without a client; the server's popped_key
    keeps which were shown, so each is shown once."""
    conn, helper, told = ctx.conn, ctx.helper, ctx.told
    if conn is None or not conn.items_listed or not step.seed_matches:
        return
    if not told.client_back_sent:
        return  # after the helper has been asked to say the client is back (client_back_tick)
    done = popped(ctx)
    if done is None:
        return  # the server's record not read yet
    await store_popped(ctx, conn, due_only=True)  # one lost with a closing connection, again
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
    # This client's record holds them until the server has them.
    ctx.seed_state.popped_mine |= set(disks) | set(maps)
    await store_popped(ctx, conn)


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
    if not home.reminded:
        if not discs.popups_handed(conn.session.announced, len(p.codes)):
            return  # the received disks' popups first
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
