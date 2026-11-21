"""client/ticks.py: what the side tasks tell the helper, against a stand-in client and helper
(no game, no AP)."""

from __future__ import annotations

import asyncio
import logging
import struct
import types
from typing import NamedTuple

import pytest
from fakes import (
    SECRET_LIST,
    Answering,
    FakeProcess,
    LinuxPm,
    connection,
    context,
    said,
    secret_plan,
    seed_state,
    slot_data,
    zc,
)

from client import (
    aplabels,
    deathlink,
    discs,
    game,
    ids,
    linux_helper,
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


def taking() -> Answering:
    """A helper that takes every request: it deals a death, takes labels and the disks' count
    (what the ticks branch on), and answers any other "none"."""
    return Answering(
        {
            "die": "died",
            "ap_labels": lambda text: f"labeled {text.count(chr(10)) // 2}",
            "discs": lambda text: (
                "discs off" if text.split("\n", 1)[0] == discs.OFF else "discs on"
            ),
        }
    )


def ticks_ctx(conn: state.Connection) -> zc.ZexionContext:
    """The client's context connected as ``conn``, a helper that takes every request attached."""
    ctx = context(conn, None)
    ctx.helper, ctx.told = taking(), state.HelperState()
    return ctx


def scout(ctx: zc.ZexionContext, progression: bool = False) -> None:
    """The server's answer for each location holding another player's item."""
    flags = aplabels.PROGRESSION if progression else 0
    for i in ctx.conn.foreign:
        loc = ids.LOCATION_ID_BASE + i
        ctx.locations_info[loc] = Scouted(ids.ITEM_ID_BASE + 16, loc, 2, flags)


def unlock_assists(ctx: zc.ZexionContext) -> None:
    """Assist Unlock received (from the server console, say)."""
    item = types.SimpleNamespace(item=ids.ITEM_ID_BASE + ids.ASSIST_UNLOCK, player=1)
    ctx.items_received.append(item)


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
    ctx = ticks_ctx(connection(death_link=True))
    ctx.seed_state.deaths.receive()
    run(ticks.deathlink_tick(ctx, in_play(goal=True)))
    assert ctx.helper.requests == ["die"]
    assert ctx.seed_state.deaths.owed == 0


def test_deaths_queued_when_the_ending_begins_in_this_load_are_dropped() -> None:
    ctx = ticks_ctx(connection(death_link=True))
    ctx.seed_state.session.holding = True  # the goal read going true in this load (Session.step)
    ctx.seed_state.deaths.receive()
    ctx.seed_state.deaths.receive()  # the whole queue
    run(ticks.deathlink_tick(ctx, in_play(goal=True)))
    assert ctx.helper.requests == []
    assert ctx.seed_state.deaths.owed == 0


def linux_mod(replies: dict) -> linux_helper.LinuxMod:
    """The Linux helper, configured on a connection that answers ``replies``: what it is asked from
    here on is in ``mod.pm.requests``."""
    mod = linux_helper.LinuxMod(LinuxPm(replies))
    mod.pm.requests.clear()
    return mod


def linux_death_ctx(reply) -> zc.ZexionContext:
    ctx = ticks_ctx(connection(death_link=True))
    ctx.helper = None  # the game's Linux build: no Windows helper
    ctx.linux_mod = linux_mod({"die": reply})
    ctx.seed_state.deaths.receive()
    return ctx


def deal(ctx: zc.ZexionContext, step: session.Step) -> None:
    """deathlink_tick as the poll runs it: a failed Linux helper is dropped by side_tick."""
    run(zc.side_tick(ctx, "DeathLink", lambda: ticks.deathlink_tick(ctx, step)))


def slow(clock, reply):
    """A reply that takes the socket's whole timeout of the stand-in clock: ``reply``, or raised."""

    def answer(text: str) -> str:
        clock.now += 5.0
        if isinstance(reply, Exception):
            raise reply
        return reply

    return answer


def test_on_linux_a_received_death_is_dealt_through_the_linux_helper() -> None:
    ctx = linux_death_ctx("died")
    deal(ctx, in_play(goal=False))
    assert ctx.linux_mod.pm.requests == ["die"] and ctx.seed_state.deaths.owed == 0


def test_on_linux_a_refused_death_stays_queued() -> None:
    ctx = linux_death_ctx("refused message showing")
    deal(ctx, in_play(goal=False))
    deaths = ctx.seed_state.deaths
    assert deaths.owed == 1 and deaths.asked_at is None and ctx.linux_mod is not None


@pytest.mark.parametrize(
    ("error", "maybe_dealt"),
    [
        (linux_helper.NoReply("the helper did not answer within 5 s"), True),
        (linux_helper.ConnectionLost("the helper closed its socket"), True),
        (linux_helper.LinuxHelperError("connecting to the helper again failed"), False),
    ],
)
def test_on_linux_a_failed_death_request_drops_the_linux_helper(error, maybe_dealt) -> None:
    # The Linux helper's to drop (side_tick does, by the error's own type). One that may have
    # been dealt is the death seen soon after; one never sent stays queued.
    ctx = linux_death_ctx(error)
    deal(ctx, in_play(goal=False))
    deaths = ctx.seed_state.deaths
    assert ctx.linux_mod is None and ctx.run.linux_back_by > 0
    assert deaths.owed == 1 and (deaths.asked_at is not None) is maybe_dealt


def test_on_linux_a_death_whose_reply_timed_out_is_looked_for_from_then(monkeypatch) -> None:
    # The socket's timeout outlasts DEALT_SECONDS: the window starts when the reply was lost.
    clock = types.SimpleNamespace(now=1000.0)
    monkeypatch.setattr(ticks, "time", types.SimpleNamespace(monotonic=lambda: clock.now))
    ctx = linux_death_ctx("died")
    ctx.linux_mod = linux_mod(
        {"die": slow(clock, linux_helper.NoReply("the helper did not answer within 5 s"))}
    )
    deal(ctx, in_play(goal=False))
    assert ctx.seed_state.deaths.asked_at == 1005.0


@pytest.mark.parametrize("reply_comes", [False, True])
def test_on_linux_a_dealt_death_is_not_sent_back_nor_dealt_again_after_a_slow_poll(
    monkeypatch, reply_comes
) -> None:
    # With the reply lost, the next poll reads the state only after configuring the helper again:
    # more than DEALT_SECONDS after the ask. The death seen then is the dealt one all the same.
    clock = types.SimpleNamespace(now=1000.0)
    monkeypatch.setattr(ticks, "time", types.SimpleNamespace(monotonic=lambda: clock.now))
    ctx = linux_death_ctx("died")
    reply = "died" if reply_comes else linux_helper.NoReply("the helper did not answer within 5 s")
    ctx.linux_mod = linux_mod({"die": slow(clock, reply)})
    ctx.seed_state.deaths.receive()  # two queued: one dealt, one not yet
    ctx.seed_state.deaths.observe(game.IN_PLAY, clock.now)
    deal(ctx, in_play(goal=False))
    deaths = ctx.seed_state.deaths
    assert deaths.owed == (1 if reply_comes else 2)  # a reply is counted, a lost one by its death
    # With the reply, the next poll reads at once; without, it configures the helper first.
    clock.now += 1.0 if reply_comes else 3 * deathlink.DEALT_SECONDS
    ctx.linux_mod = linux_mod({"die": "refused not settled"})  # (the next ask comes later)
    ctx.seed_state.death_retry_at = clock.now + 60
    dead = session.Step(goal=False, seed_matches=True, state=game.DEAD, ended=False)
    deal(ctx, dead)
    assert ctx.deaths_sent == [], "the dealt death is not the player's own"
    assert deaths.owed == 1, "and was counted, so it is not dealt again"


# --- the offline seed file ---------------------------------------------------------------------


def offline_ctx(error: native.HelperError) -> zc.ZexionContext:
    """Due to send the helper the offline config file, which fails with ``error``."""
    ctx = ticks_ctx(connection())
    ctx.helper = Answering(default=error)
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


@pytest.mark.parametrize("size, sent", [(offline.MAX_FILE + 1, True), (offline.MAX_REQUEST, False)])
def test_a_seed_file_too_large_to_keep_still_reaches_the_helper(monkeypatch, size, sent) -> None:
    # The running helper takes a seed's text for its own use (its own traps, the disk count)
    # before it refuses to keep the file, so the client sends one over offline.MAX_FILE all
    # the same; only a request the pipe would not take is never sent.
    ctx = offline_ctx(native.HelperError("unused"))
    ctx.helper = Answering(default="error too large")
    ctx.offline_config = (ctx.game_conn, None)  # no config file to send here
    monkeypatch.setattr(ticks, "offline_seed", lambda c, p: "x" * size)
    run(ticks.offline_tick(ctx, no_finds()))
    assert [r.startswith("offline_put seed ") for r in ctx.helper.requests] == [True] * sent
    # Taken by the helper, though not kept: not put again while it is the same text. One the
    # pipe would not take was never handed over, and is tried again.
    assert ("seed" in ctx.told.offline_sent) == sent
    ctx.told.offline_at = 0.0
    run(ticks.offline_tick(ctx, no_finds()))
    assert len(ctx.helper.requests) == sent


def test_a_seed_file_whose_write_failed_is_put_again(monkeypatch) -> None:
    ctx = offline_ctx(native.HelperError("unused"))
    ctx.helper = Answering(default="error cannot write, error 5")
    ctx.offline_config = (ctx.game_conn, None)
    monkeypatch.setattr(ticks, "offline_seed", lambda c, p: "seed text")
    for _ in range(2):
        ctx.told.offline_at = 0.0
        run(ticks.offline_tick(ctx, no_finds()))
    assert len(ctx.helper.requests) == 2 and "seed" not in ctx.told.offline_sent


def test_the_seed_file_is_looked_for_every_poll_until_there_is_one(monkeypatch) -> None:
    # The helper's look for this seed waits for its file: with no seed text yet (the server has
    # not listed the items) the next poll looks again, not five seconds later.
    ctx = offline_ctx(native.HelperError("unused"))
    ctx.helper = Answering(default="stored")
    text: list[str | None] = [None]
    monkeypatch.setattr(ticks, "offline_seed", lambda c, p: text[0])
    monkeypatch.setattr(ctx, "drop_helper", lambda: None, raising=False)
    ctx.told.offline_at = 0.0
    run(ticks.offline_tick(ctx, no_finds()))
    assert ctx.told.offline_at == 0.0, "no seed text: looked at again next poll"
    text[0] = "seed text"
    run(ticks.offline_tick(ctx, no_finds()))
    assert any(r.startswith("offline_put seed ") for r in ctx.helper.requests)
    assert ctx.told.offline_at > 0.0, "sent: the next look in a few seconds"


def test_the_offline_file_locks_assist_until_assist_unlock_is_received() -> None:
    # Before this seed's save is seen in a connection, conn.lock_assists is the slot data's:
    # the file follows Assist Unlock, received on an earlier connection, all the same.
    ctx = ticks_ctx(connection(allow_assists=False))
    scout(ctx)
    assert "assists lock" in ticks.offline_seed(ctx, no_finds()).splitlines()
    unlock_assists(ctx)
    assert ctx.conn.lock_assists
    assert "assists free" in ticks.offline_seed(ctx, no_finds()).splitlines()


def test_the_offline_file_leaves_assist_free_when_the_seed_allows_it() -> None:
    ctx = ticks_ctx(connection(allow_assists=True))
    scout(ctx)
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
    ctx = ticks_ctx(secret_connection())
    scout(ctx)
    p = ctx.conn.plan
    assert p.table != bytes(p.location_items), "the 155th moves the entries after it"
    lines = ticks.offline_seed(ctx, no_finds()).splitlines()
    assert lines[1] == "table " + p.table.hex()
    assert lines[1] == offline.seed_text(p.table, p.start, False, False, None, None).splitlines()[1]


def test_traps_look_important() -> None:
    # Location 3 holds a stand-in: this player's own trap there is a Zexion upgrade, alone;
    # another Zexion player's, for them; another game's trap a progression item.
    ctx = ticks_ctx(connection(trap_locations=[[3, ids.DISRUPTION_TRAP]], trap_looks=[[3, 24]]))
    loc = ids.LOCATION_ID_BASE + 3
    cell = ctx.conn.plan.cells[3]
    for player, game_name, code, text in (
        (1, "Zexion", ids.DISRUPTION_TRAP, aplabels.trap_label(24, ids.DISRUPTION_TRAP)),
        (2, "Zexion", ids.DISORIENT_TRAP, aplabels.trap_label(24, ids.DISORIENT_TRAP, "Bob")),
        (2, "Other", ids.DISORIENT_TRAP, aplabels.label(True, "Bob")),
    ):
        ctx.slot_info = {2: types.SimpleNamespace(game=game_name)}
        ctx.locations_info = {loc: Scouted(ids.ITEM_ID_BASE + code, loc, player, 0b100)}
        labels, own = ticks.foreign_labels(ctx)
        assert labels == {cell: text} and own == 0, (player, game_name)


def test_an_own_trap_is_the_helpers_to_set_off() -> None:
    # Found in this world: the helper sets it off at the pickup, from the seed's file, which
    # lists its cell and item. The client sends nothing for it, and records nothing.
    ctx = ticks_ctx(connection(trap_locations=[[3, ids.DISORIENT_TRAP]]))
    ctx.server = None
    ctx.helper = homing()
    ctx.told.client_back_sent = True
    ctx.stored_data = {ctx.conn.popped_key: {}}
    ctx.load.save_collected = {3}
    shown = session.Step(seed_matches=True, state=game.IN_PLAY)
    run(ticks.followups_tick(ctx, no_finds(), shown, ctx.helper, ctx.told))
    assert ctx.helper.requests == [] and not ctx.seed_state.popped_mine
    scout(ctx)
    x, y = ctx.conn.plan.cells[3]
    assert f"traps {y * 54 + x}:103" in ticks.offline_seed(ctx, no_finds()).splitlines()


def test_a_seed_with_no_own_trap_lists_none_for_the_helper() -> None:
    ctx = ticks_ctx(connection())
    scout(ctx)
    assert "traps -" in ticks.offline_seed(ctx, no_finds()).splitlines()


def test_a_lost_reply_sends_a_disks_count_again() -> None:
    # The disk at 12 picked up: the reply lost, its count is sent again (shown twice at worst).
    ctx = homing_ctx()
    ctx.server = None
    ctx.load.save_collected = {12}

    def lost(text: str) -> str:
        ctx.helper.requests.append(text)
        raise native.ReplyLost("no reply within 3.0 s")

    p = types.SimpleNamespace(codes=[ids.DATA_DISC] * 3, found=[])
    shown = session.Step(seed_matches=True, state=game.IN_PLAY)
    ctx.helper.apply = lost
    with pytest.raises(native.ReplyLost):
        run(ticks.followups_tick(ctx, p, shown, ctx.helper, ctx.told))
    assert ctx.load.disks_counted == set(), "still owed"
    del ctx.helper.apply  # the class's: applied
    run(ticks.followups_tick(ctx, p, shown, ctx.helper, ctx.told))
    again = ctx.helper.requests[1].splitlines()
    assert again == ["apply", "notice 44 OS Install Disk 4"]
    assert ctx.load.disks_counted == {12}
    run(ticks.followups_tick(ctx, p, shown, ctx.helper, ctx.told))
    assert len(ctx.helper.requests) == 2, "once it has shown"


def test_an_own_disk_picked_up_again_after_a_reload_shows_its_count_again() -> None:
    ctx = homing_ctx()
    ctx.server = None
    hunt, load = ctx.conn.hunt, ctx.load
    p = types.SimpleNamespace(codes=[ids.DATA_DISC], found=[])  # one received, handed over
    ctx.seed_state.session.announced = 1
    shown = session.Step(seed_matches=True, state=game.IN_PLAY)

    def look(collected: set[int]) -> None:
        """A poll's look at the save (zexion_client.poll), then its follow-ups."""
        load.save_collected = set(collected)
        load.disks_counted = discs.counted(load.disks_counted, collected, hunt.locations)
        run(ticks.followups_tick(ctx, p, shown, ctx.helper, ctx.told))

    look(set())
    assert notices(ctx) == []
    look({12})  # picked up
    assert notices(ctx) == ["notice 44 OS Install Disk 2"]
    look({12})
    assert len(notices(ctx)) == 1, "once a pickup"
    look(set())  # a death or a reload without saving: the disk is back in its room
    look({12})  # picked up again
    assert notices(ctx) == ["notice 44 OS Install Disk 2"] * 2, "it counts, and says so, again"


def test_a_disk_the_save_held_when_first_looked_at_shows_no_count() -> None:
    # Picked up before this load was watched (with no client, or before a reconnect): no notice.
    ctx = homing_ctx()
    ctx.server = None
    ctx.load.disks_counted = None  # the load's second look not made yet
    ctx.load.save_collected = {12}
    p = types.SimpleNamespace(codes=[], found=[])
    shown = session.Step(seed_matches=True, state=game.IN_PLAY)
    run(ticks.followups_tick(ctx, p, shown, ctx.helper, ctx.told))
    ctx.load.disks_counted = discs.counted(None, {12}, ctx.conn.hunt.locations)
    run(ticks.followups_tick(ctx, p, shown, ctx.helper, ctx.told))
    assert notices(ctx) == [] and ctx.load.disks_counted == {12}


def test_the_helpers_count_is_the_loaded_saves_own() -> None:
    # One disk received, the own one at 12 picked up: 2. A reload from before the pickup: 1.
    ctx = ticks_ctx(connection(**HUNT))
    ctx.seed_state.dressed = True
    p = types.SimpleNamespace(codes=[ids.DATA_DISC], found=[12])  # found once, in some save
    ctx.load.save_collected = {12}
    run(ticks.discs_helper_tick(ctx, p, ctx.helper, ctx.told))
    assert ctx.helper.requests[-1].startswith("discs 2 3 ")
    ctx.load.save_collected = set()
    run(ticks.discs_helper_tick(ctx, p, ctx.helper, ctx.told))
    assert ctx.helper.requests[-1].startswith("discs 1 3 "), "the found record counts no disk"


def test_the_offline_file_counts_each_saves_own_disks_from_its_map() -> None:
    # What every save shares (the start and the received), and every own disk's cell: the
    # helper adds those a save's map shows collected (native/src/offline.h), whatever the
    # loaded save or the found record hold.
    ctx = ticks_ctx(connection(**HUNT))
    scout(ctx)
    x, y = ctx.conn.plan.cells[12]
    for collected in (set(), {12}):
        ctx.load.save_collected = collected
        p = types.SimpleNamespace(codes=[ids.DATA_DISC, ids.DATA_DISC], found=[12])
        lines = ticks.offline_seed(ctx, p).splitlines()
        assert [line for line in lines if line.startswith("discs")][0].startswith("discs 2 3 ")
        assert f"unfound {y * 54 + x}" in lines


def test_a_secret_shuffle_seeds_labels_go_on_each_locations_own_cell() -> None:
    ctx = ticks_ctx(secret_connection())
    scout(ctx, progression=True)
    labels, own = ticks.foreign_labels(ctx)
    # The 155th's cell is the one at list position 100, not the list's 155th.
    assert labels == {SECRET_LIST[100]: aplabels.label(True, "Bob")} and own == 0


# --- a dropped connection and the reconnect ----------------------------------------------------


def dressed_save(**changes) -> zc.ZexionContext:
    """This seed's save loaded, its labels and disk count sent to the helper."""
    ctx = ticks_ctx(connection(**changes))
    scout(ctx)
    ctx.seed_state.dressed, ctx.seed_state.discs_told = True, True
    run(ticks.labels_tick(ctx, ctx.helper, ctx.told))
    run(ticks.discs_helper_tick(ctx, no_finds(), ctx.helper, ctx.told))
    assert ctx.helper.firsts() == ["ap_labels", "discs"]
    assert ctx.told.discs.sent == discs.request(0, 3, told=True)
    ctx.helper.requests.clear()
    return ctx


def test_a_pickup_rooms_item_shows_as_scrap_once_the_server_has_it_checked(monkeypatch) -> None:
    # A room that waits for its item's pickup is never marked collected (client/session.py):
    # once its item is checked, what is left there is shown as the game's Scrap Metal.
    ctx = dressed_save(**HUNT)
    ctx.conn.items_listed = True
    i = ctx.conn.foreign[0]
    cell = ctx.conn.plan.cells[i]
    before, _ = ticks.foreign_labels(ctx)
    monkeypatch.setattr(ids, "PICKUP_ROOMS", frozenset({cell}))
    run(ticks.labels_tick(ctx, ctx.helper, ctx.told))
    assert ctx.helper.requests == [], "not checked: the labels as they were"
    ctx.checked_locations = {ids.LOCATION_ID_BASE + i}
    labels, own = ticks.foreign_labels(ctx)
    assert labels[cell] == "@46" and {c: t for c, t in labels.items() if c != cell} == {
        c: t for c, t in before.items() if c != cell
    }
    run(ticks.labels_tick(ctx, ctx.helper, ctx.told))
    assert ctx.helper.firsts() == ["ap_labels"] and ctx.told.labels_sent == labels
    assert ctx.helper.requests[0] == aplabels.labels_request(labels)
    run(ticks.labels_tick(ctx, ctx.helper, ctx.told))
    assert len(ctx.helper.requests) == 1, "sent once a change"
    # Any other checked item keeps its label (it is marked collected instead), and so does
    # this one while the items are not listed yet (a dropped connection).
    monkeypatch.setattr(ids, "PICKUP_ROOMS", frozenset())
    assert ticks.foreign_labels(ctx)[0] == before
    monkeypatch.setattr(ids, "PICKUP_ROOMS", frozenset({cell}))
    ctx.conn.items_listed = False
    assert ticks.foreign_labels(ctx)[0] == before


def reconnect(ctx: zc.ZexionContext, same_seed: bool) -> None:
    """A dropped connection, then Connected (zexion_client.connected): a new connection, its
    items not listed yet, with the same SeedState for the same seed and a new one for another."""
    ctx.conn.items_listed = False  # reset_server_state
    ctx.conn = state.Connection.from_slot_data(slot_data(**HUNT), 0, 1)
    if not same_seed:
        ctx.seed_state = seed_state(ctx.conn)


def test_a_reconnect_to_the_same_seed_leaves_the_save_dressed() -> None:
    ctx = dressed_save(**HUNT)
    reconnect(ctx, same_seed=True)
    run(ticks.labels_tick(ctx, ctx.helper, ctx.told))
    run(
        ticks.discs_helper_tick(ctx, no_finds(), ctx.helper, ctx.told)
    )  # the items not listed yet: waits
    ctx.conn.items_listed = True
    scout(ctx)
    run(ticks.labels_tick(ctx, ctx.helper, ctx.told))
    run(ticks.discs_helper_tick(ctx, no_finds(), ctx.helper, ctx.told))
    assert "ap_labels" not in ctx.helper.firsts(), "the labels neither taken back nor sent again"
    assert "discs" not in ctx.helper.firsts(), "nor the disk count"


def test_labels_taken_while_the_connection_changed_are_taken_back() -> None:
    # The helper held none; labels were sent, and a Disconnect came before the reply.
    ctx = ticks_ctx(connection())
    scout(ctx)
    ctx.seed_state.dressed = True
    ctx.told.labels_held = False
    request = ctx.helper.request

    def disconnect_meanwhile(text: str, seconds: float = 2.0) -> str:
        ctx.conn = None
        return request(text, seconds)

    ctx.helper.request = disconnect_meanwhile
    run(ticks.labels_tick(ctx, ctx.helper, ctx.told))
    ctx.helper.request = request
    run(ticks.labels_tick(ctx, ctx.helper, ctx.told))
    assert ctx.helper.firsts() == ["ap_labels", "ap_labels"]
    assert ctx.helper.requests[1] == "ap_labels", "taken back: none wanted now"


def test_a_reconnect_to_another_seed_takes_the_save_back() -> None:
    ctx = dressed_save(**HUNT)
    reconnect(ctx, same_seed=False)
    run(ticks.labels_tick(ctx, ctx.helper, ctx.told))
    run(ticks.discs_helper_tick(ctx, no_finds(), ctx.helper, ctx.told))
    assert ctx.helper.requests == ["ap_labels", discs.OFF]


# --- the new game ------------------------------------------------------------------------------


def arming_ctx(keep_randomizer: bool = False) -> zc.ZexionContext:
    ctx = ticks_ctx(connection())
    ctx.helper = Answering({"ap_newgame": "armed"})
    ctx.game_conn, ctx.keep_randomizer, ctx.run.disarm_pending = None, keep_randomizer, False
    return ctx


LEFT = session.Step(left_seed=True)  # the title, the file select or another save
IN_SEED = session.Step(seed_matches=True, state=game.IN_PLAY)  # this seed's save in play


def test_arming_with_archipelago_also_says_randomizers_route_and_the_seed_once(caplog) -> None:
    ctx = arming_ctx()
    with caplog.at_level(logging.INFO, logger="Client"):
        run(ticks.newgame_tick(ctx, ctx.helper))
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
        run(ticks.newgame_tick(ctx, ctx.helper))
        ticks.how_to_play(ctx, LEFT)
        assert ctx.conn.newgame is state.NewGame.ARMED and len(said(caplog)) == 2


def test_arming_for_randomizer_only_keeps_its_one_line(caplog) -> None:
    ctx = arming_ctx(keep_randomizer=True)
    with caplog.at_level(logging.INFO, logger="Client"):
        run(ticks.newgame_tick(ctx, ctx.helper))
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
        run(ticks.newgame_tick(ctx, ctx.helper))
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
        run(ticks.newgame_tick(ctx, ctx.helper))
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
    run(ticks.newgame_tick(ctx, ctx.helper))
    helper, held = ctx.helper, ctx.conn.newgame_lines
    ctx.helper, ctx.conn.newgame = None, None
    ticks.how_to_play(ctx, LEFT)
    assert ctx.conn.newgame_lines == held, "no ARCHIPELAGO to send the player to"
    ctx.helper = helper  # attached again: armed, the lines not made again (newgame_said)
    run(ticks.newgame_tick(ctx, ctx.helper))
    assert ctx.conn.newgame is state.NewGame.ARMED and ctx.conn.newgame_lines == held
    ticks.how_to_play(ctx, LEFT)
    assert ctx.conn.newgame_lines == ()


def test_how_to_play_comes_in_another_seeds_save() -> None:
    ctx = arming_ctx()
    run(ticks.newgame_tick(ctx, ctx.helper))
    ticks.how_to_play(ctx, session.Step(left_seed=True, other_seed=True, state=game.IN_PLAY))
    assert ctx.conn.newgame_lines == ()


# --- another seed's save ------------------------------------------------------------------------


def other_save_polls(ctx: zc.ZexionContext, steps: list[session.Step]) -> None:
    """What each poll does with its step: the window's lines, then the helper's notice."""
    for step in steps:
        ticks.how_to_play(ctx, step)
        if not step.other_seed and (step.left_seed or step.seed_matches):
            ctx.said.clear("other save")  # zexion_client.poll
        run(ticks.other_save_tick(ctx, step, ctx.helper, ctx.told))


def test_a_new_game_of_this_seed_gets_no_other_seed_line_or_notice(caplog) -> None:
    # ARCHIPELAGO pressed: the file select and the fade read as no save loaded, then the new
    # game is in play holding the seed (the section the helper placed: test_newgame).
    ctx = arming_ctx()
    ctx.helper = Answering({"ap_newgame": "armed", "other_save": "told"})
    with caplog.at_level(logging.INFO, logger="Client"):
        run(ticks.newgame_tick(ctx, ctx.helper))
        other_save_polls(ctx, [LEFT, session.Step(), LEFT, IN_SEED, IN_SEED])
    assert "other_save" not in ctx.helper.requests
    assert state.OTHER_SAVE not in said(caplog)


def test_another_ap_seeds_save_loaded_while_armed_still_gets_the_line_and_notice(caplog) -> None:
    ctx = arming_ctx()
    ctx.helper = Answering({"ap_newgame": "armed", "other_save": "told"})
    other = session.Step(left_seed=True, other_seed=True, state=game.IN_PLAY)
    with caplog.at_level(logging.INFO, logger="Client"):
        run(ticks.newgame_tick(ctx, ctx.helper))
        assert ctx.conn.newgame is state.NewGame.ARMED, "armed, as at the title"
        other_save_polls(ctx, [LEFT, other, other, other])
    assert ctx.helper.requests.count("other_save") == 1, "asked from the second poll, once"
    assert said(caplog).count(state.OTHER_SAVE) == 1
    assert ctx.conn.newgame is state.NewGame.ARMED, "another empty slot can still start it"


# --- the game's language -----------------------------------------------------------------------


def speaking(lang: str) -> Answering:
    """A helper whose hello says the game shows ``lang`` (``hello_fields``, ``hellos`` asked)."""
    helper = Answering()
    helper.hello_fields["lang"] = lang
    return helper


def test_another_language_is_said_once_a_game_run(caplog, monkeypatch) -> None:
    clock = [100.0]
    monkeypatch.setattr(ticks.time, "monotonic", lambda: clock[0])
    ctx = ticks_ctx(connection())
    ctx.helper = helper = speaking("French")
    with caplog.at_level(logging.INFO, logger="Client"):
        asyncio.run(ticks.language_tick(ctx, ticks.helper_link(ctx)))
        lines = said(caplog)
        assert len(lines) == 1 and lines[0].startswith("Zexion is set to French:")
        asyncio.run(ticks.language_tick(ctx, ticks.helper_link(ctx)))
        assert helper.hellos == 1, "asked again only after a few seconds"
        clock[0] += state.LANGUAGE_ASK_SECONDS
        helper.hello_fields["lang"] = "Japanese"
        asyncio.run(ticks.language_tick(ctx, ticks.helper_link(ctx)))
        assert helper.hellos == 2 and len(said(caplog)) == 1, "once a game run, whichever"
        # The next game (game_closed clears it, tests/test_zexion_client.py): told again.
        ctx.said.clear("language")
        clock[0] += state.LANGUAGE_ASK_SECONDS
        asyncio.run(ticks.language_tick(ctx, ticks.helper_link(ctx)))
        assert len(said(caplog)) == 2


def test_the_default_english_and_english_hd_are_not_said(caplog, monkeypatch) -> None:
    clock = [100.0]
    monkeypatch.setattr(ticks.time, "monotonic", lambda: clock[0])
    ctx = ticks_ctx(connection())
    with caplog.at_level(logging.INFO, logger="Client"):
        for lang in ("-", "English(HD)", "?"):
            ctx.helper = speaking(lang)
            asyncio.run(ticks.language_tick(ctx, ticks.helper_link(ctx)))
            clock[0] += state.LANGUAGE_ASK_SECONDS
    assert said(caplog) == []


# --- RETURN HOME WITH THE DISKS? ---------------------------------------------------------------


def homing() -> Answering:
    """A helper that shows popups, queues RETURN HOME and says how that prompt stands (its
    ``replies["mission_status"]``)."""
    return Answering({"mission_prompt": "queued", "mission_status": "not asked"})


def homing_ctx() -> zc.ZexionContext:
    """A disks seed's save loaded with the client back, its follow-up popups all shown."""
    ctx = ticks_ctx(connection(**HUNT))
    ctx.helper = homing()
    ctx.told.client_back_sent = True
    ctx.stored_data = {ctx.conn.popped_key: {}}
    ctx.seed_state.session.announced = 3  # the received disks' popups handed over
    ctx.seed_state.session.announced_read = True
    ctx.load.disks_counted = set()  # the load looked at twice: no own disk in the save
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


def prompts(ctx: zc.ZexionContext) -> int:
    return ctx.helper.requests.count(discs.MISSION_REQUEST)


def notices(ctx: zc.ZexionContext) -> list[str]:
    """The notice lines of every batch applied."""
    return ctx.helper.notices()


@pytest.mark.parametrize("handed", [0, 1])
def test_a_found_disks_count_leaves_out_a_received_disk_not_handed_over_yet(handed) -> None:
    # A received disk waits (an item owed with it) while the player picks up the disk at
    # location 12: the found one shows 1, and the received one, handed over after it, 2
    # (discs.received_notes). Handed over first (this poll's step), the found one shows 2.
    ctx = homing_ctx()
    ctx.server = None  # the record of what was shown is kept for a later poll
    current = ctx.seed_state.session
    current.announced = handed
    ctx.load.save_collected = {12}
    p = types.SimpleNamespace(codes=[ids.DATA_DISC], found=[12])
    shown = session.Step(seed_matches=True, state=game.IN_PLAY)
    current.announced_read = False  # a new connection, its popup count not read yet
    run(ticks.followups_tick(ctx, p, shown, ctx.helper, ctx.told))
    assert notices(ctx) == [], "a found disk's count waits for the popup count"
    current.announced_read = True
    run(ticks.followups_tick(ctx, p, shown, ctx.helper, ctx.told))
    assert notices(ctx) == [f"notice 44 OS Install Disk {1 + handed}"]
    if not handed:  # the received one's count, handed over on a later poll
        load, hunt = ctx.load, ctx.conn.hunt
        pending = discs.uncounted(load.disks_counted, load.save_collected, hunt.locations)
        later = discs.received_notes(hunt, p.codes, load.save_collected, False, pending)
        assert later == {0: "OS Install Disk 2"}


def test_a_save_past_its_ending_is_offered_return_home_after_a_save_at_the_pod() -> None:
    for ended in (False, True):
        ctx = homing_ctx()
        run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(100, ended), ctx.helper, ctx.told))
        assert prompts(ctx) == 0  # loaded there: the reminder alone
        assert ctx.load.return_home.reminded
        run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(200, ended), ctx.helper, ctx.told))
        assert prompts(ctx) == 1  # saved there
        assert ctx.load.replay == ended  # its YES plays the ending again


