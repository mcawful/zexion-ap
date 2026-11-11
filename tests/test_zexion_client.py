"""client/zexion_client.py: a load ended by the game closing, or seen to end while no connection
steps, a reconnect meanwhile included, what a reconnect carries on with, the helper failing or
let go, and a received disk's count and Intel Chip's hint after its popup. Archipelago is not
installed here, so the parts of it the module imports are stand-ins."""

from __future__ import annotations

import asyncio
import logging
import math
import os
import sys
import types
from unittest import mock

import pytest
from fakes import connection, seed_state, slot_data

from client import game, ids, native, session, state, ticks


class CommonContext:
    """What ZexionContext and a poll read of CommonClient's context."""

    def __init__(self, server_address: str | None, password: str | None) -> None:
        self.items_received: list = []
        self.locations_checked: set[int] = set()
        self.stored_data: dict = {}
        self.stored_data_notification_keys: set[str] = set()
        self.gets: list[list[str]] = []  # the keys of each Get sent, as CommonClient sends them
        self.player_names: dict[int, str] = {}
        self.slot, self.team = 1, 0
        self.server_seed_name = self.seed_name = None
        self.finished_game = False

    def set_notify(self, *keys: str) -> None:
        if new := [k for k in keys if k not in self.stored_data_notification_keys]:
            self.stored_data_notification_keys.update(new)
            self.gets.append(new)

    async def disconnect(self, allow_autoreconnect: bool = False) -> None:
        pass

    async def update_death_link(self, on: bool) -> None:
        pass

    async def send_msgs(self, msgs: list[dict]) -> None:
        pass


def _archipelago() -> dict[str, types.ModuleType]:
    common = types.ModuleType("CommonClient")
    common.CommonContext = CommonContext
    common.ClientCommandProcessor = object
    common.logger = logging.getLogger("Client")
    common.get_base_parser = common.handle_url_arg = common.server_loop = None
    common.gui_enabled = False
    net = types.ModuleType("NetUtils")
    net.ClientStatus = None
    return {"CommonClient": common, "NetUtils": net, "Utils": types.ModuleType("Utils")}


# Only the stand-ins are put back as they were (the client's modules stay the ones the other
# tests use), and this copy of zexion_client is not left for another test to import.
STAND_INS = _archipelago()
saved = {name: sys.modules.get(name) for name in STAND_INS}
sys.modules.update(STAND_INS)
try:
    with mock.patch.dict(os.environ, ZEXION_AP_DIR=os.devnull):  # not a folder: not on the path
        from client import zexion_client as zc
finally:
    sys.modules.pop("client.zexion_client", None)
    vars(sys.modules["client"]).pop("zexion_client", None)  # the package's name for it too
    for name, module in saved.items():
        if module is None:
            del sys.modules[name]
        else:
            sys.modules[name] = module


class Closed:
    """A game that has exited: every read raises GameNotRunning, after ``meanwhile``."""

    def __init__(self, meanwhile=lambda: None) -> None:
        self.meanwhile = meanwhile
        self.pm = types.SimpleNamespace(close_process=lambda: None)

    def __getattr__(self, name: str):
        def read(*args, **kwargs):
            self.meanwhile()
            raise game.GameNotRunning("Zexion is not running")

        return read


def context(conn: state.Connection, g, dropped: bool = False) -> zc.ZexionContext:
    ctx = zc.ZexionContext(None, None)
    ctx.conn, ctx.seed_state, ctx.game_conn = conn, seed_state(conn), g
    if dropped:
        ctx.slot = None  # reset_server_state: no connection steps (away_tick)
    return ctx


def loaded(ctx: zc.ZexionContext) -> state.SeedState:
    """The seed's save loaded and dressed, in its ending."""
    seed = ctx.seed_state
    seed.dressed, seed.session.holding = True, True
    return seed


def reconnect(ctx: zc.ZexionContext) -> None:
    """Connected to the same seed while a read ran in a thread (ZexionContext.connected): a new
    connection, the same SeedState."""
    ctx.conn = connection()
    ctx.seed_state.session.announced_read = False


def ended(seed: state.SeedState) -> bool:
    return not seed.dressed and not seed.session.holding


