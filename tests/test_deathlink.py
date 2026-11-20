from __future__ import annotations

from client import deathlink, game


def in_play() -> deathlink.DeathLink:
    """A DeathLink that has seen the player in play."""
    link = deathlink.DeathLink()
    link.observe(game.IN_PLAY, 0.0)
    return link


def test_a_death_in_play_is_sent() -> None:
    link = deathlink.DeathLink()
    assert not link.observe(game.IN_PLAY, 0.0)
    assert link.observe(game.DEAD, 1.0)
    assert not link.observe(game.DEAD, 1.5)  # the same death, still on screen


def test_a_death_not_seen_coming_is_not_sent() -> None:
    # the client started, or a save loaded, with the player already dead
    link = deathlink.DeathLink()
    assert not link.observe(game.DEAD, 0.0)
    link = in_play()
    link.observe(game.NO_SAVE, 1.0)
    assert not link.observe(game.DEAD, 2.0)


def test_menus_and_changing_reads_do_not_break_the_chain() -> None:
    link = in_play()
    assert not link.observe(game.MENU, 1.0)
    assert not link.observe(game.CHANGING, 2.0)
    assert link.observe(game.DEAD, 3.0)


def test_a_dealt_death_is_not_sent_back() -> None:
    link = in_play()
    assert link.receive() == 1
    link.dealing(10.0)
    link.dealt(10.0)
    assert link.owed == 0
    assert not link.observe(game.DEAD, 10.5)
    link.observe(game.IN_PLAY, 20.0)
    assert link.observe(game.DEAD, 30.0)  # the next one is the player's own


def test_received_deaths_queue_and_are_dealt_one_at_a_time() -> None:
    link = in_play()
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
    link = in_play()
    link.receive()
    link.receive()
    link.dealing(10.0)  # the reply never came
    assert not link.observe(game.DEAD, 11.0)
    assert link.owed == 1


def test_a_lost_reply_looks_for_the_death_from_the_first_state_read_after_it() -> None:
    # The Linux helper's connection closed (or timed out, 5 s): the next poll configures it again
    # before it reads any state, and that takes longer than DEALT_SECONDS.
    link = in_play()
    link.receive()
    link.receive()
    link.dealing(10.0)
    link.reply_lost(15.0)
    late = 15.0 + 3 * deathlink.DEALT_SECONDS
    assert not link.observe(game.CHANGING, late - 1.0)  # not a read of in play or dead
    assert not link.observe(game.DEAD, late)  # dealt, not sent back
    assert link.owed == 1  # and counted, so the player is not killed again for it
    assert not link.observe(game.DEAD, late + 0.5)  # still on screen
    link.observe(game.IN_PLAY, late + 8.0)
    assert link.observe(game.DEAD, late + 20.0)  # the next death is the player's own


def test_a_lost_reply_waits_for_a_death_after_an_in_play_read_too() -> None:
    link = in_play()
    link.receive()
    link.reply_lost(15.0)
    late = 15.0 + 3 * deathlink.DEALT_SECONDS
    assert not link.observe(game.IN_PLAY, late)  # the death not on screen yet
    assert not link.observe(game.DEAD, late + 1.0)
    assert link.owed == 0 and not link.observe(game.DEAD, late + 1.5)


def test_a_lost_reply_of_a_death_never_dealt_hides_none_that_come_later() -> None:
    link = in_play()
    link.receive()
    link.reply_lost(15.0)
    late = 15.0 + 3 * deathlink.DEALT_SECONDS
    link.observe(game.IN_PLAY, late)  # the window opens here, and nothing happens in it
    assert link.observe(game.DEAD, late + deathlink.DEALT_SECONDS + 1)
    assert link.owed == 1


def test_a_lost_reply_ends_with_the_save_left() -> None:
    link = in_play()
    link.receive()
    link.reply_lost(15.0)
    link.observe(game.NO_SAVE, 16.0)  # another save, or none: the death was not this one's
    link.observe(game.IN_PLAY, 17.0)
    assert link.observe(game.DEAD, 18.0)  # the player's own, not hidden by the old ask


def test_a_refused_deal_stays_queued_and_the_next_death_is_the_players() -> None:
    link = in_play()
    link.receive()
    link.dealing(10.0)
    link.refused()
    assert link.owed == 1
    assert link.observe(game.DEAD, 11.0)
    assert link.owed == 1  # the player's own death leaves the queue as it was


