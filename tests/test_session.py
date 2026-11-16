"""client/session.py against a fake game (no process, no AP)."""

from __future__ import annotations

import functools
import logging
import struct

import pytest
from fakes import CELLS, SECRET_CELLS, SECRET_LIST, plan, secret_plan, section

from client import game, ids, native, offline, reconcile
from client import save_patch as sp
from client import session as ss


class FakeGame:
    def __init__(self, p: sp.SeedPlan):
        self.plan = p
        self.state_value = game.IN_PLAY
        self.grid = bytearray(54 * 54)
        self.inv = bytearray(51)
        for item, n in p.start_items.items():
            self.inv[item] = n
        self.sup = [32, 32, 13, 13, 0, 0]
        self.blob = bytes(section(p))
        self.flicker = False
        self.reads = 0
        self.marker = 1
        self.flags = bytearray(0x9C4)
        self.stamp = bytes(14)
        self.saved: int | None = None  # game_saved
        self.cell = (0, 0)  # the player state's, the game's (X, Y)
        self.items = bytearray(54 * 54)  # each cell's item state (game.Game.cell_items)
        self.live: set[tuple[int, int]] = set()  # the cells of the item objects in the rooms
        self.loaded = bytearray(54 * 54)  # each cell's loaded byte (game.Game.loaded_cells)
        # The game ends an item object once its cell's state reads collected or held (its
        # update, each frame); False: not yet, as between two of the game's updates.
        self.ends_items = True
        self.spawn: set[tuple[int, int]] = set()  # rooms loaded between a mark's look and write
        self.load_after_writes = False  # a load beginning as the marking's last write lands
        self.pickups: int | None = 0  # the pickup counter (None: not found in this build)
        self.timer = 0  # the transition timer: a TELEPORT TO counts it down while in play

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
        return game.PlayerState(bytes(self.inv), tuple(self.sup), self.sup[0], self.cell)

    def save_stamp(self) -> bytes:
        return self.stamp

    def game_saved(self) -> int | None:
        return self.saved

    def pickup_counter(self) -> int:
        if self.pickups is None:
            raise game.PatternError("pickup_counter was not found in this build of the game")
        return self.pickups

    def transition_timer(self) -> int:
        return self.timer

    def set_item(self, item: int, expected: int, value: int) -> None:
        if self.inv[item] != expected:
            raise game.GameError("not the expected value")
        self.inv[item] = value

    def cell_items(self) -> bytes:
        return bytes(self.items)

    def live_item_cells(self) -> set[tuple[int, int]]:
        if self.ends_items:
            self.live = {(x, y) for x, y in self.live if self.items[y * 54 + x] not in (1, 2)}
        return set(self.live)

    def loaded_cells(self) -> bytes:
        return bytes(self.loaded)

    def set_item_collected(self, cell: tuple[int, int]) -> None:
        x, y = cell
        if cell in self.spawn:  # the game loaded the room first: its item is there now
            self.spawn.discard(cell)
            self.live.add(cell)
            self.loaded[y * 54 + x] = 1
        if self.items[y * 54 + x] != game.ITEM_THERE:
            raise game.GameError("not the expected state")
        self.items[y * 54 + x] = game.ITEM_COLLECTED

    def set_map_collected(self, cell: tuple[int, int], expected: int) -> None:
        x, y = cell
        if self.grid[y * 54 + x] != expected:
            raise game.GameError("not the expected marker")
        self.grid[y * 54 + x] = 0x1B
        if self.load_after_writes:
            self.state_value = game.MENU

    def set_supplies(self, expected, new) -> None:
        if tuple(self.sup) != tuple(expected):
            raise game.GameError("not the expected supplies")
        self.sup = list(new)

    def collect(self, i: int) -> None:
        """A native pickup: the map cell and the item the table holds there."""
        x, y = CELLS[i]
        self.grid[y * 54 + x] = 0x1B
        self.items[y * 54 + x] = game.ITEM_COLLECTED
        self.live.discard((x, y))
        item = self.plan.location_items[i]
        # Records 46 to 50 all raise Scrap Metal's byte (reconcile.COUNTED_AS_SCRAP).
        self.inv[ids.SCRAP_METAL if item >= ids.SCRAP_METAL else item] += 1
        if item not in reconcile.UNCOUNTED_PICKUPS and self.pickups is not None:
            self.pickups += 1  # the game's pickup counter


# The flags the goal reads, by number (docs/re/memory.md, "The ending: flag 250"): the ending's,
# set as its first cutscene begins, and one the finale sets and the ending clears.
ENDING, FINALE = 250, 211
# Starts with 2 Energy Tanks, 3 Missile Containers, a Missile Box, Machine Gun.
PLAN = plan()


def give(
    session: ss.Session, g: FakeGame, received: list[int], senders: list[str] | None = None
) -> dict[int, int]:
    session.step(g, received, gap=0, senders=senders)
    return session.step(g, received, gap=0, senders=senders).granted


def begin_ending(session: ss.Session, g: FakeGame) -> ss.Step:
    """A poll before the ending, then the poll that sees it begin."""
    session.step(g, [], gap=0)
    g.flags[ENDING] = 1
    return session.step(g, [], gap=0)


def look_away(session: ss.Session, g: FakeGame) -> None:
    """What the client does with Session.look_away while disconnected (state.SeedState.end_load)."""
    if session.look_away(g, gap=0):
        session.leave()


def reconnect(session: ss.Session) -> ss.Session:
    """The session as the next connection to its seed finds it (zexion_client's connected):
    the same one, its popup count to read again."""
    session.connected()
    return session


@pytest.fixture
def setup():
    return ss.Session(PLAN), FakeGame(PLAN)


def test_collected_cells_are_reported_as_checks(setup) -> None:
    session, g = setup
    g.collect(5)
    g.collect(60)
    step = session.step(g, [], gap=0)
    assert step.checked == {5, 60} and step.seed_matches and not step.goal


def test_secret_shuffles_155th_is_checked_by_its_location_index() -> None:
    p = secret_plan()
    session, g = ss.Session(p), FakeGame(PLAN)
    g.blob = bytes(section(p))
    x, y = SECRET_LIST[100]  # list position 100: location index 154
    g.grid[y * 54 + x] = 0x1B
    x, y = SECRET_LIST[101]  # list position 101: location index 100
    g.grid[y * 54 + x] = 0x1B
    step = session.step(g, [], gap=0)
    assert step.seed_matches and step.checked == {154, 100}
    # The same cells in another order are not this seed's list.
    other = ss.Session(p)
    other.listed = list(SECRET_CELLS)
    assert other.step(g, [], gap=0).left_seed


def test_a_new_connection_gives_nothing_before_its_own_look_at_the_save(setup) -> None:
    # Its poll then knows which found disks' counts are still to show (discs.received_notes).
    session, g = setup
    session.step(g, [17], gap=0)  # owed, seen by the last connection's poll
    reconnect(session)
    assert session.step(g, [17], gap=0).granted == {}
    assert session.step(g, [17], gap=0).granted == {17: 1}


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


