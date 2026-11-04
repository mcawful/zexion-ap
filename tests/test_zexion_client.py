"""client/zexion_client.py: a load ended by the game closing, or seen to end while no connection
steps, for the connection kept and one a reconnect carried it to meanwhile. Archipelago is not
installed here, so the parts of it the module imports are stand-ins."""

from __future__ import annotations

import asyncio
import logging
import math
import os
import sys
import types
from unittest import mock

from fakes import connection

from client import game, native, session, state, ticks


class CommonContext:
    """What ZexionContext and a poll read of CommonClient's context."""

    def __init__(self, server_address: str | None, password: str | None) -> None:
        self.items_received: list = []
        self.locations_checked: set[int] = set()
        self.stored_data: dict = {}
        self.player_names: dict[int, str] = {}
        self.slot, self.team = 1, 0
        self.server_seed_name = self.seed_name = None


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


def loaded() -> state.Connection:
    """A connection whose save is loaded and dressed, in its ending."""
    conn = connection()
    conn.dressed, conn.session.holding = True, True
    return conn


def context(conn: state.Connection, g, dropped: bool = False) -> zc.ZexionContext:
    ctx = zc.ZexionContext(None, None)
    ctx.conn, ctx.game_conn = conn, g
    if dropped:
        ctx.slot = None  # reset_server_state: no connection steps (away_tick)
    return ctx


def reconnect(ctx: zc.ZexionContext, old: state.Connection) -> state.Connection:
    """Connected to the same seed while a read ran in a thread (ZexionContext.connected)."""
    new = connection()
    state.carry_over(new, old, same_seed=True)
    ctx.conn = new
    return new


def ended(conn: state.Connection) -> bool:
    return not conn.dressed and not conn.session.holding


def test_the_game_closing_in_a_poll_ends_the_load_for_a_reconnect_carrying_it_too() -> None:
    old = loaded()
    ctx = context(old, None)
    ctx.game_conn = Closed(lambda: reconnect(ctx, old) if ctx.conn is old else None)
    asyncio.run(zc.poll(ctx))
    new = ctx.conn
    assert new is not old and ctx.game_conn is None
    assert ended(new), "the next game's helper would dress a save at its title"
    assert not old.session.holding


def test_a_save_left_while_dropped_ends_the_load_for_a_reconnect_carrying_it_too() -> None:
    old = loaded()
    ctx = context(old, object(), dropped=True)

    def left(g) -> bool:
        reconnect(ctx, old)
        return True  # the title, read twice

    old.session.look_away = left
    asyncio.run(zc.poll(ctx))
    assert ended(old) and ended(ctx.conn)


def test_the_game_closing_while_dropped_ends_the_load_kept() -> None:
    kept = loaded()
    ctx = context(kept, Closed(), dropped=True)
    asyncio.run(zc.poll(ctx))
    assert ended(kept) and ctx.game_conn is None


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
    zc.game_closed(ctx, ctx.conn)
    assert ctx.helper_retry_at < math.inf, "a restarted Zexion is tried again"


def test_the_language_is_told_again_in_the_next_game() -> None:
    # language_tick says it once a game run (ctx.said's "language"): a closed game clears it.
    ctx = context(connection(), Closed())
    assert ctx.said("language", "told") and not ctx.said("language", "told")
    zc.game_closed(ctx, ctx.conn)
    assert ctx.said("language", "told"), "the next game's language is told"


def test_a_mismatch_the_session_found_is_said_once_not_as_a_lost_helper(caplog) -> None:
    ctx = context(connection(), Closed())
    ctx.helper = Mismatched()
    with caplog.at_level(logging.WARNING, logger="Client"):
        for _ in range(2):
            ctx.mismatched(native.ModMismatch("both are version 0, but from different builds"))
    lines = [r.getMessage() for r in caplog.records if r.name == "Client"]
    assert len(lines) == 1 and "don't match" in lines[0] and ctx.helper_retry_at == math.inf


def test_dropping_the_helper_forgets_a_mismatch_of_the_one_before() -> None:
    # Else a matching helper of a restarted Zexion, failing for any other reason later, would
    # be taken for one that doesn't match and left alone.
    ctx = context(connection(), Closed())
    ctx.helper = Mismatched()
    ctx.conn.session.mismatch = native.ModMismatch("different builds")
    zc.game_closed(ctx, ctx.conn)
    assert ctx.conn.session.mismatch is None


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
    ctx.left_conn, ctx.conn = ctx.conn, None  # disconnect(): the connection left
    asyncio.run(zc.poll(ctx))
    assert ctx.helper is None


