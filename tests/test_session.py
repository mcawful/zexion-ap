"""client/session.py against a fake game (no process, no AP)."""

from __future__ import annotations

import pytest
from fakes import CELLS, SECRET_CELLS, SECRET_LIST, fresh, plan, secret_plan, secret_section

from client import game, native
from client import save_patch as sp
from client import session as ss


def blob_for(p: sp.SeedPlan) -> bytes:
    out = sp.patch(bytes(fresh(cells=True)), p)
    return out[sp.SECTION : sp.SECTION + game.RANDO_BLOB_SIZE]


class FakeGame:
    def __init__(self, p: sp.SeedPlan):
        self.state_value = game.IN_PLAY
        self.grid = bytearray(54 * 54)
        self.inv = bytearray(51)
        for item, n in p.start_items.items():
            self.inv[item] = n
        self.sup = [32, 32, 13, 13, 0, 0]
        self.blob = blob_for(p)
        self.flicker = False
        self.reads = 0
        self.marker = 1
        self.flags = bytearray(0x9C4)
        self.stamp = bytes(14)

    def state(self, energy: int | None = None) -> str:
        self.reads += 1
        if self.flicker and self.reads % 2 == 0:
            return game.MENU
        return self.state_value

    def map_grid(self) -> bytes:
        return bytes(self.grid)

    def inventory(self) -> bytes:
        return bytes(self.inv)

    def supplies(self) -> tuple[int, ...]:
        return tuple(self.sup)

    def rando_blob(self) -> bytes:
        return self.blob

    def rando_marker(self) -> int:
        return self.marker

    def flag_bytes(self, first: int, count: int) -> bytes:
        return bytes(self.flags[first : first + count])

    def player_state(self) -> game.PlayerState:
        return game.PlayerState(bytes(self.inv), tuple(self.sup), self.sup[0], (0, 0))

    def save_stamp(self) -> bytes:
        return self.stamp

    def set_item(self, item: int, expected: int, value: int) -> None:
        if self.inv[item] != expected:
            raise game.GameError("not the expected value")
        self.inv[item] = value

    def set_supplies(self, expected, new) -> None:
        if tuple(self.sup) != tuple(expected):
            raise game.GameError("not the expected supplies")
        self.sup = list(new)

    def collect(self, i: int) -> None:
        """A native pickup: the map cell and the item the table holds there."""
        x, y = CELLS[i]
        self.grid[y * 54 + x] = 0x1B
        item = PLAN.location_items[i]
        self.inv[item] += 1


PLAN = plan()  # starts with 2 Energy Tanks, 3 Missile Containers, a Missile Box, Machine Gun


def give(
    session: ss.Session, g: FakeGame, received: list[int], senders: list[str] | None = None
) -> dict[int, int]:
    session.step(g, received, gap=0, senders=senders)
    return session.step(g, received, gap=0, senders=senders).granted


def begin_ending(session: ss.Session, g: FakeGame) -> ss.Step:
    """A poll before the ending, then the poll that sees it begin."""
    session.step(g, [], gap=0)
    g.flags[ss.ENDING_FLAG] = 1
    return session.step(g, [], gap=0)


def look_away(session: ss.Session, g: FakeGame) -> None:
    """What the client does with Session.look_away while disconnected (state.end_kept_load)."""
    if session.look_away(g, gap=0):
        session.leave()


def reconnect(before: ss.Session) -> ss.Session:
    """The next connection's session, carrying ``before``'s ending (state.carry_over)."""
    after = ss.Session(PLAN, [list(c) for c in CELLS])
    after.keep_ending(before)
    return after


@pytest.fixture
def setup():
    return ss.Session(PLAN, [list(c) for c in CELLS]), FakeGame(PLAN)


def test_collected_cells_are_reported_as_checks(setup) -> None:
    session, g = setup
    g.collect(5)
    g.collect(60)
    step = session.step(g, [], gap=0)
    assert step.checked == {5, 60} and step.seed_matches and not step.goal


