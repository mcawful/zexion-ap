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

from client import game, native, state


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