def test_the_game_closing_in_a_poll_ends_the_load_after_a_reconnect_meanwhile_too() -> None:
    old = connection()
    ctx = context(old, None)
    seed = loaded(ctx)
    ctx.game_conn = Closed(lambda: reconnect(ctx) if ctx.conn is old else None)
    asyncio.run(zc.poll(ctx))
    assert ctx.conn is not old and ctx.seed_state is seed and ctx.game_conn is None
    assert ended(seed), "the next game's helper would dress a save at its title"


def test_a_save_left_while_dropped_ends_the_load_after_a_reconnect_meanwhile_too() -> None:
    ctx = context(connection(), object(), dropped=True)
    seed = loaded(ctx)

    def left(g) -> bool:
        reconnect(ctx)
        return True  # the title, read twice

    seed.session.look_away = left
    asyncio.run(zc.poll(ctx))
    assert ended(seed) and ctx.seed_state is seed


def test_the_game_closing_while_dropped_ends_the_load_kept() -> None:
    ctx = context(connection(), Closed(), dropped=True)
    seed = loaded(ctx)
    asyncio.run(zc.poll(ctx))
    assert ended(seed) and ctx.game_conn is None


def test_a_reconnect_carries_on_with_the_seeds_state_and_another_seed_starts_afresh(
    monkeypatch,
) -> None:
    monkeypatch.setattr(zc.Utils, "async_start", lambda coro, name="": coro.close(), raising=False)
    ctx = zc.ZexionContext(None, None)
    ctx.seed_name = "A"
    ctx.connected(slot_data())
    seed, first = ctx.seed_state, ctx.conn
    seed.dressed, seed.session.holding = True, True
    seed.session.announced, seed.session.announced_read = 4, True
    seed.shields.stopped, seed.shields.given = True, 2
    ctx.connected(slot_data())  # a drop, then Connected again
    assert ctx.seed_state is seed and ctx.conn is not first
    assert seed.dressed and seed.session.holding and seed.session.announced == 4
    assert seed.shields.stopped and seed.shields.given == 2
    assert not seed.session.announced_read, "the popup count is read from the server again"
    ctx.seed_name = "B"  # another room's seed, with the same team and slot
    ctx.connected(slot_data())
    other = ctx.seed_state
    assert other is not seed and not other.dressed and not other.session.holding
    assert other.session.announced == 0 and other.session.plan is ctx.conn.plan
    # Its slot's shield count has the same data storage key: the stop and the count not sent
    # yet were the other seed's.
    assert not other.shields.stopped and other.shields.given is None


class Mismatched:
    """A helper whose every request is one another build of the mod doesn't know."""

    def __init__(self) -> None:
        self.closed = 0

    def request(self, text: str, seconds: float = 2.0) -> str:
        raise native.ModMismatch("both are version 0, but from different builds")

    def close(self) -> None:
        self.closed += 1


def test_a_mod_of_another_build_is_said_once_and_left_until_zexion_restarts(caplog) -> None:
    ctx = context(connection(), Closed())
    ctx.helper = Mismatched()
    with caplog.at_level(logging.WARNING, logger="Client"):
        asyncio.run(
            zc.side_tick(ctx, "new game", lambda: asyncio.to_thread(ctx.helper.request, "x"))
        )
        assert ctx.helper is None and ctx.helper_retry_at == math.inf, "not attached again"
        ctx.helper = Mismatched()  # the same again (an attach meanwhile)
        asyncio.run(
            zc.side_tick(ctx, "new game", lambda: asyncio.to_thread(ctx.helper.request, "x"))
        )
    lines = [r.getMessage() for r in caplog.records if r.name == "Client"]
    assert len(lines) == 1 and lines[0].startswith("The Zexion mod and this client don't match")
    assert zc.HELPER_LOST not in lines
    zc.game_closed(ctx, ctx.seed_state)
    assert ctx.helper_retry_at < math.inf, "a restarted Zexion is tried again"


