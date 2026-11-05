"""client/ticks.py: what the side tasks tell the helper, against a stand-in client and helper
(no game, no AP)."""

from __future__ import annotations

import asyncio
import logging
import struct
import types
from typing import NamedTuple

import pytest
from fakes import SECRET_LIST, FakeProcess, connection, secret_plan, seed_state, slot_data

from client import (
    aplabels,
    discs,
    game,
    ids,
    native,
    newgame,
    offline,
    session,
    shields,
    state,
    ticks,
)

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
        self.seed_state = seed_state(conn)
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
    ctx.seed_state.session.holding = True  # the goal read going true in this load (Session.step)
    ctx.seed_state.deaths.receive()
    ctx.seed_state.deaths.receive()  # the whole queue
    run(ticks.deathlink_tick(ctx, in_play(goal=True)))
    assert ctx.helper.requests == []
    assert ctx.seed_state.deaths.owed == 0


# --- the offline seed file ---------------------------------------------------------------------


class Failing(Helper):
    """A helper whose every request fails with ``error``."""

    def __init__(self, error: native.HelperError) -> None:
        super().__init__()
        self.error = error

    def request(self, text: str, seconds: float = 2.0) -> str:
        self.requests.append(text)
        raise self.error


def offline_ctx(error: native.HelperError) -> Ctx:
    """Due to send the helper the offline config file, which fails with ``error``."""
    ctx = Ctx(connection())
    ctx.helper = Failing(error)
    ctx.game_conn = object()
    ctx.offline_config = (ctx.game_conn, "config")
    ctx.server_seed_name, ctx.seed_name, ctx.team = "S", None, 0
    return ctx


@pytest.mark.parametrize(
    "error", [native.ReplyLost("no reply within 2.0 s"), native.ModMismatch("another build")]
)
def test_a_lost_offline_reply_reaches_side_tick(error: native.HelperError) -> None:
    # side_tick drops the helper (or calls it mismatched) before the poll's step, which would
    # otherwise give through a closed connection and write the items with no popup.
    with pytest.raises(type(error)):
        run(ticks.offline_tick(offline_ctx(error), no_finds()))


def test_an_offline_file_the_helper_refused_keeps_the_helper(caplog) -> None:
    ctx = offline_ctx(native.HelperError("the file could not be written"))
    with caplog.at_level(logging.WARNING):
        run(ticks.offline_tick(ctx, no_finds()))
    assert ctx.helper.requests and "config" not in ctx.told.offline_sent


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
    conn.plan = secret_plan()
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
    ctx.seed_state.dressed, ctx.seed_state.discs_told = True, True
    run(ticks.labels_tick(ctx))
    run(ticks.discs_helper_tick(ctx, no_finds()))
    assert ctx.helper.firsts() == ["ap_labels", "discs"]
    assert ctx.told.discs.sent == discs.request(0, 3, told=True)
    ctx.helper.requests.clear()
    return ctx


def reconnect(ctx: Ctx, same_seed: bool) -> None:
    """A dropped connection, then Connected (zexion_client.connected): a new connection, its
    items not listed yet, with the same SeedState for the same seed and a new one for another."""
    ctx.conn.items_listed = False  # reset_server_state
    ctx.conn = state.Connection.from_slot_data(slot_data(**HUNT), 0, 1)
    if not same_seed:
        ctx.seed_state = seed_state(ctx.conn)


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
    ctx.seed_state.dressed = True
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
    ctx = Ctx(connection())
    ctx.helper = Arming()
    ctx.game_conn, ctx.keep_randomizer, ctx.disarm_pending = None, keep_randomizer, False
    return ctx


def said(caplog) -> list[str]:
    return [r.getMessage() for r in caplog.records if r.name == "Client"]


LEFT = session.Step(left_seed=True)  # the title, the file select or another save
IN_SEED = session.Step(seed_matches=True, state=game.IN_PLAY)  # this seed's save in play