def test_a_poll_out_of_play_starts_the_two_poll_wait_over(setup) -> None:
    # What was owed before a menu (or a load) is not taken as agreed by the first poll after:
    # the pause may have hidden a pickup half recorded, or a load changed the inventory.
    session, g = setup
    assert session.step(g, [17], gap=0).granted == {}  # owed once
    g.state_value = game.MENU
    assert session.step(g, [17], gap=0).granted == {}
    g.state_value = game.IN_PLAY
    assert session.step(g, [17], gap=0).granted == {}, "owed once again, not twice"
    assert session.step(g, [17], gap=0).granted == {17: 1}


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
    g.flags[ENDING] = 1  # the ending's cutscenes read as menu
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


def test_an_ending_played_again_holds_until_the_load_ends(setup) -> None:
    session, g = setup
    g.flags[ENDING] = 1  # a save past its ending, loaded
    give(session, g, [17])
    assert not session.holding and g.inv[17] == 1  # flag 250 alone holds nothing
    session.hold_ending()  # RETURN HOME's YES played the ending again (ticks.replay_tick)
    for _ in range(3):
        assert session.step(g, [17, 22], gap=0).granted == {}
    assert session.holding and g.inv[22] == 0
    g.state_value = game.NO_SAVE  # the ending ends at the title
    session.step(g, [17, 22], gap=0)
    assert not session.holding
    g.state_value = game.IN_PLAY
    assert give(session, g, [17, 22]) == {22: 1}


def test_grabbing_zexion_is_not_the_goal(setup) -> None:
    session, g = setup
    g.inv[31] = 1  # Zexion grabbed: the escape and the final boss are still ahead
    assert not session.step(g, [], gap=0).goal
    g.flags[ENDING] = 1
    assert session.step(g, [], gap=0).goal


def test_the_games_escape_skip_is_not_the_goal_but_the_ending_after_it_is(setup) -> None:
    session, g = setup
    # The game's own SKIP ESCAPE AND FINAL BATTLE? (a game that has reached the ending before).
    g.flags[ENDING] = 1  # YES: flag 250 at once, flags 185 to 212 set
    g.flags[FINALE] = 1
    assert not session.step(g, [], gap=0).goal
    g.flags[FINALE] = 0  # the ending clears them as its first cutscene starts
    assert session.step(g, [], gap=0).goal


def test_the_ending_of_another_seed_is_not_the_goal(setup) -> None:
    session, g = setup
    g.flags[ENDING] = 1
    g.marker = 0  # a normal save, its flags those of another game
    assert not session.step(g, [], gap=0).goal


def test_nothing_happens_while_the_state_is_changing(setup) -> None:
    session, g = setup
    g.flicker = True
    g.collect(5)
    assert session.step(g, [17], gap=0) == ss.Step()


def test_another_seed_gets_nothing(setup) -> None:
    session, g = setup
    g.blob = bytes(section(plan(start=(6, 1, 1))))
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


def test_home_is_the_landing_pods_save_point_not_the_seeds_start(setup) -> None:
    # PLAN starts in the save room at (23, 22), tiles 326 and 449: a save there is not home.
    session, g = setup
    g.stamp = struct.pack("<iHHHHH", 3, 100, 0, 0, 449, 326)
    g.cell = (22, 23)
    step = session.step(g, [], gap=0)
    assert step.seed_matches and not step.at_home and not step.in_home_cell
    # A save at the Landing Pod's save point (191, 329), the player in its cell (X16 Y13).
    g.stamp = struct.pack("<iHHHHH", 3, 200, 0, 0, 329, 191)
    g.cell = (16, 13)
    step = session.step(g, [], gap=0)
    assert step.at_home and step.in_home_cell
    assert step.game_saved is None
    g.saved = 9  # GAME SAVED the last message
    assert session.step(g, [], gap=0).game_saved == 9


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


def through(session: ss.Session, helper: FakeHelper) -> None:
    """Every step of ``session`` gives through ``helper``, as the client's poll passes it."""
    session.step = functools.partial(session.step, helper=helper)


@pytest.fixture
def with_helper(setup):
    session, g = setup
    helper = FakeHelper(g)
    through(session, helper)
    session.announced_read = True  # the server's count, read: no popup shown yet
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


def test_a_helper_that_fails_gives_nothing_and_is_kept_in_the_step(with_helper) -> None:
    # The client attaches it again before the next step (zexion_client's poll), so the items
    # owed get their popups: none is written directly meanwhile. The checks still go.
    session, g, helper, writes = with_helper
    helper.fail = True
    g.collect(5)
    session.step(g, [17], gap=0)
    step = session.step(g, [17], gap=0)
    assert isinstance(step.helper_error, native.HelperError)
    assert step.seed_matches and step.checked == {5} and step.granted == {}
    assert writes == [] and g.inv[17] == 0
    assert session.announced == 0, "it never reached the helper: its popup is still to show"


def test_a_lost_reply_counts_its_popups_as_handed_over(with_helper) -> None:
    session, g, helper, writes = with_helper

    def apply(text: str) -> str:
        helper.requests.append(text)
        raise native.ReplyLost("no reply within 3.0 s")

    helper.apply = apply
    session.step(g, [17], gap=0, senders=["Bob"])
    step = session.step(g, [17], gap=0, senders=["Bob"])
    assert isinstance(step.helper_error, native.ReplyLost)
    assert writes == [] and helper.popups() == ["popup 17 from Bob"]
    assert session.announced == 1, "the helper may have shown it: not shown again"


def test_a_lost_reply_counts_the_held_traps_handed_over(with_helper) -> None:
    # The helper may have queued them: not sent again (each trap goes off at most once).
    session, g, helper, _ = with_helper

    def apply(text: str) -> str:
        helper.requests.append(text)
        raise native.ReplyLost("no reply within 3.0 s")

    helper.apply = apply
    received, senders = [17, ids.DISORIENT_TRAP], ["Bob", "Alice"]
    session.announced, session.held_traps = 2, [1]
    session.step(g, received, gap=0, senders=senders)
    step = session.step(g, received, gap=0, senders=senders)
    assert isinstance(step.helper_error, native.ReplyLost)
    assert helper.popups() == ["popup 103 from Alice"] and session.held_traps == []


def test_a_big_batch_goes_in_requests_under_the_pipes_limit(with_helper) -> None:
    # A release received while the client was closed: the helper drops a request over 64 KB,
    # which the client would take for a lost reply and count every popup as shown.
    session, g, helper, _ = with_helper
    received = [ids.DISORIENT_TRAP] + [17] * 600
    senders = ["x" * 400] * len(received)
    session.announced, session.held_traps = 1, [0]
    assert give(session, g, received, senders=senders) == {17: 1}
    assert len(helper.requests) > 2, "more than one batch"
    assert all(len(r.encode()) <= offline.MAX_REQUEST for r in helper.requests)
    assert [r for r in helper.requests if "cas " in r] == helper.requests[:1], "writes go first"
    popups = [line for r in helper.requests for line in r.splitlines() if line.startswith("popup")]
    assert popups[0] == f"popup {ids.DISORIENT_TRAP} from {'x' * 400}", "the held trap first"
    assert len(popups) == 1 + 600
    assert session.announced == len(received) and session.held_traps == []


