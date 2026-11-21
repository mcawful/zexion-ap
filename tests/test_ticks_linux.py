"""The ticks and the poll with the game's Linux build: its helper configured over the connection
(linux_helper.LinuxMod), the poll's ticks through it (ticks.helper_link, zexion_client.link_ticks),
the step's grants through it, and it lost, let go, kept from going silent, and handed the seed's
file. Against stand-ins: the helper's socket is tests/fakes.LinuxPm."""

from __future__ import annotations

import asyncio
import logging
import os
import time
import types

import pytest
from fakes import LinuxPm, Running, Showing, connection, context, said, stepping, zc

from client import (
    discs,
    game,
    linux_helper,
    native,
    newgame,
    offline,
    patterns,
    session,
    state,
    ticks,
)


async def nothing(*args) -> None:
    return None


def linux_ctx(
    replies: dict | None = None, *, configured: bool = False, dropped: bool = False, **seed
) -> zc.ZexionContext:
    """A context on the game's Linux build, its helper's connection answering ``replies`` (and
    ``config`` "ok"), the Linux helper configured on it if ``configured``. ``seed``: the slot
    data's changes."""
    g = Running(game.NO_SAVE)
    g.platform = "linux"
    g.build = patterns.KNOWN_LINUX_BUILDS[0]
    g.pm = LinuxPm(replies)
    ctx = context(connection(**seed), g, dropped=dropped)
    if configured:
        ctx.linux_mod = linux_helper.LinuxMod(g.pm)
    return ctx


def poll(ctx: zc.ZexionContext) -> None:
    asyncio.run(zc.poll(ctx))


def via_link(ctx: zc.ZexionContext, tick) -> None:
    """``tick(link)`` as the poll runs a tick (zexion_client.link_ticks): through the Linux helper
    if it is configured on its connection, else not at all; a failure drops the helper."""
    asyncio.run(zc.link_ticks(ctx, (("test", tick),)))


def arm(ctx: zc.ZexionContext) -> None:
    """What the poll's first ticks do: configure the Linux helper (if need be), then arm the seed
    through it."""

    async def both() -> None:
        await zc.side_tick(ctx, "Linux helper", lambda: ticks.configure_linux(ctx))
        await zc.link_ticks(ctx, (("new game", lambda link: ticks.newgame_link_tick(ctx, link)),))

    asyncio.run(both())


def configs(ctx: zc.ZexionContext) -> int:
    return sum(t.startswith("config ") for t in ctx.game_conn.pm.sent)


# --- the Linux helper as the one helper link ------------------------------------------------------


@pytest.mark.parametrize(
    ("windows", "mod", "connection_now", "connected", "link"),
    [
        (True, False, 1, True, "windows"),  # the Windows helper, whatever the Linux one is
        (True, True, 2, True, "windows"),
        (False, True, 1, True, "linux"),  # configured on this connection
        (False, True, 2, True, None),  # a connection made again since: not configured on it
        (False, True, 1, False, None),  # closed, not made again yet: as good as gone
        (False, False, 1, True, None),  # none attached, or lost
    ],
)
def test_a_tick_has_the_windows_helper_or_the_linux_one_only_on_its_configured_connection(
    windows, mod, connection_now, connected, link
) -> None:
    ctx = linux_ctx(configured=mod)
    pm = ctx.game_conn.pm
    pm.connection, pm.connected = connection_now, connected
    if windows:
        ctx.helper, ctx.told = object(), state.HelperState()
    found = ticks.helper_link(ctx)
    assert (None if found is None else "linux" if found.linux else "windows") == link
    if link == "linux":
        assert found.helper is ctx.linux_mod and found.told is ctx.linux_mod.told
    if link == "windows":
        assert found.helper is ctx.helper and found.told is ctx.told


def test_a_connection_made_again_mid_poll_leaves_the_ticks_after_it_for_the_next_poll(
    monkeypatch,
) -> None:
    # One rule for every tick: a request on a connection made again since the configuration (a
    # read in the poll does it) reaches a helper with no client on it. The labels, the replay, the
    # seed's file and the language went there once; now only the poll's first tick, which
    # configures again, may.
    ctx = linux_ctx({"ap_newgame": "armed"}, configured=True)
    pm = ctx.game_conn.pm
    monkeypatch.setattr(zc, "attach_helper", nothing)
    monkeypatch.setattr(zc, "hints_tick", nothing)
    monkeypatch.setattr(zc, "announced_tick", nothing)
    monkeypatch.setattr(zc, "offline_tick", nothing)

    async def reconnected(*args) -> None:
        pm.connection += 1  # as a read of the game that made the connection again

    monkeypatch.setattr(zc, "labels_tick", reconnected)
    stepping(monkeypatch, ctx, session.Step())  # the game stays attached
    poll(ctx)
    assert pm.firsts()[1:] == ["ap_newgame"], "the new game, then the labels' tick closed the way"
    poll(ctx)
    assert pm.firsts()[2] == "config", "configured on the connection made again, before any tick"


# --- configuring it, and the new game ------------------------------------------------------------


def test_on_linux_the_seed_is_armed_with_archipelago_through_the_linux_helper(caplog) -> None:
    ctx = linux_ctx({"ap_newgame": "armed"})
    with caplog.at_level("INFO"):
        arm(ctx)
    sent = ctx.game_conn.pm.sent
    assert sent[0].startswith("config map_grid=0x9fc83d cell_state=0x9fd3a1 ")
    assert "rando_blob=0x12f6e00" in sent[0] and "current_slot=0x14945a0" in sent[0]
    assert sent[1].startswith("ap_newgame\n") and sent[1].endswith("\nchoice")  # ARCHIPELAGO
    assert ctx.conn.newgame is state.NewGame.ARMED
    assert ctx.conn.newgame_lines == newgame.choice_message(ctx.conn.plan)
    arm(ctx)
    assert configs(ctx) == 1  # configured once an attach