def test_secret_shuffles_155th_is_checked_by_its_location_index() -> None:
    p = secret_plan()
    session, g = ss.Session(p, [list(c) for c in SECRET_CELLS]), FakeGame(PLAN)
    g.blob = bytes(secret_section(p))
    x, y = SECRET_LIST[100]  # list position 100: location index 154
    g.grid[y * 54 + x] = 0x1B
    x, y = SECRET_LIST[101]  # list position 101: location index 100
    g.grid[y * 54 + x] = 0x1B
    step = session.step(g, [], gap=0)
    assert step.seed_matches and step.checked == {154, 100}
    # The same cells in another order are not this seed's list.
    other = ss.Session(p, [list(c) for c in SECRET_CELLS])
    other.listed = list(SECRET_CELLS)
    assert other.step(g, [], gap=0).left_seed


def test_a_received_item_is_given_once(setup) -> None:
    session, g = setup
    assert session.step(g, [17], gap=0).granted == {}  # owed once: wait for a second poll
    assert session.step(g, [17], gap=0).granted == {17: 1}
    assert session.step(g, [17], gap=0).granted == {}
    assert g.inv[17] == 1


def test_a_half_recorded_pickup_is_not_taken_for_an_item_owed(setup) -> None:
    session, g = setup
    x, y = CELLS[0]  # location 0 holds an Energy Tank
    g.grid[y * 54 + x] = 0x1B  # the cell is set; the inventory byte not yet
    assert session.step(g, [], gap=0).granted == {}
    g.inv[0] += 1  # the game finishes recording it
    assert session.step(g, [], gap=0).granted == {}
    assert session.step(g, [], gap=0).granted == {}
    assert g.inv[0] == 3


def test_a_received_copy_of_a_native_pickup_is_still_given(setup) -> None:
    # The cells collected count toward what is owned (reconcile.should_own): the pickup at
    # location 0 (an Energy Tank) and the one received are both owed, on top of the start's 2.
    session, g = setup
    g.collect(0)
    give(session, g, [0])
    assert g.inv[0] == 4


def test_an_energy_tank_raises_max_and_current_energy(setup) -> None:
    session, g = setup
    give(session, g, [0])
    assert g.inv[0] == 3
    assert g.sup[:2] == [48, 48]


def test_a_missile_box_raises_missiles_by_ten(setup) -> None:
    session, g = setup
    give(session, g, [36])
    assert g.sup[2:4] == [23, 23]


def test_nothing_is_given_outside_play_but_checks_and_goal_still_count(setup) -> None:
    session, g = setup
    g.state_value = game.MENU
    g.collect(5)
    g.flags[ss.ENDING_FLAG] = 1  # the ending's cutscenes read as menu
    step = session.step(g, [17], gap=0)
    assert step.granted == {} and g.inv[17] == 0
    assert step.checked == {5} and step.goal


def test_items_received_once_the_ending_begins_wait_for_the_next_load(setup) -> None:
    session, g = setup
    # The ending begins; some of its cutscenes read as play.
    assert begin_ending(session, g).goal and session.holding
    for _ in range(3):  # a release or collect on the goal sends items now
        assert session.step(g, [17], gap=0).granted == {}
    assert g.inv[17] == 0
    g.state_value = game.NO_SAVE  # the ending ends at the title
    session.step(g, [17], gap=0)
    assert not session.holding
    g.state_value = game.IN_PLAY  # the save loaded again, its ending reached before
    give(session, g, [17])
    assert g.inv[17] == 1


def test_grabbing_zexion_is_not_the_goal(setup) -> None:
    session, g = setup
    g.inv[31] = 1  # Zexion grabbed: the escape and the final boss are still ahead
    assert not session.step(g, [], gap=0).goal
    g.flags[ss.ENDING_FLAG] = 1
    assert session.step(g, [], gap=0).goal