def test_a_save_at_the_pod_then_a_pause_is_offered_return_home() -> None:
    # The first look at the new stamp is in the pause menu; its GAME SAVED says it is a save.
    for ended in (False, True):
        ctx = homing_ctx()
        run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(100, ended), ctx.helper, ctx.told))
        menu = at_the_pod(200, ended, state=game.MENU, game_saved=7)
        run(ticks.mission_tick(ctx, enough_disks(), menu, ctx.helper, ctx.told))
        assert prompts(ctx) == 0  # not while paused
        run(
            ticks.mission_tick(
                ctx, enough_disks(), at_the_pod(200, ended, game_saved=7), ctx.helper, ctx.told
            )
        )
        assert prompts(ctx) == 1
        assert ctx.load.replay == ended


def test_a_death_respawning_the_player_at_the_pod_is_not_offered_return_home() -> None:
    # A death writes the block's play time and death count, then the player respawns at the
    # pod, the continue point. A save there afterwards is still offered, past the ending too.
    for ended in (False, True):
        ctx = homing_ctx()
        run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(100, ended), ctx.helper, ctx.told))
        run(
            ticks.mission_tick(
                ctx, enough_disks(), at_the_pod(200, ended, 1, game.DEAD), ctx.helper, ctx.told
            )
        )
        run(
            ticks.mission_tick(ctx, enough_disks(), at_the_pod(200, ended, 1), ctx.helper, ctx.told)
        )
        assert prompts(ctx) == 0
        run(
            ticks.mission_tick(ctx, enough_disks(), at_the_pod(300, ended, 1), ctx.helper, ctx.told)
        )
        assert prompts(ctx) == 1
        assert ctx.load.replay == ended