def test_a_linux_build_takes_the_start_table_of_its_windows_build() -> None:
    linux = types.SimpleNamespace(build=patterns.KNOWN_LINUX_BUILDS[0])
    windows = types.SimpleNamespace(build=patterns.KNOWN_BUILDS[0])
    assert ticks.start_table_build(linux) == patterns.KNOWN_BUILDS[0].sha256
    assert ticks.start_table_build(windows) == patterns.KNOWN_BUILDS[0].sha256
    assert ticks.start_table_build(None) == ""


def test_on_linux_a_helper_that_cant_be_reached_is_told_once_and_armed_again(caplog) -> None:
    lost = linux_helper.LinuxHelperError("the helper closed its socket")
    ctx = linux_ctx({"ap_newgame": lost})
    with caplog.at_level("WARNING"):
        arm(ctx)
        arm(ctx)
    lines = [line for line in said(caplog) if "on Linux" in line]
    assert len(lines) == 1
    assert ctx.linux_mod is None and ctx.conn.newgame is None
    ctx.game_conn.pm.replies["ap_newgame"] = "armed"
    arm(ctx)
    assert ctx.conn.newgame is state.NewGame.ARMED


def test_the_linux_helper_takes_an_apply_as_the_windows_helper_does() -> None:
    pm = LinuxPm({"apply": "applied"})
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
    ctx = linux_ctx({"ap_newgame": "armed"})
    arm(ctx)
    assert ctx.conn.newgame is state.NewGame.ARMED and ctx.linux_mod is not None
    with caplog.at_level("WARNING"):
        ticks.linux_mod_lost(ctx, native.ReplyLost("the Linux helper: no reply"))
    assert ctx.linux_mod is None and ctx.conn.newgame is None  # armed again with it
    arm(ctx)
    assert configs(ctx) == 2
    assert ctx.conn.newgame is state.NewGame.ARMED


# --- the poll's step: through the Linux helper, held back, or written directly -------------------


def with_the_step_alone(monkeypatch, ctx: zc.ZexionContext) -> None:
    """The poll's ticks, the new game's stubbed: what the Linux helper's holds depend on."""
    for name in (
        "labels_tick",
        "linux_seed_tick",
        "language_tick",
        "discs_helper_tick",
        "toggle_tick",
        "replay_tick",
        "client_back_tick",
        "other_save_tick",
        "offline_tick",
        "announced_tick",
        "hints_tick",
        "attach_helper",
    ):
        monkeypatch.setattr(zc, name, nothing)


def linux_steps(monkeypatch, fails: int) -> tuple[zc.ZexionContext, list]:
    """A poll's step on the game's Linux build, the Linux helper answering every configuration:
    the first ``fails`` steps given through it fail (a reply lost); ``seen`` holds the ``helper``
    and ``give`` each step was handed."""
    ctx = linux_ctx({"ap_newgame": "armed"})
    seen: list = []
    with_the_step_alone(monkeypatch, ctx)

    def step(g, received, helper=None, give=True, **kwargs):
        seen.append((helper, give))
        lost = helper is not None and sum(h is not None for h, _ in seen) <= fails
        error = linux_helper.NoReply("the helper did not answer within 5 s") if lost else None
        return session.Step(state=game.IN_PLAY, helper_error=error)

    monkeypatch.setattr(ctx.seed_state.session, "step", step)
    return ctx, seen


def test_with_the_linux_build_the_step_goes_on_without_the_helper(monkeypatch, caplog) -> None:
    """The game's Linux build has no Windows helper: the step is not held for one that won't
    come, gives what is owed (through the Linux helper when it is there), and the player is told
    nothing of the Windows one."""
    ctx = linux_ctx({"config": "refused no frame"})
    monkeypatch.setattr(zc, "attach_helper", ticks.attach_helper)  # the real one: Windows only
    for name in ("offline_tick", "announced_tick", "hints_tick"):
        monkeypatch.setattr(zc, name, nothing)
    steps = stepping(monkeypatch, ctx)
    with caplog.at_level("WARNING"):
        poll(ctx)
        ctx.game_conn = Running(game.NO_SAVE)  # (the stand-in step said the game closed)
        ctx.game_conn.platform = "linux"
        ctx.game_conn.pm = LinuxPm({"config": "refused no frame"})
        poll(ctx)
    assert steps and steps[0]["helper"] is None and steps[0]["give"]
    assert not [line for line in said(caplog) if "Zexion mod" in line]


def test_on_linux_a_mod_configured_afresh_after_failed_steps_gives_the_step(monkeypatch) -> None:
    # Two failed steps, each dropping the mod and configured again at the next poll: the third
    # step gives through the mod configured afresh (its traps and popups), not directly.
    ctx, seen = linux_steps(monkeypatch, fails=2)
    for _ in range(3):
        poll(ctx)
    assert [h is not None and give for h, give in seen] == [True, True, True]
    assert all(isinstance(h, linux_helper.LinuxMod) for h, _ in seen)
    assert len({id(h) for h, _ in seen}) == 3, "each configured afresh"
    assert configs(ctx) == 3
    assert ctx.run.linux_failed == 0 and ctx.linux_mod is not None, "the third step went through"