def test_a_held_trap_the_count_has_not_reached_is_sent_once(with_helper) -> None:
    # The record of the held traps is stored before the count: a client stopped between the two
    # finds the trap in the record and past the count, and sends its popup with the rest.
    session, g, helper, _ = with_helper
    received = [17, ids.DISORIENT_TRAP]
    session.announced, session.held_traps = 1, [1]
    give(session, g, received, senders=["Ann", "Bob"])
    assert helper.popups() == [f"popup {ids.DISORIENT_TRAP} from Bob"]
    assert session.announced == 2 and session.held_traps == []


def test_held_traps_in_later_batches_are_kept_until_their_batch_is_applied(with_helper) -> None:
    session, g, helper, _ = with_helper
    received = [ids.DISORIENT_TRAP] * 300
    senders = ["x" * 400] * len(received)
    session.announced, session.held_traps = len(received), list(range(len(received)))
    real = helper.apply

    def apply(text: str) -> str:
        helper.reply = "refused not in play" if len(helper.requests) == 1 else "applied"
        return real(text)

    helper.apply = apply
    give(session, g, received, senders=senders)
    first = helper.requests[0].count("popup")
    assert 0 < first < len(received), "split in two"
    assert session.held_traps == list(range(first, len(received))), "the second batch is still owed"


def test_a_batch_refused_part_way_keeps_the_rest_for_the_next_poll(with_helper) -> None:
    session, g, helper, _ = with_helper
    received, senders = [17] * 300, ["x" * 400] * 300
    real = helper.apply

    def apply(text: str) -> str:
        helper.reply = "refused not in play" if len(helper.requests) == 1 else "applied"
        return real(text)

    helper.apply = apply
    assert give(session, g, received, senders=senders) == {17: 1}, "the first batch went in"
    assert 0 < session.announced < len(received)
    sent = session.announced
    for _ in range(2):  # the second poll agrees with the first
        session.step(g, received, gap=0, senders=senders)
    assert session.announced == len(received) > sent
    applied = helper.requests[:1] + helper.requests[2:]  # the second was refused: not shown
    popups = [line for r in applied for line in r.splitlines() if line.startswith("popup")]
    assert len(popups) == len(received), "none twice, none lost"


def test_a_lost_reply_part_way_counts_only_the_batches_sent(with_helper) -> None:
    session, g, helper, _ = with_helper
    received, senders = [17] * 600, ["x" * 400] * 600
    real = helper.apply

    def apply(text: str) -> str:
        if len(helper.requests) == 1:
            helper.requests.append(text)
            raise native.ReplyLost("no reply within 3.0 s")
        return real(text)

    helper.apply = apply
    session.step(g, received, gap=0, senders=senders)
    step = session.step(g, received, gap=0, senders=senders)
    assert isinstance(step.helper_error, native.ReplyLost)
    shown = sum(line.startswith("popup") for r in helper.requests for line in r.splitlines())
    assert session.announced == shown < len(received), "the batches unsent are still to show"


def test_a_helper_that_doesnt_match_is_kept_for_the_poll_to_say(with_helper) -> None:
    session, g, helper, writes = with_helper
    mismatch = native.ModMismatch("both are version 0, but from different builds")

    def apply(text: str) -> str:
        raise mismatch

    helper.apply = apply
    session.step(g, [17], gap=0)
    assert session.step(g, [17], gap=0).helper_error is mismatch and writes == []


def test_popups_announce_each_received_item_once(with_helper) -> None:
    session, g, helper, _ = with_helper
    session.announced = 1  # the first had its popup before this connection (received offline)
    give(session, g, [17, 36], senders=["Bob", "Alice"])
    assert helper.popups() == ["popup 36 from Alice"]
    assert session.announced == 2
    session.step(g, [17, 36], gap=0, senders=["Bob", "Alice"])
    assert len(helper.requests) == 1, "nothing more to send: no repeat popup"


def test_received_scrap_pops_up_as_itself_once(with_helper) -> None:
    # The helper spawns a received scrap's drops after its popup (native/README.md, "Received
    # scrap"), so each keeps its own item there, though all three count as Scrap Metal.
    session, g, helper, _ = with_helper
    scrap = [ids.SCRAP_METAL, ids.STAND_IN, ids.STAND_IN_PROGRESSION]
    assert give(session, g, scrap, senders=["Bob"] * 3) == {ids.SCRAP_METAL: 3}
    assert helper.requests[-1].splitlines()[1:] == [
        "cas 46 0 3",
        "popup 46 from Bob",
        "popup 47 from Bob",
        "popup 48 from Bob",
    ]
    session.step(g, scrap, gap=0, senders=["Bob"] * 3)
    assert len(helper.requests) == 1 and session.announced == 3, "no popup, no drop, twice"


def test_a_received_trap_is_its_popup_alone_once(with_helper) -> None:
    # No byte: the helper sets the trap off with its popup (native/README.md,
    # "Traps"), so it rides on the popup count, as received scrap's drops do.
    session, g, helper, writes = with_helper
    traps = [ids.DISRUPTION_TRAP, ids.DISORIENT_TRAP]
    assert give(session, g, traps, senders=["Bob", "Alice"]) == {}
    assert helper.requests[-1].splitlines() == [
        "apply",
        "popup 102 from Bob",
        "popup 103 from Alice",
    ]
    assert session.announced == 2 and writes == []
    session.step(g, traps, gap=0, senders=["Bob", "Alice"])
    assert len(helper.requests) == 1, "never again"


def test_traps_held_at_the_first_connection_pop_up_once_before_the_rest(with_helper) -> None:
    # announced_tick counted all three as handed over (no stored count), keeping the trap.
    session, g, helper, _ = with_helper
    received, senders = [17, ids.DISORIENT_TRAP, 22], ["Bob", "Alice", "Dan"]
    session.announced, session.held_traps = 3, [1]
    session.step(g, received, gap=0, senders=senders)
    session.step(g, received, gap=0, senders=senders)
    assert helper.popups() == ["popup 103 from Alice"], "the trap alone, no flood of old popups"
    assert session.held_traps == [] and session.announced == 3
    session.step(g, received, gap=0, senders=senders)
    assert len(helper.requests) == 1, "once"


def test_a_second_copy_of_a_single_item_has_its_popup_but_no_write(with_helper) -> None:
    # Zenith Thrusters (26) sent again from the console: its byte stays 1.
    session, g, helper, _ = with_helper
    assert give(session, g, [26], senders=["Bob"]) == {26: 1}
    assert give(session, g, [26, 26], senders=["Bob", "Bob"]) == {}
    assert g.inv[26] == 1
    assert helper.requests[-1].splitlines()[1:] == ["popup 26 from Bob"]
    assert session.announced == 2


def shown(helper: FakeHelper) -> list[str]:
    """The popup and notice lines of the last batch, in order."""
    lines = helper.requests[-1].splitlines()
    return [line for line in lines if line.startswith(("popup", "notice"))]