def test_the_language_is_told_again_in_the_next_game() -> None:
    # language_tick says it once a game run (ctx.said's "language"): a closed game clears it.
    ctx = context(connection(), Closed())
    assert ctx.said("language", "told") and not ctx.said("language", "told")
    zc.game_closed(ctx, ctx.seed_state)
    assert ctx.said("language", "told"), "the next game's language is told"


def test_a_mismatch_the_session_found_is_said_once_not_as_a_lost_helper(caplog) -> None:
    ctx = context(connection(), Closed())
    ctx.helper = Mismatched()
    with caplog.at_level(logging.WARNING, logger="Client"):
        for _ in range(2):
            ctx.mismatched(native.ModMismatch("both are version 0, but from different builds"))
    lines = [r.getMessage() for r in caplog.records if r.name == "Client"]
    assert len(lines) == 1 and "don't match" in lines[0] and ctx.helper_retry_at == math.inf


# --- the helper let go while no connection steps -------------------------------------------


class Helper:
    """An attached helper: requests answered as taken, and how often it was closed."""

    def __init__(self) -> None:
        self.closed = 0

    def request(self, text: str, seconds: float = 2.0) -> str:
        return "ok"

    def close(self) -> None:
        self.closed += 1


class Showing:
    """A running game whose state() is ``shown``; nothing else is read."""

    def __init__(self, shown: str) -> None:
        self.shown = shown

    def state(self) -> str:
        return self.shown


def away(conn: state.Connection, shown: str) -> zc.ZexionContext:
    """A dropped connection (``conn``), the game showing ``shown``, with a helper attached."""
    ctx = context(conn, Showing(shown), dropped=True)
    ctx.helper, ctx.told = Helper(), state.HelperState()
    return ctx


def test_a_save_loaded_while_dropped_leaves_the_helper_its_offline_look(caplog) -> None:
    # The save the drop kept was left, and one loaded since (this seed's or not): no client
    # dresses it while a client is attached, so the client lets the helper go.
    conn = connection()
    ctx = away(conn, game.IN_PLAY)
    helper = ctx.helper
    with caplog.at_level(logging.DEBUG):  # a detail (ticks.detail)
        asyncio.run(zc.poll(ctx))
    assert ctx.helper is None and helper.closed == 1
    assert ctx.helper_left, "attached again as soon as a connection steps"
    assert any("Not connected" in r.getMessage() for r in caplog.records)


def test_a_save_loaded_after_a_disconnect_leaves_the_helper_its_offline_look() -> None:
    ctx = away(connection(), game.MENU)
    ctx.conn, ctx.seed_state.dressed = None, False  # disconnect()
    asyncio.run(zc.poll(ctx))
    assert ctx.helper is None


def test_a_save_dressed_before_the_drop_keeps_the_helper() -> None:
    ctx = away(connection(), game.IN_PLAY)
    ctx.seed_state.dressed = True  # kept for the reconnect (state.SeedState)
    helper = ctx.helper
    asyncio.run(zc.poll(ctx))
    assert ctx.helper is helper and helper.closed == 0


def test_at_the_title_the_helper_is_kept_for_an_armed_new_game() -> None:
    ctx = away(connection(), game.NO_SAVE)
    helper = ctx.helper
    asyncio.run(zc.poll(ctx))
    assert ctx.helper is helper and helper.closed == 0


def test_a_game_that_closed_while_dropped_leaves_nothing_to_let_go() -> None:
    ctx = context(connection(), Closed(), dropped=True)
    ctx.helper, ctx.told = Helper(), state.HelperState()
    ctx.helper_left = True  # let go before: the next game's helper is attached with it
    asyncio.run(zc.poll(ctx))
    assert ctx.game_conn is None and ctx.helper is None and not ctx.helper_left


def test_a_reconnect_attaches_the_helper_before_the_first_step(monkeypatch) -> None:
    # Items received meanwhile get their popups: a step with no helper writes them directly.
    conn = connection()
    ctx = context(conn, Showing(game.IN_PLAY))  # connected again, the game still attached
    ctx.helper_left = True  # as leave_helper_tick left it
    order: list[str] = []
    helper = Helper()

    async def attach(c) -> None:
        order.append("attach")
        c.helper = helper

    def step(g, received, helper=None, **kwargs):
        order.append("step with the helper" if helper is not None else "step")
        order.append("still away" if ctx.helper_left else "back")
        raise game.GameNotRunning("Zexion is not running")  # the poll ends here

    monkeypatch.setattr(zc, "attach_helper", attach)
    monkeypatch.setattr(ctx.seed_state.session, "step", step)
    asyncio.run(zc.poll(ctx))
    assert order == ["attach", "step with the helper", "back"]