def test_on_linux_steps_that_keep_failing_are_written_directly_not_held_back(monkeypatch) -> None:
    # A request the helper keeps refusing: after LINUX_DIRECT_AFTER failed steps with mods
    # configured in turn the next step's items are written directly, once, then the helper again.
    ctx, seen = linux_steps(monkeypatch, fails=state.LINUX_DIRECT_AFTER)
    for _ in range(state.LINUX_DIRECT_AFTER + 2):
        poll(ctx)
    kinds = [h is not None for h, _ in seen]
    assert kinds == [True] * state.LINUX_DIRECT_AFTER + [False, True]
    assert all(give for _, give in seen)


def test_on_linux_no_mod_to_configure_is_written_directly_after_the_hold(monkeypatch) -> None:
    ctx, seen = linux_steps(monkeypatch, fails=0)
    ctx.game_conn.pm.replies["config"] = "refused no frame"
    ctx.run.linux_back_by = time.monotonic() + ticks.HELPER_RETRY_SECONDS  # as the attach holds
    poll(ctx)
    assert seen[-1] == (None, False), "held while it is configured, as after any lost one"
    ctx.run.linux_back_by = time.monotonic() - 1.0  # the hold over
    poll(ctx)
    assert seen[-1] == (None, True) and ctx.linux_mod is None


def test_on_linux_two_failed_steps_then_no_mod_to_configure_hold_then_write_directly(
    monkeypatch,
) -> None:
    ctx, seen = linux_steps(monkeypatch, fails=2)
    for _ in range(2):
        poll(ctx)
    assert ctx.run.linux_failed == 2 and ctx.linux_mod is None
    ctx.game_conn.pm.replies["config"] = "refused no frame"  # the reconfiguration fails too
    poll(ctx)
    assert seen[-1] == (None, False), "held: the mod was lost, not written to directly at once"
    assert ctx.run.linux_failed == 2, "a held step is no step that went through"
    ctx.run.linux_back_by = time.monotonic() - 1.0  # the hold over
    poll(ctx)
    assert seen[-1] == (None, True) and ctx.linux_mod is None
    assert ctx.run.linux_failed == 0, "the direct step ended the failures in a row"


def test_on_linux_an_attach_holds_the_step_until_the_linux_helper_is_configured(
    monkeypatch,
) -> None:
    # The Linux helper is configured from the next poll (configure_linux): the attach's own step
    # gives nothing, so no item goes without its popup and no waiting trap is dropped.
    ctx = linux_ctx({"config": "refused no frame"})
    linux = ctx.game_conn
    ctx.game_conn = None
    monkeypatch.setattr(game.Game, "attach", classmethod(lambda cls: linux))
    steps = stepping(monkeypatch, ctx)
    poll(ctx)
    assert steps and not steps[0]["give"]
    # (The stand-in step said the game closed, which dropped it.) Attached again: held, until a
    # configuration takes.
    ctx.game_conn = linux
    ctx.run.linux_back_by = time.monotonic() + ticks.HELPER_RETRY_SECONDS
    arm(ctx)  # no frame yet: still held
    assert ctx.linux_mod is None and ctx.run.linux_back_by > time.monotonic()
    linux.pm.replies["config"] = "ok"
    arm(ctx)
    assert ctx.linux_mod is not None and ctx.run.linux_back_by == 0.0


def test_a_linux_helper_lost_once_configured_holds_the_step_a_while() -> None:
    ctx = linux_ctx({"ap_newgame": "armed"})
    ticks.linux_mod_lost(ctx, linux_helper.LinuxHelperError("never configured"))
    assert ctx.run.linux_back_by == 0.0, "nothing to wait for: written directly"
    arm(ctx)
    before = time.monotonic()
    ticks.linux_mod_lost(ctx, linux_helper.NoReply("no reply"))
    assert before + ticks.HELPER_RETRY_SECONDS <= ctx.run.linux_back_by
    ticks.linux_mod_lost(ctx, linux_helper.NoReply("no reply"))  # still lost: not held longer
    assert ctx.run.linux_back_by <= time.monotonic() + ticks.HELPER_RETRY_SECONDS
    arm(ctx)
    assert ctx.linux_mod is not None and ctx.run.linux_back_by == 0.0


def test_a_linux_helper_connected_again_is_configured_and_armed_again() -> None:
    # The helper drops a client's plan and labels when its connection closes (a request with no
    # reply closes it; the next read connects again): told both again.
    ctx = linux_ctx({"ap_newgame": "armed"})
    arm(ctx)
    first = ctx.linux_mod
    ctx.linux_mod.told.labels_for = ctx.seed_state  # labels sent on the old connection
    ctx.game_conn.pm.connected = False  # closed, not made again yet: as good as gone
    assert not ctx.linux_mod.current()
    ctx.game_conn.pm.connected = True
    ctx.game_conn.pm.connection += 1
    arm(ctx)
    sent = ctx.game_conn.pm.sent
    assert configs(ctx) == 2
    assert sum(t.startswith("ap_newgame") for t in sent) == 2
    assert ctx.linux_mod is not first and ctx.linux_mod.told.labels_for is None
    assert ctx.conn.newgame is state.NewGame.ARMED


def test_on_linux_a_connection_made_again_takes_the_saves_disks_for_counted() -> None:
    # The helper had no client between the two connections, so it counted an own disk picked
    # up then itself: the client's next look takes the save's disks for counted (no second count
    # popup), as attach_helper does on Windows. Not after a failure that kept the connection.
    ctx = linux_ctx({"ap_newgame": "armed"})
    pm = ctx.game_conn.pm
    arm(ctx)
    ctx.load.disks_counted = {12}
    ticks.linux_mod_lost(ctx, linux_helper.LinuxHelperError("the helper refused the addresses"))
    arm(ctx)  # the same connection: the helper kept its client
    assert ctx.load.disks_counted == {12}
    pm.connection = 2  # a request with no reply closed it; the next read made another
    arm(ctx)
    assert ctx.load.disks_counted is None
    assert ctx.linux_mod is not None and ctx.linux_mod.current()


