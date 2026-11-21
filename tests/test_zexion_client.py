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
import time
import types
from unittest import mock

import pytest
from fakes import connection, seed_state, slot_data

from client import (
    game,
    ids,
    linux_helper,
    native,
    newgame,
    offline,
    patterns,
    session,
    state,
    ticks,
)


class CommonContext:
    """What ZexionContext and a poll read of CommonClient's context."""

    def __init__(self, server_address: str | None, password: str | None) -> None:
        self.items_received: list = []
        self.locations_checked: set[int] = set()
        self.checked_locations: set[int] = set()  # the server's
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


@pytest.mark.parametrize("platform", ["windows", "linux"])
def test_a_reconnect_sends_the_labels_at_once_whichever_helper_holds_them(
    monkeypatch, platform
) -> None:
    monkeypatch.setattr(zc.Utils, "async_start", lambda coro, name="": coro.close(), raising=False)
    ctx = zc.ZexionContext(None, None)
    ctx.seed_name = "A"
    ctx.connected(slot_data())
    told = state.HelperState()
    told.labels_retry_at = math.inf  # a refusal's retry delay, still running
    if platform == "linux":
        ctx.linux_mod = types.SimpleNamespace(told=told)
    else:
        ctx.told = told
    ctx.connected(slot_data())  # a drop, then Connected again
    assert told.labels_retry_at == 0.0


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

    loaded_here = False  # native.Helper's: already in the game when attached

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


class Back(Helper):
    """An attached helper that had dressed the save with no client: client_back says shown."""

    def __init__(self, order: list[str]) -> None:
        super().__init__()
        self.order = order

    def request(self, text: str, seconds: float = 2.0) -> str:
        if text == "client_back":
            self.order.append(text)
            return "shown"
        return "ok"

    def hello(self) -> dict[str, str]:
        return {}


class OnB1(Showing):
    """A running B1 game (newgame_tick reads its build) whose state() is ``shown``."""

    build = patterns.KNOWN_BUILDS[0]


@pytest.mark.parametrize(
    ("states", "expected"),
    [
        # A reconnect in play: the first look gives nothing (two polls must agree), the second
        # gives what was received meanwhile, after CLIENT CONNECTED is asked for.
        pytest.param([game.IN_PLAY, game.IN_PLAY], ["step", "client_back", "step"], id="in play"),
        # Paused at the reconnect: asked once the poll before found it in play. The code before
        # the fix asked in the same order here (the first poll in play gives nothing); this case
        # guards against a change that would ask from a poll in a menu, or late.
        pytest.param(
            [game.MENU, game.IN_PLAY, game.IN_PLAY],
            ["step", "step", "client_back", "step"],
            id="paused, a regression guard",
        ),
    ],
)
def test_client_connected_is_asked_for_before_the_step_that_gives(
    monkeypatch, states, expected
) -> None:
    conn = connection()
    ctx = context(conn, OnB1(game.IN_PLAY))
    order: list[str] = []
    ctx.helper, ctx.told = Back(order), state.HelperState()
    looks = iter(states)

    def step(g, received, **kwargs):
        order.append("step")
        return session.Step(seed_matches=True, state=next(looks, game.IN_PLAY))

    monkeypatch.setattr(ctx.seed_state.session, "step", step)
    for _ in states:
        asyncio.run(zc.poll(ctx))
    assert order == expected
    asyncio.run(zc.poll(ctx))
    assert order.count("client_back") == 1, "once an attach"


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
    # A menu open or a trap waiting, say: a later try may unload it and load the game folder's
    # copy, so the step waits for it a while, as for a helper lost, then goes on. It is asked
    # again only every few seconds: each try has it put back what a client going away does.
    ctx, order = mismatched_reattach(monkeypatch, tmp_path / "zexion_ap.dll", native.NotUnloaded)
    asyncio.run(zc.poll(ctx))
    assert order == ["attach"], "no step gives items without their popups meanwhile"
    asyncio.run(zc.poll(ctx))
    assert order == ["attach"], "held, and not asked again at once"
    ctx.attach_again_at, ctx.helper_back_by = 0.0, 1.0  # both long past: given up
    asyncio.run(zc.poll(ctx))
    assert len(order) == 3 and order[2] != "step, retry at inf"


def test_traps_are_kept_after_the_hold_while_a_swap_is_pending_whatever_refuses_it(
    monkeypatch, tmp_path
) -> None:
    # The step goes on without a helper once the hold is over; a trap received then rides on no
    # popup, unless an old helper is still to be replaced: kept for the new one, through every
    # refusal after the first (a trap, then a menu opened by pausing, then no answer).
    ctx, order = mismatched_reattach(monkeypatch, tmp_path / "zexion_ap.dll", native.NotUnloaded)
    session_ = ctx.seed_state.session
    errors = iter(
        [
            native.NotUnloaded("a trap is running"),
            native.NotUnloaded("a menu is open"),
            native.ReplyLost("no reply within 3.0 s"),
        ]
    )

    def attach(*args) -> None:
        order.append("attach")
        raise next(errors)

    def step(g, received, **kwargs):
        order.append(f"step, keep {session_.keep_traps}")
        return session.Step(state=game.IN_PLAY)

    monkeypatch.setattr(native, "attach", attach)
    monkeypatch.setattr(session_, "step", step)
    for _ in range(3):
        ctx.attach_again_at = 0.0
        asyncio.run(zc.poll(ctx))
        assert ctx.swap_pending, order
    ctx.attach_again_at, ctx.helper_back_by = 0.0, 1.0  # the hold is over
    ctx.helper_lost = True
    errors = iter([native.ReplyLost("no reply within 3.0 s")])  # still not a helper attached
    monkeypatch.setattr(native, "attach", lambda *a: (_ for _ in ()).throw(next(errors)))
    asyncio.run(zc.poll(ctx))
    assert ctx.swap_pending and order[-1] == "step, keep True"


def test_a_swap_no_longer_waits_once_the_old_helper_is_gone(monkeypatch, tmp_path) -> None:
    # The old helper unloaded, then the new one failed to load: no helper is coming to show the
    # traps, so they are not held (for hours) behind it.
    ctx, order = mismatched_reattach(monkeypatch, tmp_path / "zexion_ap.dll", native.NotUnloaded)
    session_ = ctx.seed_state.session
    gone = native.HelperError("loading zexion_ap.dll into the game failed")
    gone.old_gone = True
    errors = iter([native.NotUnloaded("a menu is open"), gone, gone])

    def attach(*args) -> None:
        order.append("attach")
        raise next(errors)

    def step(g, received, **kwargs):
        order.append(f"step, keep {session_.keep_traps}")
        return session.Step(state=game.IN_PLAY)

    monkeypatch.setattr(native, "attach", attach)
    monkeypatch.setattr(session_, "step", step)
    asyncio.run(zc.poll(ctx))
    assert ctx.swap_pending
    ctx.attach_again_at = 0.0
    asyncio.run(zc.poll(ctx))
    assert not ctx.swap_pending
    ctx.attach_again_at, ctx.helper_back_by = 0.0, 1.0  # the hold is over
    asyncio.run(zc.poll(ctx))
    assert order[-1] == "step, keep False"


def test_a_swap_is_over_when_a_helper_attaches_or_the_game_closes(monkeypatch) -> None:
    ctx = context(connection(), Closed())
    ctx.swap_pending = True
    monkeypatch.setattr(native, "attach", lambda *a: Helper())
    assert asyncio.run(ticks.attach_helper(ctx)) and ctx.helper is not None
    assert not ctx.swap_pending
    ctx.swap_pending = True
    zc.game_closed(ctx, ctx.seed_state)
    assert not ctx.swap_pending


def test_traps_are_not_kept_with_no_swap_to_wait_for(monkeypatch, tmp_path) -> None:
    ctx, order = mismatched_reattach(monkeypatch, tmp_path / "zexion_ap.dll")
    session_ = ctx.seed_state.session

    def step(g, received, **kwargs):
        order.append(f"step, keep {session_.keep_traps}")
        return session.Step(state=game.IN_PLAY)

    monkeypatch.setattr(session_, "step", step)
    asyncio.run(zc.poll(ctx))
    assert not ctx.swap_pending and order[-1] == "step, keep False"