def test_a_save_in_its_ending_keeps_the_helper() -> None:
    # A disconnect in the ending undresses the save but keeps its hold: letting the helper go
    # would put back its Feats guard and RETURN HOME's ending meanwhile.
    ctx = away(connection(), game.IN_PLAY)
    ctx.seed_state.session.holding = True
    ctx.conn, ctx.seed_state.dressed = None, False  # disconnect()
    helper = ctx.helper
    asyncio.run(zc.poll(ctx))
    assert ctx.helper is helper and helper.closed == 0


class Running(Showing):
    """A running game, its process alive (game_alive)."""

    def __init__(self, shown: str) -> None:
        super().__init__(shown)
        self.pm = types.SimpleNamespace(alive=lambda: True)


def failing_reattach(monkeypatch) -> tuple[zc.ZexionContext, list[str]]:
    """Connected again after a time away, the game running, its helper not attaching."""
    conn = connection()
    ctx = context(conn, Running(game.IN_PLAY))
    ctx.helper_left = True
    order: list[str] = []

    async def attach(c) -> bool:
        order.append("attach")
        c.helper_retry_at = 30.0  # drop_helper
        return True  # worth trying again soon

    def step(g, received, **kwargs):
        order.append("step")
        raise game.GameNotRunning("Zexion is not running")

    monkeypatch.setattr(zc, "attach_helper", attach)
    monkeypatch.setattr(zc, "BACK_RETRY_SECONDS", 0.0)
    monkeypatch.setattr(ctx.seed_state.session, "step", step)
    return ctx, order


def test_a_failed_reattach_holds_back_the_step_for_a_while(monkeypatch) -> None:
    ctx, order = failing_reattach(monkeypatch)
    asyncio.run(zc.poll(ctx))
    asyncio.run(zc.poll(ctx))
    assert order == ["attach", "attach"], "no step gives items without their popups"
    assert ctx.helper_left
    ctx.helper_back_by = 1.0  # long past: given up, as after any failed attach
    asyncio.run(zc.poll(ctx))
    assert order[-2:] == ["attach", "step"] and not ctx.helper_left


def mismatched_reattach(
    monkeypatch, dll, error: type[native.ModMismatch] = native.ModMismatch
) -> tuple[zc.ZexionContext, list[str]]:
    """failing_reattach through the real attach_helper, the mod not matching (``error``),
    ``dll`` the client's own build."""
    ctx, order = failing_reattach(monkeypatch)
    monkeypatch.setattr(zc, "attach_helper", ticks.attach_helper)

    def attach(*args) -> None:
        order.append("attach")
        raise error("the mod is another build of this version")

    def step(g, received, **kwargs):
        order.append(f"step, retry at {ctx.helper_retry_at}")
        raise game.GameNotRunning("Zexion is not running")

    monkeypatch.setattr(native, "attach", attach)
    monkeypatch.setattr(native, "default_dll", lambda: dll)
    monkeypatch.setattr(ctx.seed_state.session, "step", step)
    return ctx, order


def test_a_failed_reattach_of_a_mod_that_does_not_match_does_not_wait(
    monkeypatch, tmp_path
) -> None:
    ctx, order = mismatched_reattach(monkeypatch, tmp_path / "zexion_ap.dll")  # no build
    asyncio.run(zc.poll(ctx))
    assert order == ["attach", "step, retry at inf"]  # not tried again until Zexion restarts


def test_a_mismatch_a_rebuild_can_fix_is_tried_again_without_holding_the_step(
    monkeypatch, tmp_path
) -> None:
    dll = tmp_path / "zexion_ap.dll"
    dll.write_bytes(b"")  # a build of the client's own
    ctx, order = mismatched_reattach(monkeypatch, dll)
    asyncio.run(zc.poll(ctx))
    assert len(order) == 2 and order[1].startswith("step, retry at ")
    assert order[1] != "step, retry at inf"