def test_arming_with_archipelago_also_says_randomizers_route_and_the_seed_once(caplog) -> None:
    ctx = arming_ctx()
    with caplog.at_level(logging.INFO, logger="Client"):
        run(ticks.newgame_tick(ctx))
        assert ctx.conn.newgame is state.NewGame.ARMED
        assert ctx.helper.requests[-1].endswith("\nchoice")
        assert said(caplog) == [], "said by the poll's step, once it has read the game"
        ticks.how_to_play(ctx, LEFT)
        lines = said(caplog)
        assert len(lines) == 2 and "choose ARCHIPELAGO" in lines[0]
        assert "RANDOMIZER instead of ARCHIPELAGO" in lines[0]
        assert lines[1] == "0123456789ABCDEF", "the seed alone, to click and copy"
        # Armed again after a new game in the same connection: not said again.
        ctx.conn.newgame = None
        run(ticks.newgame_tick(ctx))
        ticks.how_to_play(ctx, LEFT)
        assert ctx.conn.newgame is state.NewGame.ARMED and len(said(caplog)) == 2


def test_arming_for_randomizer_only_keeps_its_one_line(caplog) -> None:
    ctx = arming_ctx(keep_randomizer=True)
    with caplog.at_level(logging.INFO, logger="Client"):
        run(ticks.newgame_tick(ctx))
        ticks.how_to_play(ctx, LEFT)
    assert said(caplog) == [
        "To play, load this seed's save, or start a new game: "
        f"{newgame.randomizer_message(ctx.conn.plan)}."
    ]


def test_a_seed_archipelago_cannot_start_says_randomizers_route_with_why(
    caplog, monkeypatch
) -> None:
    plan_text = newgame.plan_text

    def no_choice(plan, build="", offer_choice=True) -> str:
        if offer_choice:
            raise newgame.NewGameError("no start table for this build")
        return plan_text(plan, build, offer_choice)

    monkeypatch.setattr(newgame, "plan_text", no_choice)
    ctx = arming_ctx()
    with caplog.at_level(logging.INFO, logger="Client"):
        run(ticks.newgame_tick(ctx))
        ticks.how_to_play(ctx, LEFT)
    assert said(caplog) == [
        ticks.NO_ARCHIPELAGO,
        "To play, load this seed's save, or start a new game: "
        f"{newgame.randomizer_message(ctx.conn.plan)}.",
    ]


def test_how_to_play_waits_while_this_seeds_save_is_loaded_and_comes_on_leaving_it(
    caplog,
) -> None:
    # Connected with this seed's save in play (live53, 2026-11-04): armed on the poll after the
    # one that found it loaded, or on the first, before the game was read (an unsettled read
    # gives an empty Step).
    ctx = arming_ctx()
    with caplog.at_level(logging.INFO, logger="Client"):
        run(ticks.newgame_tick(ctx))
        for step in (session.Step(), IN_SEED, IN_SEED):
            ticks.how_to_play(ctx, step)
        assert said(caplog) == []
        # Left to the title: the player may want another slot of the seed.
        ticks.how_to_play(ctx, LEFT)
        assert len(said(caplog)) == 2
        ticks.how_to_play(ctx, LEFT)
        ticks.how_to_play(ctx, IN_SEED)
    assert len(said(caplog)) == 2, "once a connection"


def test_how_to_play_waits_for_a_helper_armed_with_the_seed() -> None:
    # Held in the seed's save, then the helper lost (drop_helper: newgame back to None).
    ctx = arming_ctx()
    run(ticks.newgame_tick(ctx))
    helper, held = ctx.helper, ctx.conn.newgame_lines
    ctx.helper, ctx.conn.newgame = None, None
    ticks.how_to_play(ctx, LEFT)
    assert ctx.conn.newgame_lines == held, "no ARCHIPELAGO to send the player to"
    ctx.helper = helper  # attached again: armed, the lines not made again (newgame_said)
    run(ticks.newgame_tick(ctx))
    assert ctx.conn.newgame is state.NewGame.ARMED and ctx.conn.newgame_lines == held
    ticks.how_to_play(ctx, LEFT)
    assert ctx.conn.newgame_lines == ()