def test_the_games_escape_skip_is_not_the_goal_but_the_ending_after_it_is(setup) -> None:
    session, g = setup
    # The game's own SKIP ESCAPE AND FINAL BATTLE? (a game that has reached the ending before).
    g.flags[ss.ENDING_FLAG] = 1  # YES: flag 250 at once, flags 185 to 212 set
    g.flags[ss.FINALE_FLAG] = 1
    assert not session.step(g, [], gap=0).goal
    g.flags[ss.FINALE_FLAG] = 0  # the ending clears them as its first cutscene starts
    assert session.step(g, [], gap=0).goal


def test_the_ending_of_another_seed_is_not_the_goal(setup) -> None:
    session, g = setup
    g.flags[ss.ENDING_FLAG] = 1
    g.marker = 0  # a normal save, its flags those of another game
    assert not session.step(g, [], gap=0).goal


def test_nothing_happens_while_the_state_is_changing(setup) -> None:
    session, g = setup
    g.flicker = True
    g.collect(5)
    assert session.step(g, [17], gap=0) == ss.Step()


def test_another_seed_gets_nothing(setup) -> None:
    session, g = setup
    g.blob = blob_for(plan(start=(6, 1, 1)))
    g.collect(5)
    step = session.step(g, [17], gap=0)
    # Another seed's save, said so (the helper tells the player: other_save_tick).
    assert step.left_seed and step.other_seed and not step.seed_matches and not step.checked
    assert g.inv[17] == 0


def test_a_normal_save_loaded_after_this_seed_gets_nothing(setup) -> None:
    # The game keeps the last randomizer section in memory when a normal save loads.
    session, g = setup
    g.marker = 0
    g.collect(5)
    assert session.step(g, [17], gap=0) == ss.Step(left_seed=True)
    assert session.step(g, [17], gap=0) == ss.Step(left_seed=True)
    assert g.inv[17] == 0


def test_no_save_loaded_gets_nothing(setup) -> None:
    session, g = setup
    g.state_value = game.NO_SAVE
    assert session.step(g, [17], gap=0) == ss.Step(left_seed=True)


def test_a_grant_the_game_changed_first_is_left_for_the_next_poll(setup) -> None:
    session, g = setup
    real = g.set_item

    def game_got_there_first(item, expected, value):
        g.inv[item] += 1
        real(item, expected, value)

    g.set_item = game_got_there_first
    assert give(session, g, [17]) == {}


# --- through the in-game helper ------------------------------------------------------------


class FakeHelper:
    """Applies a batch to the fake game as the native helper would (or refuses, or fails)."""

    def __init__(self, g: FakeGame, reply: str = "applied"):
        self.g, self.reply, self.fail, self.closed = g, reply, False, False
        self.requests: list[str] = []

    def apply(self, text: str) -> str:
        self.requests.append(text)
        if self.fail:
            raise native.HelperError("the pipe closed")
        if self.reply == "applied":
            for line in text.splitlines()[1:]:
                if line.startswith("cas "):
                    item, expected, value = map(int, line.split()[1:])
                    assert self.g.inv[item] == expected
                    self.g.inv[item] = value
        return self.reply

    def popups(self) -> list[str]:
        """The popup lines of the last batch."""
        return [line for line in self.requests[-1].splitlines() if line.startswith("popup")]

    def close(self) -> None:
        self.closed = True


@pytest.fixture
def with_helper(setup):
    session, g = setup
    helper = FakeHelper(g)
    session.helper = helper
    session.announced = 0
    writes = []
    real = g.set_item
    g.set_item = lambda *a: (writes.append(a), real(*a))
    return session, g, helper, writes


def test_grants_go_through_the_helper_in_one_batch(with_helper) -> None:
    session, g, helper, writes = with_helper
    assert give(session, g, [17, 22]) == {17: 1, 22: 1}
    assert helper.requests[-1].splitlines()[:3] == ["apply", "cas 17 0 1", "cas 22 0 1"]
    assert writes == [], "no direct writes while the helper is attached"


def test_a_refused_batch_gives_nothing_and_is_tried_again(with_helper) -> None:
    session, g, helper, writes = with_helper
    helper.reply = "refused not in play"
    assert give(session, g, [17]) == {}
    assert g.inv[17] == 0 and writes == []
    helper.reply = "applied"
    assert session.step(g, [17], gap=0).granted == {17: 1}


