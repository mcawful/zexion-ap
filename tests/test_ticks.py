"""client/ticks.py: what the side tasks tell the helper, against a stand-in client and helper
(no game, no AP)."""

from __future__ import annotations

import asyncio
import logging
import types
from typing import NamedTuple

from fakes import SECRET_LIST, connection, plan, secret_plan, slot_data

from client import aplabels, discs, game, ids, newgame, offline, session, state, ticks

HUNT = dict(goal="os_install_disks", discs_required=3, discs_placed=5, disc_locations=[12])


class Scouted(NamedTuple):
    """What the server said a location holds (NetUtils.NetworkItem's fields)."""

    item: int
    location: int
    player: int
    flags: int


class Helper:
    """A helper that takes every request, and keeps them."""

    def __init__(self) -> None:
        self.requests: list[str] = []

    def request(self, text: str, seconds: float = 2.0) -> str:
        self.requests.append(text)
        first = text.split("\n", 1)[0]
        if first == "die":
            return "died"
        if first == "ap_labels":
            return f"labeled {text.count(chr(10)) // 2}"
        if first.startswith("discs"):
            return "discs off" if first == discs.OFF else "discs on"
        return "none"

    def firsts(self) -> list[str]:
        """Each request's first word."""
        return [r.split("\n", 1)[0].split(" ", 1)[0] for r in self.requests]


class Ctx:
    """The parts of ZexionContext the ticks read."""

    game = "Zexion"

    def __init__(self, conn: state.Connection) -> None:
        self.conn = conn
        self.helper = Helper()
        self.told = state.HelperState()
        self.seed_state = state.SeedState(("S", 0, 1))
        self.load = state.LoadState()
        self.said = state.SayOnce()
        self.slot = 1
        self.player_names = {1: "Me", 2: "Bob"}
        self.slot_info: dict = {}
        self.locations_info: dict = {}
        self.unlocked = False
        self.deaths_sent: list[str] = []

    def assists_unlocked(self) -> bool:
        return self.unlocked

    async def send_death(self, text: str = "") -> None:
        self.deaths_sent.append(text)

    def scout(self, progression: bool = False) -> None:
        """The server's answer for each location holding another player's item."""
        flags = aplabels.PROGRESSION if progression else 0
        for i in self.conn.foreign:
            loc = ids.LOCATION_ID_BASE + i
            self.locations_info[loc] = Scouted(ids.ITEM_ID_BASE + 16, loc, 2, flags)


def no_finds() -> types.SimpleNamespace:
    """A Poll: no items received, none of this world's locations found."""
    return types.SimpleNamespace(codes=[], found=set())


def run(tick) -> None:
    asyncio.run(tick)


# --- DeathLink ---------------------------------------------------------------------------------


def in_play(goal: bool) -> session.Step:
    return session.Step(goal=goal, seed_matches=True, state=game.IN_PLAY, ended=goal)


def test_a_save_past_its_ending_loaded_again_deals_received_deaths() -> None:
    # Flag 250 stays set and is saved: a later load of the ended save reads the goal, but its
    # ending is not running (Session.holding is False), and the player is in play.
    ctx = Ctx(connection(death_link=True))
    ctx.seed_state.deaths.receive()
    run(ticks.deathlink_tick(ctx, in_play(goal=True)))
    assert ctx.helper.requests == ["die"]
    assert ctx.seed_state.deaths.owed == 0


def test_deaths_queued_when_the_ending_begins_in_this_load_are_dropped() -> None:
    ctx = Ctx(connection(death_link=True))
    ctx.conn.session.holding = True  # the goal read going true in this load (Session.step)
    ctx.seed_state.deaths.receive()
    ctx.seed_state.deaths.receive()  # the whole queue
    run(ticks.deathlink_tick(ctx, in_play(goal=True)))
    assert ctx.helper.requests == []
    assert ctx.seed_state.deaths.owed == 0


# --- the offline seed file ---------------------------------------------------------------------


def test_the_offline_file_locks_assist_until_assist_unlock_is_received() -> None:
    # Before this seed's save is seen in a connection, conn.lock_assists is the slot data's:
    # the file follows Assist Unlock, received on an earlier connection, all the same.
    ctx = Ctx(connection(allow_assists=False))
    ctx.scout()
    assert "assists lock" in ticks.offline_seed(ctx, no_finds()).splitlines()
    ctx.unlocked = True
    assert ctx.conn.lock_assists
    assert "assists free" in ticks.offline_seed(ctx, no_finds()).splitlines()


def test_the_offline_file_leaves_assist_free_when_the_seed_allows_it() -> None:
    ctx = Ctx(connection(allow_assists=True))
    ctx.scout()
    assert "assists free" in ticks.offline_seed(ctx, no_finds()).splitlines()


def secret_connection() -> state.Connection:
    """A Secret Shuffle seed's connection: 155 locations, the 155th (index 154) listed last by
    location index and at list position 100 in the game's order (fakes.SECRET_CELLS)."""
    conn = connection()
    p = secret_plan()
    conn.plan, conn.session = p, session.Session(p)
    conn.kept = frozenset(range(155))
    conn.foreign = [154]
    return conn


def test_a_secret_shuffle_seeds_offline_table_is_in_the_games_order() -> None:
    ctx = Ctx(secret_connection())
    ctx.scout()
    p = ctx.conn.plan
    assert p.table != bytes(p.location_items), "the 155th moves the entries after it"
    lines = ticks.offline_seed(ctx, no_finds()).splitlines()
    assert lines[1] == "table " + p.table.hex()
    assert lines[1] == offline.seed_text(p.table, p.start, False, False, None, None).splitlines()[1]