# --- the keep-alive -----------------------------------------------------------------------------


def test_on_linux_a_failed_keep_alive_drops_the_helper_connection(caplog) -> None:
    # A ping with no reply closes the connection; the next request makes another that never
    # sent config, which the poll's ticks would use as configured: lost, so configured again.
    ctx = linux_ctx({"ap_newgame": "armed"})
    arm(ctx)
    assert ctx.linux_mod is not None
    asyncio.run(ticks.linux_keepalive_tick(ctx))
    assert ctx.game_conn.pm.sent[-1] == "ping" and ctx.linux_mod is not None
    ctx.game_conn.pm.replies["ping"] = linux_helper.NoReply("the helper closed its socket")
    with caplog.at_level("WARNING"):
        asyncio.run(ticks.linux_keepalive_tick(ctx))
    assert ctx.linux_mod is None and ctx.conn.newgame is None
    arm(ctx)
    assert configs(ctx) == 2
    assert ctx.linux_mod is not None


def linux_away(shown: str = game.IN_PLAY, replies: dict | None = None) -> zc.ZexionContext:
    """A dropped connection, the Linux build showing ``shown``, its helper configured."""
    ctx = linux_ctx(replies, configured=True, dropped=True)
    ctx.game_conn.shown = shown
    return ctx


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


# --- each part through the Linux helper ---------------------------------------------------------


def test_on_linux_the_labels_go_through_the_linux_helper() -> None:
    ctx = linux_ctx({"ap_labels": "labeled 0"}, configured=True)
    ctx.seed_state.dressed = False  # not this seed's save: any labels it may hold taken back
    via_link(ctx, lambda link: ticks.labels_tick(ctx, link.helper, link.told))
    assert ctx.game_conn.pm.sent[-1] == "ap_labels"
    assert not ctx.linux_mod.told.labels_held
    ctx.linux_mod.told.labels_held = True
    ctx.game_conn.pm.replies["ap_labels"] = linux_helper.NoReply("the helper closed its socket")
    via_link(ctx, lambda link: ticks.labels_tick(ctx, link.helper, link.told))
    assert ctx.linux_mod is None, "configured again on a later poll"


def seed_tick(ctx: zc.ZexionContext) -> None:
    via_link(ctx, lambda link: ticks.linux_seed_tick(ctx, link.helper, None))


def seeded(replies: dict, **kwargs) -> zc.ZexionContext:
    """A configured Linux helper for the seed's file: the seed's name and slot known."""
    ctx = linux_ctx({"offline_put": "stored", **replies}, configured=True, **kwargs)
    ctx.seed_name, ctx.team, ctx.slot = "S", 0, 1
    return ctx


def test_on_linux_the_seeds_own_traps_are_handed_to_the_linux_helper(monkeypatch) -> None:
    ctx = seeded({})
    monkeypatch.setattr(ticks, "linux_seed", lambda ctx, p: "zexion-ap offline 1\ntraps 100:103")
    monkeypatch.setattr(ticks, "offline_seed", lambda ctx, p: None)  # not known yet
    seed_tick(ctx)
    sent = ctx.game_conn.pm.sent[-1]
    assert sent.startswith("offline_put seed ") and sent.endswith("traps 100:103")
    ctx.linux_mod.told.offline_at = 0.0
    seed_tick(ctx)
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
    ctx = seeded({"offline_put": reply})
    monkeypatch.setattr(ticks, "linux_seed", lambda ctx, p: "zexion-ap offline 1\ntraps -")
    monkeypatch.setattr(ticks, "offline_seed", lambda ctx, p: None)  # not known yet
    seed_tick(ctx)
    ctx.linux_mod.told.offline_at = 0.0
    seed_tick(ctx)
    puts = [t for t in ctx.game_conn.pm.sent if t.startswith("offline_put")]
    assert len(puts) == (2 if put_again else 1)


def test_on_linux_the_whole_seed_file_is_handed_over_once_known(monkeypatch) -> None:
    # Its labels, for the look with no client (offline_seed, as on Windows).
    ctx = seeded({})
    monkeypatch.setattr(ticks, "linux_seed", lambda ctx, p: "zexion-ap offline 1\ntraps -")
    monkeypatch.setattr(ticks, "offline_seed", lambda ctx, p: "zexion-ap offline 1\nap_labels")
    seed_tick(ctx)
    assert ctx.game_conn.pm.sent[-1].endswith("\nap_labels")


def test_on_linux_the_whole_seed_file_is_never_replaced_by_the_slot_datas(monkeypatch) -> None:
    # After a reconnect the items are listed afresh: offline_seed is None for a while, and the
    # slot data's text would replace the file the look dresses saves from.
    ctx = seeded({})
    full = types.SimpleNamespace(text="zexion-ap offline 1\nap_labels")
    monkeypatch.setattr(ticks, "linux_seed", lambda ctx, p: "zexion-ap offline 1\ntraps -")
    monkeypatch.setattr(ticks, "offline_seed", lambda ctx, p: full.text)
    seed_tick(ctx)
    full.text = None  # a reconnect: not known again yet
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)  # a new connection: told afresh
    seed_tick(ctx)
    puts = [t for t in ctx.game_conn.pm.sent if t.startswith("offline_put")]
    assert len(puts) == 1 and puts[0].endswith("\nap_labels")