def give_with(session, g, received, senders, followups) -> None:
    """Two polls giving ``received``, with the notices after their popups."""
    for _ in range(2):
        session.step(g, received, gap=0, senders=senders, followups=followups)


def test_each_received_disk_or_chip_pops_up_from_its_sender_then_its_note(with_helper) -> None:
    session, g, helper, _ = with_helper
    received, senders = [44, 49, 17, 44], ["Bob", "Alice", "Dan", "Carol"]
    followups = {
        0: "OS Install Disk 1 of 3",
        1: "HINT: SLIDE MOD AT GUARDIA 22,7",
        3: "OS Install Disk 2 of 3",
    }
    give_with(session, g, received, senders, followups)
    assert len(helper.requests) == 1, "in one batch"
    assert shown(helper) == [
        "popup 44 from Bob",
        "notice 44 OS Install Disk 1 of 3",
        "popup 49 from Alice",
        "notice 49 HINT: SLIDE MOD AT GUARDIA 22,7",
        "popup 17 from Dan",
        "popup 44 from Carol",
        "notice 44 OS Install Disk 2 of 3",
    ]
    assert session.announced == 4, "four items handed over, not seven popups"
    give_with(session, g, received, senders, followups)
    assert len(helper.requests) == 1, "nothing shown again"


def test_a_refused_batch_sends_both_of_a_disks_popups_again(with_helper) -> None:
    session, g, helper, _ = with_helper
    helper.reply = "refused not in play"
    give_with(session, g, [44], ["Bob"], {0: "OS Install Disk 1 of 3"})
    assert session.announced == 0
    helper.reply = "applied"
    session.step(g, [44], gap=0, senders=["Bob"], followups={0: "OS Install Disk 1 of 3"})
    assert len(helper.requests) == 2
    assert shown(helper) == ["popup 44 from Bob", "notice 44 OS Install Disk 1 of 3"]
    assert session.announced == 1


def test_nothing_is_given_through_the_helper_until_the_popup_count_is_known(setup) -> None:
    session, g = setup
    helper = FakeHelper(g)
    through(session, helper)
    assert give(session, g, [17], senders=["Alice"]) == {}
    assert helper.requests == [] and g.inv[17] == 0
    session.announced_read = True  # the server's count: no popup shown yet
    give(session, g, [17], senders=["Alice"])
    assert g.inv[17] == 1 and "popup 17 from Alice" in helper.requests[-1]


def test_a_received_weapon_already_owned_pops_up_as_its_upgrade(with_helper) -> None:
    session, g, helper, _ = with_helper
    # The Machine Gun is a starting item; the Force Blaster is not, until the first arrives.
    give(session, g, [3, 4, 4], senders=["Alice"] * 3)
    assert helper.popups() == ["popup 9 from Alice", "popup 4 from Alice", "popup 14 from Alice"]


def test_a_received_upgrade_with_guns_first_pops_up_as_the_byte_it_sets(with_helper) -> None:
    session, g, helper, _ = with_helper
    # A Pentashot sent from the server console is a Force Blaster's copy (reconcile): the first
    # sets the gun's byte, so it and the Force Blaster after it pop up as written.
    give(session, g, [14, 4], senders=["Alice"] * 2)
    assert helper.popups() == ["popup 4 from Alice", "popup 14 from Alice"]
    assert (g.inv[4], g.inv[14]) == (1, 1)


@pytest.mark.parametrize("chips", [True, False])
def test_a_seed_with_intel_chips_says_so_in_its_batches(chips: bool) -> None:
    # The helper names its Area Maps' popups Intel Chip from it, whatever it has been told since.
    session, g = ss.Session(PLAN, intel_chips=chips), FakeGame(PLAN)
    helper = FakeHelper(g)
    through(session, helper)
    session.announced_read = True
    give(session, g, [49], senders=["Bob"])
    lines = helper.requests[-1].splitlines()
    assert ("intel" in lines) is chips and lines[-1] == "popup 49 from Bob"


def test_without_guns_first_each_item_pops_up_as_itself() -> None:
    off = plan(options={10: 0, 42: 2, 72: 1})
    session, g = ss.Session(off), FakeGame(off)
    helper = FakeHelper(g)
    through(session, helper)
    session.announced_read = True
    # The Machine Gun is a starting item: a received Pulse Wave and Pentashot are themselves.
    give(session, g, [9, 14], senders=["Alice"] * 2)
    assert helper.popups() == ["popup 9 from Alice", "popup 14 from Alice"]
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
    give(session, g, [17])
    assert g.inv[17] == 1 and session.announced == 0, "before the count is read: left as it is"
    session.announced_read = True
    give(session, g, [17, 22])
    assert g.inv[22] == 1 and session.announced == 2


def test_direct_writes_drop_the_waiting_traps_and_say_so(setup, caplog) -> None:
    # No helper: a trap rides on its popup, so those held and new ones do nothing, once.
    session, g = setup
    session.announced_read = True
    received = [ids.DISORIENT_TRAP, 17, ids.DISRUPTION_TRAP]
    session.announced, session.held_traps = 2, [0]
    with caplog.at_level(logging.INFO, logger="Zexion"):
        give(session, g, received)
    assert session.held_traps == [] and session.announced == 3 and g.inv[17] == 1
    assert "do nothing: Disorient Trap, Disruption Trap." in caplog.text


def test_direct_writes_name_a_trap_once_when_it_is_held_and_past_the_count(setup, caplog) -> None:
    # After a restart between the record and the count stores, the trap is in both.
    session, g = setup
    session.announced_read = True
    received = [17, ids.DISRUPTION_TRAP]
    session.announced, session.held_traps = 1, [1]
    with caplog.at_level(logging.INFO, logger="Zexion"):
        give(session, g, received)
    assert "do nothing: Disruption Trap." in caplog.text


def test_a_lost_reply_part_way_clears_only_the_held_traps_it_may_have_handed_over(
    with_helper,
) -> None:
    session, g, helper, _ = with_helper
    received, senders = [ids.DISORIENT_TRAP] * 300, ["x" * 400] * 300

    def apply(text: str) -> str:
        helper.requests.append(text)
        raise native.ReplyLost("no reply within 3.0 s")

    helper.apply = apply
    session.announced, session.held_traps = len(received), list(range(len(received)))
    session.step(g, received, gap=0, senders=senders)
    session.step(g, received, gap=0, senders=senders)
    first = helper.requests[0].count("popup")
    assert 0 < first < len(received), "split in two"
    assert session.held_traps == list(range(first, len(received)))