def test_an_old_helper_that_did_not_unload_is_tried_again_without_a_build(
    monkeypatch, tmp_path
) -> None:
    # A menu open, say: a later try may unload it and load the game folder's copy.
    ctx, order = mismatched_reattach(monkeypatch, tmp_path / "zexion_ap.dll", native.NotUnloaded)
    asyncio.run(zc.poll(ctx))
    assert len(order) == 2 and order[1] != "step, retry at inf"


async def nothing(*args) -> None:
    return None


def lost_helper(
    monkeypatch, attaches: bool, step_fails: int = 0
) -> tuple[zc.ZexionContext, list[str]]:
    """Connected, the game running with a helper, which the offline files' tick loses (a late
    reply), or with ``step_fails`` the first that many steps with it; attaching it again works
    if ``attaches``."""
    ctx = context(connection(), Running(game.IN_PLAY))
    ctx.helper, ctx.told = Helper(), state.HelperState()
    order: list[str] = []

    async def late(c, p) -> None:
        if c.helper is not None and not step_fails:  # as offline_tick
            raise native.ReplyLost("no reply within 2.0 s")

    async def attach(c) -> bool:
        order.append("attach")
        if attaches:
            c.helper, c.told = Helper(), state.HelperState()
        return True  # worth trying again soon

    def step(g, received, helper=None, **kwargs):
        assert helper is ctx.helper, "the step gives through the helper attached"
        order.append("step with the helper" if helper is not None else "step without it")
        if helper is not None and order.count("step with the helper") <= step_fails:
            lost = native.ReplyLost("no reply within 3.0 s")  # the batch went out
            return session.Step(state=game.IN_PLAY, helper_error=lost)  # session.py
        raise game.GameNotRunning("Zexion is not running")

    for name in ("newgame_tick", "labels_tick", "toggle_tick", "discs_helper_tick"):
        monkeypatch.setattr(zc, name, nothing)
    for name in ("slots_tick", "language_tick", "replay_tick"):
        monkeypatch.setattr(zc, name, nothing)
    monkeypatch.setattr(zc, "offline_tick", late)
    monkeypatch.setattr(zc, "attach_helper", attach)
    monkeypatch.setattr(zc, "BACK_RETRY_SECONDS", 0.0)
    monkeypatch.setattr(ctx.seed_state.session, "step", step)
    return ctx, order


def test_a_helper_lost_in_a_side_tick_is_attached_again_before_the_step(monkeypatch) -> None:
    # Else the step gives the items owed with no helper: written directly, with no popup.
    ctx, order = lost_helper(monkeypatch, attaches=True)
    asyncio.run(zc.poll(ctx))
    assert order == ["attach", "step with the helper"]
    assert not ctx.helper_lost


def test_a_helper_lost_in_the_step_is_attached_again_before_the_next(monkeypatch) -> None:
    # The step gives nothing that poll (session.py), and no item is written directly.
    ctx, order = lost_helper(monkeypatch, attaches=True, step_fails=1)
    asyncio.run(zc.poll(ctx))
    assert order == ["step with the helper"] and ctx.helper is None and ctx.helper_lost
    asyncio.run(zc.poll(ctx))
    assert order == ["step with the helper", "attach", "step with the helper"]
    assert not ctx.helper_lost


def test_a_helper_failing_the_step_again_is_left_to_the_usual_retry(monkeypatch) -> None:
    # A request it keeps refusing: the items are written directly meanwhile, as with no
    # helper, rather than held back for good.
    ctx, order = lost_helper(monkeypatch, attaches=True, step_fails=2)
    for _ in range(3):
        asyncio.run(zc.poll(ctx))
    assert order == ["step with the helper", "attach", "step with the helper", "step without it"]
    assert ctx.helper is None and not ctx.helper_lost and ctx.helper_retry_at > 0