def test_on_linux_client_connected_is_asked_of_the_linux_helper() -> None:
    ctx = linux_ctx({"client_back": "shown"}, configured=True)
    assert not ctx.linux_mod.told.client_back_sent  # asked on Linux too, now there is a look
    ctx.load.seed_loaded = ctx.load.in_play = True
    for _ in range(2):
        via_link(ctx, lambda link: ticks.client_back_tick(ctx, link.helper, link.told))
    assert ctx.game_conn.pm.sent.count("client_back") == 1
    assert ctx.linux_mod.told.client_back_sent


def test_the_linux_seed_needs_only_the_connection() -> None:
    # Known from the slot data: no wait for the items or the server's scouts.
    ctx = linux_ctx()
    ctx.conn.items_listed = False
    text = ticks.linux_seed(ctx, None)
    assert text is not None and "\ntraps " in text and "\ndiscs -" in text


def test_the_linux_seed_carries_the_disks_once_the_items_are_listed() -> None:
    # The count every save shares and this world's own disks' cells, for the helper's count at
    # their pickup with no client (Game::watch_own_disks).
    ctx = linux_ctx(goal="os_install_disks", discs_required=3, discs_placed=5, disc_locations=[12])
    ctx.conn.items_listed = False
    received = types.SimpleNamespace(codes=[44, 44])  # two disks received
    assert "\ndiscs -" in ticks.linux_seed(ctx, received)
    ctx.conn.items_listed = True
    lines = ticks.linux_seed(ctx, received).split("\n")
    discs_line = next(line for line in lines if line.startswith("discs "))
    assert discs_line.split()[1:3] == ["2", "3"]
    unfound = next(line for line in lines if line.startswith("unfound "))
    assert unfound != "unfound -"


def test_the_linux_seed_and_the_whole_file_are_one_text_where_they_agree() -> None:
    # Where the seed has no other players' items the whole file has no labels to add: the two are
    # the same bytes, whatever the goal, the traps or the assist lock (ticks.seed_parts).
    ctx = linux_ctx(goal="os_install_disks", discs_required=3, discs_placed=5, disc_locations=[12])
    ctx.conn.foreign = []
    received = types.SimpleNamespace(codes=[44, 44])
    assert ticks.offline_seed(ctx, received) == ticks.linux_seed(ctx, received) is not None


def test_on_linux_the_disks_count_goes_through_the_linux_helper() -> None:
    ctx = linux_ctx({"discs": "discs off"}, configured=True)
    ctx.seed_state.dressed = False  # not this seed's save: the count off

    def tick(link) -> object:
        return ticks.discs_helper_tick(ctx, None, link.helper, link.told)

    via_link(ctx, tick)
    assert ctx.game_conn.pm.sent[-1] == discs.OFF and ctx.linux_mod.told.discs.sent == discs.OFF
    ctx.linux_mod.told.discs.sent = None
    ctx.game_conn.pm.replies["discs"] = linux_helper.NoReply("the helper closed its socket")
    via_link(ctx, tick)
    assert ctx.linux_mod is None, "configured again on a later poll"


def assists(ctx: zc.ZexionContext) -> None:
    via_link(
        ctx,
        lambda link: ticks.toggle_tick(
            link.told.assists, ctx.load.assists_want, ticks.ASSISTS, link.helper
        ),
    )


def intel(ctx: zc.ZexionContext) -> None:
    via_link(
        ctx,
        lambda link: ticks.toggle_tick(
            link.told.intel, ctx.load.intel_want, ticks.INTEL, link.helper
        ),
    )


def test_on_linux_intel_chips_go_through_the_linux_helper() -> None:
    ctx = linux_ctx({"intel": "intel on"}, configured=True)
    ctx.load.intel_want = True  # this seed's save loaded, its Area Maps Intel Chips
    intel(ctx)
    intel(ctx)
    assert ctx.game_conn.pm.sent.count("intel on") == 1 and ctx.linux_mod.told.intel.sent is True
    ctx.game_conn.pm.replies["intel"] = linux_helper.NoReply("the helper closed its socket")
    ctx.load.intel_want = False
    intel(ctx)
    assert ctx.linux_mod is None, "configured again on a later poll"


def test_on_linux_the_assist_lock_goes_through_the_linux_helper() -> None:
    ctx = linux_ctx({"assists": "locked"}, configured=True)
    ctx.load.assists_want = True  # this seed's save loaded, assists disallowed
    assists(ctx)
    assists(ctx)
    sent = ctx.game_conn.pm.sent
    assert sent.count("assists lock") == 1 and ctx.linux_mod.told.assists.sent is True
    ctx.game_conn.pm.replies["assists"] = linux_helper.NoReply("the helper closed its socket")
    ctx.load.assists_want = False
    assists(ctx)
    assert ctx.linux_mod is None, "configured again on a later poll"


def test_on_linux_a_setting_is_sent_only_on_the_connection_that_sent_config() -> None:
    # A request on a connection made again since (a read in the same poll) would set the ASSIST
    # lock, the Intel Chip or the disks' count in a helper that keeps it past that connection's
    # close, as the connection never sent config.
    ctx = linux_ctx(
        {"assists": "locked", "intel": "intel on", "discs": "discs off"}, configured=True
    )
    ctx.load.assists_want = ctx.load.intel_want = True
    ctx.seed_state.dressed = False
    pm = ctx.game_conn.pm
    pm.connection = 2
    sent = len(pm.sent)
    assists(ctx)
    intel(ctx)
    via_link(ctx, lambda link: ticks.discs_helper_tick(ctx, None, link.helper, link.told))
    assert len(pm.sent) == sent and ctx.linux_mod is not None
    pm.connection = 1  # the connection it was configured on
    assists(ctx)
    intel(ctx)
    assert pm.sent[sent:] == ["assists lock", "intel on"]