def test_a_save_dressed_before_the_drop_keeps_the_helper() -> None:
    conn = connection()
    conn.dressed = True  # carried over to the reconnect (state.carry_over)
    ctx = away(conn, game.IN_PLAY)
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
        c.conn.session.helper = helper

    def step(g, received, **kwargs):
        order.append("step with the helper" if conn.session.helper is helper else "step")
        order.append("still away" if ctx.helper_left else "back")
        raise game.GameNotRunning("Zexion is not running")  # the poll ends here

    monkeypatch.setattr(zc, "attach_helper", attach)
    monkeypatch.setattr(conn.session, "step", step)
    asyncio.run(zc.poll(ctx))
    assert order == ["attach", "step with the helper", "back"]


def test_a_save_in_its_ending_keeps_the_helper() -> None:
    # A disconnect in the ending undresses the save but keeps its hold: letting the helper go
    # would put back its Feats guard and RETURN HOME's ending meanwhile.
    conn = connection()
    conn.session.holding = True
    ctx = away(conn, game.IN_PLAY)
    ctx.left_conn, ctx.conn = ctx.conn, None
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
    monkeypatch.setattr(conn.session, "step", step)
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
    monkeypatch.setattr(ctx.conn.session, "step", step)
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


def lost_in_a_side_tick(monkeypatch, attaches: bool) -> tuple[zc.ZexionContext, list[str]]:
    """Connected, the game running with a helper, which the offline files' tick loses (a late
    reply); attaching it again works if ``attaches``."""
    ctx = context(connection(), Running(game.IN_PLAY))
    ctx.helper, ctx.told = Helper(), state.HelperState()
    order: list[str] = []

    async def nothing(*args) -> None:
        return None

    async def late(c, p) -> None:
        if c.helper is not None:  # as offline_tick
            raise native.ReplyLost("no reply within 2.0 s")

    async def attach(c) -> bool:
        order.append("attach")
        if attaches:
            c.helper, c.told = Helper(), state.HelperState()
        return True  # worth trying again soon

    def step(g, received, **kwargs):
        order.append("step with the helper" if ctx.helper is not None else "step without it")
        raise game.GameNotRunning("Zexion is not running")

    for name in ("newgame_tick", "labels_tick", "toggle_tick", "discs_helper_tick"):
        monkeypatch.setattr(zc, name, nothing)
    for name in ("slots_tick", "language_tick"):
        monkeypatch.setattr(zc, name, nothing)
    monkeypatch.setattr(zc, "offline_tick", late)
    monkeypatch.setattr(zc, "attach_helper", attach)
    monkeypatch.setattr(zc, "BACK_RETRY_SECONDS", 0.0)
    monkeypatch.setattr(ctx.conn.session, "step", step)
    return ctx, order


def test_a_helper_lost_in_a_side_tick_is_attached_again_before_the_step(monkeypatch) -> None:
    # Else the step gives the items owed with no helper: written directly, with no popup.
    ctx, order = lost_in_a_side_tick(monkeypatch, attaches=True)
    asyncio.run(zc.poll(ctx))
    assert order == ["attach", "step with the helper"]
    assert not ctx.helper_lost


def test_a_helper_lost_for_good_holds_back_the_step_for_a_while_only(monkeypatch) -> None:
    ctx, order = lost_in_a_side_tick(monkeypatch, attaches=False)
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
    ctx.shields.stopped, ctx.shields.misses = True, 3
    zc.game_closed(ctx, ctx.conn)
    assert not ctx.shields.stopped and ctx.shields.misses == 0
    conn = connection()
    ctx = context(conn, Running(game.IN_PLAY))
    steps = [session.Step(seed_matches=True, state=game.IN_PLAY, save_stamp=bytes(14))]
    monkeypatch.setattr(conn.session, "step", lambda g, received, **kwargs: steps[0])
    monkeypatch.setattr(zc, "attach_helper", lambda c: asyncio.sleep(0))
    asyncio.run(zc.poll(ctx))
    assert ctx.conn is conn and ctx.load.seed_loaded, "the poll took the load"
    ctx.shields.stopped = True
    ctx.load.seed_loaded = False  # a reconnect: the same load, looked at afresh (connected)
    asyncio.run(zc.poll(ctx))
    assert ctx.shields.stopped, "a reconnect does not end it"
    steps[0] = session.Step(left_seed=True)  # the title, or another save
    asyncio.run(zc.poll(ctx))
    assert not ctx.shields.stopped, "leaving the save ends it"