def test_a_teleport_or_reload_from_save_to_the_pod_is_not_offered_return_home() -> None:
    # The pause menu's TELEPORT TO START leaves the slot's block as it was (time 100); RELOAD
    # FROM SAVE writes its play time behind its menu (200): the first look back in play is no
    # save.
    ctx = homing_ctx()
    run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(100, False), ctx.helper, ctx.told))
    for time in (100, 200):
        run(
            ticks.mission_tick(
                ctx, enough_disks(), at_the_pod(time, False, state=game.MENU), ctx.helper, ctx.told
            )
        )
        run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(time, False), ctx.helper, ctx.told))
    assert prompts(ctx) == 0
    run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(300, False), ctx.helper, ctx.told))
    assert prompts(ctx) == 1


def test_return_home_is_not_offered_while_an_ending_runs_in_this_load() -> None:
    ctx = homing_ctx()
    run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(100, True), ctx.helper, ctx.told))
    ctx.seed_state.session.holding = True  # the ending began (flag 250, or replay_tick)
    # The ending saves the slot as it begins, with the player still in the pod's cell.
    run(ticks.mission_tick(ctx, enough_disks(), at_the_pod(200, True), ctx.helper, ctx.told))
    assert prompts(ctx) == 0


def test_the_ending_played_again_holds_what_is_received() -> None:
    ctx = homing_ctx()
    run(ticks.replay_tick(ctx, ctx.helper))
    assert discs.MISSION_STATUS not in ctx.helper.requests  # no prompt on an ended save yet
    ctx.load.replay = True  # queued on a save past its ending
    for status in ("shown", "yes: ending when idle, the screen kept black"):
        ctx.helper.replies["mission_status"] = status
        run(ticks.replay_tick(ctx, ctx.helper))
        assert not ctx.seed_state.session.holding
    ctx.helper.replies["mission_status"] = discs.ENDING_STARTED
    run(ticks.replay_tick(ctx, ctx.helper))
    assert ctx.seed_state.session.holding
    asked = ctx.helper.requests.count(discs.MISSION_STATUS)
    run(ticks.replay_tick(ctx, ctx.helper))
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