@pytest.mark.parametrize(
    "tick",
    [
        lambda ctx, link: ticks.labels_tick(ctx, link.helper, link.told),
        lambda ctx, link: ticks.linux_seed_tick(ctx, link.helper, None),
        lambda ctx, link: ticks.language_tick(ctx, link),
        lambda ctx, link: ticks.discs_helper_tick(ctx, None, link.helper, link.told),
        lambda ctx, link: ticks.replay_tick(ctx, link.helper),
        lambda ctx, link: ticks.client_back_tick(ctx, link.helper, link.told),
        lambda ctx, link: ticks.other_save_tick(ctx, session.Step(), link.helper, link.told),
        lambda ctx, link: ticks.return_home_tick(ctx, link.helper),
        lambda ctx, link: ticks.followups_tick(ctx, None, session.Step(), link.helper, link.told),
        lambda ctx, link: ticks.mission_tick(ctx, None, session.Step(), link.helper, link.told),
    ],
    ids=[
        "labels",
        "seed",
        "language",
        "discs",
        "replay",
        "client back",
        "other save",
        "return home",
        "follow-ups",
        "mission",
    ],
)
def test_on_linux_no_tick_goes_through_a_connection_made_again(tick) -> None:
    # The one rule (ticks.helper_link): the labels', the replay's, the seed's file's and the
    # language's ticks once went through a stale connection, the others did not.
    # Many of these send nothing even when they run (nothing to tell, in this context), so the
    # tick itself is watched: it is called on the configured connection, and not on another.
    ctx = linux_ctx(configured=True)
    called: list[int] = []

    async def watched(link) -> None:
        called.append(1)
        await tick(ctx, link)

    via_link(ctx, watched)
    assert called == [1], "run through the connection it was configured on"
    ctx.game_conn.pm.connection = 2
    sent = len(ctx.game_conn.pm.sent)
    via_link(ctx, watched)
    assert called == [1], "not run through a connection made again"
    assert len(ctx.game_conn.pm.sent) == sent


def test_on_linux_the_language_is_asked_of_the_linux_helper(caplog) -> None:
    hello = "zexion-ap 1.0.0 build=B1 hooked=1 platform=linux folder=1 lang=French"
    ctx = linux_ctx({"hello": hello}, configured=True)
    with caplog.at_level(logging.WARNING, logger="Client"):
        via_link(ctx, lambda link: ticks.language_tick(ctx, link))
        via_link(ctx, lambda link: ticks.language_tick(ctx, link))
    assert ctx.game_conn.pm.firsts().count("hello") == 1, "asked again only after a few seconds"
    assert [line for line in said(caplog) if line.startswith("Zexion is set to French")]


def test_on_linux_a_failed_follow_up_drops_the_linux_helper_not_the_windows_one(
    monkeypatch,
) -> None:
    # LinuxMod.apply raises the Linux helper's own errors (a NoReply is a native.ReplyLost): the
    # Linux helper's to drop (linux_mod_lost), whichever tick it failed in.
    ctx = linux_ctx(configured=True)

    async def lost(*args) -> None:
        raise linux_helper.NoReply("the helper did not answer within 5 s")

    monkeypatch.setattr(ticks, "followups_tick", lost)
    via_link(ctx, lambda link: ticks.followups_tick(ctx, None, None, link.helper, link.told))
    assert ctx.linux_mod is None and ctx.run.reattach is not state.Reattach.LOST


def home_tick(ctx: zc.ZexionContext) -> None:
    via_link(ctx, lambda link: ticks.return_home_tick(ctx, link.helper))


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
    status, offered, followed
) -> None:
    ctx = linux_ctx({"mission_status": status}, configured=True)
    home = ctx.load.return_home
    home.queued = True
    home_tick(ctx)
    assert home.offer is offered and home.queued is followed


def test_on_linux_a_shown_prompt_put_back_by_a_closed_connection_is_offered_again() -> None:
    # Shown, then the connection closes with the prompt up: the helper puts YES back to NO's
    # callback (status "put back"), so pressing YES does nothing: asked for again.
    ctx = linux_ctx({"mission_status": "shown"}, configured=True)
    pm = ctx.game_conn.pm
    home = ctx.load.return_home
    home.queued = True
    home_tick(ctx)
    home_tick(ctx)
    assert pm.sent.count("mission_status") == 1, "shown: asked once on a connection"
    assert home.queued and not home.offer
    pm.connection = 2  # closed and made again; configured again (configure_linux)
    ctx.linux_mod = linux_helper.LinuxMod(pm)
    pm.replies["mission_status"] = "put back: the prompt was up, YES is NO"
    home_tick(ctx)
    assert home.offer and not home.queued


@pytest.mark.parametrize(
    "status",
    [
        "put back: the prompt was up, YES is NO",  # still up when the connection closed
        "put back: YES was pressed, the ending had not started",  # YES, dropped before the ending
    ],
)
def test_on_linux_a_shown_prompt_dropped_up_or_after_yes_is_offered_again(status) -> None:
    # Read as "shown" on one connection, so no longer asked about there: a YES pressed meanwhile
    # whose connection closed before the ending started reads "put back", not "not asked".
    ctx = linux_ctx({"mission_status": "shown"}, configured=True)
    pm = ctx.game_conn.pm
    home = ctx.load.return_home
    home.queued = True
    home_tick(ctx)
    assert home.queued and home.shown_on is not None
    pm.connection = 2
    ctx.linux_mod = linux_helper.LinuxMod(pm)
    pm.replies["mission_status"] = status
    home_tick(ctx)
    assert home.offer and not home.queued