def test_a_step_that_goes_through_ends_the_failures_in_a_row(monkeypatch) -> None:
    # A failure, a step through the helper, a failure: attached again before the next step,
    # as after a first failure.
    ctx, order = lost_helper(monkeypatch, attaches=True)
    monkeypatch.setattr(zc, "offline_tick", nothing)
    fails = iter([True, False, True, False])

    def step(g, received, helper=None, **kwargs):
        order.append("step")
        lost = helper is not None and next(fails)
        error = native.ReplyLost("no reply within 3.0 s") if lost else None
        return session.Step(state=game.IN_PLAY, helper_error=error)

    monkeypatch.setattr(ctx.seed_state.session, "step", step)
    for _ in range(4):
        asyncio.run(zc.poll(ctx))
    assert order == ["step", "attach", "step", "step", "attach", "step"]


@pytest.mark.parametrize("fails", [1, 2])
def test_a_helper_lost_just_before_the_step_holds_its_grants_once(monkeypatch, fails) -> None:
    # Lost in the ending-again tick, after the attach and before the step: the step gives
    # nothing, rather than write the items with no popup, and the helper is attached again
    # first. Lost again there, the next step gives directly, as after two failed steps.
    ctx, order = lost_helper(monkeypatch, attaches=True)
    monkeypatch.setattr(zc, "offline_tick", nothing)
    lost: list[int] = []

    async def replay(c) -> None:
        if c.helper is not None and len(lost) < fails:
            lost.append(1)
            raise native.ReplyLost("no reply within 2.0 s")

    def step(g, received, helper=None, give=True, **kwargs):
        order.append(
            f"{'helper' if helper is not None else 'none'}, {'gives' if give else 'waits'}"
        )
        return session.Step(state=game.IN_PLAY)

    monkeypatch.setattr(zc, "replay_tick", replay)
    monkeypatch.setattr(ctx.seed_state.session, "step", step)
    asyncio.run(zc.poll(ctx))
    asyncio.run(zc.poll(ctx))
    after = "helper, gives" if fails == 1 else "none, gives"
    assert order == ["none, waits", "attach", after]


def test_received_items_keep_the_traps_with_their_senders() -> None:
    # A trap is no game item, but it is given as its popup (session.py): kept in step.
    ctx = context(connection(), None)
    ctx.slot, ctx.player_names = 1, {1: "Me", 2: "Bob"}
    ctx.items_received = [
        types.SimpleNamespace(item=ids.ITEM_ID_BASE + code, player=player)
        for code, player in ((17, 2), (ids.DISRUPTION_TRAP, 2), (ids.DISORIENT_TRAP, 1))
    ]
    assert ctx.received() == ([17, ids.DISRUPTION_TRAP, ids.DISORIENT_TRAP], ["Bob", "Bob", ""])


def test_a_failed_reattach_after_reload_mod_holds_back_the_step(monkeypatch) -> None:
    # /reload_mod unloaded the helper and the attach after it failed: the step gives nothing
    # (no item written with no popup, no trap dropped) while the attach is tried again.
    ctx, order = lost_helper(monkeypatch, attaches=False)
    monkeypatch.setattr(zc, "offline_tick", nothing)
    monkeypatch.setattr(zc, "RELOAD_SECONDS", 0.0)
    ctx.reload_mod_pending = True
    ctx.helper.unload = lambda: "bye"
    asyncio.run(zc.poll(ctx))
    assert order == ["attach", "attach"], "no step: the helper is waited for"
    assert ctx.helper is None and ctx.helper_lost


def test_a_disconnect_undresses_the_save_for_the_next_connection() -> None:
    # A reconnect to the seed carries on with its SeedState: the helper takes the look back.
    ctx = context(connection(), None)
    ctx.seed_state.dressed = True
    asyncio.run(ctx.disconnect())
    assert ctx.conn is None and not ctx.seed_state.dressed


