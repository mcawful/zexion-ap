"""The client's records in the server's data storage, and the Area Map hints kept there.

Each key is per team and slot (client/state.py, ``Connection.key``): how many received items
have had their popup (``announced_tick``), this world's locations found in game
(``own_found``, ``store_found``), the follow-up popups shown (``popped``) and the entry each
Area Map used for its hint (``hints_tick``). Every key is open to any client in the room, so
what is read from one is checked for form. client/zexion_client.py's game loop calls these.
"""

from __future__ import annotations

import asyncio
import logging
import time
from typing import TYPE_CHECKING

from . import hints, ids, state
from .ids import ITEM_ID_BASE, LOCATION_ID_BASE

if TYPE_CHECKING:
    from .zexion_client import Poll, ZexionContext

logger = logging.getLogger("Client")  # CommonClient's: the client's window shows it
log = logging.getLogger("Zexion")  # the log only (client/ticks.py, log)

HINTS_RESEND_SECONDS = 5.0  # before storing the Area Map hints again if the server lacks them
FOUND_RESEND_SECONDS = 5.0  # likewise for the locations found (store_found)
POPPED_RESEND_SECONDS = 5.0  # and the follow-up popups shown (store_popped)


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
        logger.warning(f"{what} failed (connection lost?); trying again.")
        log.warning(f"{what} failed ({exc}).")
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
        current.announced = queued
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
    return ctx.seed_state.popped_mine | stored_indices(ctx, conn.popped_key)


async def store_popped(ctx: ZexionContext, conn: state.Connection, due_only: bool = False) -> None:
    """Record the follow-up popups this client showed (popped) that the server's record lacks;
    with ``due_only``, only when POPPED_RESEND_SECONDS have passed since the last store (one sent
    may have been lost with a closing connection, and a new connection must not show it again)."""
    seed = ctx.seed_state
    missing = seed.popped_mine - stored_indices(ctx, conn.popped_key)
    if not missing or (due_only and time.monotonic() < seed.popped_sent_at + POPPED_RESEND_SECONDS):
        return
    seed.popped_sent_at = time.monotonic()
    value = {str(i): 1 for i in sorted(missing)}
    if not await store(ctx, conn.popped_key, "update", value, {}, "Recording the popups shown"):
        seed.popped_sent_at = 0.0  # sent again on the next poll


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
    # Once a connection, every hint given so far is created again: one sent as a connection
    # closed may never have reached the server (it keeps only new ones: nothing doubles).
    recreate = not conn.hints_recreated and server_open(ctx)
    if recreate and await create_hints(ctx, used.values()):
        conn.hints_recreated = True
    keys = hints.area_maps(p.codes, conn.plan.location_items, p.found)
    # The (player, location) of each item the player has from a location: received from
    # another world, or found in one of this world's (own_found).
    owned = {(item.player, item.location) for item in ctx.items_received}
    owned |= {(ctx.slot, LOCATION_ID_BASE + i) for i in p.found}
    new = hints.assign(conn.hint_targets, used, keys, owned)
    stored = stored_hints(ctx)
    if not new:
        behind = seed.hints_mine and any(k not in stored for k in seed.hints_mine)
        if behind and time.monotonic() >= seed.hints_sent_at + HINTS_RESEND_SECONDS:
            await store_hints(ctx, conn)  # the server has not taken the last ones yet
        return
    if not await create_hints(ctx, new.values()):
        return  # tried again once connected
    seed.hints_mine = (seed.hints_mine or {}) | new
    await store_hints(ctx, conn)


async def create_hints(ctx: ZexionContext, entries) -> bool:
    """The Archipelago hints for these entries (player, location), NO HINTS LEFT skipped;
    False, warned, when the send failed (a closing connection)."""
    by_player: dict[int, list[int]] = {}
    for player, location in entries:
        if (player, location) != hints.NONE_LEFT:
            by_player.setdefault(player, []).append(location)
    try:
        for player, locations in by_player.items():
            await ctx.send_msgs([{"cmd": "CreateHints", "player": player, "locations": locations}])
    except Exception as exc:
        logger.warning("Creating the Area Map hints failed (connection lost?); trying again.")
        log.warning(f"Creating the Area Map hints failed ({exc}).")
        return False
    return True


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