@pytest.mark.parametrize(
    ("error", "message", "own_build", "held"),
    [
        # An old helper (loaded by an earlier build) that didn't unload for the client's build.
        (native.NotUnloaded, "the mod loaded in Zexion is 0.29.0 (a menu is open)", True, True),
        (native.ReplyLost, "no reply", False, True),  # no frame yet
        # The Launcher's client, with no helper installed: nothing to wait for.
        (native.ModFixable, "it isn't installed", False, False),
        # Turned off by zexion_ap.disabled, which turns off the game folder's copy only.
        (native.ModDisabled, "The mod is turned off", False, False),
        # A developer's own build, and the proxy loaded the folder's 0.29.0 copy meanwhile.
        (native.ModMismatch, "the mod in Zexion's folder (0.29.0) loaded meanwhile", True, False),
    ],
)
def test_a_first_attach_holds_back_the_step_only_for_a_helper_that_may_still_come(
    monkeypatch, error, message, own_build, held
) -> None:
    # The client started with the game running: an old helper refusing to unload (or no
    # frame yet) gives no step that writes items with no popup or drops a waiting trap; a mod
    # missing, not matching or turned off gives the step at once, as with no helper.
    ctx = context(connection(), None)
    order: list[str] = []
    monkeypatch.setattr(zc.game.Game, "attach", staticmethod(lambda: Running(game.IN_PLAY)))

    def attach(*args) -> None:
        order.append("attach")
        raise error(message)

    def step(g, received, helper=None, give=True, **kwargs):
        order.append(f"step, {'gives' if give else 'waits'}")
        return session.Step(state=game.IN_PLAY)

    monkeypatch.setattr(native, "attach", attach)
    dll = types.SimpleNamespace(is_file=lambda: own_build)  # native/build or ZEXION_DLL
    monkeypatch.setattr(native, "default_dll", lambda: dll)
    monkeypatch.setattr(zc, "BACK_RETRY_SECONDS", 0.0)
    monkeypatch.setattr(ctx.seed_state.session, "step", step)
    for name in ("newgame_tick", "labels_tick", "toggle_tick", "discs_helper_tick"):
        monkeypatch.setattr(zc, name, nothing)
    for name in ("language_tick", "replay_tick", "offline_tick"):
        monkeypatch.setattr(zc, name, nothing)
    asyncio.run(zc.poll(ctx))
    if not held:
        assert order == ["attach", "step, gives"] and not ctx.helper_lost
        return
    assert order == ["attach", "step, waits"] and ctx.helper_lost
    ctx.attach_again_at = 0.0
    asyncio.run(zc.poll(ctx))
    assert order[-1] == "attach", "then held while it is tried again"


def test_a_disconnect_during_the_hold_lets_the_next_connection_hold_afresh(monkeypatch) -> None:
    ctx, order = failing_reattach(monkeypatch)
    asyncio.run(zc.poll(ctx))
    assert order == ["attach"] and ctx.helper_back_by > 0
    ctx.slot = None  # dropped mid-hold
    asyncio.run(zc.poll(ctx))
    assert ctx.helper_back_by == 0.0, "the bound starts over"
    ctx.slot = 1
    asyncio.run(zc.poll(ctx))
    assert order == ["attach", "attach"], "held again, not stepped at once without the helper"


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
    for name in ("language_tick", "replay_tick"):
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


def test_the_goal_counts_the_loaded_saves_own_disks_not_the_found_record(monkeypatch) -> None:
    # Three disks needed, two received, this world's own at location 12. An ending in a save
    # that holds it collected is the goal; in one reloaded from before its pickup it is not,
    # though the disk was found once (this client reported it, and the found record has it).
    hunt = dict(goal="os_install_disks", discs_required=3, discs_placed=5, disc_locations=[12])
    conn = connection(**hunt)
    ctx = context(conn, Running(game.IN_PLAY))
    ctx.locations_checked = {zc.LOCATION_ID_BASE + 12}
    ctx.stored_data[conn.found_key] = {"12": 1}
    sent: list[dict] = []

    async def send_msgs(msgs: list[dict]) -> None:
        sent.extend(msgs)

    ctx.send_msgs = send_msgs
    monkeypatch.setattr(ctx, "received", lambda: ([44, 44], ["Bob", "Bob"]))
    monkeypatch.setattr(zc, "ClientStatus", types.SimpleNamespace(CLIENT_GOAL=30))
    monkeypatch.setattr(zc, "attach_helper", lambda c: asyncio.sleep(0))
    monkeypatch.setattr(zc, "hints_tick", nothing)
    ended = dict(seed_matches=True, state=game.IN_PLAY, goal=True, ended=True, save_stamp=bytes(14))
    step = session.Step(**ended)  # the disk not collected in this save
    monkeypatch.setattr(ctx.seed_state.session, "step", lambda g, received, **kwargs: step)
    for _ in range(3):
        asyncio.run(zc.poll(ctx))
    assert not ctx.finished_game and not [m for m in sent if m["cmd"] == "StatusUpdate"]
    assert ctx.load.disks_counted == set(), "looked at twice: no own disk in the save"
    step = session.Step(checked={12}, **ended)  # picked up (again)
    for _ in range(2):
        asyncio.run(zc.poll(ctx))
    assert ctx.finished_game and [m["cmd"] for m in sent].count("StatusUpdate") == 1


def test_a_loads_own_disks_are_taken_as_counted_at_its_second_look(monkeypatch) -> None:
    hunt = dict(goal="os_install_disks", discs_required=3, discs_placed=5, disc_locations=[12])
    ctx = context(connection(**hunt), Running(game.IN_PLAY))
    monkeypatch.setattr(zc, "attach_helper", lambda c: asyncio.sleep(0))
    monkeypatch.setattr(zc, "hints_tick", nothing)
    held = session.Step(seed_matches=True, state=game.IN_PLAY, checked={12}, save_stamp=bytes(14))
    monkeypatch.setattr(ctx.seed_state.session, "step", lambda g, received, **kwargs: held)
    asyncio.run(zc.poll(ctx))
    assert ctx.load.disks_counted is None, "a first look may be of a load half way"
    asyncio.run(zc.poll(ctx))
    assert ctx.load.disks_counted == {12}, "picked up before this load was watched: no notice"
    monkeypatch.setattr(zc.Utils, "async_start", lambda coro, name="": coro.close(), raising=False)
    ctx.connected(slot_data(**hunt))  # a reconnect: its first look starts over
    assert ctx.load.disks_counted is None


class Noting(Helper):
    """A helper that takes every batch and keeps its notices."""

    def __init__(self) -> None:
        super().__init__()
        self.notices: list[str] = []

    def apply(self, text: str) -> str:
        self.notices += [line for line in text.splitlines() if line.startswith("notice")]
        return "applied"


def disks_poll(monkeypatch, received: int = 1):
    """A disks seed's save in play with a helper, the client back and the popup count read:
    the context, and ``look(collected, slot)``, a poll that finds the save holding those
    locations collected (through zc.poll, its real disk-count lines and follow-ups)."""
    hunt = dict(goal="os_install_disks", discs_required=3, discs_placed=5, disc_locations=[12, 40])
    ctx = context(connection(**hunt), Running(game.IN_PLAY))
    ctx.helper, ctx.told = Noting(), state.HelperState()
    ctx.told.client_back_sent = True
    ctx.seed_state.session.announced = received  # the received disks' popups handed over
    ctx.seed_state.session.announced_read = True
    ctx.server = None
    monkeypatch.setattr(ctx, "received", lambda: ([44] * received, ["Bob"] * received))
    for name in (
        "newgame_tick",
        "labels_tick",
        "toggle_tick",
        "discs_helper_tick",
        "language_tick",
        "replay_tick",
        "offline_tick",
        "announced_tick",
        "client_back_tick",
        "hints_tick",
        "mission_tick",
        "feats_tick",
        "shields_tick",
        "other_save_tick",
    ):
        monkeypatch.setattr(zc, name, nothing)

    def look(collected: set[int], slot: int = 3) -> None:
        stamp = bytes([slot, 0, 0, 0]) + bytes(10)
        step = session.Step(
            seed_matches=True, state=game.IN_PLAY, checked=set(collected), save_stamp=stamp
        )
        monkeypatch.setattr(ctx.seed_state.session, "step", lambda g, received, **kwargs: step)
        asyncio.run(zc.poll(ctx))

    return ctx, look


def test_an_own_disk_shows_its_count_at_each_pickup_through_the_poll(monkeypatch) -> None:
    ctx, look = disks_poll(monkeypatch)
    look({12})  # the load's first look: the save holds the disk at 12
    look({12})
    assert ctx.helper.notices == [], "picked up before this load was watched: no notice"
    look({12, 40})  # the one at 40 picked up
    assert ctx.helper.notices == ["notice 44 OS Install Disk 3"]
    look({12, 40})
    assert len(ctx.helper.notices) == 1, "once a pickup"
    look({12})  # a death or a reload without saving: it is back in its room
    look({12})
    assert len(ctx.helper.notices) == 1
    look({12, 40})  # picked up again
    assert ctx.helper.notices == ["notice 44 OS Install Disk 3"] * 2, "counted, and said, again"


def test_another_slot_of_the_seed_shows_no_count_for_the_disks_it_holds(monkeypatch) -> None:
    # Loaded while the polls paused (the helper attached again, say), so no poll saw the title:
    # the disks the other slot holds were not picked up now.
    ctx, look = disks_poll(monkeypatch)
    look(set(), slot=3)
    look(set(), slot=3)
    look({12, 40}, slot=4)
    look({12, 40}, slot=4)
    assert ctx.helper.notices == [] and ctx.load.disks_counted == {12, 40}
    look({12}, slot=4)  # a reload in it, then the pickup again
    look({12, 40}, slot=4)
    assert ctx.helper.notices == ["notice 44 OS Install Disk 3"]


def test_a_disk_picked_up_while_the_helper_was_lost_is_the_helpers_to_count(monkeypatch) -> None:
    # With no client the helper shows an own disk's count itself (watch_own_disks): the client,
    # back, takes the disks the save holds for counted, as at a connection.
    ctx, look = disks_poll(monkeypatch)
    look({12})
    look({12})
    helper = ctx.helper
    ctx.drop_helper()  # the mod stopped responding
    assert ctx.load.disks_counted is None
    ctx.helper, ctx.told = helper, state.HelperState()  # attached again
    ctx.told.client_back_sent = True
    look({12, 40})  # the one at 40 picked up meanwhile
    look({12, 40})
    assert helper.notices == [], "counted by the helper's own notice: not again"
    look({12})  # a reload, then the pickup again
    look({12, 40})
    assert helper.notices == ["notice 44 OS Install Disk 3"]