def test_how_to_play_comes_in_another_seeds_save() -> None:
    ctx = arming_ctx()
    run(ticks.newgame_tick(ctx))
    ticks.how_to_play(ctx, session.Step(left_seed=True, other_seed=True, state=game.IN_PLAY))
    assert ctx.conn.newgame_lines == ()


# --- another seed's save ------------------------------------------------------------------------


class Telling(Arming):
    """A helper that takes the new game's plan and knows another seed's save (an AP save)."""

    def request(self, text: str, seconds: float = 2.0) -> str:
        if text == "other_save":
            self.requests.append(text)
            return "told"
        return super().request(text, seconds)


def other_save_polls(ctx: Ctx, steps: list[session.Step]) -> None:
    """What each poll does with its step: the window's lines, then the helper's notice."""
    for step in steps:
        ticks.how_to_play(ctx, step)
        if not step.other_seed and (step.left_seed or step.seed_matches):
            ctx.said.clear("other save")  # zexion_client.poll
        run(ticks.other_save_tick(ctx, step))


def test_a_new_game_of_this_seed_gets_no_other_seed_line_or_notice(caplog) -> None:
    # ARCHIPELAGO pressed: the file select and the fade read as no save loaded, then the new
    # game is in play holding the seed (the section the helper placed: test_newgame).
    ctx = arming_ctx()
    ctx.helper = Telling()
    with caplog.at_level(logging.INFO, logger="Client"):
        run(ticks.newgame_tick(ctx))
        other_save_polls(ctx, [LEFT, session.Step(), LEFT, IN_SEED, IN_SEED])
    assert "other_save" not in ctx.helper.requests
    assert state.OTHER_SAVE not in said(caplog)


def test_another_ap_seeds_save_loaded_while_armed_still_gets_the_line_and_notice(caplog) -> None:
    ctx = arming_ctx()
    ctx.helper = Telling()
    other = session.Step(left_seed=True, other_seed=True, state=game.IN_PLAY)
    with caplog.at_level(logging.INFO, logger="Client"):
        run(ticks.newgame_tick(ctx))
        assert ctx.conn.newgame is state.NewGame.ARMED, "armed, as at the title"
        other_save_polls(ctx, [LEFT, other, other, other])
    assert ctx.helper.requests.count("other_save") == 1, "asked from the second poll, once"
    assert said(caplog).count(state.OTHER_SAVE) == 1
    assert ctx.conn.newgame is state.NewGame.ARMED, "another empty slot can still start it"


# --- the game's language -----------------------------------------------------------------------


class Speaking(Helper):
    """A helper whose hello says the game shows ``lang``."""

    def __init__(self, lang: str) -> None:
        super().__init__()
        self.lang, self.hellos = lang, 0

    def hello(self) -> dict[str, str]:
        self.hellos += 1
        return {"version": "1.0.0", "lang": self.lang}


def test_another_language_is_said_once_a_game_run(caplog, monkeypatch) -> None:
    clock = [100.0]
    monkeypatch.setattr(ticks.time, "monotonic", lambda: clock[0])
    ctx = Ctx(connection())
    ctx.helper = helper = Speaking("French")
    with caplog.at_level(logging.INFO, logger="Client"):
        asyncio.run(ticks.language_tick(ctx))
        lines = said(caplog)
        assert len(lines) == 1 and lines[0].startswith("Zexion is set to French:")
        asyncio.run(ticks.language_tick(ctx))
        assert helper.hellos == 1, "asked again only after a few seconds"
        clock[0] += state.LANGUAGE_ASK_SECONDS
        helper.lang = "Japanese"
        asyncio.run(ticks.language_tick(ctx))
        assert helper.hellos == 2 and len(said(caplog)) == 1, "once a game run, whichever"
        # The next game (game_closed clears it, tests/test_zexion_client.py): told again.
        ctx.said.clear("language")
        clock[0] += state.LANGUAGE_ASK_SECONDS
        asyncio.run(ticks.language_tick(ctx))
        assert len(said(caplog)) == 2


