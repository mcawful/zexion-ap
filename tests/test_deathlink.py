from __future__ import annotations

import deathlink
import game


def test_a_death_in_play_is_sent() -> None:
    link = deathlink.DeathLink()
    assert not link.observe(game.IN_PLAY, 0.0)
    assert link.observe(game.DEAD, 1.0)
    assert not link.observe(game.DEAD, 1.5)  # the same death, still on screen


def test_a_death_not_seen_coming_is_not_sent() -> None:
    # the client started, or a save loaded, with the player already dead
    link = deathlink.DeathLink()
    assert not link.observe(game.DEAD, 0.0)
    link = deathlink.DeathLink()
    link.observe(game.IN_PLAY, 0.0)
    link.observe(game.NO_SAVE, 1.0)
    assert not link.observe(game.DEAD, 2.0)


def test_menus_and_changing_reads_do_not_break_the_chain() -> None:
    link = deathlink.DeathLink()
    link.observe(game.IN_PLAY, 0.0)
    assert not link.observe(game.MENU, 1.0)
    assert not link.observe(game.CHANGING, 2.0)
    assert link.observe(game.DEAD, 3.0)


def test_a_dealt_death_is_not_sent_back() -> None:
    link = deathlink.DeathLink()
    link.observe(game.IN_PLAY, 0.0)
    assert link.receive() == 1
    link.dealing(10.0)
    link.dealt(10.0)
    assert link.owed == 0
    assert not link.observe(game.DEAD, 10.5)
    link.observe(game.IN_PLAY, 20.0)
    assert link.observe(game.DEAD, 30.0)  # the next one is the player's own


def test_received_deaths_queue_and_are_dealt_one_at_a_time() -> None:
    link = deathlink.DeathLink()
    link.observe(game.IN_PLAY, 0.0)
    assert [link.receive(), link.receive()] == [1, 2]
    link.dealing(1.0)
    link.dealt(1.0)
    assert link.owed == 1
    assert not link.observe(game.DEAD, 1.2)
    assert link.receive() == 2  # one more, received while dead: queued too
    link.observe(game.IN_PLAY, 6.0)  # the respawn
    link.dealing(7.0)
    link.dealt(7.0)
    assert not link.observe(game.DEAD, 7.2)
    assert link.owed == 1


def test_a_death_whose_reply_was_lost_counts_as_dealt() -> None:
    link = deathlink.DeathLink()
    link.observe(game.IN_PLAY, 0.0)
    link.receive()
    link.receive()
    link.dealing(10.0)  # the reply never came
    assert not link.observe(game.DEAD, 11.0)
    assert link.owed == 1


def test_a_refused_deal_stays_queued_and_the_next_death_is_the_players() -> None:
    link = deathlink.DeathLink()
    link.observe(game.IN_PLAY, 0.0)
    link.receive()
    link.dealing(10.0)
    link.refused()
    assert link.owed == 1
    assert link.observe(game.DEAD, 11.0)
    assert link.owed == 1  # the player's own death leaves the queue as it was


def test_an_unanswered_ask_long_ago_does_not_hide_a_later_death() -> None:
    link = deathlink.DeathLink()
    link.observe(game.IN_PLAY, 0.0)
    link.receive()
    link.dealing(10.0)  # lost, and nothing died
    assert link.observe(game.DEAD, 10.0 + deathlink.DEALT_SECONDS + 1)
    assert link.owed == 1


def test_a_dealt_death_that_did_not_happen_does_not_hide_a_later_one() -> None:
    link = deathlink.DeathLink()
    link.observe(game.IN_PLAY, 0.0)
    link.receive()
    link.dealing(10.0)
    link.dealt(10.0)
    assert link.observe(game.DEAD, 10.0 + deathlink.DEALT_SECONDS + 1)


def test_the_ending_drops_the_queue() -> None:
    link = deathlink.DeathLink()
    link.observe(game.IN_PLAY, 0.0)
    link.receive()
    link.receive()
    link.ending()
    assert link.owed == 0


def test_a_death_received_in_a_menu_is_queued_until_play() -> None:
    link = deathlink.DeathLink()
    link.observe(game.MENU, 0.0)
    assert link.receive() == 1 and link.owed == 1