def test_the_goal_is_sent_once(monkeypatch) -> None:
    ctx = context(connection(), Running(game.IN_PLAY))
    sent: list[dict] = []

    async def send_msgs(msgs: list[dict]) -> None:
        sent.extend(msgs)

    ctx.send_msgs = send_msgs
    monkeypatch.setattr(zc, "ClientStatus", types.SimpleNamespace(CLIENT_GOAL=30))
    monkeypatch.setattr(zc, "attach_helper", lambda c: asyncio.sleep(0))
    ended = session.Step(
        seed_matches=True, state=game.IN_PLAY, goal=True, ended=True, save_stamp=bytes(14)
    )
    monkeypatch.setattr(ctx.seed_state.session, "step", lambda g, received, **kwargs: ended)
    for _ in range(4):
        asyncio.run(zc.poll(ctx))
    assert [m["cmd"] for m in sent if m["cmd"] == "StatusUpdate"] == ["StatusUpdate"]
    assert ctx.finished_game


def test_a_helper_lost_for_good_holds_back_the_step_for_a_while_only(monkeypatch) -> None:
    ctx, order = lost_helper(monkeypatch, attaches=False)
    asyncio.run(zc.poll(ctx))
    assert order == ["attach"], "no step gives items without their popups"
    ctx.helper_back_by = 1.0  # long past: given up, as after any failed attach
    asyncio.run(zc.poll(ctx))
    assert order[-2:] == ["attach", "step without it"] and not ctx.helper_lost


def test_a_failed_reattach_to_a_closed_game_does_not_wait(monkeypatch) -> None:
    ctx, order = failing_reattach(monkeypatch)
    ctx.game_conn.pm = types.SimpleNamespace(alive=lambda: False, close_process=lambda: None)
    asyncio.run(zc.poll(ctx))
    assert order == ["attach", "step"] and ctx.game_conn is None


def test_a_stop_on_energy_shields_ends_with_the_game_or_the_load(monkeypatch) -> None:
    # client/shields.py: a stop holds for the load it happened in, a reconnect included.
    ctx = context(connection(), Closed())
    sh = ctx.seed_state.shields
    sh.stopped, sh.misses = True, 3
    zc.game_closed(ctx, ctx.seed_state)
    assert not sh.stopped and sh.misses == 0
    conn = connection()
    ctx = context(conn, Running(game.IN_PLAY))
    sh = ctx.seed_state.shields
    steps = [session.Step(seed_matches=True, state=game.IN_PLAY, save_stamp=bytes(14))]
    monkeypatch.setattr(ctx.seed_state.session, "step", lambda g, received, **kwargs: steps[0])
    monkeypatch.setattr(zc, "attach_helper", lambda c: asyncio.sleep(0))
    asyncio.run(zc.poll(ctx))
    assert ctx.conn is conn and ctx.load.seed_loaded, "the poll took the load"
    sh.stopped = True
    ctx.load.seed_loaded = False  # a reconnect: the same load, looked at afresh (connected)
    asyncio.run(zc.poll(ctx))
    assert sh.stopped, "a reconnect does not end it"
    steps[0] = session.Step(left_seed=True)  # the title, or another save
    asyncio.run(zc.poll(ctx))
    assert not sh.stopped, "leaving the save ends it"


# --- how to start the seed -----------------------------------------------------------------------


def test_the_poll_says_how_to_play_once_no_save_of_this_seed_is_loaded(monkeypatch, caplog) -> None:
    # Connected with this seed's save in play (live53, 2026-11-04): the arm's lines wait.
    conn = connection()
    ctx = context(conn, Showing(game.IN_PLAY))
    ctx.helper, ctx.told = Helper(), state.HelperState()
    how = ("To play, load this seed's save, or start a new game: ...", "SEED")
    conn.newgame, conn.newgame_lines = state.NewGame.ARMED, how  # armed with the save in play
    steps = [
        session.Step(),  # a read that did not settle
        session.Step(seed_matches=True, state=game.IN_PLAY),
        session.Step(left_seed=True),  # back at the title
    ]

    async def no_attach(c) -> bool:
        return False

    monkeypatch.setattr(zc, "attach_helper", no_attach)
    monkeypatch.setattr(ctx.seed_state.session, "step", lambda g, received, **kw: steps.pop(0))
    with caplog.at_level(logging.INFO, logger="Client"):
        said = []
        for _ in range(3):
            asyncio.run(zc.poll(ctx))
            lines = [r.getMessage() for r in caplog.records if r.name == "Client"]
            said.append([line for line in lines if line in how])
    assert said[0] == [] and said[1] == [], "not before the game is read, nor in the seed's save"
    assert said[2] == list(how)
    assert conn.newgame_lines == ()