def test_the_default_english_and_english_hd_are_not_said(caplog, monkeypatch) -> None:
    clock = [100.0]
    monkeypatch.setattr(ticks.time, "monotonic", lambda: clock[0])
    ctx = Ctx(connection())
    with caplog.at_level(logging.INFO, logger="Client"):
        for lang in ("-", "English(HD)", "?"):
            ctx.helper = Speaking(lang)
            asyncio.run(ticks.language_tick(ctx))
            clock[0] += state.LANGUAGE_ASK_SECONDS
    assert said(caplog) == []


# --- RETURN HOME WITH THE DISKS? ---------------------------------------------------------------


class Homing(Helper):
    """A helper that shows popups, queues RETURN HOME and says how that prompt stands."""

    def __init__(self, status: str = "not asked") -> None:
        super().__init__()
        self.status = status

    def request(self, text: str, seconds: float = 2.0) -> str:
        reply = super().request(text, seconds)
        if text == discs.MISSION_REQUEST:
            return "queued"
        if text == discs.MISSION_STATUS:
            return self.status
        return reply

    def apply(self, text: str) -> str:
        self.requests.append(text)
        return "applied"


def homing_ctx() -> Ctx:
    """A disks seed's save loaded with the client back, its follow-up popups all shown."""
    ctx = Ctx(connection(**HUNT))
    ctx.helper = Homing()
    ctx.told.client_back_sent = True
    ctx.stored_data = {ctx.conn.popped_key: {}}
    ctx.seed_state.session.announced = 3  # the received disks' popups handed over
    ctx.seed_state.session.announced_read = True
    return ctx


def enough_disks() -> types.SimpleNamespace:
    """A Poll: the three disks needed received."""
    return types.SimpleNamespace(codes=[ids.DATA_DISC] * 3, found=set())


def at_the_pod(
    time: int,
    ended: bool,
    deaths: int = 0,
    state: str = game.IN_PLAY,
    game_saved: int | None = None,
) -> session.Step:
    """This seed's save in the pod's cell (in play unless ``state`` says otherwise), its block
    last written at ``time`` with the pod as continue point, ``deaths`` its death count, and
    ``game_saved`` as game.Game.game_saved reads the last message."""
    stamp = struct.pack("<iHHHHHI", 3, time, 0, 0, discs.HOME[1], discs.HOME[0], deaths)
    return session.Step(
        goal=ended,
        seed_matches=True,
        state=state,
        ended=ended,
        save_stamp=stamp,
        at_home=True,
        in_home_cell=True,
        game_saved=game_saved,
    )


def prompts(ctx: Ctx) -> int:
    return ctx.helper.requests.count(discs.MISSION_REQUEST)


def test_a_save_past_its_ending_is_offered_return_home_after_a_save_at_the_pod() -> None:
    for ended in (False, True):
        ctx = homing_ctx()
        run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(100, ended)))
        assert prompts(ctx) == 0  # loaded there: the reminder alone
        assert ctx.load.return_home.reminded
        run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(200, ended)))
        assert prompts(ctx) == 1  # saved there
        assert ctx.load.replay == ended  # its YES plays the ending again


def test_a_save_at_the_pod_then_a_pause_is_offered_return_home() -> None:
    # The first look at the new stamp is in the pause menu; its GAME SAVED says it is a save.
    for ended in (False, True):
        ctx = homing_ctx()
        run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(100, ended)))
        menu = at_the_pod(200, ended, state=game.MENU, game_saved=7)
        run(ticks.mission_tick(ctx, enough_disks(), menu))
        assert prompts(ctx) == 0  # not while paused
        run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(200, ended, game_saved=7)))
        assert prompts(ctx) == 1
        assert ctx.load.replay == ended