def spawning(world: ShieldWorld, error: Exception | None = None):
    """The reply to a ``shield`` request: the world spawns one, and the reply is ``spawned``, or
    ``error`` is raised after the spawn (a reply lost with the pipe)."""

    def answer(text: str) -> str:
        world.spawn()
        if error is not None:
            raise error
        return "spawned"

    return answer


def shield_helper(world: ShieldWorld) -> Answering:
    return Answering({"shield": spawning(world)})


def shield_ctx(world: game.Game, owed: int = 1) -> zc.ZexionContext:
    ctx = ticks_ctx(connection())
    ctx.game_conn = world
    ctx.stored_data = {ctx.conn.shield_key: 0}
    ctx.sent: list[dict] = []
    ctx.server = types.SimpleNamespace(socket=types.SimpleNamespace(open=True, closed=False))

    async def send_msgs(msgs: list[dict]) -> None:
        ctx.sent.extend(msgs)

    ctx.send_msgs = send_msgs
    ctx.received = [ids.ENERGY_SHIELD] * owed
    return ctx


def play_for(ctx: zc.ZexionContext, seconds: float, monkeypatch) -> None:
    """shields_tick each half second of ``seconds`` of the stand-in clock."""
    clock = types.SimpleNamespace(now=1000.0)
    monkeypatch.setattr(ticks, "time", types.SimpleNamespace(monotonic=lambda: clock.now))

    async def ticks_for() -> None:  # one event loop: each new one opens a socket pair
        while clock.now < 1000.0 + seconds:
            await ticks.shields_tick(ctx, ctx.received)
            clock.now += 0.5

    run(ticks_for())