def test_a_failed_helper_is_dropped_for_direct_writes(with_helper) -> None:
    session, g, helper, writes = with_helper
    helper.fail = True
    assert give(session, g, [17]) == {17: 1}
    assert writes == [(17, 0, 1)] and session.helper is None and helper.closed


def test_popups_announce_each_received_item_once(with_helper) -> None:
    session, g, helper, _ = with_helper
    session.announced = 1  # the first had its popup before this connection (received offline)
    give(session, g, [17, 36], senders=["Bob", "Alice"])
    assert helper.popups() == ["popup 36 FROM Alice"]
    assert session.announced == 2
    session.step(g, [17, 36], gap=0, senders=["Bob", "Alice"])
    assert len(helper.requests) == 1, "nothing more to send: no repeat popup"


def test_an_area_maps_popup_shows_its_hint(with_helper) -> None:
    session, g, helper, _ = with_helper
    received, senders = [17, 49], ["Bob", "Alice"]
    notes = {1: "HINT: SLIDE MOD AT GUARDIA 22,7"}
    session.step(g, received, gap=0, senders=senders, notes=notes)
    session.step(g, received, gap=0, senders=senders, notes=notes)
    lines = helper.requests[-1].splitlines()
    popups = [line for line in lines if line.startswith(("popup", "notice"))]
    assert popups == ["popup 17 FROM Bob", "notice 49 HINT: SLIDE MOD AT GUARDIA 22,7"]


def test_nothing_is_given_through_the_helper_until_the_popup_count_is_known(setup) -> None:
    session, g = setup
    helper = FakeHelper(g)
    session.helper = helper
    assert give(session, g, [17], senders=["Alice"]) == {}
    assert helper.requests == [] and g.inv[17] == 0
    session.announced = 0  # the server's count: no popup shown yet
    give(session, g, [17], senders=["Alice"])
    assert g.inv[17] == 1 and "popup 17 FROM Alice" in helper.requests[-1]


def test_a_received_weapon_already_owned_pops_up_as_its_upgrade(with_helper) -> None:
    session, g, helper, _ = with_helper
    # The Machine Gun is a starting item; the Force Blaster is not, until the first arrives.
    give(session, g, [3, 4, 4], senders=["Alice"] * 3)
    assert helper.popups() == ["popup 9 FROM Alice", "popup 4 FROM Alice", "popup 14 FROM Alice"]


def test_a_received_upgrade_with_guns_first_pops_up_as_the_byte_it_sets(with_helper) -> None:
    session, g, helper, _ = with_helper
    # A Pentashot sent from the server console is a Force Blaster's copy (reconcile): the first
    # sets the gun's byte, so it and the Force Blaster after it pop up as written.
    give(session, g, [14, 4], senders=["Alice"] * 2)
    assert helper.popups() == ["popup 4 FROM Alice", "popup 14 FROM Alice"]
    assert (g.inv[4], g.inv[14]) == (1, 1)


def test_without_guns_first_each_item_pops_up_as_itself() -> None:
    off = plan(options={10: 0, 42: 2, 72: 1})
    session, g = ss.Session(off, [list(c) for c in CELLS]), FakeGame(off)
    helper = FakeHelper(g)
    session.helper, session.announced = helper, 0
    # The Machine Gun is a starting item: a received Pulse Wave and Pentashot are themselves.
    give(session, g, [9, 14], senders=["Alice"] * 2)
    assert helper.popups() == ["popup 9 FROM Alice", "popup 14 FROM Alice"]
    assert (g.inv[3], g.inv[4], g.inv[9], g.inv[14]) == (1, 0, 1, 1)


def test_lagging_supplies_are_refreshed_through_the_helper(with_helper) -> None:
    session, g, helper, _ = with_helper
    g.sup[3] = 3  # max missiles below the 3 containers + 10 for the Missile Box
    session.step(g, [], gap=0)
    session.step(g, [], gap=0)
    assert helper.requests and helper.requests[-1] == "apply\nrefresh"


