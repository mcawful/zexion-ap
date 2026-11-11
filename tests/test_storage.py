"""client/storage.py: the records kept in the server's data storage, with a stand-in context (no
AP, no server)."""

from __future__ import annotations

import asyncio
import types

from fakes import connection, seed_state

from client import ids, state, storage

HINTED = [1, 3392510_520]  # (player, location) of a hint an Area Map gave


class FakeContext:
    """What storage.py reads of ZexionContext: the server's stored data, a socket that is open,
    and the messages sent."""

    def __init__(self, conn: state.Connection, seed: state.SeedState | None = None) -> None:
        self.conn = conn
        self.seed_state = seed or seed_state(conn)
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
    ctx.stored_data[conn.held_key] = []
    p = types.SimpleNamespace(codes=[16, 17], found=[])
    current = ctx.seed_state.session
    asyncio.run(storage.announced_tick(ctx, conn, current, p))
    assert not current.announced_read and ctx.sent == []
    conn.items_listed = True
    asyncio.run(storage.announced_tick(ctx, conn, current, p))
    assert current.announced_read and current.announced == 2 and stored(ctx) == [2]


def held_and_count(ctx: FakeContext) -> None:
    """The server takes what the client stored: the held traps' record and the count."""
    conn = ctx.conn
    for m in ctx.sent:
        if m["cmd"] == "Set":
            ctx.stored_data[m["key"]] = m["operations"][0]["value"]
    ctx.sent.clear()
    assert conn.held_key in ctx.stored_data and conn.announced_key in ctx.stored_data


def test_traps_held_at_a_slots_first_connection_still_pop_up() -> None:
    # No count stored: everything held counts as handed over (no flood of old popups), but the
    # traps among it are kept for their popup, which sets them off (Session.held_traps); their
    # record goes to the server before the count, which is stored at once.
    conn = connection()
    ctx = FakeContext(conn)
    ctx.stored_data[conn.announced_key] = ctx.stored_data[conn.held_key] = None
    held = [17, ids.DISRUPTION_TRAP, 46, ids.DISORIENT_TRAP]
    current = ctx.seed_state.session
    asyncio.run(storage.announced_tick(ctx, conn, current, types.SimpleNamespace(codes=held)))
    assert current.announced == 4 and current.held_traps == [1, 3]
    assert stored(ctx) == [[1, 3], 4], "the waiting traps, then the count"


def test_held_traps_and_later_popups_survive_a_reconnect() -> None:
    # The first connection holds the trap at 1; two items come before the player is in play;
    # a reconnect keeps the trap waiting and still pops the two up (the count stayed 3).
    conn = connection()
    ctx = FakeContext(conn)
    ctx.stored_data[conn.announced_key] = ctx.stored_data[conn.held_key] = None
    current = ctx.seed_state.session
    first = types.SimpleNamespace(codes=[17, ids.DISRUPTION_TRAP, 46])
    asyncio.run(storage.announced_tick(ctx, conn, current, first))
    held_and_count(ctx)
    current.connected()
    later = types.SimpleNamespace(codes=[17, ids.DISRUPTION_TRAP, 46, 46, 18])
    asyncio.run(storage.announced_tick(ctx, conn, current, later))
    assert current.held_traps == [1] and current.announced == 3, "both kept"
    assert ctx.sent == [], "nothing to store again"
    current.held_traps, current.announced = [], 5  # handed over (give_through_helper)
    asyncio.run(storage.announced_tick(ctx, conn, current, later))
    assert stored(ctx) == [[], 5], "the record emptied, then the count"


def test_held_traps_and_later_popups_survive_a_client_restart() -> None:
    conn = connection()
    ctx = FakeContext(conn)
    ctx.stored_data[conn.announced_key] = ctx.stored_data[conn.held_key] = None
    first = types.SimpleNamespace(codes=[ids.DISORIENT_TRAP, 17])
    asyncio.run(storage.announced_tick(ctx, conn, ctx.seed_state.session, first))
    held_and_count(ctx)
    restarted = FakeContext(connection())
    restarted.stored_data = dict(ctx.stored_data)
    again = restarted.seed_state.session
    later = types.SimpleNamespace(codes=[ids.DISORIENT_TRAP, 17, 46])
    asyncio.run(storage.announced_tick(restarted, restarted.conn, again, later))
    assert again.held_traps == [0] and again.announced == 2, "the trap waits, the 46 pops up"


def test_a_failed_record_of_the_held_traps_stores_no_count() -> None:
    # Sent again on the next poll, the count with it: a restart never finds one without the other.
    conn = connection()
    ctx = FakeContext(conn)
    ctx.stored_data[conn.announced_key] = ctx.stored_data[conn.held_key] = None
    ctx.fail = True
    current = ctx.seed_state.session
    codes = types.SimpleNamespace(codes=[ids.DISORIENT_TRAP])
    asyncio.run(storage.announced_tick(ctx, conn, current, codes))
    assert ctx.sent == [] and conn.held_sent is None and conn.announced_sent is None
    ctx.fail = False
    asyncio.run(storage.announced_tick(ctx, conn, current, codes))
    assert stored(ctx) == [[0], 1]