def counts_sent(ctx: zc.ZexionContext) -> list[int]:
    return [m["operations"][0]["value"] for m in ctx.sent if m["key"] == ctx.conn.shield_key]


def test_a_shield_received_after_attaching_mid_play_is_spawned_once(monkeypatch) -> None:
    # Attached to a save already in play: a received shield that comes up is read up, not
    # spawned again.
    world = ShieldWorld()
    ctx = shield_ctx(world)
    ctx.helper = shield_helper(world)
    play_for(ctx, 300.0, monkeypatch)
    assert ctx.helper.requests == ["shield"]
    assert counts_sent(ctx) and set(counts_sent(ctx)) == {1}


def test_a_spawn_whose_reply_was_lost_is_looked_for_not_spawned_again(monkeypatch) -> None:
    world = ShieldWorld()
    ctx = shield_ctx(world)
    ctx.helper = Answering(
        {"shield": spawning(world, native.ReplyLost("the helper's pipe closed: 109"))}
    )
    clock = types.SimpleNamespace(now=1000.0)
    monkeypatch.setattr(ticks, "time", types.SimpleNamespace(monotonic=lambda: clock.now))
    with pytest.raises(native.ReplyLost):  # side_tick then drops the helper
        run(ticks.shields_tick(ctx, ctx.received))
    asked = ctx.helper.requests
    ctx.helper = None
    clock.now += 0.5
    run(ticks.shields_tick(ctx, ctx.received))
    assert asked == ["shield"] and counts_sent(ctx) == [1]