def test_a_death_respawning_the_player_at_the_pod_is_not_offered_return_home() -> None:
    # A death writes the block's play time and death count, then the player respawns at the
    # pod, the continue point. A save there afterwards is still offered, past the ending too.
    for ended in (False, True):
        ctx = homing_ctx()
        run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(100, ended)))
        run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(200, ended, 1, game.DEAD)))
        run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(200, ended, 1)))
        assert prompts(ctx) == 0
        run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(300, ended, 1)))
        assert prompts(ctx) == 1
        assert ctx.load.replay == ended


def test_a_teleport_or_reload_from_save_to_the_pod_is_not_offered_return_home() -> None:
    # The pause menu's TELEPORT TO START leaves the slot's block as it was (time 100); RELOAD
    # FROM SAVE writes its play time behind its menu (200): the first look back in play is no
    # save.
    ctx = homing_ctx()
    run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(100, False)))
    for time in (100, 200):
        run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(time, False, state=game.MENU)))
        run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(time, False)))
    assert prompts(ctx) == 0
    run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(300, False)))
    assert prompts(ctx) == 1


def test_return_home_is_not_offered_while_an_ending_runs_in_this_load() -> None:
    ctx = homing_ctx()
    run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(100, True)))
    ctx.seed_state.session.holding = True  # the ending began (flag 250, or replay_tick)
    # The ending saves the slot as it begins, with the player still in the pod's cell.
    run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(200, True)))
    assert prompts(ctx) == 0


def test_the_ending_played_again_holds_what_is_received() -> None:
    ctx = homing_ctx()
    run(ticks.replay_tick(ctx))
    assert discs.MISSION_STATUS not in ctx.helper.requests  # no prompt on an ended save yet
    ctx.load.replay = True  # queued on a save past its ending
    for status in ("shown", "yes: ending when idle, the screen kept black"):
        ctx.helper.status = status
        run(ticks.replay_tick(ctx))
        assert not ctx.seed_state.session.holding
    ctx.helper.status = discs.ENDING_STARTED
    run(ticks.replay_tick(ctx))
    assert ctx.seed_state.session.holding
    asked = ctx.helper.requests.count(discs.MISSION_STATUS)
    run(ticks.replay_tick(ctx))
    assert ctx.helper.requests.count(discs.MISSION_STATUS) == asked  # held: not asked again


# --- received Energy Shields -------------------------------------------------------------------

PLAYER = 0x100  # the stand-in's player state
SHIELD_OBJECT, SHIELD_SLOT, STALE_LINK = 0x2000, 7, 0x2800


class ShieldWorld(game.Game):
    """A game already in play when the client attached, with a shield made as the game makes
    one: the player's link set, the object's serial, type and slot, and its +0x3C8 left as the
    slot's last object left it (docs/re/memory.md, "Received Energy Shields")."""

    def __init__(self) -> None:
        super().__init__(
            pm=FakeProcess(0x3000),
            base=0,
            rva={
                "inventory": PLAYER + game.INVENTORY_IN_PLAYER_STATE,
                "save_loaded": 0x10,
                "menu_state": 0x11,
            },
            pid=1,
            build=None,
        )
        memory = self.pm.memory
        memory[0x10], memory[0x11] = 3, 0  # slot 4 loaded, no menu
        struct.pack_into("<h", memory, PLAYER + game.ENERGY_NOW, 48)  # in play
        struct.pack_into("<Q", memory, SHIELD_OBJECT + 0x3C8, STALE_LINK)
        self.serial = 0x8DD0

    def _alive(self) -> bool:
        return True

    def settled_state(self, gap: float = 0.0) -> str:
        return self.state()

    def spawn(self) -> None:
        """The pickup's event: a new shield, or the one up refreshed (the same object)."""
        memory = self.pm.memory
        link = PLAYER + game.SHIELD_LINK
        if struct.unpack_from("<Q", memory, link)[0] == SHIELD_OBJECT:
            return
        self.serial += 1
        handle = game.HANDLE.pack(self.serial, game.SHIELD_POOL_TYPE, SHIELD_SLOT)
        memory[link : link + 8] = struct.pack("<Q", SHIELD_OBJECT)
        memory[link + 8 : link + 8 + len(handle)] = handle
        at = SHIELD_OBJECT + game.OBJECT_HANDLE
        memory[at : at + len(handle)] = handle