def test_a_disk_picked_up_in_an_outage_longer_than_a_poll_is_counted_once(monkeypatch) -> None:
    # A poll with no helper takes the disks the save then holds for counted again, so one
    # picked up after it would look new to the client once the helper is back, though the
    # helper, with no client, showed its count itself: the attach starts the count over.
    ctx, look = disks_poll(monkeypatch)
    look({12})
    look({12})
    helper = ctx.helper
    ctx.drop_helper()  # the mod stopped responding
    look({12})  # a poll without it (the next try is not due yet)
    assert ctx.helper is None and ctx.load.disks_counted == {12}
    monkeypatch.setattr(native, "attach", lambda g, dll, notes=None: helper)
    ctx.helper_retry_at = 0.0
    look({12, 40})  # the one at 40 picked up meanwhile; this poll attaches the helper again
    assert ctx.helper is helper
    ctx.told.client_back_sent = True
    look({12, 40})
    look({12, 40})
    assert helper.notices == [], "counted by the helper's own notice: not again"
    look({12})  # a reload, then the pickup again
    look({12, 40})
    assert helper.notices == ["notice 44 OS Install Disk 3"]


def test_a_disk_picked_up_with_no_helper_in_the_game_is_the_clients_to_count(monkeypatch) -> None:
    # The helper gone from the game (/reload_mod's few seconds, a load that failed): nothing
    # there counted a disk picked up meanwhile, so once the client has loaded the helper again
    # it shows that disk's count itself.
    ctx, look = disks_poll(monkeypatch)
    look({12})
    look({12})
    helper = ctx.helper
    ctx.drop_helper()
    look({12})  # a poll without it
    helper.loaded_here = True  # native.attach loaded it into the game itself
    monkeypatch.setattr(native, "attach", lambda g, dll, notes=None: helper)
    ctx.helper_retry_at = 0.0
    look({12, 40})  # picked up with no helper in the game; this poll loads it again
    assert ctx.helper is helper and ctx.load.disks_counted == {12}
    ctx.told.client_back_sent = True
    look({12, 40})
    assert helper.notices == ["notice 44 OS Install Disk 3"]


def test_own_traps_with_no_mod_are_said_once(monkeypatch, caplog) -> None:
    conn = connection(trap_locations=[[3, ids.DISRUPTION_TRAP]])
    ctx = context(conn, Running(game.IN_PLAY))
    ctx.helper_retry_at = float("inf")  # no attach tried here
    monkeypatch.setattr(zc, "hints_tick", nothing)
    monkeypatch.setattr(zc, "offline_tick", nothing)
    loaded = session.Step(seed_matches=True, state=game.IN_PLAY, save_stamp=bytes(14))
    monkeypatch.setattr(ctx.seed_state.session, "step", lambda g, received, **kwargs: loaded)
    caplog.set_level(logging.INFO, logger="Client")
    for _ in range(3):
        asyncio.run(zc.poll(ctx))
    said = [r.message for r in caplog.records if "own traps" in r.message]
    assert said == ["Without the mod, your own traps do nothing when you pick them up."]


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
    ctx.load.save_collected = {12}
    ctx.load.disks_counted = {12} if shown else set()
    monkeypatch.setattr(ctx, "received", lambda: ([44], ["Bob"]))
    monkeypatch.setattr(zc, "hints_tick", nothing)
    passed: dict = {}

    def step(g, received, **kwargs):
        passed.update(kwargs)
        raise game.GameNotRunning("Zexion is not running")  # the poll ends here

    monkeypatch.setattr(ctx.seed_state.session, "step", step)
    asyncio.run(zc.poll(ctx))
    assert passed["followups"] == {0: f"OS Install Disk {2 if shown else 1}"}


def test_the_step_is_given_the_other_players_items_the_server_has_checked(monkeypatch) -> None:
    # Location 3 holds another player's item, 12 this player's own disk, 7 its own Scrap Metal.
    # The server has all three as checked (a release, say); the client itself reported none.
    hunt = dict(goal="os_install_disks", discs_required=3, discs_placed=5, disc_locations=[12])
    items = list(slot_data()["location_items"])
    items[12] = ids.STAND_IN_PROGRESSION  # as the world places an own disk
    ctx = context(connection(location_items=items, **hunt), Showing(game.IN_PLAY))
    ctx.checked_locations = {zc.LOCATION_ID_BASE + i for i in (3, 7, 12)}
    monkeypatch.setattr(zc, "hints_tick", nothing)
    passed: dict = {}

    def step(g, received, **kwargs):
        passed.update(kwargs)
        raise game.GameNotRunning("Zexion is not running")  # the poll ends here

    monkeypatch.setattr(ctx.seed_state.session, "step", step)
    asyncio.run(zc.poll(ctx))
    assert passed["gone"] == {3}, "never one of this world's own items"
    # After a drop the checked locations are the last connection's until the next Connected:
    # nothing is kept collected from them (a server back with an older save has fewer).
    ctx.conn.items_listed = False  # as reset_server_state leaves the connection
    ctx.game_conn = Showing(game.IN_PLAY)
    asyncio.run(zc.poll(ctx))
    assert not passed["gone"]


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


def test_with_the_linux_build_the_step_goes_on_without_the_helper(monkeypatch, caplog) -> None:
    """The game's Linux build has no Windows helper: the step is not held for one that won't
    come, gives what is owed (through the Linux helper, when it is there: the linux_ ticks), and
    the player is told nothing about it."""
    ctx, _ = failing_reattach(monkeypatch)
    ctx.game_conn.platform = "linux"
    ctx.helper_left = False  # no Windows helper was ever let go (the Linux one: leave_helper_tick)
    monkeypatch.setattr(zc, "attach_helper", ticks.attach_helper)
    steps: list[dict] = []

    def step(g, received, **kwargs):
        steps.append(kwargs)
        raise game.GameNotRunning("Zexion is not running")

    monkeypatch.setattr(ctx.seed_state.session, "step", step)
    with caplog.at_level("WARNING"):
        asyncio.run(zc.poll(ctx))
        asyncio.run(zc.poll(ctx))
    assert steps and steps[0]["helper"] is None and steps[0]["give"]
    said = [r.getMessage() for r in caplog.records if r.name == "Client"]
    assert not [line for line in said if "Zexion mod" in line]


class LinuxHelperStandIn:
    """The game's Linux helper connection (linux_helper.LinuxProcess): replies by request."""

    def __init__(self, replies: dict[str, str]) -> None:
        from test_linux_targets import b1_like

        self.game = types.SimpleNamespace(image=b1_like(extra=True))
        self.replies = {"trap_status": "queued 0; last none", **replies}
        self.sent: list[str] = []
        self.connection = 1  # linux_helper.LinuxProcess's: the connections made
        self.connected = True
        self.disconnects = 0

    def disconnect(self) -> None:  # linux_helper.LinuxProcess's: closed, made again on use
        self.connected = False
        self.disconnects += 1

    def request(self, text: str) -> str:
        self.sent.append(text)
        first = text.split(None, 1)[0]
        if first in self.replies:
            reply = self.replies[first]
            if isinstance(reply, Exception):
                raise reply
            return reply
        return "error unknown request"

    def alive(self) -> bool:
        return True


def linux_game(replies: dict[str, str]):
    g = Running(game.NO_SAVE)
    g.platform = "linux"
    g.build = patterns.KNOWN_LINUX_BUILDS[0]
    g.pm = LinuxHelperStandIn(replies)
    return g


def test_on_linux_the_seed_is_armed_with_archipelago_through_the_linux_helper(caplog) -> None:
    ctx = context(connection(), linux_game({"config": "ok", "ap_newgame": "armed"}))
    with caplog.at_level("INFO"):
        asyncio.run(ticks.linux_newgame_tick(ctx))
    sent = ctx.game_conn.pm.sent
    assert sent[0].startswith("config rando_blob=0x12f6e00 randomizer_menu=0x12f6ce0 ")
    assert "current_menu=0x12f5700" in sent[0] and "tr_flag=0x12b48d1" in sent[0]
    assert sent[1] == "trap_status"  # a helper from before the traps is refused
    assert sent[2].startswith("ap_newgame\n") and sent[2].endswith("\nchoice")  # ARCHIPELAGO
    assert ctx.conn.newgame is state.NewGame.ARMED
    assert ctx.conn.newgame_lines == newgame.choice_message(ctx.conn.plan)
    asyncio.run(ticks.linux_newgame_tick(ctx))
    assert sum(t.startswith("config ") for t in sent) == 1  # configured once an attach


def test_a_linux_build_takes_the_start_table_of_its_windows_build() -> None:
    linux = types.SimpleNamespace(build=patterns.KNOWN_LINUX_BUILDS[0])
    windows = types.SimpleNamespace(build=patterns.KNOWN_BUILDS[0])
    assert ticks.start_table_build(linux) == patterns.KNOWN_BUILDS[0].sha256
    assert ticks.start_table_build(windows) == patterns.KNOWN_BUILDS[0].sha256
    assert ticks.start_table_build(None) == ""


