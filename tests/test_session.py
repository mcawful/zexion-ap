"""client/session.py against a fake game (no process, no AP)."""

from __future__ import annotations

import struct

import pytest
from test_save_patch import fresh, plan

import game
import save_patch as sp
import session as ss

CELLS = [(i % 54, i // 54) for i in range(sp.LOCATIONS)]  # distinct, sorted by y then x


def blob_for(p: sp.SeedPlan) -> bytes:
    save = fresh()
    for i, (x, y) in enumerate(CELLS):
        struct.pack_into("<HH", save, sp.SECTION + 4 * i, x, y)
    out = sp.patch(bytes(save), p)
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

    def state(self) -> str:
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


def give(session: ss.Session, g: FakeGame, received: list[int]) -> dict[int, int]:
    session.step(g, received, gap=0)
    return session.step(g, received, gap=0).granted


@pytest.fixture
def setup():
    return ss.Session(PLAN, [list(c) for c in CELLS]), FakeGame(PLAN)


def test_collected_cells_are_reported_as_checks(setup) -> None:
    session, g = setup
    g.collect(5)
    g.collect(60)
    step = session.step(g, [], gap=0)
    assert step.checked == {5, 60} and step.seed_matches and not step.goal


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


def test_a_native_pickup_is_not_given_again(setup) -> None:
    session, g = setup
    g.collect(0)  # location 0 holds item 0, an Energy Tank
    assert session.step(g, [], gap=0).granted == {}


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
    g.inv[ss.GOAL_ITEM] = 1
    step = session.step(g, [17], gap=0)
    assert step.granted == {} and g.inv[17] == 0
    assert step.checked == {5} and step.goal


def test_nothing_happens_while_the_state_is_changing(setup) -> None:
    session, g = setup
    g.flicker = True
    g.collect(5)
    assert session.step(g, [17], gap=0) == ss.Step()


def test_another_seed_gets_nothing(setup) -> None:
    session, g = setup
    g.blob = blob_for(plan(start=(6, 1, 1)))
    g.collect(5)
    assert session.step(g, [17], gap=0) == ss.Step()
    assert g.inv[17] == 0


def test_a_normal_save_loaded_after_this_seed_gets_nothing(setup) -> None:
    # The game keeps the last randomizer section in memory when a normal save loads.
    session, g = setup
    g.marker = 0
    g.collect(5)
    assert session.step(g, [17], gap=0) == ss.Step()
    assert session.step(g, [17], gap=0) == ss.Step()
    assert g.inv[17] == 0


def test_no_save_loaded_gets_nothing(setup) -> None:
    session, g = setup
    g.state_value = game.NO_SAVE
    assert session.step(g, [17], gap=0) == ss.Step()


def test_a_grant_the_game_changed_first_is_left_for_the_next_poll(setup) -> None:
    session, g = setup
    real = g.set_item

    def game_got_there_first(item, expected, value):
        g.inv[item] += 1
        real(item, expected, value)

    g.set_item = game_got_there_first
    assert give(session, g, [17]) == {}
