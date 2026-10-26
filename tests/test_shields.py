"""client/shields.py: when a received Energy Shield is spawned again and when it counts (no AP)."""

from __future__ import annotations

from client import game, shields

KEY, OTHER = "zexion_shields_0_1", "zexion_shields_0_2"


def test_the_count_given_is_the_servers_or_this_clients_if_ahead() -> None:
    sh = shields.Shields(given=(KEY, 3))
    assert sh.given_count(KEY, 1) == 3
    assert sh.given_count(KEY, 5) == 5
    assert sh.given_count(OTHER, 1) == 1, "another slot's count is the server's"


def test_the_count_is_sent_again_until_the_server_has_it() -> None:
    sh = shields.Shields(given=(KEY, 2), count_sent_at=10.0)
    assert not sh.resend_due(KEY, 1, 10.0 + shields.GAP_SECONDS - 0.1)
    assert sh.resend_due(KEY, 1, 10.0 + shields.GAP_SECONDS)
    assert not sh.resend_due(KEY, 2, 99.0) and sh.given is None, "the server has it"


def test_a_shield_spawned_for_another_slot_is_dropped() -> None:
    sh = shields.Shields()
    sh.spawned(OTHER, 0.0)
    assert sh.current(KEY) is None and sh.pending is None


def test_a_shield_seen_up_in_play_counts_once() -> None:
    sh = shields.Shields(misses=2)
    sh.spawned(KEY, 0.0)
    assert sh.looked(sh.pending, game.IN_PLAY, True, KEY, 4, 1.0) == shields.UP
    assert sh.given == (KEY, 5) and sh.pending is None and sh.misses == 0


def test_a_shield_not_seen_in_time_is_spawned_again_then_backs_off() -> None:
    sh = shields.Shields()
    for n in range(shields.MISSES):
        sh.spawned(KEY, 0.0)
        assert sh.looked(sh.pending, game.IN_PLAY, False, KEY, 0, 1.0) == shields.WAIT
        late = shields.SEEN_SECONDS + 1
        outcome = sh.looked(sh.pending, game.IN_PLAY, False, KEY, 0, late)
        assert outcome == (shields.BACKOFF if n == shields.MISSES - 1 else shields.AGAIN)
    assert sh.misses == 0 and sh.retry_at == late + shields.BACKOFF_SECONDS


def test_the_time_to_see_it_runs_in_play_only() -> None:
    sh = shields.Shields()
    sh.spawned(KEY, 0.0)
    pending = sh.pending
    assert sh.looked(pending, game.MENU, False, KEY, 0, 50.0) == shields.WAIT
    assert sh.looked(pending, game.IN_PLAY, False, KEY, 0, 51.0) == shields.WAIT


def test_a_death_or_unload_since_the_spawn_spawns_it_again() -> None:
    sh = shields.Shields()
    sh.spawned(KEY, 0.0)
    pending = sh.pending
    sh.left()
    assert sh.looked(pending, game.IN_PLAY, True, KEY, 0, 1.0) == shields.AGAIN
    sh.spawned(KEY, 2.0)
    assert sh.looked(sh.pending, game.DEAD, False, KEY, 0, 3.0) == shields.AGAIN


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
    assert shields.look(ShieldGame(error=game.GameError("bad link")), game.IN_PLAY) == (
        False,
        "bad link",
    )