def test_on_linux_a_helper_that_cant_be_reached_is_told_once_and_armed_again(caplog) -> None:
    lost = linux_helper.LinuxHelperError("the helper closed its socket")
    ctx = context(connection(), linux_game({"config": "ok", "ap_newgame": lost}))
    with caplog.at_level("WARNING"):
        asyncio.run(ticks.linux_newgame_tick(ctx))
        asyncio.run(ticks.linux_newgame_tick(ctx))
    said = [r for r in caplog.records if r.name == "Client" and "on Linux" in r.getMessage()]
    assert len(said) == 1
    assert ctx.linux_mod is None and ctx.conn.newgame is None
    ctx.game_conn.pm.replies["ap_newgame"] = "armed"
    asyncio.run(ticks.linux_newgame_tick(ctx))
    assert ctx.conn.newgame is state.NewGame.ARMED


def test_the_linux_helper_takes_an_apply_as_the_windows_helper_does() -> None:
    pm = LinuxHelperStandIn({"config": "ok", "apply": "applied"})
    mod = linux_helper.LinuxMod(pm)
    text = native.batch_text([(17, 0, 1)], [(17, "from Bob")])
    assert mod.apply(text) == "applied" and pm.sent[-1] == text
    pm.replies["apply"] = "refused not in play"
    assert mod.apply(text) == "refused not in play"
    pm.replies["apply"] = "error something"
    with pytest.raises(native.HelperError, match="unexpected reply"):
        mod.apply(text)
    pm.replies["apply"] = linux_helper.NoReply("the helper closed its socket")
    with pytest.raises(native.ReplyLost):  # it went out: it may have been applied
        mod.apply(text)
    pm.replies["apply"] = linux_helper.LinuxHelperError("the helper's socket failed")
    with pytest.raises(native.HelperError) as failed:  # never sent: not applied
        mod.apply(text)
    assert not isinstance(failed.value, native.ReplyLost)


def test_a_linux_helper_that_fails_the_step_is_configured_again(caplog) -> None:
    ctx = context(connection(), linux_game({"config": "ok", "ap_newgame": "armed"}))
    asyncio.run(ticks.linux_newgame_tick(ctx))
    assert ctx.conn.newgame is state.NewGame.ARMED and ctx.linux_mod is not None
    with caplog.at_level("WARNING"):
        ticks.linux_mod_lost(ctx, native.ReplyLost("the Linux helper: no reply"))
    assert ctx.linux_mod is None and ctx.conn.newgame is None  # armed again with it
    asyncio.run(ticks.linux_newgame_tick(ctx))
    assert sum(t.startswith("config ") for t in ctx.game_conn.pm.sent) == 2
    assert ctx.conn.newgame is state.NewGame.ARMED


def linux_steps(monkeypatch, fails: int) -> tuple[zc.ZexionContext, list]:
    """A poll's step on the game's Linux build, the Linux helper answering every configuration:
    the first ``fails`` steps given through it fail (a reply lost); ``seen`` holds the ``helper``
    and ``give`` each step was handed."""
    ctx = context(connection(), linux_game({"config": "ok", "ap_newgame": "armed"}))
    seen: list = []

    async def nothing(*args) -> None:
        return None

    for name in (
        "linux_labels_tick",
        "linux_seed_tick",
        "linux_language_tick",
        "linux_discs_tick",
        "linux_intel_tick",
        "linux_assists_tick",
        "linux_replay_tick",
        "linux_client_back_tick",
        "linux_other_save_tick",
        "offline_tick",
        "replay_tick",
        "announced_tick",
        "hints_tick",
        "attach_helper",
    ):
        monkeypatch.setattr(zc, name, nothing)

    def step(g, received, helper=None, give=True, **kwargs):
        seen.append((helper, give))
        lost = helper is not None and sum(h is not None for h, _ in seen) <= fails
        error = native.ReplyLost("the Linux helper: no reply") if lost else None
        return session.Step(state=game.IN_PLAY, helper_error=error)

    monkeypatch.setattr(ctx.seed_state.session, "step", step)
    return ctx, seen


def test_on_linux_a_mod_configured_afresh_after_failed_steps_gives_the_step(monkeypatch) -> None:
    # Two failed steps, each dropping the mod and configured again at the next poll: the third
    # step gives through the mod configured afresh (its traps and popups), not directly.
    ctx, seen = linux_steps(monkeypatch, fails=2)
    for _ in range(3):
        asyncio.run(zc.poll(ctx))
    assert [h is not None and give for h, give in seen] == [True, True, True]
    assert all(isinstance(h, linux_helper.LinuxMod) for h, _ in seen)
    assert len({id(h) for h, _ in seen}) == 3, "each configured afresh"
    assert sum(t.startswith("config ") for t in ctx.game_conn.pm.sent) == 3
    assert ctx.linux_failed == 0 and ctx.linux_mod is not None, "the third step went through"


def test_on_linux_steps_that_keep_failing_are_written_directly_not_held_back(monkeypatch) -> None:
    # A request the helper keeps refusing: after LINUX_DIRECT_AFTER failed steps with mods
    # configured in turn the next step's items are written directly, once, then the helper again.
    ctx, seen = linux_steps(monkeypatch, fails=ticks.LINUX_DIRECT_AFTER)
    for _ in range(ticks.LINUX_DIRECT_AFTER + 2):
        asyncio.run(zc.poll(ctx))
    kinds = [h is not None for h, _ in seen]
    assert kinds == [True] * ticks.LINUX_DIRECT_AFTER + [False, True]
    assert all(give for _, give in seen)


def test_on_linux_no_mod_to_configure_is_written_directly_after_the_hold(monkeypatch) -> None:
    ctx, seen = linux_steps(monkeypatch, fails=0)
    ctx.game_conn.pm.replies["config"] = "refused no frame"
    ctx.linux_back_by = time.monotonic() + ticks.LINUX_HOLD_SECONDS  # as the attach holds
    asyncio.run(zc.poll(ctx))
    assert seen[-1] == (None, False), "held while it is configured, as after any lost one"
    ctx.linux_back_by = time.monotonic() - 1.0  # the hold over
    asyncio.run(zc.poll(ctx))
    assert seen[-1] == (None, True) and ctx.linux_mod is None


def test_on_linux_a_failed_keep_alive_drops_the_helper_connection(caplog) -> None:
    # A ping with no reply closes the connection; the next request makes another that never
    # sent config, which the poll's ticks would use as configured: lost, so configured again.
    ctx = context(connection(), linux_game({"config": "ok", "ap_newgame": "armed"}))
    asyncio.run(ticks.linux_newgame_tick(ctx))
    assert ctx.linux_mod is not None
    asyncio.run(ticks.linux_keepalive_tick(ctx))
    assert ctx.game_conn.pm.sent[-1] == "ping" and ctx.linux_mod is not None
    ctx.game_conn.pm.replies["ping"] = linux_helper.NoReply("the helper closed its socket")
    with caplog.at_level("WARNING"):
        asyncio.run(ticks.linux_keepalive_tick(ctx))
    assert ctx.linux_mod is None and ctx.conn.newgame is None
    asyncio.run(ticks.linux_newgame_tick(ctx))
    assert sum(t.startswith("config ") for t in ctx.game_conn.pm.sent) == 2
    assert ctx.linux_mod is not None


def test_a_linux_helper_lost_once_configured_holds_the_step_a_while() -> None:
    ctx = context(connection(), linux_game({"config": "ok", "ap_newgame": "armed"}))
    ticks.linux_mod_lost(ctx, linux_helper.LinuxHelperError("never configured"))
    assert ctx.linux_back_by == 0.0, "nothing to wait for: written directly"
    asyncio.run(ticks.linux_newgame_tick(ctx))
    before = time.monotonic()
    ticks.linux_mod_lost(ctx, linux_helper.NoReply("no reply"))
    assert before + ticks.LINUX_HOLD_SECONDS <= ctx.linux_back_by
    ticks.linux_mod_lost(ctx, linux_helper.NoReply("no reply"))  # still lost: not held longer
    assert ctx.linux_back_by <= time.monotonic() + ticks.LINUX_HOLD_SECONDS
    asyncio.run(ticks.linux_newgame_tick(ctx))
    assert ctx.linux_mod is not None and ctx.linux_back_by == 0.0


def test_a_linux_helper_connected_again_is_configured_and_armed_again() -> None:
    # The helper drops a client's plan and labels when its connection closes (a request with no
    # reply closes it; the next read connects again): told both again.
    ctx = context(connection(), linux_game({"config": "ok", "ap_newgame": "armed"}))
    asyncio.run(ticks.linux_newgame_tick(ctx))
    first = ctx.linux_mod
    ctx.linux_mod.told.labels_for = ctx.seed_state  # labels sent on the old connection
    ctx.game_conn.pm.connected = False  # closed, not made again yet: as good as gone
    assert not ctx.linux_mod.current()
    ctx.game_conn.pm.connected = True
    ctx.game_conn.pm.connection += 1
    asyncio.run(ticks.linux_newgame_tick(ctx))
    sent = ctx.game_conn.pm.sent
    assert sum(t.startswith("config ") for t in sent) == 2
    assert sum(t.startswith("ap_newgame") for t in sent) == 2
    assert ctx.linux_mod is not first and ctx.linux_mod.told.labels_for is None
    assert ctx.conn.newgame is state.NewGame.ARMED