def test_direct_writes_keep_the_waiting_traps_for_a_helper_about_to_replace_the_old(
    setup, caplog
) -> None:
    # The old helper refuses to unload only because a trap is running (keep_traps): the traps
    # received while the step goes on without a helper are held, not dropped, and the helper
    # that replaces it shows their popups first.
    session, g = setup
    session.announced_read, session.keep_traps = True, True
    received = [ids.DISORIENT_TRAP, 17, ids.DISRUPTION_TRAP]
    session.announced, session.held_traps = 2, [0]
    with caplog.at_level(logging.INFO, logger="Zexion"):
        give(session, g, received, senders=["Ann", "Bob", "Cy"])
    assert session.held_traps == [0, 2] and session.announced == 3 and g.inv[17] == 1
    assert "do nothing" not in caplog.text
    helper = FakeHelper(g)
    through(session, helper)
    session.keep_traps = False
    give(session, g, received, senders=["Ann", "Bob", "Cy"])
    assert helper.popups() == [
        f"popup {ids.DISORIENT_TRAP} from Ann",
        f"popup {ids.DISRUPTION_TRAP} from Cy",
    ]
    assert session.held_traps == [] and session.announced == 3


def test_the_endings_hold_carries_over_a_reconnect(setup) -> None:
    session, g = setup
    assert begin_ending(session, g).goal and session.holding
    after = reconnect(session)
    for _ in range(3):
        assert after.step(g, [17], gap=0).granted == {}
    assert g.inv[17] == 0
    g.flags[ENDING] = 0  # another save of the seed, before its ending, loaded meanwhile
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
    g.flags[ENDING] = 1  # it begins while disconnected, in the same load
    after = reconnect(session)
    after.step(g, [17], gap=0)
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
    g.flags[ENDING] = 1
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


# --- other players' items kept collected (Session.mark) --------------------------------------

# A seed whose location 3 holds another player's item (a stand-in, whose pickup gives one
# Scrap Metal) and location 5 another's progression item.
OTHERS_PLAN = plan(
    location_items=tuple(
        {3: ids.STAND_IN, 5: ids.STAND_IN_PROGRESSION}.get(i, i % 46) for i in range(sp.LOCATIONS)
    )
)


@pytest.fixture
def others():
    g = FakeGame(OTHERS_PLAN)
    g.cell = (40, 40)  # the player far from the locations marked here (ss.NEAR_CELLS)
    return ss.Session(OTHERS_PLAN), g


def at(i: int) -> int:
    x, y = CELLS[i]
    return y * 54 + x


def marked(g: FakeGame, i: int) -> bool:
    """Whether location i reads as a pickup leaves it: no item made, and collected on the map."""
    return g.items[at(i)] == game.ITEM_COLLECTED and g.grid[at(i)] == 0x1B


def test_a_checked_item_of_another_player_is_marked_collected_on_the_second_poll(others) -> None:
    session, g = others
    first = session.step(g, [], gap=0, gone={3})
    assert not first.marked and not marked(g, 3), "one poll alone marks nothing"
    second = session.step(g, [], gap=0, gone={3})
    assert second.marked == {3} and marked(g, 3)
    assert g.items[at(5)] == game.ITEM_THERE and g.grid[at(5)] == 0, "only what the server has"
    third = session.step(g, [], gap=0, gone={3})
    assert third.checked == {3} and not third.marked, "it reads as collected from then on"


def test_a_reload_from_before_is_marked_again(others) -> None:
    session, g = others
    session.step(g, [], gap=0, gone={3})
    session.step(g, [], gap=0, gone={3})
    g.items[at(3)], g.grid[at(3)] = game.ITEM_THERE, 0x0D  # the save from before, hinted
    session.step(g, [], gap=0, gone={3})
    assert session.step(g, [], gap=0, gone={3}).marked == {3} and marked(g, 3)


def test_a_marked_cell_gives_no_scrap_metal(others) -> None:
    session, g = others
    scrap = g.inv[ids.SCRAP_METAL]
    for _ in range(6):
        assert not session.step(g, [], gap=0, gone={3}).granted
    assert marked(g, 3) and g.inv[ids.SCRAP_METAL] == scrap, "only a pickup gives its own"


def test_a_stand_in_picked_up_still_counts_beside_the_marked_ones(others) -> None:
    session, g = others
    for _ in range(3):
        session.step(g, [], gap=0, gone={3})
    assert marked(g, 3)
    g.collect(5)  # the other stand-in, picked up: the game gives its one Scrap Metal
    scrap = g.inv[ids.SCRAP_METAL]
    for _ in range(3):
        assert not session.step(g, [], gap=0, gone={3}).granted
    # A Scrap Metal received on top is owed, the picked-up stand-in's not standing in for it.
    for _ in range(3):
        session.step(g, [ids.SCRAP_METAL], gap=0, gone={3})
    assert g.inv[ids.SCRAP_METAL] == scrap + 1
    # A death takes the received one's byte back: given again, once.
    g.inv[ids.SCRAP_METAL] = scrap
    for _ in range(3):
        session.step(g, [ids.SCRAP_METAL], gap=0, gone={3})
    assert g.inv[ids.SCRAP_METAL] == scrap + 1


def test_a_counter_that_fails_to_read_fails_the_poll_and_is_not_no_counter(others) -> None:
    # Only a counter not found in this build reads as None. A read that fails in a running
    # game must not: every marked cell would count as picked up, and its Scrap Metal be given.
    session, g = others
    for _ in range(3):
        session.step(g, [], gap=0, gone={3})
    assert marked(g, 3)

    def failing() -> int:
        raise game.GameError("read at 0x1234 failed in a running game")

    g.pickup_counter = failing
    scrap = g.inv[ids.SCRAP_METAL]
    for _ in range(3):
        with pytest.raises(game.GameError, match="failed in a running game"):
            session.step(g, [], gap=0, gone={3})
    assert g.inv[ids.SCRAP_METAL] == scrap


def test_a_stand_in_picked_up_then_lost_to_a_death_is_marked_and_gives_none(others) -> None:
    session, g = others
    g.collect(3)  # picked up: the game gives its Scrap Metal, and the check is sent
    assert session.step(g, [], gap=0, gone=set()).checked == {3}
    scrap = g.inv[ids.SCRAP_METAL]
    # A death before the game saved: the cell, the byte and the counter are the save's again.
    g.grid[at(3)], g.items[at(3)], g.pickups = 0, game.ITEM_THERE, 0
    g.inv[ids.SCRAP_METAL] = scrap - 1
    for _ in range(4):
        session.step(g, [], gap=0, gone={3})  # the server has it as checked
    assert marked(g, 3) and g.inv[ids.SCRAP_METAL] == scrap - 1, "marked: no Scrap Metal"


def test_a_save_marked_under_the_old_rule_keeps_its_scrap_metal(others) -> None:
    session, g = others
    g.grid[at(3)], g.items[at(3)] = 0x1B, game.ITEM_COLLECTED  # marked by an older client,
    g.inv[ids.SCRAP_METAL] += 1  # which gave the Scrap Metal a pickup would have
    scrap = g.inv[ids.SCRAP_METAL]
    for _ in range(3):
        assert not session.step(g, [], gap=0, gone={3}).granted
    assert g.inv[ids.SCRAP_METAL] == scrap, "nothing is taken away, and nothing given again"
    # That surplus stands in for a Scrap Metal received later: the byte stays.
    for _ in range(3):
        assert not session.step(g, [ids.SCRAP_METAL], gap=0, gone={3}).granted
    assert g.inv[ids.SCRAP_METAL] == scrap