def test_a_shield_never_seen_up_stops_being_spawned(monkeypatch, caplog) -> None:
    world = ShieldWorld()
    ctx = shield_ctx(world, owed=2)
    ctx.helper = Answering({"shield": "spawned"})
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


def failed_rounds(
    ctx: zc.ZexionContext, world: ShieldWorld, error: Exception, rounds: int, monkeypatch
):
    """``rounds`` of: a request that fails with ``error``, the helper dropped (side_tick), the
    next ticks without it, then the helper back."""
    clock = types.SimpleNamespace(now=1000.0)
    monkeypatch.setattr(ticks, "time", types.SimpleNamespace(monotonic=lambda: clock.now))

    async def rounds_of() -> None:  # one event loop: each new one opens a socket pair
        for _ in range(rounds):
            ctx.helper = Answering({"shield": error})
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
    ctx.helper = shield_helper(world)  # the helper back: the shield is still spawned
    play_for(ctx, 120.0, monkeypatch)  # the stand-in clock starts again at 1000
    assert ctx.helper.requests == ["shield"] and counts_sent(ctx)


def test_on_linux_a_received_shield_is_spawned_through_the_linux_helper(monkeypatch) -> None:
    world = ShieldWorld()
    ctx = shield_ctx(world)
    ctx.helper = None  # the game's Linux build: no Windows helper
    ctx.linux_mod = linux_mod({"shield": spawning(world)})
    play_for(ctx, 300.0, monkeypatch)
    assert ctx.linux_mod.pm.requests == ["shield"]
    assert counts_sent(ctx) and set(counts_sent(ctx)) == {1}