def test_on_linux_the_labels_go_through_the_linux_helper() -> None:
    ctx = context(connection(), linux_game({"config": "ok", "ap_labels": "labeled 0"}))
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    ctx.seed_state.dressed = False  # not this seed's save: any labels it may hold taken back
    asyncio.run(ticks.linux_labels_tick(ctx))
    assert ctx.game_conn.pm.sent[-1] == "ap_labels"
    assert not ctx.linux_mod.told.labels_held
    ctx.linux_mod.told.labels_held = True
    ctx.game_conn.pm.replies["ap_labels"] = linux_helper.NoReply("the helper closed its socket")
    asyncio.run(ticks.linux_labels_tick(ctx))
    assert ctx.linux_mod is None, "configured again on a later poll"


def test_on_linux_the_seeds_own_traps_are_handed_to_the_linux_helper(monkeypatch) -> None:
    ctx = context(connection(), linux_game({"config": "ok", "offline_put": "stored"}))
    ctx.seed_name, ctx.team, ctx.slot = "S", 0, 1
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    monkeypatch.setattr(ticks, "linux_seed", lambda ctx, p: "zexion-ap offline 1\ntraps 100:103")
    monkeypatch.setattr(ticks, "offline_seed", lambda ctx, p: None)  # not known yet
    asyncio.run(ticks.linux_seed_tick(ctx, None))
    sent = ctx.game_conn.pm.sent[-1]
    assert sent.startswith("offline_put seed ") and sent.endswith("traps 100:103")
    ctx.linux_mod.told.offline_at = 0.0
    asyncio.run(ticks.linux_seed_tick(ctx, None))
    puts = [t for t in ctx.game_conn.pm.sent if t.startswith("offline_put")]
    assert puts == [sent], "once a text"


@pytest.mark.parametrize(
    ("reply", "put_again"),
    [
        ("unchanged", False),
        ("error no folder", False),  # taken for this run, not kept: the same would be refused again
        ("error cannot write, error 28", False),  # its write failed: taken all the same
        ("error bad name", True),
    ],
)
def test_on_linux_a_seed_not_kept_is_put_again_only_if_it_was_not_taken(
    monkeypatch, reply, put_again
) -> None:
    ctx = context(connection(), linux_game({"config": "ok", "offline_put": reply}))
    ctx.seed_name, ctx.team, ctx.slot = "S", 0, 1
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    monkeypatch.setattr(ticks, "linux_seed", lambda ctx, p: "zexion-ap offline 1\ntraps -")
    monkeypatch.setattr(ticks, "offline_seed", lambda ctx, p: None)  # not known yet
    asyncio.run(ticks.linux_seed_tick(ctx, None))
    ctx.linux_mod.told.offline_at = 0.0
    asyncio.run(ticks.linux_seed_tick(ctx, None))
    puts = [t for t in ctx.game_conn.pm.sent if t.startswith("offline_put")]
    assert len(puts) == (2 if put_again else 1)


def test_on_linux_the_whole_seed_file_is_handed_over_once_known(monkeypatch) -> None:
    # Its labels, for the look with no client (offline_seed, as on Windows).
    ctx = context(connection(), linux_game({"config": "ok", "offline_put": "stored"}))
    ctx.seed_name, ctx.team, ctx.slot = "S", 0, 1
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    monkeypatch.setattr(ticks, "linux_seed", lambda ctx, p: "zexion-ap offline 1\ntraps -")
    monkeypatch.setattr(ticks, "offline_seed", lambda ctx, p: "zexion-ap offline 1\nap_labels")
    asyncio.run(ticks.linux_seed_tick(ctx, None))
    assert ctx.game_conn.pm.sent[-1].endswith("\nap_labels")


def test_on_linux_the_whole_seed_file_is_never_replaced_by_the_slot_datas(monkeypatch) -> None:
    # After a reconnect the items are listed afresh: offline_seed is None for a while, and the
    # slot data's text would replace the file the look dresses saves from.
    ctx = context(connection(), linux_game({"config": "ok", "offline_put": "stored"}))
    ctx.seed_name, ctx.team, ctx.slot = "S", 0, 1
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    full = types.SimpleNamespace(text="zexion-ap offline 1\nap_labels")
    monkeypatch.setattr(ticks, "linux_seed", lambda ctx, p: "zexion-ap offline 1\ntraps -")
    monkeypatch.setattr(ticks, "offline_seed", lambda ctx, p: full.text)
    asyncio.run(ticks.linux_seed_tick(ctx, None))
    full.text = None  # a reconnect: not known again yet
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)  # a new connection: told afresh
    asyncio.run(ticks.linux_seed_tick(ctx, None))
    puts = [t for t in ctx.game_conn.pm.sent if t.startswith("offline_put")]
    assert len(puts) == 1 and puts[0].endswith("\nap_labels")


def test_on_linux_client_connected_is_asked_of_the_linux_helper() -> None:
    ctx = context(connection(), linux_game({"config": "ok", "client_back": "shown"}))
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    assert not ctx.linux_mod.told.client_back_sent  # asked on Linux too, now there is a look
    ctx.load.seed_loaded = ctx.load.in_play = True
    asyncio.run(ticks.linux_client_back_tick(ctx))
    asyncio.run(ticks.linux_client_back_tick(ctx))
    assert ctx.game_conn.pm.sent.count("client_back") == 1
    assert ctx.linux_mod.told.client_back_sent


def test_the_linux_seed_needs_only_the_connection() -> None:
    # Known from the slot data: no wait for the items or the server's scouts.
    conn = connection()
    conn.items_listed = False
    ctx = context(conn, linux_game({}))
    text = ticks.linux_seed(ctx, None)
    assert text is not None and "\ntraps " in text and "\ndiscs -" in text


def test_the_linux_seed_carries_the_disks_once_the_items_are_listed() -> None:
    # The count every save shares and this world's own disks' cells, for the helper's count at
    # their pickup with no client (Game::watch_own_disks).
    hunt = dict(goal="os_install_disks", discs_required=3, discs_placed=5, disc_locations=[12])
    conn = connection(**hunt)
    conn.items_listed = False
    ctx = context(conn, linux_game({}))
    received = types.SimpleNamespace(codes=[44, 44])  # two disks received
    assert "\ndiscs -" in ticks.linux_seed(ctx, received)
    conn.items_listed = True
    lines = ticks.linux_seed(ctx, received).split("\n")
    discs_line = next(line for line in lines if line.startswith("discs "))
    assert discs_line.split()[1:3] == ["2", "3"]
    unfound = next(line for line in lines if line.startswith("unfound "))
    assert unfound != "unfound -"


def test_a_linux_helper_from_before_the_traps_is_told_too_old(caplog) -> None:
    # Same version, older build: it would refuse every batch with a trap's popup.
    ctx = context(
        connection(), linux_game({"config": "ok", "trap_status": "error unknown request"})
    )
    with caplog.at_level("WARNING"):
        asyncio.run(ticks.linux_newgame_tick(ctx))
        asyncio.run(ticks.linux_newgame_tick(ctx))
    assert ctx.linux_mod is None and ctx.linux_back_by == 0.0, "items written directly meanwhile"
    said = [r for r in caplog.records if r.name == "Client" and "older" in r.getMessage()]
    assert len(said) == 1


def test_on_linux_an_attach_holds_the_step_until_the_linux_helper_is_configured(
    monkeypatch,
) -> None:
    # The Linux helper is configured from the next poll (linux_newgame_tick): the attach's own
    # step gives nothing, so no item goes without its popup and no waiting trap is dropped.
    linux = linux_game({"config": "refused no frame"})
    ctx = context(connection(), linux)
    ctx.game_conn = None
    monkeypatch.setattr(game.Game, "attach", classmethod(lambda cls: linux))
    steps: list[dict] = []

    def step(g, received, **kwargs):
        steps.append(kwargs)
        raise game.GameNotRunning("Zexion is not running")

    monkeypatch.setattr(ctx.seed_state.session, "step", step)
    asyncio.run(zc.poll(ctx))
    assert steps and not steps[0]["give"]
    # (The stand-in step said the game closed, which dropped it.) Attached again: held, until a
    # configuration takes.
    ctx.game_conn = linux
    ctx.linux_back_by = time.monotonic() + ticks.LINUX_HOLD_SECONDS
    asyncio.run(ticks.linux_newgame_tick(ctx))  # no frame yet: still held
    assert ctx.linux_mod is None and ctx.linux_back_by > time.monotonic()
    linux.pm.replies["config"] = "ok"
    asyncio.run(ticks.linux_newgame_tick(ctx))
    assert ctx.linux_mod is not None and ctx.linux_back_by == 0.0


def test_a_linux_helper_with_no_frame_for_trap_status_is_not_told_too_old(caplog) -> None:
    # A frame not come in time (the game in the background) is tried again, as any failed
    # configuration; only an unknown request is a helper from before the traps.
    ctx = context(connection(), linux_game({"config": "ok", "trap_status": "refused no frame"}))
    with caplog.at_level("WARNING"):
        asyncio.run(ticks.linux_newgame_tick(ctx))
    assert ctx.linux_mod is None
    said = [r.getMessage() for r in caplog.records if r.name == "Client"]
    assert said and not [line for line in said if "older" in line]