def test_nothing_is_marked_while_the_pickup_counter_cannot_be_read(others, caplog) -> None:
    session, g = others
    g.pickups = None
    with caplog.at_level(logging.WARNING, logger="Zexion"):
        for _ in range(3):
            step = session.step(g, [], gap=0, gone={3})
            assert not step.marked and step.seed_matches
    assert g.items[at(3)] == game.ITEM_THERE and g.grid[at(3)] == 0
    assert sum("pickup counter was not found" in r.message for r in caplog.records) == 1
    # A cell marked before (or picked up: they cannot be told apart) counts as picked up.
    g.collect(5)
    g.inv[ids.SCRAP_METAL] -= 1
    scrap = g.inv[ids.SCRAP_METAL]
    for _ in range(3):
        session.step(g, [], gap=0, gone={3})
    assert g.inv[ids.SCRAP_METAL] == scrap + 1


def in_the_room(g: FakeGame, i: int) -> None:
    """Location i's item made by the game in a room loaded around the player (a reload at a
    save point beside it)."""
    x, y = CELLS[i]
    g.cell = (y, x)
    g.loaded[at(i)] = 1
    g.live.add(CELLS[i])


def test_an_item_object_in_the_loaded_rooms_is_ended_by_its_state_and_marked(others) -> None:
    session, g = others
    in_the_room(g, 3)
    assert not session.step(g, [], gap=0, gone={3}).marked
    assert g.items[at(3)] == game.ITEM_THERE and CELLS[3] in g.live, "two polls, as for any"
    # The state written, the game's update ends the object; the map cell follows at once.
    assert session.step(g, [], gap=0, gone={3}).marked == {3} and marked(g, 3)
    assert CELLS[3] not in g.live_item_cells()
    assert not session.step(g, [], gap=0, gone={3}).marked


def test_the_map_cell_waits_for_an_item_object_the_game_has_not_ended_yet(others) -> None:
    session, g = others
    in_the_room(g, 3)
    g.ends_items = False
    session.step(g, [], gap=0, gone={3})
    for _ in range(2):
        assert not session.step(g, [], gap=0, gone={3}).marked
        assert g.items[at(3)] == game.ITEM_COLLECTED and g.grid[at(3)] == 0, "never under it"
    # Ended and freed between two polls, the room still loaded around the player: marked, as
    # the cell is known to have had an item object of its own.
    g.live.clear()
    assert session.step(g, [], gap=0, gone={3}).marked == {3} and marked(g, 3)


def test_a_save_death_or_reload_forgets_which_cells_had_an_item_object(others) -> None:
    session, g = others
    in_the_room(g, 3)
    g.ends_items = False
    session.step(g, [], gap=0, gone={3})
    session.step(g, [], gap=0, gone={3})
    g.live.clear()
    g.stamp = bytes([0, 0, 0, 0, 9]) + bytes(9)  # the same slot, saved again
    for _ in range(3):
        assert not session.step(g, [], gap=0, gone={3}).marked, "a loaded cell with no object"
    g.loaded[at(3)] = 0
    g.cell = (40, 40)
    assert session.step(g, [], gap=0, gone={3}).marked == {3}


def test_an_item_picked_up_before_the_game_ends_it_counts_once(others) -> None:
    session, g = others
    in_the_room(g, 3)
    g.ends_items = False
    scrap = g.inv[ids.SCRAP_METAL]
    session.step(g, [], gap=0, gone={3})
    session.step(g, [], gap=0, gone={3})  # the state written under it
    g.collect(3)  # the player takes it in that frame: the game records the pickup
    for _ in range(4):
        assert not session.step(g, [], gap=0, gone={3}).marked
    assert marked(g, 3) and g.inv[ids.SCRAP_METAL] == scrap + 1


@pytest.mark.parametrize("held", [2, 3])
def test_an_item_an_object_holds_or_has_dropped_is_left(others, held) -> None:
    session, g = others
    g.items[at(3)] = held
    for _ in range(3):
        assert not session.step(g, [], gap=0, gone={3}).marked
    assert g.items[at(3)] == held and g.grid[at(3)] == 0
    g.items[at(3)] = game.ITEM_THERE  # its room left: the game puts the state back
    assert session.step(g, [], gap=0, gone={3}).marked == {3}


def test_a_room_loaded_between_the_look_and_the_write_gets_its_map_cell_later(others) -> None:
    session, g = others
    g.spawn.add(CELLS[3])
    session.step(g, [], gap=0, gone={3})
    step = session.step(g, [], gap=0, gone={3})
    # The state is written, so the game ends the item it had just made; the map cell is not:
    # the cell reads loaded, and no item object of its own was seen there before the write.
    assert not step.marked and g.items[at(3)] == game.ITEM_COLLECTED and g.grid[at(3)] == 0
    assert CELLS[3] not in g.live_item_cells()
    scrap = g.inv[ids.SCRAP_METAL]
    assert not session.step(g, [], gap=0, gone={3}).marked, "its cell still reads loaded"
    g.loaded[at(3)] = 0
    assert session.step(g, [], gap=0, gone={3}).marked == {3} and marked(g, 3)
    for _ in range(3):
        session.step(g, [], gap=0, gone={3})
    assert g.inv[ids.SCRAP_METAL] == scrap, "marked, not picked up: no Scrap Metal"


def test_a_map_cell_the_game_changed_since_the_look_is_marked_by_a_later_poll(others) -> None:
    session, g = others
    session.step(g, [], gap=0, gone={3})
    real = g.set_map_collected

    def changed(cell, expected):
        g.grid[at(3)] = 0x19  # the room's reveal, between the look and the write
        real(cell, expected)

    g.set_map_collected = changed
    assert not session.step(g, [], gap=0, gone={3}).marked
    assert g.items[at(3)] == game.ITEM_COLLECTED and g.grid[at(3)] == 0x19
    g.set_map_collected = real
    assert session.step(g, [], gap=0, gone={3}).marked == {3} and marked(g, 3)


def test_nothing_is_marked_out_of_play_or_in_another_save(others) -> None:
    session, g = others
    session.step(g, [], gap=0, gone={3})
    g.state_value = game.MENU
    assert not session.step(g, [], gap=0, gone={3}).marked
    g.state_value = game.IN_PLAY
    assert not session.step(g, [], gap=0, gone={3}).marked, "two polls in play in a row again"
    g.marker = 0  # a normal save loaded: the section in memory is still the seed's
    assert not session.step(g, [], gap=0, gone={3}).marked
    g.marker = 1
    g.blob = bytes(section(PLAN))  # a save of another seed with the same locations
    for _ in range(3):
        assert not session.step(g, [], gap=0, gone={3}).marked
    assert not marked(g, 3) and g.items[at(3)] == game.ITEM_THERE
    g.blob = bytes(section(OTHERS_PLAN))
    session.step(g, [], gap=0, gone={3})
    assert session.step(g, [], gap=0, gone={3}).marked == {3}