def test_a_received_disk_has_its_count_and_a_chip_its_hint_after_its_popup(monkeypatch) -> None:
    # Each follows the item's own popup ("from Bob"), not in its place.
    hunt = dict(goal="os_install_disks", discs_required=3, discs_placed=5, disc_locations=[12])
    ctx = context(connection(**hunt), Showing(game.IN_PLAY))
    monkeypatch.setattr(ctx, "received", lambda: ([49, 44, 44], ["Alice", "Bob", "Carol"]))
    monkeypatch.setattr(zc, "hints_tick", nothing)
    monkeypatch.setattr(zc, "hint_notes", lambda c, codes: {0: "HINT: SLIDE MOD"})
    passed: dict = {}

    def step(g, received, **kwargs):
        passed.update(kwargs)
        raise game.GameNotRunning("Zexion is not running")  # the poll ends here

    monkeypatch.setattr(ctx.seed_state.session, "step", step)
    asyncio.run(zc.poll(ctx))
    assert passed["followups"] == {
        0: "HINT: SLIDE MOD",
        1: "OS Install Disk 1",
        2: "OS Install Disk 2",
    }


@pytest.mark.parametrize("shown", [False, True])
def test_a_received_disks_count_leaves_out_a_found_disk_whose_count_is_still_to_show(
    shown, monkeypatch
) -> None:
    # The player picked up the disk at location 12 and its count has not shown yet: it counts
    # in that notice (followups_tick), which comes after this one. Once shown, it counts here.
    hunt = dict(goal="os_install_disks", discs_required=3, discs_placed=5, disc_locations=[12])
    conn = connection(**hunt)
    ctx = context(conn, Showing(game.IN_PLAY))
    ctx.locations_checked = {zc.LOCATION_ID_BASE + 12}
    ctx.load.save_collected = {12}
    ctx.stored_data[conn.popped_key] = {"12": 1} if shown else {}
    monkeypatch.setattr(ctx, "received", lambda: ([44], ["Bob"]))
    monkeypatch.setattr(zc, "hints_tick", nothing)
    passed: dict = {}

    def step(g, received, **kwargs):
        passed.update(kwargs)
        raise game.GameNotRunning("Zexion is not running")  # the poll ends here

    monkeypatch.setattr(ctx.seed_state.session, "step", step)
    asyncio.run(zc.poll(ctx))
    assert passed["followups"] == {0: f"OS Install Disk {2 if shown else 1}"}


@pytest.mark.parametrize("hints", [[], [[1, 3392510_520, 16]]])
def test_the_popup_count_and_the_hints_are_asked_for_in_one_get(hints, monkeypatch) -> None:
    # Two Gets get two replies, and a poll between them would read the count without the hints:
    # an Intel Chip received while the client was away would pop up with no hint.
    monkeypatch.setattr(zc.Utils, "async_start", lambda coro, name="": coro.close(), raising=False)
    ctx = zc.ZexionContext(None, None)
    ctx.seed_name = "A"
    ctx.connected(slot_data(hints=hints, intel_chip=bool(hints)))
    conn = ctx.conn
    keys = [conn.shield_key, conn.found_key, conn.popped_key, conn.announced_key, conn.held_key]
    assert ctx.gets == [keys + ([conn.hints_key] if hints else [])]
    # The replies, then a drop: reset_server_state keeps stored_data and what is subscribed.
    ctx.stored_data = {key: 3 for key in keys} | {conn.hints_key: {}}
    ctx.gets = []
    ctx.connected(slot_data(hints=hints, intel_chip=bool(hints)))
    conn = ctx.conn
    assert conn.announced_key not in ctx.stored_data, "the last connection's count not read"
    assert conn.held_key not in ctx.stored_data, "nor its waiting traps"
    assert conn.hints_key not in ctx.stored_data
    reread = [conn.announced_key, conn.held_key]
    assert ctx.gets == [reread + ([conn.hints_key] if hints else [])]