def test_on_linux_the_disks_count_goes_through_the_linux_helper() -> None:
    from client import discs

    ctx = context(connection(), linux_game({"config": "ok", "discs": "discs off"}))
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    ctx.seed_state.dressed = False  # not this seed's save: the count off
    asyncio.run(ticks.linux_discs_tick(ctx, None))
    assert ctx.game_conn.pm.sent[-1] == discs.OFF and ctx.linux_mod.told.discs.sent == discs.OFF
    ctx.linux_mod.told.discs.sent = None
    ctx.game_conn.pm.replies["discs"] = linux_helper.NoReply("the helper closed its socket")
    asyncio.run(ticks.linux_discs_tick(ctx, None))
    assert ctx.linux_mod is None, "configured again on a later poll"


def test_on_linux_intel_chips_go_through_the_linux_helper() -> None:
    ctx = context(connection(), linux_game({"config": "ok", "intel": "intel on"}))
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    ctx.load.intel_want = True  # this seed's save loaded, its Area Maps Intel Chips
    asyncio.run(ticks.linux_intel_tick(ctx))
    asyncio.run(ticks.linux_intel_tick(ctx))
    assert ctx.game_conn.pm.sent.count("intel on") == 1 and ctx.linux_mod.told.intel.sent is True
    ctx.game_conn.pm.replies["intel"] = linux_helper.NoReply("the helper closed its socket")
    ctx.load.intel_want = False
    asyncio.run(ticks.linux_intel_tick(ctx))
    assert ctx.linux_mod is None, "configured again on a later poll"


def test_on_linux_the_assist_lock_goes_through_the_linux_helper() -> None:
    ctx = context(connection(), linux_game({"config": "ok", "assists": "locked"}))
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    ctx.load.assists_want = True  # this seed's save loaded, assists disallowed
    asyncio.run(ticks.linux_assists_tick(ctx))
    asyncio.run(ticks.linux_assists_tick(ctx))
    sent = ctx.game_conn.pm.sent
    assert sent.count("assists lock") == 1 and ctx.linux_mod.told.assists.sent is True
    ctx.game_conn.pm.replies["assists"] = linux_helper.NoReply("the helper closed its socket")
    ctx.load.assists_want = False
    asyncio.run(ticks.linux_assists_tick(ctx))
    assert ctx.linux_mod is None, "configured again on a later poll"


def test_on_linux_a_setting_is_sent_only_on_the_connection_that_sent_config() -> None:
    # A request on a connection made again since (a read in the same poll) would set the ASSIST
    # lock, the Intel Chip or the disks' count in a helper that keeps it past that connection's
    # close, as the connection never sent config.
    ctx = context(
        connection(),
        linux_game(
            {"config": "ok", "assists": "locked", "intel": "intel on", "discs": "discs off"}
        ),
    )
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    ctx.load.assists_want = ctx.load.intel_want = True
    ctx.seed_state.dressed = False
    pm = ctx.game_conn.pm
    pm.connection = 2
    sent = len(pm.sent)
    asyncio.run(ticks.linux_assists_tick(ctx))
    asyncio.run(ticks.linux_intel_tick(ctx))
    asyncio.run(ticks.linux_discs_tick(ctx, None))
    assert len(pm.sent) == sent and ctx.linux_mod is not None
    pm.connection = 1  # the connection it was configured on
    asyncio.run(ticks.linux_assists_tick(ctx))
    asyncio.run(ticks.linux_intel_tick(ctx))
    assert pm.sent[sent:] == ["assists lock", "intel on"]


def test_on_linux_a_connection_made_again_takes_the_saves_disks_for_counted() -> None:
    # The helper had no client between the two connections, so it counted an own disk picked
    # up then itself: the client's next look takes the save's disks for counted (no second count
    # popup), as attach_helper does on Windows. Not after a failure that kept the connection.
    ctx = context(connection(), linux_game({"config": "ok", "ap_newgame": "armed"}))
    pm = ctx.game_conn.pm
    asyncio.run(ticks.linux_newgame_tick(ctx))
    ctx.load.disks_counted = {12}
    ticks.linux_mod_lost(ctx, linux_helper.LinuxHelperError("the helper refused the addresses"))
    asyncio.run(ticks.linux_newgame_tick(ctx))  # the same connection: the helper kept its client
    assert ctx.load.disks_counted == {12}
    pm.connection = 2  # a request with no reply closed it; the next read made another
    asyncio.run(ticks.linux_newgame_tick(ctx))
    assert ctx.load.disks_counted is None
    assert ctx.linux_mod is not None and ctx.linux_mod.current()


def test_on_linux_no_follow_up_goes_through_a_connection_made_again(monkeypatch) -> None:
    ctx = context(connection(), linux_game({"config": "ok"}))
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    ctx.game_conn.pm.connection = 2  # not configured on this one: the helper has no client

    async def refused(*args) -> None:
        raise AssertionError("no follow-up through it")

    monkeypatch.setattr(ticks, "followups_tick", refused)
    asyncio.run(ticks.linux_finds_tick(ctx, None, None))


def test_on_linux_a_failed_follow_up_drops_the_linux_helper_not_the_windows_one(
    monkeypatch,
) -> None:
    # LinuxMod.apply raises native's errors: still the Linux helper's to drop (linux_mod_lost).
    ctx = context(connection(), linux_game({"config": "ok"}))
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)

    async def lost(*args) -> None:
        raise native.ReplyLost("the Linux helper: no reply")

    monkeypatch.setattr(ticks, "followups_tick", lost)
    asyncio.run(ticks.linux_finds_tick(ctx, None, None))
    assert ctx.linux_mod is None and not ctx.helper_lost


@pytest.mark.parametrize(
    ("status", "offered", "followed"),
    [
        ("not asked", True, False),  # dropped before it showed: offered again
        ("put back: the prompt was up, YES is NO", True, False),  # shown, then put back: again
        ("asked", False, True),  # still on its way
        ("opening after the fade", False, True),
        ("yes: ending when idle (another transition came first)", False, True),
        ("yes: ending when idle, the screen kept black", False, True),
        ("ending started", False, False),  # the ending is going: nothing left to drop
        ("shown", False, True),  # followed once a connection: a put back one is offered again
        ("refused no frame", False, True),  # no news: still followed, asked again next poll
        ("lapsed: the player left the landing pod", False, False),
    ],
)
def test_on_linux_a_prompt_dropped_before_it_showed_is_offered_again(
    monkeypatch, status, offered, followed
) -> None:
    ctx = context(connection(), linux_game({"config": "ok", "mission_status": status}))
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)

    async def nothing(*args) -> None:
        return None

    monkeypatch.setattr(ticks, "followups_tick", nothing)
    monkeypatch.setattr(ticks, "mission_tick", nothing)
    home = ctx.load.return_home
    home.queued = True
    asyncio.run(ticks.linux_finds_tick(ctx, None, None))
    assert home.offer is offered and home.queued is followed


def test_on_linux_a_shown_prompt_put_back_by_a_closed_connection_is_offered_again(
    monkeypatch,
) -> None:
    # Shown, then the connection closes with the prompt up: the helper puts YES back to NO's
    # callback (status "put back"), so pressing YES does nothing: asked for again.
    ctx = context(connection(), linux_game({"config": "ok", "mission_status": "shown"}))
    pm = ctx.game_conn.pm
    ctx.linux_mod = linux_helper.LinuxMod(pm)

    async def nothing(*args) -> None:
        return None

    monkeypatch.setattr(ticks, "followups_tick", nothing)
    monkeypatch.setattr(ticks, "mission_tick", nothing)
    home = ctx.load.return_home
    home.queued = True
    asyncio.run(ticks.linux_finds_tick(ctx, None, None))
    asyncio.run(ticks.linux_finds_tick(ctx, None, None))
    assert pm.sent.count("mission_status") == 1, "shown: asked once on a connection"
    assert home.queued and not home.offer
    pm.connection = 2  # closed and made again; configured again (linux_newgame_tick)
    ctx.linux_mod = linux_helper.LinuxMod(pm)
    pm.replies["mission_status"] = "put back: the prompt was up, YES is NO"
    asyncio.run(ticks.linux_finds_tick(ctx, None, None))
    assert home.offer and not home.queued


@pytest.mark.parametrize(
    "status",
    [
        "put back: the prompt was up, YES is NO",  # still up when the connection closed
        "put back: YES was pressed, the ending had not started",  # YES, dropped before the ending
    ],
)
def test_on_linux_a_shown_prompt_dropped_up_or_after_yes_is_offered_again(
    monkeypatch, status
) -> None:
    # Read as "shown" on one connection, so no longer asked about there: a YES pressed meanwhile
    # whose connection closed before the ending started reads "put back", not "not asked".
    ctx = context(connection(), linux_game({"config": "ok", "mission_status": "shown"}))
    pm = ctx.game_conn.pm
    ctx.linux_mod = linux_helper.LinuxMod(pm)

    async def nothing(*args) -> None:
        return None

    monkeypatch.setattr(ticks, "followups_tick", nothing)
    monkeypatch.setattr(ticks, "mission_tick", nothing)
    home = ctx.load.return_home
    home.queued = True
    asyncio.run(ticks.linux_finds_tick(ctx, None, None))
    assert home.queued and home.shown_on is not None
    pm.connection = 2
    ctx.linux_mod = linux_helper.LinuxMod(pm)
    pm.replies["mission_status"] = status
    asyncio.run(ticks.linux_finds_tick(ctx, None, None))
    assert home.offer and not home.queued