@pytest.mark.parametrize(
    ("error", "looked_for"),
    [
        (linux_helper.NoReply("the helper did not answer within 2 s"), True),  # may have spawned
        (linux_helper.ConnectionLost("the helper closed its socket"), True),
        (linux_helper.LinuxHelperError("the helper's socket failed: 32"), False),  # never sent
    ],
)
def test_on_linux_a_failed_shield_request_drops_the_linux_helper(error, looked_for) -> None:
    world = ShieldWorld()
    ctx = shield_ctx(world)
    ctx.helper = None
    ctx.linux_mod = linux_mod({"shield": error})
    # The Linux helper's to drop (side_tick does, by the error's own type); a reply lost is looked
    # for, not spawned again.
    run(zc.side_tick(ctx, "Energy Shields", lambda: ticks.shields_tick(ctx, ctx.received)))
    assert ctx.linux_mod is None and ctx.run.linux_back_by > 0  # held back, configured again later
    assert (ctx.seed_state.shields.pending is not None) is looked_for


def test_a_request_canceled_on_its_way_stays_looked_for(monkeypatch) -> None:
    # Its thread may still deliver it: dropping it would ask for it twice.
    world = ShieldWorld()
    ctx = shield_ctx(world)
    ctx.helper = Answering({"shield": asyncio.CancelledError()})
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