class ShieldHelper(Helper):
    def __init__(self, world: ShieldWorld) -> None:
        super().__init__()
        self.world = world

    def request(self, text: str, seconds: float = 2.0) -> str:
        if text == "shield":
            self.requests.append(text)
            self.world.spawn()
            return "spawned"
        return super().request(text, seconds)


def shield_ctx(world: game.Game, owed: int = 1) -> Ctx:
    ctx = Ctx(connection())
    ctx.game_conn = world
    ctx.stored_data = {ctx.conn.shield_key: 0}
    ctx.sent: list[dict] = []
    ctx.server = types.SimpleNamespace(socket=types.SimpleNamespace(open=True, closed=False))

    async def send_msgs(msgs: list[dict]) -> None:
        ctx.sent.extend(msgs)

    ctx.send_msgs = send_msgs
    ctx.received = [ids.ENERGY_SHIELD] * owed
    return ctx


def play_for(ctx: Ctx, seconds: float, monkeypatch) -> None:
    """shields_tick each half second of ``seconds`` of the stand-in clock."""
    clock = types.SimpleNamespace(now=1000.0)
    monkeypatch.setattr(ticks, "time", types.SimpleNamespace(monotonic=lambda: clock.now))

    async def ticks_for() -> None:  # one event loop: each new one opens a socket pair
        while clock.now < 1000.0 + seconds:
            await ticks.shields_tick(ctx, ctx.received)
            clock.now += 0.5

    run(ticks_for())


def counts_sent(ctx: Ctx) -> list[int]:
    return [m["operations"][0]["value"] for m in ctx.sent if m["key"] == ctx.conn.shield_key]


def test_a_shield_received_after_attaching_mid_play_is_spawned_once(monkeypatch) -> None:
    # Attached to a save already in play: a received shield that comes up is read up, not
    # spawned again.
    world = ShieldWorld()
    ctx = shield_ctx(world)
    ctx.helper = ShieldHelper(world)
    play_for(ctx, 300.0, monkeypatch)
    assert ctx.helper.requests == ["shield"]
    assert counts_sent(ctx) and set(counts_sent(ctx)) == {1}


class LostReply(ShieldHelper):
    """Spawns it, then the reply is lost with the pipe."""

    def request(self, text: str, seconds: float = 2.0) -> str:
        super().request(text, seconds)
        raise native.ReplyLost("the helper's pipe closed: 109")


def test_a_spawn_whose_reply_was_lost_is_looked_for_not_spawned_again(monkeypatch) -> None:
    world = ShieldWorld()
    ctx = shield_ctx(world)
    ctx.helper = LostReply(world)
    clock = types.SimpleNamespace(now=1000.0)
    monkeypatch.setattr(ticks, "time", types.SimpleNamespace(monotonic=lambda: clock.now))
    with pytest.raises(native.ReplyLost):  # side_tick then drops the helper
        run(ticks.shields_tick(ctx, ctx.received))
    asked = ctx.helper.requests
    ctx.helper = None
    clock.now += 0.5
    run(ticks.shields_tick(ctx, ctx.received))
    assert asked == ["shield"] and counts_sent(ctx) == [1]


class NeverSeen(ShieldHelper):
    """The game takes the request, but no shield ever reads up."""

    def request(self, text: str, seconds: float = 2.0) -> str:
        if text == "shield":
            self.requests.append(text)
            return "spawned"
        return super().request(text, seconds)


def test_a_shield_never_seen_up_stops_being_spawned(monkeypatch, caplog) -> None:
    world = ShieldWorld()
    ctx = shield_ctx(world, owed=2)
    ctx.helper = NeverSeen(world)
    with caplog.at_level(logging.WARNING):
        play_for(ctx, 3600.0, monkeypatch)
    assert ctx.helper.requests == ["shield"] * shields.MISSES
    assert counts_sent(ctx) == []
    assert sum("no more are asked for" in r.getMessage() for r in caplog.records) == 1