def test_on_linux_a_prompt_answered_no_is_not_offered_again_by_a_new_connection(
    monkeypatch,
) -> None:
    ctx = context(connection(), linux_game({"config": "ok", "mission_status": "shown"}))
    pm = ctx.game_conn.pm
    ctx.linux_mod = linux_helper.LinuxMod(pm)

    async def nothing(*args) -> None:
        return None

    monkeypatch.setattr(ticks, "followups_tick", nothing)
    monkeypatch.setattr(ticks, "mission_tick", nothing)
    home = ctx.load.return_home
    home.queued = True
    asyncio.run(ticks.linux_finds_tick(ctx, None, None))
    pm.connection = 2  # a NO closed it first: its status stays "shown"
    ctx.linux_mod = linux_helper.LinuxMod(pm)
    asyncio.run(ticks.linux_finds_tick(ctx, None, None))
    assert not home.offer and home.queued and pm.sent.count("mission_status") == 2
    pm.connection = 3
    ctx.linux_mod = linux_helper.LinuxMod(pm)
    pm.replies["mission_status"] = "not asked"  # the NO closed it; the cancel reset the status
    asyncio.run(ticks.linux_finds_tick(ctx, None, None))
    assert not home.offer and not home.queued


def test_on_linux_the_poll_asks_the_linux_helper_for_shields() -> None:
    # The Linux build has no Windows helper: the Linux helper's is enough for shields_tick.
    ctx = context(connection(), linux_game({"config": "ok"}))
    seed = ctx.seed_state
    ctx.helper = None
    assert not zc.shields_due(ctx, seed)  # nothing to spawn with, nothing to look for
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    assert zc.shields_due(ctx, seed)


def test_on_linux_a_received_death_is_not_said_to_wait_for_the_mod(caplog, monkeypatch) -> None:
    # The Linux helper deals it (ticks.deathlink_tick): no "the Zexion mod isn't running".
    base = zc.ZexionContext.__mro__[1]  # CommonContext's stand-in here
    monkeypatch.setattr(base, "on_deathlink", lambda self, data: None, raising=False)
    conn = connection()
    conn.death_link_on = True
    ctx = context(conn, linux_game({"config": "ok"}))
    ctx.helper = None
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    death = {"time": 1.0, "source": "Other", "cause": "Other died."}
    with caplog.at_level("WARNING"):
        ctx.on_deathlink(death)
        assert not any("isn't running" in r.getMessage() for r in caplog.records)
        ctx.linux_mod = None  # no helper of either kind: said
        ctx.on_deathlink(dict(death, time=2.0))
    assert sum("isn't running" in r.getMessage() for r in caplog.records) == 1
    assert ctx.seed_state.deaths.owed == 2


def test_on_windows_the_linux_tick_does_nothing() -> None:
    ctx = context(connection(), Running(game.NO_SAVE))
    asyncio.run(ticks.linux_newgame_tick(ctx))
    assert ctx.linux_mod is None


def test_on_linux_reload_mod_says_it_cannot_reload() -> None:
    # The Linux helper is preloaded with the game: /reload_mod says so, and asks for nothing.
    ctx = context(connection(), linux_game({"config": "ok"}))
    said: list[str] = []
    processor = types.SimpleNamespace(ctx=ctx, output=said.append)  # AP's processor is stubbed here
    assert not zc.ZexionCommandProcessor._cmd_reload_mod(processor)
    assert said == ["On Linux the Zexion mod can't be reloaded: restart Zexion instead."]
    assert not ctx.reload_mod_pending


def test_on_linux_the_helper_retry_leaves_the_linux_helpers_plan_alone(caplog) -> None:
    ctx = context(connection(), linux_game({"config": "ok", "ap_newgame": "armed"}))
    asyncio.run(ticks.linux_newgame_tick(ctx))
    assert ctx.conn.newgame is state.NewGame.ARMED
    with caplog.at_level("WARNING"):
        assert not asyncio.run(ticks.attach_helper(ctx))
        assert not asyncio.run(ticks.attach_helper(ctx))
    assert ctx.conn.newgame is state.NewGame.ARMED  # not reset as drop_helper would
    assert not [r for r in caplog.records if r.name == "Client"]  # nothing to say there


# --- the Linux helper let go while no connection steps, and kept from going silent --------------


def linux_away(shown: str = game.IN_PLAY, replies: dict | None = None) -> zc.ZexionContext:
    """A dropped connection, the Linux build showing ``shown``, its helper configured."""
    ctx = context(connection(), linux_game({"config": "ok", **(replies or {})}), dropped=True)
    ctx.game_conn.shown = shown
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    return ctx


def test_on_linux_a_save_loaded_while_dropped_lets_the_linux_helper_go(caplog) -> None:
    # Its lang every 5 s would keep the helper's client open for good: the helper dresses the save
    # itself once the connection closes, as the Windows helper does when it is let go.
    ctx = linux_away()
    ctx.conn.newgame = state.NewGame.ARMED
    with caplog.at_level(logging.DEBUG):
        asyncio.run(zc.leave_helper_tick(ctx))
    assert ctx.linux_mod is None and ctx.helper_left
    assert ctx.game_conn.pm.disconnects == 1 and ctx.conn.newgame is None  # armed again later
    assert any("Not connected" in r.getMessage() for r in caplog.records)


def test_on_linux_the_helper_let_go_is_not_configured_while_no_connection_steps() -> None:
    ctx = linux_away()
    asyncio.run(zc.leave_helper_tick(ctx))
    pm = ctx.game_conn.pm
    sent = len(pm.sent)
    asyncio.run(ticks.linux_newgame_tick(ctx))  # a poll with no connection: not configured again
    asyncio.run(zc.leave_helper_tick(ctx))  # nor let go twice
    assert ctx.linux_mod is None and len(pm.sent) == sent and pm.disconnects == 1
    ctx.slot = 1  # a connection steps: configured again, before that poll's step
    asyncio.run(ticks.linux_newgame_tick(ctx))
    assert ctx.linux_mod is not None and pm.sent[sent].startswith("config ")


def test_on_linux_a_reconnect_holds_the_step_until_the_helper_is_configured(monkeypatch) -> None:
    # As after a Windows helper let go (helper_back_by): the step gives nothing with no popup.
    ctx = linux_away()
    asyncio.run(zc.leave_helper_tick(ctx))
    ctx.game_conn.pm.replies["config"] = "refused"  # not taken when the connection comes back
    ctx.slot = 1
    steps: list[dict] = []

    def step(g, received, **kwargs):
        steps.append(kwargs)
        raise game.GameNotRunning("Zexion is not running")

    monkeypatch.setattr(ctx.seed_state.session, "step", step)
    asyncio.run(zc.poll(ctx))
    assert steps and not steps[0]["give"] and not ctx.helper_left


@pytest.mark.parametrize(
    ("shown", "dressed", "holding"),
    [(game.NO_SAVE, False, False), (game.IN_PLAY, True, False), (game.IN_PLAY, False, True)],
)
def test_on_linux_the_helper_is_kept_where_the_windows_one_is(shown, dressed, holding) -> None:
    ctx = linux_away(shown)
    ctx.seed_state.dressed, ctx.seed_state.session.holding = dressed, holding
    mod = ctx.linux_mod
    asyncio.run(zc.leave_helper_tick(ctx))
    assert ctx.linux_mod is mod and not ctx.helper_left and ctx.game_conn.pm.disconnects == 0


def test_on_windows_nothing_is_let_go_without_a_helper_or_a_linux_helper() -> None:
    ctx = context(connection(), Showing(game.IN_PLAY), dropped=True)
    asyncio.run(zc.leave_helper_tick(ctx))
    assert not ctx.helper_left


def test_on_linux_the_client_pings_the_helper_it_is_attached_to() -> None:
    # The helper drops a client silent for 30 s: the ping keeps it, whatever the poll waits on.
    ctx = linux_away(replies={"ping": "pong frames=1"})
    asyncio.run(ticks.linux_keepalive_tick(ctx))
    assert ctx.game_conn.pm.sent[-1] == "ping"


def test_on_linux_the_ping_is_left_to_a_helper_not_configured_on_this_connection() -> None:
    ctx = linux_away(replies={"ping": "pong frames=1"})
    pm = ctx.game_conn.pm
    sent = len(pm.sent)
    pm.connection = 2  # made again since: the poll's ticks configure it again
    asyncio.run(ticks.linux_keepalive_tick(ctx))
    ctx.linux_mod = None  # let go, or lost
    asyncio.run(ticks.linux_keepalive_tick(ctx))
    assert len(pm.sent) == sent


def test_a_ping_that_fails_drops_the_helper_to_be_configured_again() -> None:
    ctx = linux_away(replies={"ping": linux_helper.NoReply("no answer")})
    asyncio.run(ticks.linux_keepalive_tick(ctx))  # no error raised: the loop goes on
    assert ctx.linux_mod is None, "the next request would make a connection that sent no config"