def test_a_stored_count_holds_back_no_trap() -> None:
    conn = connection()
    ctx = FakeContext(conn)
    ctx.stored_data[conn.announced_key] = 1
    ctx.stored_data[conn.held_key] = []
    codes = [ids.DISRUPTION_TRAP, ids.DISORIENT_TRAP]
    current = ctx.seed_state.session
    asyncio.run(storage.announced_tick(ctx, conn, current, types.SimpleNamespace(codes=codes)))
    assert current.announced == 1 and current.held_traps == [], "the second pops up as new"


def test_a_reconnect_reads_the_count_again_never_below_the_sessions_own() -> None:
    # The session handed 3 over before the connection dropped; the last count stored was 1.
    conn = connection()
    ctx = FakeContext(conn)
    current = ctx.seed_state.session
    current.announced, current.announced_read = 3, False  # zexion_client's connected
    ctx.stored_data[conn.announced_key] = 1
    ctx.stored_data[conn.held_key] = []
    p = types.SimpleNamespace(codes=[16, 17, 18, 19], found=[])
    asyncio.run(storage.announced_tick(ctx, conn, current, p))
    assert current.announced_read and current.announced == 3 and stored(ctx) == [3]


def test_a_connection_that_has_ended_neither_reads_nor_stores_the_count() -> None:
    # A poll that began before a Disconnect and Connect (to this seed, or another room's with
    # the same team and slot, whose key is the same): the new connection reads its own.
    old = connection()
    ctx = FakeContext(old)
    current = ctx.seed_state.session
    current.announced = 3
    ctx.conn = connection()
    ctx.stored_data[old.announced_key] = 1
    ctx.stored_data[old.held_key] = []
    p = types.SimpleNamespace(codes=[16, 17, 18], found=[])
    asyncio.run(storage.announced_tick(ctx, old, current, p))
    assert not current.announced_read and ctx.sent == []


CHIP_HINT = "Hint: Slide Mod is at Guardia 22,7"


def chip_seed() -> tuple[FakeContext, state.Connection, types.SimpleNamespace]:
    """An Intel Chip seed whose slot has received one chip from another world (while the client
    was away), its popup not handed over yet."""
    conn = connection(hints=[[*HINTED, 16]], intel_chip=True)
    ctx = FakeContext(conn)
    ctx.item_names = types.SimpleNamespace(lookup_in_slot=lambda code, slot: "Slide Mod")
    ctx.location_names = types.SimpleNamespace(lookup_in_slot=lambda loc, player: "Guardia 22,7")
    ctx.seed_state.session.announced, ctx.seed_state.session.announced_read = 0, False
    return ctx, conn, types.SimpleNamespace(codes=[ids.AREA_MAP], found=[])


def test_the_popup_count_is_not_read_before_the_hints() -> None:
    # Were it read first, the chip's popup would go out with no hint and the count move past it:
    # its hint, given later, would never show in the game.
    ctx, conn, p = chip_seed()
    current = ctx.seed_state.session
    ctx.stored_data[conn.announced_key] = 0
    ctx.stored_data[conn.held_key] = []
    asyncio.run(storage.announced_tick(ctx, conn, current, p))
    assert not current.announced_read, "not before the hints are read"
    ctx.stored_data[conn.hints_key] = None  # read: the server has none stored
    asyncio.run(storage.announced_tick(ctx, conn, current, p))
    assert current.announced_read and current.announced == 0
    asyncio.run(storage.hints_tick(ctx, p))
    assert storage.hint_notes(ctx, p.codes) == {0: CHIP_HINT}


def test_without_area_map_hints_the_popup_count_needs_no_hints_read() -> None:
    conn = connection()
    ctx = FakeContext(conn)
    current = ctx.seed_state.session
    current.announced_read = False
    ctx.stored_data[conn.announced_key] = 1
    ctx.stored_data[conn.held_key] = []
    asyncio.run(storage.announced_tick(ctx, conn, current, types.SimpleNamespace(codes=[16, 17])))
    assert current.announced_read and current.announced == 1


def test_a_chip_whose_hint_failed_to_send_still_has_its_hint_and_it_is_sent_again() -> None:
    ctx, conn, p = chip_seed()
    ctx.stored_data[conn.hints_key] = {}
    conn.hints_recreated = True  # this connection's hints created again already
    ctx.fail = True
    asyncio.run(storage.hints_tick(ctx, p))
    assert storage.hint_notes(ctx, p.codes) == {0: CHIP_HINT}
    ctx.fail = False
    asyncio.run(storage.hints_tick(ctx, p))
    hints = [m for m in ctx.sent if m["cmd"] == "CreateHints"]
    assert hints == [{"cmd": "CreateHints", "player": 1, "locations": [3392510_520]}]