# Requests that never reached the helper, or that it answered with an error: nothing spawned.
UNSENT = {
    "pipe closed at the write": native.HelperError("the helper's pipe closed: 232"),
    "error reply": native.HelperError("the game is not settled"),
    "another build": native.ModMismatch("both are version 1.0.0, but from different builds"),
}


class NotSent(ShieldHelper):
    def __init__(self, world: ShieldWorld, error: Exception) -> None:
        super().__init__(world)
        self.error = error

    def request(self, text: str, seconds: float = 2.0) -> str:
        if text == "shield":
            self.requests.append(text)
            raise self.error
        return super().request(text, seconds)


def failed_rounds(ctx: Ctx, world: ShieldWorld, error: Exception, rounds: int, monkeypatch):
    """``rounds`` of: a request that fails with ``error``, the helper dropped (side_tick), the
    next ticks without it, then the helper back."""
    clock = types.SimpleNamespace(now=1000.0)
    monkeypatch.setattr(ticks, "time", types.SimpleNamespace(monotonic=lambda: clock.now))

    async def rounds_of() -> None:  # one event loop: each new one opens a socket pair
        for _ in range(rounds):
            ctx.helper = NotSent(world, error)
            with pytest.raises(type(error)):
                await ticks.shields_tick(ctx, ctx.received)
            ctx.helper = None
            for _ in range(int(shields.SEEN_SECONDS * 2) + 2):
                clock.now += 0.5
                await ticks.shields_tick(ctx, ctx.received)
            clock.now += shields.GAP_SECONDS

    run(rounds_of())


@pytest.mark.parametrize("why", list(UNSENT))
def test_a_request_that_never_reached_the_helper_counts_no_shield_up(why, monkeypatch) -> None:
    # A shield already up (the save's, or one picked up) must not be counted for a received
    # one that nothing spawned: the received one is still owed.
    world = ShieldWorld()
    world.spawn()
    ctx = shield_ctx(world)
    failed_rounds(ctx, world, UNSENT[why], 1, monkeypatch)
    assert ctx.seed_state.shields.pending is None and counts_sent(ctx) == []


@pytest.mark.parametrize("why", list(UNSENT))
def test_requests_that_never_reached_the_helper_are_not_misses(why, monkeypatch) -> None:
    world = ShieldWorld()
    ctx = shield_ctx(world)
    failed_rounds(ctx, world, UNSENT[why], shields.MISSES + 2, monkeypatch)
    assert not ctx.seed_state.shields.stopped and ctx.seed_state.shields.misses == 0
    ctx.helper = ShieldHelper(world)  # the helper back: the shield is still spawned
    play_for(ctx, 120.0, monkeypatch)  # the stand-in clock starts again at 1000
    assert ctx.helper.requests == ["shield"] and counts_sent(ctx)


def test_a_request_cancelled_on_its_way_stays_looked_for(monkeypatch) -> None:
    # Its thread may still deliver it: dropping it would ask for it twice.
    world = ShieldWorld()
    ctx = shield_ctx(world)
    ctx.helper = NotSent(world, asyncio.CancelledError())
    with pytest.raises(asyncio.CancelledError):
        run(ticks.shields_tick(ctx, ctx.received))
    assert ctx.seed_state.shields.pending is not None


def test_a_shield_seen_up_after_a_switch_to_another_seed_is_not_counted() -> None:
    # Connected to another room's seed, with the same team and slot (the same data storage key),
    # while the look ran: the count is not this seed's to store there.
    world = ShieldWorld()
    ctx = shield_ctx(world)
    world.spawn()
    old = ctx.seed_state
    old.shields.pending = shields.Pending(0.0)
    real = world.settled_state

    def switch(gap: float = 0.0) -> str:
        ctx.seed_state = seed_state(connection())
        return real(gap)

    world.settled_state = switch
    run(ticks.confirm_shield(ctx, world, old, ctx.conn.shield_key))
    assert counts_sent(ctx) == [] and old.shields.given is None
