"""client/storage.py: the records kept in the server's data storage, with a stand-in context (no
AP, no server)."""

from __future__ import annotations

import asyncio
import types

from fakes import connection

from client import state, storage

HINTED = [1, 3392510_520]  # (player, location) of a hint an Area Map gave


class FakeContext:
    """What storage.py reads of ZexionContext: the server's stored data, a socket that is open,
    and the messages sent."""

    def __init__(self, conn: state.Connection, seed: state.SeedState | None = None) -> None:
        self.conn = conn
        self.seed_state = seed or state.SeedState()
        self.slot = conn.slot
        self.stored_data: dict = {}
        self.items_received: list = []
        self.locations_checked: set[int] = set()
        self.server = types.SimpleNamespace(socket=types.SimpleNamespace(open=True, closed=False))
        self.sent: list[dict] = []
        self.fail = False

    async def send_msgs(self, msgs: list[dict]) -> None:
        if self.fail:
            raise ConnectionError("the connection is closing")
        self.sent.extend(msgs)


def stored(ctx: FakeContext) -> list[dict]:
    """The values of the Set messages sent."""
    return [m["operations"][0]["value"] for m in ctx.sent if m["cmd"] == "Set"]


def test_a_follow_up_record_lost_with_a_connection_is_stored_again() -> None:
    ctx = FakeContext(connection())
    ctx.seed_state.popped_mine = {3, 5}
    ctx.stored_data[ctx.conn.popped_key] = {"3": 1}  # the server lacks 5: its store was lost
    asyncio.run(storage.store_popped(ctx, ctx.conn, due_only=True))
    assert stored(ctx) == [{"5": 1}]
    asyncio.run(storage.store_popped(ctx, ctx.conn, due_only=True))
    assert len(ctx.sent) == 1, "not again before the resend time"
    ctx.stored_data[ctx.conn.popped_key] = {"3": 1, "5": 1}
    asyncio.run(storage.store_popped(ctx, ctx.conn))
    assert len(ctx.sent) == 1, "the server has them all: nothing to send"


def test_the_locations_found_are_stored_until_the_server_has_them() -> None:
    ctx = FakeContext(connection())
    conn = ctx.conn
    asyncio.run(storage.store_found(ctx, conn, {1, 2, 3}, {3}))
    assert stored(ctx) == [{"1": 1, "2": 1, "3": 1}], "the record not read yet: every one"
    ctx.stored_data[conn.found_key] = {"1": 1}  # the server lacks 2 and 3: a store was lost
    asyncio.run(storage.store_found(ctx, conn, {1, 2, 3}, set()))
    assert len(ctx.sent) == 1, "not again before the resend time"
    conn.found_sent_at -= storage.RESEND_SECONDS
    asyncio.run(storage.store_found(ctx, conn, {1, 2, 3}, set()))
    assert stored(ctx)[-1] == {"2": 1, "3": 1}, "due: the ones it lacks"
    asyncio.run(storage.store_found(ctx, conn, {1, 2, 3, 4}, {4}))
    assert stored(ctx)[-1] == {"2": 1, "3": 1, "4": 1}, "a new find: at once"


def test_the_follow_ups_shown_carry_over_a_reconnect() -> None:
    ctx = FakeContext(connection())
    ctx.seed_state.popped_mine = {5}
    ctx.conn = connection()  # a reconnect: a new connection, the same seed's state
    ctx.stored_data[ctx.conn.popped_key] = {}
    assert storage.popped(ctx) == {5}, "not shown again while the server's record lacks it"


def test_a_failed_follow_up_store_is_sent_on_the_next_poll() -> None:
    ctx = FakeContext(connection())
    ctx.seed_state.popped_mine = {5}
    ctx.stored_data[ctx.conn.popped_key] = {}
    ctx.fail = True
    asyncio.run(storage.store_popped(ctx, ctx.conn))
    ctx.fail = False
    asyncio.run(storage.store_popped(ctx, ctx.conn, due_only=True))
    assert stored(ctx) == [{"5": 1}]


def test_the_hints_given_are_created_again_once_a_connection() -> None:
    conn = connection(hints=[[*HINTED, 16]])
    ctx = FakeContext(conn)
    ctx.stored_data[conn.hints_key] = {"l7": HINTED, "l9": [-1, -1]}  # and one NO HINTS LEFT
    p = types.SimpleNamespace(codes=[], found=[])
    ctx.fail = True
    asyncio.run(storage.hints_tick(ctx, p))
    assert not conn.hints_recreated, "a failed send is tried again"
    ctx.fail = False
    asyncio.run(storage.hints_tick(ctx, p))
    hints = [m for m in ctx.sent if m["cmd"] == "CreateHints"]
    assert hints == [{"cmd": "CreateHints", "player": 1, "locations": [3392510_520]}]
    assert conn.hints_recreated
    asyncio.run(storage.hints_tick(ctx, p))
    assert len([m for m in ctx.sent if m["cmd"] == "CreateHints"]) == 1, "once a connection"


def test_the_popup_count_waits_for_the_items_to_be_listed() -> None:
    # Until the server lists the slot's items, the poll's codes are not all of them: a slot with
    # no count stored would count too few, and pop up again the items it already held.
    conn = connection()
    conn.items_listed = False
    ctx = FakeContext(conn)
    ctx.stored_data[conn.announced_key] = None  # read: none stored
    p = types.SimpleNamespace(codes=[16, 17], found=[])
    asyncio.run(storage.announced_tick(ctx, conn, p))
    assert conn.session.announced is None and ctx.sent == []
    conn.items_listed = True
    asyncio.run(storage.announced_tick(ctx, conn, p))
    assert conn.session.announced == 2 and stored(ctx) == [2]
