"""client/shields.py: when a received Energy Shield is spawned again and when it counts (no AP)."""

from __future__ import annotations

from client import game, shields


def test_a_spawned_shield_counts_once_seen_up_in_play() -> None:
    assert shields.shield_outcome(True, up=True, left_play=False, overdue=False) == shields.UP
    # overdue does not matter once it is up
    assert shields.shield_outcome(True, up=True, left_play=False, overdue=True) == shields.UP


def test_a_spawned_shield_not_seen_waits_then_is_spawned_again() -> None:
    assert shields.shield_outcome(True, up=False, left_play=False, overdue=False) == shields.WAIT
    assert shields.shield_outcome(True, up=False, left_play=False, overdue=True) == shields.AGAIN


def test_a_paused_game_waits_for_play_however_long_it_takes() -> None:
    for up in (False, True):
        for overdue in (False, True):
            assert shields.shield_outcome(False, up, False, overdue) == shields.WAIT


def test_a_death_or_unload_since_the_spawn_means_spawning_again() -> None:
    # a shield up afterwards may be the reloaded save's own, not the one spawned
    for in_play in (False, True):
        for up in (False, True):
            assert shields.shield_outcome(in_play, up, True, overdue=False) == shields.AGAIN


def test_the_count_given_is_the_servers_or_this_clients_if_ahead() -> None:
    sh = shields.Shields(given=3)
    assert sh.given_count(1) == 3
    assert sh.given_count(5) == 5
    assert shields.Shields().given_count(1) == 1


def test_the_count_is_sent_again_until_the_server_has_it() -> None:
    sh = shields.Shields(given=2, count_sent_at=10.0)
    assert not sh.resend_due(1, 10.0 + shields.GAP_SECONDS - 0.1)
    assert sh.resend_due(1, 10.0 + shields.GAP_SECONDS)
    assert not sh.resend_due(2, 99.0) and sh.given is None, "the server has it"


def test_a_shield_seen_up_in_play_counts_once() -> None:
    sh = shields.Shields(misses=2)
    sh.spawned(0.0)
    assert sh.looked(sh.pending, game.IN_PLAY, True, 4, 1.0) == shields.UP
    assert sh.given == 5 and sh.pending is None and sh.misses == 0


def test_a_shield_not_seen_in_time_is_spawned_again_then_stops() -> None:
    sh = shields.Shields()
    now = 0.0
    for n in range(shields.MISSES):
        assert sh.may_spawn(now)
        sh.spawned(now)
        assert sh.looked(sh.pending, game.IN_PLAY, False, 0, now + 1) == shields.WAIT
        now += shields.SEEN_SECONDS + 1
        outcome = sh.looked(sh.pending, game.IN_PLAY, False, 0, now)
        assert outcome == (shields.STOP if n == shields.MISSES - 1 else shields.AGAIN)
        now += shields.GAP_SECONDS
    # each spawn not seen may be a shield given twice: none more in this client run
    assert sh.stopped and not sh.may_spawn(now + 1e6)


def test_a_shield_seen_up_resets_the_misses() -> None:
    sh = shields.Shields()
    for _ in range(shields.MISSES - 1):
        sh.spawned(0.0)
        assert sh.looked(sh.pending, game.IN_PLAY, False, 0, 10.0) == shields.AGAIN
    sh.spawned(0.0)
    assert sh.looked(sh.pending, game.IN_PLAY, True, 0, 1.0) == shields.UP
    sh.spawned(0.0)
    assert sh.looked(sh.pending, game.IN_PLAY, False, 1, 10.0) == shields.AGAIN
    assert not sh.stopped


def test_a_shield_is_looked_for_from_the_request_until_refused() -> None:
    sh = shields.Shields()
    sh.asking(0.0)
    assert sh.pending is not None, "a lost reply leaves it to be looked for"
    sh.refused("refused not settled", 0.5)
    assert sh.pending is None and not sh.may_spawn(1.0)


def test_the_time_to_see_it_runs_in_play_only() -> None:
    sh = shields.Shields()
    sh.spawned(0.0)
    pending = sh.pending
    assert sh.looked(pending, game.MENU, False, 0, 50.0) == shields.WAIT
    assert sh.looked(pending, game.IN_PLAY, False, 0, 51.0) == shields.WAIT


def test_a_death_or_unload_since_the_spawn_spawns_it_again() -> None:
    sh = shields.Shields()
    sh.spawned(0.0)
    pending = sh.pending
    sh.left()
    assert sh.looked(pending, game.IN_PLAY, True, 0, 1.0) == shields.AGAIN
    sh.spawned(2.0)
    assert sh.looked(sh.pending, game.DEAD, False, 0, 3.0) == shields.AGAIN


def test_a_refusal_is_said_once_and_not_for_an_unsettled_game() -> None:
    sh = shields.Shields()
    assert sh.refused("refused no frame", 0.0)
    assert not sh.refused("refused no frame", 1.0)
    assert not sh.refused("refused not settled", 2.0)
    assert sh.retry_at == 2.0 + shields.GAP_SECONDS


class ShieldGame:
    def __init__(self, up: bool = False, error: Exception | None = None):
        self.up, self.error = up, error

    def shield_up(self) -> bool:
        if self.error is not None:
            raise self.error
        return self.up


def test_the_look_reads_the_shield_in_play_only_and_a_failed_read_is_not_seen() -> None:
    assert shields.look(ShieldGame(up=True), game.IN_PLAY) == (True, "")
    assert shields.look(ShieldGame(up=True), game.MENU) == (False, "")
    broken = ShieldGame(error=game.GameError("bad link"))
    assert shields.look(broken, game.IN_PLAY) == (False, "bad link")


def test_a_shield_taken_back_by_a_death_or_unload_is_not_a_miss() -> None:
    sh = shields.Shields()
    for n in range(shields.MISSES * 2):
        sh.spawned(n * 10.0)
        sh.left()
        assert sh.looked(sh.pending, game.IN_PLAY, False, 0, n * 10.0 + 1) == shields.AGAIN
    assert sh.misses == 0 and not sh.stopped


def test_a_request_that_never_reached_the_helper_is_dropped_not_missed() -> None:
    sh = shields.Shields()
    sh.asking(0.0)
    sh.not_sent(0.5)
    assert sh.pending is None and sh.misses == 0 and not sh.may_spawn(1.0)
    assert sh.may_spawn(0.5 + shields.GAP_SECONDS)