def test_nothing_is_sent_when_all_is_in_order(with_helper) -> None:
    session, g, helper, _ = with_helper
    session.step(g, [], gap=0)
    session.step(g, [], gap=0)
    assert helper.requests == []


def test_direct_writes_count_as_handed_over_without_a_replay(setup) -> None:
    session, g = setup
    session.announced = 0
    give(session, g, [17])
    assert g.inv[17] == 1 and session.announced == 1


def test_the_endings_hold_carries_over_a_reconnect(setup) -> None:
    session, g = setup
    assert begin_ending(session, g).goal and session.holding
    after = reconnect(session)
    for _ in range(3):
        assert after.step(g, [17], gap=0).granted == {}
    assert g.inv[17] == 0
    g.flags[ss.ENDING_FLAG] = 0  # another save of the seed, before its ending, loaded meanwhile
    give(after, g, [17])
    assert not after.holding and g.inv[17] == 1, "the hold was the ending's load's alone"


def test_the_endings_hold_carries_over_while_its_load_stays(setup) -> None:
    session, g = setup
    begin_ending(session, g)
    # Disconnected: the ending's cutscenes go on, read as play.
    look_away(session, g)
    after = reconnect(session)
    for _ in range(3):
        assert after.step(g, [17], gap=0).granted == {}
    assert after.holding and g.inv[17] == 0


def test_the_ending_begun_while_disconnected_holds_after_the_reconnect(setup) -> None:
    session, g = setup
    session.step(g, [], gap=0)  # before the ending
    look_away(session, g)
    g.flags[ss.ENDING_FLAG] = 1  # it begins while disconnected, in the same load
    after = reconnect(session)
    assert after.carried_from is session
    after.step(g, [17], gap=0)
    assert after.carried_from is None, "stepped: no later end of the old load is its"
    after.step(g, [17], gap=0)
    assert after.holding and g.inv[17] == 0


def test_an_ending_played_while_disconnected_holds_nothing_on_the_next_load(setup) -> None:
    # Review 31, client finding 2, scenario A: no hold where no ending began.
    session, g = setup
    session.step(g, [], gap=0)  # this seed's save before its ending
    # Disconnected: the ending, MISSION COMPLETE, the title, then the ended save loaded.
    g.state_value = game.NO_SAVE
    look_away(session, g)
    g.state_value = game.IN_PLAY
    g.flags[ss.ENDING_FLAG] = 1
    look_away(session, g)
    after = reconnect(session)
    give(after, g, [17])
    assert not after.holding and g.inv[17] == 1


def test_a_hold_ends_with_its_load_while_disconnected(setup) -> None:
    # Scenario B: the ending began (held), then the title and the ended save loaded again.
    session, g = setup
    assert begin_ending(session, g).goal and session.holding
    g.state_value = game.NO_SAVE
    look_away(session, g)
    assert not session.holding
    g.state_value = game.IN_PLAY
    after = reconnect(session)
    give(after, g, [17])
    assert not after.holding and g.inv[17] == 1


def test_looking_away_needs_two_reads_and_reads_nothing_with_nothing_carried(setup) -> None:
    session, g = setup
    look_away(session, g)
    assert g.reads == 0, "a session that has not read a load carries nothing"
    begin_ending(session, g)
    reads = iter([game.NO_SAVE, game.IN_PLAY])
    g.state = lambda energy=None: next(reads)
    assert not session.look_away(g, gap=0), "a read the next does not agree with: not the title"
    g.state = lambda energy=None: game.NO_SAVE
    assert session.look_away(g, gap=0) and session.holding, "it ends nothing itself"


def test_the_game_closing_while_disconnected_is_raised(setup) -> None:
    session, g = setup
    session.step(g, [], gap=0)

    def closed(energy: int | None = None) -> str:
        raise game.GameNotRunning("the game exited")

    g.state = closed
    with pytest.raises(game.GameNotRunning):
        session.look_away(g, gap=0)  # the client then ends its load (game_closed)