def test_on_linux_a_prompt_answered_no_is_not_offered_again_by_a_new_connection() -> None:
    ctx = linux_ctx({"mission_status": "shown"}, configured=True)
    pm = ctx.game_conn.pm
    home = ctx.load.return_home
    home.queued = True
    home_tick(ctx)
    pm.connection = 2  # a NO closed it first: its status stays "shown"
    ctx.linux_mod = linux_helper.LinuxMod(pm)
    home_tick(ctx)
    assert not home.offer and home.queued and pm.sent.count("mission_status") == 2
    pm.connection = 3
    ctx.linux_mod = linux_helper.LinuxMod(pm)
    pm.replies["mission_status"] = "not asked"  # the NO closed it; the cancel reset the status
    home_tick(ctx)
    assert not home.offer and not home.queued


def test_on_linux_the_poll_asks_the_linux_helper_for_shields() -> None:
    # The Linux build has no Windows helper: the Linux helper's is enough for shields_tick.
    ctx = linux_ctx()
    seed = ctx.seed_state
    assert not zc.shields_due(ctx, seed)  # nothing to spawn with, nothing to look for
    ctx.linux_mod = linux_helper.LinuxMod(ctx.game_conn.pm)
    assert zc.shields_due(ctx, seed)


def test_on_linux_a_received_death_is_not_said_to_wait_for_the_mod(caplog, monkeypatch) -> None:
    # The Linux helper deals it (ticks.deathlink_tick): no "the Zexion mod isn't running".
    base = zc.ZexionContext.__mro__[1]  # CommonContext's stand-in here
    monkeypatch.setattr(base, "on_deathlink", lambda self, data: None, raising=False)
    ctx = linux_ctx(configured=True, death_link=True)
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
    asyncio.run(ticks.configure_linux(ctx))
    assert ctx.linux_mod is None


def test_on_linux_reload_mod_says_it_cannot_reload() -> None:
    # The Linux helper is preloaded with the game: /reload_mod says so, and asks for nothing.
    ctx = linux_ctx()
    said_lines: list[str] = []
    processor = types.SimpleNamespace(ctx=ctx, output=said_lines.append)  # AP's is stubbed here
    assert not zc.ZexionCommandProcessor._cmd_reload_mod(processor)
    assert said_lines == ["On Linux the Zexion mod can't be reloaded: restart Zexion instead."]
    assert not ctx.run.reload_mod_pending


def test_on_linux_the_helper_retry_leaves_the_linux_helpers_plan_alone(caplog) -> None:
    ctx = linux_ctx({"ap_newgame": "armed"})
    arm(ctx)
    assert ctx.conn.newgame is state.NewGame.ARMED
    with caplog.at_level("WARNING"):
        assert not asyncio.run(ticks.attach_helper(ctx))
        assert not asyncio.run(ticks.attach_helper(ctx))
    assert ctx.conn.newgame is state.NewGame.ARMED  # not reset as drop_helper would
    assert not said(caplog)  # nothing to say there


# --- the Linux helper let go while no connection steps -------------------------------------------


def test_on_linux_a_save_loaded_while_dropped_lets_the_linux_helper_go(caplog) -> None:
    # Its language (a hello) every 5 s would keep the helper's client open for good: the helper
    # dresses the save itself once the connection closes, as the Windows helper does when it is
    # let go.
    ctx = linux_away()
    ctx.conn.newgame = state.NewGame.ARMED
    with caplog.at_level(logging.DEBUG):
        asyncio.run(zc.leave_helper_tick(ctx))
    assert ctx.linux_mod is None and ctx.run.reattach is state.Reattach.LET_GO
    assert ctx.game_conn.pm.disconnects == 1 and ctx.conn.newgame is None  # armed again later
    assert any("Not connected" in r.getMessage() for r in caplog.records)


def test_on_linux_the_helper_let_go_is_not_configured_while_no_connection_steps() -> None:
    ctx = linux_away()
    asyncio.run(zc.leave_helper_tick(ctx))
    pm = ctx.game_conn.pm
    sent = len(pm.sent)
    asyncio.run(ticks.configure_linux(ctx))  # a poll with no connection: not configured again
    asyncio.run(zc.leave_helper_tick(ctx))  # nor let go twice
    assert ctx.linux_mod is None and len(pm.sent) == sent and pm.disconnects == 1
    ctx.slot = 1  # a connection steps: configured again, before that poll's step
    asyncio.run(ticks.configure_linux(ctx))
    assert ctx.linux_mod is not None and pm.sent[sent].startswith("config ")


def test_on_linux_a_reconnect_holds_the_step_until_the_helper_is_configured(monkeypatch) -> None:
    # As after a Windows helper let go (helper_back_by): the step gives nothing with no popup.
    ctx = linux_away()
    asyncio.run(zc.leave_helper_tick(ctx))
    ctx.game_conn.pm.replies["config"] = "refused"  # not taken when the connection comes back
    ctx.slot = 1
    steps = stepping(monkeypatch, ctx)
    poll(ctx)
    assert steps and not steps[0]["give"]
    assert ctx.run.reattach is state.Reattach.NONE