def test_the_ping_is_sent_every_few_seconds_to_stay_inside_the_helpers_30(monkeypatch) -> None:
    assert ticks.LINUX_KEEPALIVE_SECONDS * 2 < 30
    ctx = linux_away(replies={"ping": "pong frames=1"})
    ctx.exit_event = types.SimpleNamespace(is_set=lambda: len(naps) >= 2)
    naps: list[float] = []

    async def nap(seconds: float) -> None:
        naps.append(seconds)

    monkeypatch.setattr(zc.asyncio, "sleep", nap)
    asyncio.run(zc.keepalive_loop(ctx))
    assert naps == [ticks.LINUX_KEEPALIVE_SECONDS] * 2
    assert ctx.game_conn.pm.sent.count("ping") == 2


WHOLE_FILE = (
    "zexion-ap offline 1\ntable 0102\nstart 0600bf004901\nintel 0\nassists free\n"
    "discs 3 12 41\nunfound 7\ntraps -\nap_labels\nlabel 23 14 4150\n"
)
POORER_FILE = (
    "zexion-ap offline 1\ntable 0102\nstart 0600bf004901\nintel 0\nassists free\n"
    "discs -\nunfound -\ntraps 100:103\n"
)


def test_on_linux_a_new_client_run_never_puts_a_poorer_text_over_the_whole_file(
    monkeypatch,
) -> None:
    # The labels and the disk counts the file holds, put before the scouts answer (a client that
    # closes meanwhile leaves the later runs with no client the file as it was put).
    ctx = context(connection(), linux_game({"config": "ok", "offline_put": "stored"}))
    ctx.seed_name, ctx.team, ctx.slot = "S", 0, 1
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    looked: list[str] = []
    monkeypatch.setattr(
        ticks, "linux_stored_seed", lambda mod, name: looked.append(name) or WHOLE_FILE
    )
    monkeypatch.setattr(ticks, "linux_seed", lambda ctx, p: POORER_FILE)
    monkeypatch.setattr(ticks, "offline_seed", lambda ctx, p: None)  # not known yet
    asyncio.run(ticks.linux_seed_tick(ctx, None))
    sent = ctx.game_conn.pm.sent[-1]
    assert sent.startswith("offline_put seed S-0-1\n")
    text = sent.split("\n", 1)[1]
    assert "traps 100:103\n" in text, "its own traps are this run's"
    assert "discs 3 12 41\nunfound 7\n" in text and text.endswith("ap_labels\nlabel 23 14 4150\n")
    ctx.linux_mod.told.offline_at = 0.0
    asyncio.run(ticks.linux_seed_tick(ctx, None))
    assert looked == ["S-0-1"], "the file is looked at once a run"
    assert [t for t in ctx.game_conn.pm.sent if t.startswith("offline_put")] == [sent]


def test_on_linux_with_no_file_kept_the_poorer_text_goes_as_it_is(monkeypatch) -> None:
    ctx = context(connection(), linux_game({"config": "ok", "offline_put": "stored"}))
    ctx.seed_name, ctx.team, ctx.slot = "S", 0, 1
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    monkeypatch.setattr(ticks, "linux_seed", lambda ctx, p: POORER_FILE)
    monkeypatch.setattr(ticks, "offline_seed", lambda ctx, p: None)
    asyncio.run(ticks.linux_seed_tick(ctx, None))
    assert ctx.game_conn.pm.sent[-1] == "offline_put seed S-0-1\n" + POORER_FILE


def test_on_linux_the_whole_text_is_put_as_it_is_whatever_the_file_holds(monkeypatch) -> None:
    ctx = context(connection(), linux_game({"config": "ok", "offline_put": "stored"}))
    ctx.seed_name, ctx.team, ctx.slot = "S", 0, 1
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    newer = WHOLE_FILE.replace("label 23 14 4150", "label 9 9 41")
    monkeypatch.setattr(ticks, "linux_stored_seed", lambda mod, name: WHOLE_FILE)
    monkeypatch.setattr(ticks, "offline_seed", lambda ctx, p: newer)
    asyncio.run(ticks.linux_seed_tick(ctx, None))
    assert ctx.game_conn.pm.sent[-1] == "offline_put seed S-0-1\n" + newer


REAL_LINUX_STORED_SEED = ticks.linux_stored_seed  # before the autouse fixture's stand-in


def test_the_stored_seed_is_read_beside_the_newest_token(tmp_path, monkeypatch) -> None:
    folder = tmp_path / "ZexionAP"
    (folder / "seeds").mkdir(parents=True)
    (folder / "seeds" / "S-0-1.txt").write_bytes(WHOLE_FILE.encode())
    mod = types.SimpleNamespace(pm=types.SimpleNamespace(game=object()))
    monkeypatch.setattr(
        linux_helper, "find_tokens", lambda game: [(folder / "token", "ab"), (tmp_path / "t", "cd")]
    )
    assert REAL_LINUX_STORED_SEED(mod, "S-0-1") == WHOLE_FILE
    assert REAL_LINUX_STORED_SEED(mod, "other-0-1") is None  # no such file
    (folder / "seeds" / "big-0-1.txt").write_bytes(b"x" * (offline.MAX_FILE + 1))
    (folder / "seeds" / "bin-0-1.txt").write_bytes(b"\xff\xfe")
    assert REAL_LINUX_STORED_SEED(mod, "big-0-1") is None
    assert REAL_LINUX_STORED_SEED(mod, "bin-0-1") is None  # not text


def test_the_stored_seed_is_none_where_no_token_is_found(monkeypatch) -> None:
    def none(game):
        raise linux_helper.LinuxHelperError("no token")

    monkeypatch.setattr(linux_helper, "find_tokens", none)
    mod = types.SimpleNamespace(pm=types.SimpleNamespace(game=object()))
    assert REAL_LINUX_STORED_SEED(mod, "S-0-1") is None


def test_on_linux_a_merged_text_the_helper_refuses_is_put_again_as_the_slot_datas(
    monkeypatch,
) -> None:
    replies = {"config": "ok", "offline_put": "error bad file"}
    ctx = context(connection(), linux_game(replies))
    ctx.seed_name, ctx.team, ctx.slot = "S", 0, 1
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    monkeypatch.setattr(ticks, "linux_stored_seed", lambda mod, name: WHOLE_FILE)
    monkeypatch.setattr(ticks, "linux_seed", lambda ctx, p: POORER_FILE)
    monkeypatch.setattr(ticks, "offline_seed", lambda ctx, p: None)
    asyncio.run(ticks.linux_seed_tick(ctx, None))
    ctx.linux_mod.told.offline_at = 0.0
    asyncio.run(ticks.linux_seed_tick(ctx, None))
    puts = [t for t in ctx.game_conn.pm.sent if t.startswith("offline_put")]
    assert len(puts) == 2 and "ap_labels" in puts[0] and puts[1].endswith("\n" + POORER_FILE)


def test_the_stored_seed_is_read_only_as_a_plain_file(tmp_path) -> None:
    seeds = tmp_path / "seeds"
    seeds.mkdir()
    (seeds / "dir-0-1.txt").mkdir()
    assert ticks.read_stored_seed(tmp_path, "dir-0-1") is None
    if hasattr(os, "mkfifo"):
        os.mkfifo(seeds / "pipe-0-1.txt")
        assert ticks.read_stored_seed(tmp_path, "pipe-0-1") is None  # no block
    try:
        (seeds / "link-0-1.txt").symlink_to(seeds / "dir-0-1.txt")
    except OSError:
        return  # no symlinks here
    (seeds / "real.txt").write_bytes(b"x")
    (seeds / "link2-0-1.txt").symlink_to(seeds / "real.txt")
    if hasattr(os, "O_NOFOLLOW"):
        assert ticks.read_stored_seed(tmp_path, "link2-0-1") is None


def test_on_linux_a_refusal_not_about_the_files_lines_keeps_the_merged_text(monkeypatch) -> None:
    # Only a text the helper cannot parse is dropped for the slot data's alone: any other refusal
    # would take the stored labels with it for nothing.
    ctx = context(connection(), linux_game({"config": "ok", "offline_put": "error exception"}))
    ctx.seed_name, ctx.team, ctx.slot = "S", 0, 1
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    monkeypatch.setattr(ticks, "linux_stored_seed", lambda mod, name: WHOLE_FILE)
    monkeypatch.setattr(ticks, "linux_seed", lambda ctx, p: POORER_FILE)
    monkeypatch.setattr(ticks, "offline_seed", lambda ctx, p: None)
    for _ in range(2):
        asyncio.run(ticks.linux_seed_tick(ctx, None))
        ctx.linux_mod.told.offline_at = 0.0
    puts = [t for t in ctx.game_conn.pm.sent if t.startswith("offline_put")]
    assert len(puts) == 2 and all("ap_labels" in t for t in puts)


def test_the_stored_seed_is_read_beside_the_token_the_helper_took(tmp_path, monkeypatch) -> None:
    taken = tmp_path / "live" / "ZexionAP"
    (taken / "seeds").mkdir(parents=True)
    (taken / "seeds" / "S-0-1.txt").write_bytes(WHOLE_FILE.encode())
    mod = types.SimpleNamespace(pm=types.SimpleNamespace(game=object(), token_path=taken / "token"))

    def newest(game):  # a stale token in another folder, the newest: not what is read
        raise AssertionError("the token the helper took is known")

    monkeypatch.setattr(linux_helper, "find_tokens", newest)
    assert REAL_LINUX_STORED_SEED(mod, "S-0-1") == WHOLE_FILE