def test_a_secret_shuffle_seeds_labels_go_on_each_locations_own_cell() -> None:
    ctx = Ctx(secret_connection())
    ctx.scout(progression=True)
    labels, own = ticks.foreign_labels(ctx)
    # The 155th's cell is the one at list position 100, not the list's 155th.
    assert labels == {SECRET_LIST[100]: aplabels.label(True, "Bob")} and own == 0


# --- a dropped connection and the reconnect ----------------------------------------------------


def dressed_save(**changes) -> Ctx:
    """This seed's save loaded, its labels and disk count sent to the helper."""
    ctx = Ctx(connection(**changes))
    ctx.scout()
    ctx.conn.dressed, ctx.conn.discs_told = True, True
    run(ticks.labels_tick(ctx))
    run(ticks.discs_helper_tick(ctx, no_finds()))
    assert ctx.helper.firsts() == ["ap_labels", "discs"]
    assert ctx.told.discs.sent == discs.request(0, 3, told=True)
    ctx.helper.requests.clear()
    return ctx


def reconnect(ctx: Ctx, same_seed: bool) -> None:
    """A dropped connection, then Connected (zexion_client.connected): a new connection, its
    items not listed yet."""
    old = ctx.conn
    old.items_listed = False  # reset_server_state
    conn = state.Connection.from_slot_data(slot_data(**HUNT), 0, 1)
    state.carry_over(conn, old, same_seed, ctx.told)
    ctx.conn = conn


def test_a_reconnect_to_the_same_seed_leaves_the_save_dressed() -> None:
    ctx = dressed_save(**HUNT)
    reconnect(ctx, same_seed=True)
    run(ticks.labels_tick(ctx))
    run(ticks.discs_helper_tick(ctx, no_finds()))  # the items not listed yet: waits
    ctx.conn.items_listed = True
    ctx.scout()
    run(ticks.labels_tick(ctx))
    run(ticks.discs_helper_tick(ctx, no_finds()))
    assert "ap_labels" not in ctx.helper.firsts(), "the labels neither taken back nor sent again"
    assert "discs" not in ctx.helper.firsts(), "nor the disk count"


def test_labels_taken_while_the_connection_changed_are_taken_back() -> None:
    # The helper held none; labels were sent, and a Disconnect came before the reply.
    ctx = Ctx(connection())
    ctx.scout()
    ctx.conn.dressed = True
    ctx.told.labels_held = False
    request = ctx.helper.request

    def disconnect_meanwhile(text: str, seconds: float = 2.0) -> str:
        ctx.conn = None
        return request(text, seconds)

    ctx.helper.request = disconnect_meanwhile
    run(ticks.labels_tick(ctx))
    ctx.helper.request = request
    run(ticks.labels_tick(ctx))
    assert ctx.helper.firsts() == ["ap_labels", "ap_labels"]
    assert ctx.helper.requests[1] == "ap_labels", "taken back: none wanted now"


def test_a_reconnect_to_another_seed_takes_the_save_back() -> None:
    ctx = dressed_save(**HUNT)
    reconnect(ctx, same_seed=False)
    run(ticks.labels_tick(ctx))
    run(ticks.discs_helper_tick(ctx, no_finds()))
    assert ctx.helper.requests == ["ap_labels", discs.OFF]


# --- the new game ------------------------------------------------------------------------------


class Arming(Helper):
    """A helper that takes the new game's plan."""

    def request(self, text: str, seconds: float = 2.0) -> str:
        self.requests.append(text)
        return "armed" if text.startswith("ap_newgame") else "none"


def arming_ctx(keep_randomizer: bool = False) -> Ctx:
    conn = connection()
    conn.plan = plan()
    ctx = Ctx(conn)
    ctx.helper = Arming()
    ctx.game_conn, ctx.keep_randomizer, ctx.disarm_pending = None, keep_randomizer, False
    return ctx


def said(caplog) -> list[str]:
    return [r.getMessage() for r in caplog.records if r.name == "Client"]


def test_arming_with_archipelago_also_says_randomizers_route_and_the_seed_once(caplog) -> None:
    ctx = arming_ctx()
    with caplog.at_level(logging.INFO, logger="Client"):
        run(ticks.newgame_tick(ctx))
        assert ctx.conn.newgame is state.NewGame.ARMED
        assert ctx.helper.requests[-1].endswith("\nchoice")
        lines = said(caplog)
        assert len(lines) == 2 and "choose ARCHIPELAGO" in lines[0]
        assert "RANDOMIZER instead of ARCHIPELAGO" in lines[0]
        assert lines[1] == "0123456789ABCDEF", "the seed alone, to click and copy"
        # Armed again after a new game in the same connection: not said again.
        ctx.conn.newgame = None
        run(ticks.newgame_tick(ctx))
        assert ctx.conn.newgame is state.NewGame.ARMED and len(said(caplog)) == 2


def test_arming_for_randomizer_only_keeps_its_one_line(caplog) -> None:
    ctx = arming_ctx(keep_randomizer=True)
    with caplog.at_level(logging.INFO, logger="Client"):
        run(ticks.newgame_tick(ctx))
    assert said(caplog) == [
        "To play, load this seed's save, or start a new game: "
        f"{newgame.randomizer_message(ctx.conn.plan)}."
    ]