def test_nothing_is_marked_once_the_ending_began(others) -> None:
    session, g = others
    begin_ending(session, g)
    for _ in range(3):
        assert not session.step(g, [], gap=0, gone={3}).marked
    assert not marked(g, 3)


def test_a_helper_to_attach_again_holds_the_grants_not_the_marking(others) -> None:
    session, g = others
    session.step(g, [17], gap=0, gone={3}, give=False)
    assert session.step(g, [17], gap=0, gone={3}, give=False).marked == {3}
    for _ in range(3):
        session.step(g, [17], gap=0, gone={3}, give=False)
    assert g.inv[17] == 0, "the received item waits"
    for _ in range(2):
        session.step(g, [17], gap=0, gone={3})
    assert g.inv[17] == 1


def test_locations_the_seed_does_not_list_are_not_marked(others) -> None:
    session, g = others
    for _ in range(2):
        assert not session.step(g, [], gap=0, gone={-1, sp.LOCATIONS, 999}).marked
    assert not any(g.items) and not any(g.grid)


@pytest.mark.parametrize("state", [game.MENU, game.DEAD, game.NO_SAVE])
def test_a_poll_out_of_play_starts_the_two_polls_over(others, state) -> None:
    session, g = others
    session.step(g, [], gap=0, gone={3})
    g.state_value = state
    assert not session.step(g, [], gap=0, gone={3}).marked
    g.state_value = game.IN_PLAY
    assert not session.step(g, [], gap=0, gone={3}).marked and not marked(g, 3)
    assert session.step(g, [], gap=0, gone={3}).marked == {3}


@pytest.mark.parametrize("between", ["connected", "hold_ending", "leave"])
def test_a_new_connection_or_load_starts_the_two_polls_over(others, between) -> None:
    session, g = others
    session.step(g, [], gap=0, gone={3})
    getattr(session, between)()
    session.holding = False  # hold_ending's hold itself is tested above
    assert not session.step(g, [], gap=0, gone={3}).marked, "one poll of the new one alone"
    assert session.step(g, [], gap=0, gone={3}).marked == {3}


def test_the_two_polls_are_of_one_save_slot_and_close_together(others, monkeypatch) -> None:
    session, g = others
    clock = [100.0]
    monkeypatch.setattr(ss.time, "monotonic", lambda: clock[0])
    session.step(g, [], gap=0, gone={3})
    clock[0] += ss.MARK_AGREE_SECONDS + 1  # the polls paused: the helper attached again, say
    assert not session.step(g, [], gap=0, gone={3}).marked, "too long since the poll before"
    g.stamp = bytes([5]) + bytes(13)  # another slot of the seed, loaded during such a pause
    clock[0] += 1
    assert not session.step(g, [], gap=0, gone={3}).marked, "another save slot"
    clock[0] += 1
    assert session.step(g, [], gap=0, gone={3}).marked == {3} and marked(g, 3)


def test_the_cells_around_the_player_are_left(others) -> None:
    session, g = others
    x, y = CELLS[3]
    # The player state gives the game's (X, Y): this repo's (y, x). An object of another kind
    # may be the item there (a reversed-gravity room's), which the item pool does not show.
    for dx, dy in ((0, 0), (ss.NEAR_CELLS, 0), (-ss.NEAR_CELLS, ss.NEAR_CELLS)):
        g.cell = (y + dy, x + dx)
        for _ in range(3):
            assert not session.step(g, [], gap=0, gone={3}).marked
    assert g.items[at(3)] == game.ITEM_THERE and g.grid[at(3)] == 0
    g.cell = (y, x + ss.NEAR_CELLS + 1)  # far enough for its room to be unloaded
    assert session.step(g, [], gap=0, gone={3}).marked == {3} and marked(g, 3)


def test_a_cell_the_game_has_loaded_is_left(others) -> None:
    session, g = others
    # The game's own word that the cell's objects are loaded, wherever the player stands and
    # whatever kind of object the item is there (none of the item pool's here).
    g.loaded[at(3)] = 1
    for _ in range(3):
        assert not session.step(g, [], gap=0, gone={3, 5}).marked - {5}
    assert g.items[at(3)] == game.ITEM_THERE and g.grid[at(3)] == 0 and marked(g, 5)
    g.loaded[at(3)] = 0  # the room left
    session.step(g, [], gap=0, gone={3, 5})
    assert marked(g, 3)


def test_a_cell_loaded_between_the_two_writes_keeps_its_map_cell(others) -> None:
    session, g = others
    session.step(g, [], gap=0, gone={3})
    real = g.set_item_collected

    def loads_right_after(cell: tuple[int, int]) -> None:
        real(cell)
        g.loaded[at(3)] = 1  # no item object: an item of another kind may be there

    g.set_item_collected = loads_right_after
    assert not session.step(g, [], gap=0, gone={3}).marked
    assert g.items[at(3)] == game.ITEM_COLLECTED and g.grid[at(3)] == 0
    g.loaded[at(3)] = 0
    assert session.step(g, [], gap=0, gone={3}).marked == {3} and marked(g, 3)


def test_an_item_state_that_changed_since_the_look_is_not_written(others) -> None:
    session, g = others
    session.step(g, [], gap=0, gone={3, 5})
    real = g.cell_items
    calls = []

    def taken_after_the_look() -> bytes:
        states = real()
        calls.append(1)
        if len(calls) == 2:  # the read right before the writes: an object takes the item next
            g.items[at(3)] = 2
        return states

    g.cell_items = taken_after_the_look
    step = session.step(g, [], gap=0, gone={3, 5})
    # The compare-and-set refuses: nothing of that cell is written, the other is marked.
    assert step.marked == {5} and g.items[at(3)] == 2 and g.grid[at(3)] == 0
    assert not session.step(g, [], gap=0, gone={3, 5}).marked, "held by an object: left"


@pytest.mark.parametrize("change", ["stamp", "state", "marker", "no stamp", "teleport"])
def test_the_save_is_checked_again_right_before_the_writes(others, change) -> None:
    _, g = others
    stamp = g.stamp
    if change == "stamp":
        g.stamp = bytes([1]) + bytes(13)  # a save, a death or a reload since the look
    elif change == "state":
        g.state_value = game.MENU
    elif change == "marker":
        g.marker = 0
    elif change == "teleport":
        g.timer = 53  # a TELEPORT TO counting down: in play, the stamp unchanged
    else:
        stamp = b""
    assert ss.mark_collected(g, bytes(g.grid), CELLS, [3], stamp) == set()
    assert g.items[at(3)] == game.ITEM_THERE and g.grid[at(3)] == 0, "nothing written"
    g.stamp, g.state_value, g.marker, g.timer = bytes(14), game.IN_PLAY, 1, 0
    assert ss.mark_collected(g, bytes(g.grid), CELLS, [3], bytes(14)) == {3} and marked(g, 3)