@pytest.mark.parametrize(
    ("shown", "dressed", "holding"),
    [(game.NO_SAVE, False, False), (game.IN_PLAY, True, False), (game.IN_PLAY, False, True)],
)
def test_on_linux_the_helper_is_kept_where_the_windows_one_is(shown, dressed, holding) -> None:
    ctx = linux_away(shown)
    ctx.seed_state.dressed, ctx.seed_state.session.holding = dressed, holding
    mod = ctx.linux_mod
    asyncio.run(zc.leave_helper_tick(ctx))
    assert ctx.linux_mod is mod and ctx.run.reattach is state.Reattach.NONE
    assert ctx.game_conn.pm.disconnects == 0


def test_on_windows_nothing_is_let_go_without_a_helper_or_a_linux_helper() -> None:
    ctx = context(connection(), Showing(game.IN_PLAY), dropped=True)
    asyncio.run(zc.leave_helper_tick(ctx))
    assert ctx.run.reattach is state.Reattach.NONE


# --- the seed's file the helper keeps ----------------------------------------------------------

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
    ctx = seeded({})
    looked: list[str] = []
    monkeypatch.setattr(
        ticks, "linux_stored_seed", lambda mod, name: looked.append(name) or WHOLE_FILE
    )
    monkeypatch.setattr(ticks, "linux_seed", lambda ctx, p: POORER_FILE)
    monkeypatch.setattr(ticks, "offline_seed", lambda ctx, p: None)  # not known yet
    seed_tick(ctx)
    sent = ctx.game_conn.pm.sent[-1]
    assert sent.startswith("offline_put seed S-0-1\n")
    text = sent.split("\n", 1)[1]
    assert "traps 100:103\n" in text, "its own traps are this run's"
    assert "discs 3 12 41\nunfound 7\n" in text and text.endswith("ap_labels\nlabel 23 14 4150\n")
    ctx.linux_mod.told.offline_at = 0.0
    seed_tick(ctx)
    assert looked == ["S-0-1"], "the file is looked at once a run"
    assert [t for t in ctx.game_conn.pm.sent if t.startswith("offline_put")] == [sent]


def test_on_linux_with_no_file_kept_the_poorer_text_goes_as_it_is(monkeypatch) -> None:
    ctx = seeded({})
    monkeypatch.setattr(ticks, "linux_seed", lambda ctx, p: POORER_FILE)
    monkeypatch.setattr(ticks, "offline_seed", lambda ctx, p: None)
    seed_tick(ctx)
    assert ctx.game_conn.pm.sent[-1] == "offline_put seed S-0-1\n" + POORER_FILE


def test_on_linux_the_whole_text_is_put_as_it_is_whatever_the_file_holds(monkeypatch) -> None:
    ctx = seeded({})
    newer = WHOLE_FILE.replace("label 23 14 4150", "label 9 9 41")
    monkeypatch.setattr(ticks, "linux_stored_seed", lambda mod, name: WHOLE_FILE)
    monkeypatch.setattr(ticks, "offline_seed", lambda ctx, p: newer)
    seed_tick(ctx)
    assert ctx.game_conn.pm.sent[-1] == "offline_put seed S-0-1\n" + newer


def stored_seed_mod(token: object) -> types.SimpleNamespace:
    """A Linux helper whose connection's token file is ``token``'s."""
    return types.SimpleNamespace(pm=types.SimpleNamespace(token_path=token))


def test_the_stored_seed_is_read_beside_the_token_the_helper_took(tmp_path) -> None:
    taken = tmp_path / "live" / "ZexionAP"
    (taken / "seeds").mkdir(parents=True)
    (taken / "seeds" / "S-0-1.txt").write_bytes(WHOLE_FILE.encode())
    (taken / "seeds" / "big-0-1.txt").write_bytes(b"x" * (offline.MAX_FILE + 1))
    (taken / "seeds" / "bin-0-1.txt").write_bytes(b"\xff\xfe")
    mod = stored_seed_mod(taken / "token")
    assert ticks.linux_stored_seed(mod, "S-0-1") == WHOLE_FILE
    assert ticks.linux_stored_seed(mod, "other-0-1") is None  # no such file
    assert ticks.linux_stored_seed(mod, "big-0-1") is None
    assert ticks.linux_stored_seed(mod, "bin-0-1") is None  # not text


def test_the_stored_seed_is_none_before_a_token_is_taken() -> None:
    assert ticks.linux_stored_seed(stored_seed_mod(None), "S-0-1") is None


def test_on_linux_a_merged_text_the_helper_refuses_is_put_again_as_the_slot_datas(
    monkeypatch,
) -> None:
    ctx = seeded({"offline_put": "error bad file"})
    monkeypatch.setattr(ticks, "linux_stored_seed", lambda mod, name: WHOLE_FILE)
    monkeypatch.setattr(ticks, "linux_seed", lambda ctx, p: POORER_FILE)
    monkeypatch.setattr(ticks, "offline_seed", lambda ctx, p: None)
    seed_tick(ctx)
    ctx.linux_mod.told.offline_at = 0.0
    seed_tick(ctx)
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
    ctx = seeded({"offline_put": "error exception"})
    monkeypatch.setattr(ticks, "linux_stored_seed", lambda mod, name: WHOLE_FILE)
    monkeypatch.setattr(ticks, "linux_seed", lambda ctx, p: POORER_FILE)
    monkeypatch.setattr(ticks, "offline_seed", lambda ctx, p: None)
    for _ in range(2):
        seed_tick(ctx)
        ctx.linux_mod.told.offline_at = 0.0
    puts = [t for t in ctx.game_conn.pm.sent if t.startswith("offline_put")]
    assert len(puts) == 2 and all("ap_labels" in t for t in puts)