def test_an_unanswered_ask_long_ago_does_not_hide_a_later_death() -> None:
    link = in_play()
    link.receive()
    link.dealing(10.0)  # lost, and nothing died
    assert link.observe(game.DEAD, 10.0 + deathlink.DEALT_SECONDS + 1)
    assert link.owed == 1


def test_a_dealt_death_that_did_not_happen_does_not_hide_a_later_one() -> None:
    link = in_play()
    link.receive()
    link.dealing(10.0)
    link.dealt(10.0)
    assert link.observe(game.DEAD, 10.0 + deathlink.DEALT_SECONDS + 1)


def test_a_death_received_in_a_menu_is_queued_until_play() -> None:
    link = deathlink.DeathLink()
    link.observe(game.MENU, 0.0)
    assert link.receive() == 1 and link.owed == 1


def lost_reply(owed: int = 1, deaths: int | None = 3) -> deathlink.DeathLink:
    """In play, asked to deal a death at 10 s (the save's death count ``deaths``), the reply lost
    at 15 s."""
    link = in_play()
    for _ in range(owed):
        link.receive()
    link.dealing(10.0, deaths)
    link.reply_lost(15.0)
    return link


def test_a_death_dealt_unseen_and_already_respawned_is_counted_by_the_death_count() -> None:
    link = lost_reply(owed=2)
    assert link.watching
    # The first read is in play, the helper having dealt the death and the player respawned.
    assert not link.observe(game.IN_PLAY, 40.0, deaths=4)
    assert link.owed == 1 and not link.watching, "dealt once: not asked for again at once"


def test_a_death_the_count_shows_is_not_sent_as_the_players_own_when_seen_dead() -> None:
    link = lost_reply()
    assert not link.observe(game.DEAD, 40.0, deaths=4)
    assert link.owed == 0 and not link.watching
    assert not link.observe(game.DEAD, 40.5, deaths=4)  # still on screen
    link.observe(game.IN_PLAY, 50.0, deaths=4)
    assert link.observe(game.DEAD, 60.0, deaths=5)  # the next one is the player's own


def test_nothing_is_asked_for_again_while_a_lost_replys_death_may_still_come() -> None:
    link = lost_reply()
    assert not link.observe(game.IN_PLAY, 40.0, deaths=3)  # in play, the count the same
    assert link.watching, "the death may be on its way: the count follows it"
    assert not link.observe(game.IN_PLAY, 42.0, deaths=3) and link.watching
    assert not link.observe(game.IN_PLAY, 40.0 + deathlink.DEALT_SECONDS + 1, deaths=3)
    assert not link.watching and link.owed == 1, "none came: it was not dealt, and stays queued"


def test_a_death_counted_while_watching_is_the_dealt_one() -> None:
    link = lost_reply()
    link.observe(game.IN_PLAY, 40.0, deaths=3)
    assert not link.observe(game.IN_PLAY, 42.0, deaths=4)  # dealt, died and respawned between reads
    assert link.owed == 0 and not link.watching


def test_a_lost_reply_looked_into_too_late_is_told_by_the_death_count_alone() -> None:
    late = 15.0 + deathlink.LOST_SECONDS + 1
    # The count the same: none was dealt, so a death seen is the player's own, sent.
    link = lost_reply()
    assert link.observe(game.DEAD, late, deaths=3)
    assert link.owed == 1 and not link.watching
    # The count up: the dealt one, however late, never sent back nor dealt again.
    link = lost_reply()
    assert not link.observe(game.DEAD, late, deaths=4)
    assert link.owed == 0 and not link.watching
    link = lost_reply()
    assert not link.observe(game.IN_PLAY, late, deaths=4) and link.owed == 0
    # With no count to go by: nothing is known, and it stays queued.
    link = lost_reply(deaths=None)
    assert not link.observe(game.IN_PLAY, late) and not link.watching and link.owed == 1


def test_a_dealt_death_and_the_players_own_in_one_gap_are_both_told() -> None:
    link = lost_reply(owed=2)
    assert link.observe(game.IN_PLAY, 40.0, deaths=5), "two deaths: one dealt, one the player's"
    assert link.owed == 1 and not link.watching
    link = lost_reply()
    assert link.observe(game.DEAD, 40.0, deaths=5)  # dead now, of their own besides
    assert link.owed == 0
    assert not link.observe(game.DEAD, 40.5, deaths=5)


def test_a_lost_reply_without_a_death_count_works_from_the_state_alone() -> None:
    link = lost_reply(deaths=None)
    assert not link.observe(game.IN_PLAY, 40.0)
    assert link.watching
    assert not link.observe(game.DEAD, 41.0) and link.owed == 0