def test_a_teleport_counting_down_marks_nothing_until_its_load_is_over(others) -> None:
    session, g = others
    session.step(g, [], gap=0, gone={3})
    g.timer = 60  # TELEPORT TO START chosen: a second that reads as play, then its load
    for _ in range(3):
        assert not session.step(g, [], gap=0, gone={3}).marked
    assert g.items[at(3)] == game.ITEM_THERE and g.grid[at(3)] == 0, "nothing written"
    g.timer = 0
    assert session.step(g, [], gap=0, gone={3}).marked == {3} and marked(g, 3)


def test_a_mark_a_load_that_reads_as_play_cut_is_not_reported_and_is_finished(others) -> None:
    session, g = others
    session.step(g, [], gap=0, gone={3})
    write = g.set_map_collected

    def state_taken_back(cell, expected):
        write(cell, expected)
        g.items[at(3)] = game.ITEM_THERE  # the load copied the state from before the mark

    g.set_map_collected = state_taken_back
    step = session.step(g, [], gap=0, gone={3})
    assert not step.marked, "the map cell reads collected over an item that is there"
    assert g.grid[at(3)] == 0x1B and g.items[at(3)] == game.ITEM_THERE
    g.set_map_collected = write
    assert session.step(g, [], gap=0, gone={3}).marked == {3} and marked(g, 3)
    for _ in range(3):
        session.step(g, [], gap=0, gone={3})
    assert g.inv[ids.SCRAP_METAL] == 0, "and no Scrap Metal for it, cut or finished"


def test_a_teleport_begun_under_the_last_write_reports_nothing(others) -> None:
    session, g = others
    session.step(g, [], gap=0, gone={3})
    write = g.set_map_collected

    def chosen_then(cell, expected):
        write(cell, expected)
        g.timer = 60

    g.set_map_collected = chosen_then
    assert not session.step(g, [], gap=0, gone={3}).marked


def test_the_players_cell_is_read_again_right_before_the_writes(others) -> None:
    session, g = others
    session.step(g, [], gap=0, gone={3})
    states = g.cell_items

    def walked_up_since_the_look() -> bytes:
        x, y = CELLS[3]
        g.cell = (y, x + 1)  # the game's (X, Y): the cell next to the location's
        return states()

    g.cell_items = walked_up_since_the_look
    assert not session.step(g, [], gap=0, gone={3}).marked
    assert g.items[at(3)] == game.ITEM_THERE and g.grid[at(3)] == 0, "left: its item may be here"
    g.cell_items = states
    g.cell = (40, 40)
    assert session.step(g, [], gap=0, gone={3}).marked == {3}


def test_a_load_beginning_under_the_marking_counts_nothing_as_marked(others) -> None:
    session, g = others
    session.step(g, [], gap=0, gone={3})
    g.load_after_writes = True
    assert not session.step(g, [], gap=0, gone={3}).marked
    assert not session.step(g, [], gap=0, gone={3}).marked  # the load, not in play
    g.load_after_writes, g.state_value = False, game.IN_PLAY
    g.items[at(3)], g.grid[at(3)] = game.ITEM_THERE, 0  # it put the item back
    assert not session.step(g, [], gap=0, gone={3}).marked, "two polls again"
    assert session.step(g, [], gap=0, gone={3}).marked == {3} and marked(g, 3)


def test_nothing_is_marked_while_the_slots_stamp_does_not_read(others) -> None:
    session, g = others
    g.stamp = b""
    for _ in range(3):
        assert not session.step(g, [], gap=0, gone={3}).marked
    assert g.items[at(3)] == game.ITEM_THERE and g.grid[at(3)] == 0
    g.stamp = bytes(14)
    session.step(g, [], gap=0, gone={3})
    assert session.step(g, [], gap=0, gone={3}).marked == {3}


def test_a_room_that_waits_for_its_pickup_is_never_marked(others, monkeypatch) -> None:
    session, g = others
    # Exodia X20 Y0, in game 2026-11-14: marked from afar, the boss fight its pickup starts
    # never began and the room could not be left. Its item stays, to pick up.
    monkeypatch.setattr(ids, "PICKUP_ROOMS", frozenset({CELLS[3]}))
    for _ in range(3):
        session.step(g, [], gap=0, gone={3, 5})
    assert g.items[at(3)] == game.ITEM_THERE and g.grid[at(3)] == 0, "nothing written"
    assert marked(g, 5), "any other square is marked as before"
    # Nor with its item made already in the room around the player.
    in_the_room(g, 3)
    for _ in range(3):
        assert not session.step(g, [], gap=0, gone={3, 5}).marked
    assert g.items[at(3)] == game.ITEM_THERE and g.grid[at(3)] == 0 and CELLS[3] in g.live
    # Picked up as usual, it counts once, and is not marked again after.
    scrap = g.inv[ids.SCRAP_METAL]
    g.collect(3)
    for _ in range(3):
        assert not session.step(g, [], gap=0, gone={3, 5}).marked
    assert g.inv[ids.SCRAP_METAL] == scrap + 1


def test_only_a_pickup_rooms_item_to_keep_collected_reads_no_cell_state(
    others, monkeypatch
) -> None:
    session, g = others
    monkeypatch.setattr(ids, "PICKUP_ROOMS", frozenset({CELLS[3]}))
    g.cell_items = None  # not read: nothing of what is gone can be marked
    for _ in range(3):
        assert not session.step(g, [], gap=0, gone={3}).marked


def test_the_rooms_listed_are_exodia_x20_y0_and_rtalva_x34_y51() -> None:
    # The map grid's order: this repo's (x, y) is the game's (Y, X).
    assert {(0, 20), (51, 34)} == ids.PICKUP_ROOMS


def test_a_mark_a_load_cut_in_two_is_finished(others) -> None:
    session, g = others
    # The map cell collected over an item still there: a mark whose state a load took back
    # between its two writes (the game's own pickup never leaves the two so).
    g.grid[at(3)] = 0x1B
    first = session.step(g, [], gap=0, gone={3})
    assert first.checked == {3} and not first.marked
    assert session.step(g, [], gap=0, gone={3}).marked == {3} and marked(g, 3)
    assert not session.step(g, [], gap=0, gone={3}).marked


def test_a_failing_read_of_the_marking_holds_back_no_check_or_grant(others, caplog) -> None:
    session, g = others

    def broken() -> bytes:
        raise game.GameError("short read")

    g.cell_items = broken
    g.collect(7)
    caplog.set_level(logging.WARNING, logger="Zexion")
    steps = [session.step(g, [17], gap=0, gone={3}) for _ in range(3)]
    assert all(step.checked == {7} and not step.marked for step in steps)
    assert g.inv[17] == 1, "the received item is given"
    said = [r.message for r in caplog.records if "mark" in r.message]
    assert len(said) == 1 and "items and checks still work" in said[0]


def test_the_game_closing_in_the_marking_is_raised(others) -> None:
    session, g = others

    def closed() -> bytes:
        raise game.GameNotRunning("the game exited")

    g.cell_items = closed
    with pytest.raises(game.GameNotRunning):
        session.step(g, [], gap=0, gone={3})


def test_nothing_is_read_for_the_marking_with_nothing_to_keep_collected(others) -> None:
    session, g = others
    g.cell_items = None  # not called
    assert not session.step(g, [], gap=0).marked