def stamp(deaths: int) -> bytes:
    """A save stamp with a death count (discs.deaths)."""
    return bytes(discs.STAMP_DEATHS_AT) + struct.pack("<I", deaths)


def in_play_at(deaths: int) -> session.Step:
    return session.Step(
        goal=False, seed_matches=True, state=game.IN_PLAY, ended=False, save_stamp=stamp(deaths)
    )


@pytest.mark.parametrize("respawned", [True, False])
def test_on_linux_a_death_dealt_with_the_reply_lost_is_never_dealt_twice(
    monkeypatch, respawned
) -> None:
    # The helper dealt it, the reply was lost, and the next poll's first read: the player in
    # play again (respawned, the save's death count up), or not yet dead (count the same).
    clock = types.SimpleNamespace(now=1000.0)
    monkeypatch.setattr(ticks, "time", types.SimpleNamespace(monotonic=lambda: clock.now))
    ctx = linux_death_ctx(linux_helper.NoReply("the helper did not answer within 5 s"))
    first = ctx.linux_mod
    ctx.seed_state.deaths.receive()  # two queued
    ctx.seed_state.deaths.observe(game.IN_PLAY, clock.now)
    deal(ctx, in_play_at(3))
    deaths = ctx.seed_state.deaths
    assert first.pm.requests == ["die"] and deaths.watching and ctx.linux_mod is None
    ctx.linux_mod = linux_mod({"die": "died"})  # configured again, a slow poll
    clock.now += 20.0
    deal(ctx, in_play_at(4 if respawned else 3))
    assert ctx.deaths_sent == [], "the dealt death is not the player's own"
    if respawned:
        # Counted as the one dealt: the second queued is asked for now, once (not the first again).
        assert ctx.linux_mod.pm.requests == ["die"] and deaths.owed == 0 and not deaths.watching
    else:
        assert ctx.linux_mod.pm.requests == [], "not asked again in the tick that read the state"
        clock.now += 2.0
        deal(ctx, in_play_at(3))
        assert ctx.linux_mod.pm.requests == [], "still within the window"
        clock.now += deathlink.DEALT_SECONDS
        deal(ctx, in_play_at(3))
        assert ctx.linux_mod.pm.requests == ["die"] and deaths.owed == 1  # none came: asked again
