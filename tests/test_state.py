"""client/state.py: the client's state built from slot data, and what each part resets (no AP)."""

from __future__ import annotations

import pytest
from fakes import slot_data

from client import discs, game, native, state


def test_a_connection_reads_the_seed_from_its_slot_data() -> None:
    conn = state.Connection.from_slot_data(
        slot_data(
            hints=[[1, 3392510_520, 16]],
            allow_assists=False,
            intel_chip=True,
            death_link=True,
            goal="os_install_disks",
            discs_required=3,
            discs_placed=5,
            disc_locations=[12],
        ),
        team=0,
        slot=2,
    )
    assert conn.mode == "Standard" and conn.plan.start == (5, 326, 449)
    assert conn.session.plan is conn.plan
    assert conn.foreign == [3], "location 9 is not kept"
    assert conn.hint_targets == [[1, 3392510_520, 16]]
    assert conn.lock_assists and conn.intel_chip and conn.death_link_on
    assert conn.hunt == discs.DiscHunt(3, 5, 0, frozenset({12}))
    assert not conn.items_listed and conn.awaiting_backlog
    assert conn.newgame is None and not conn.goal_sent and not conn.labels_want


def test_the_options_off_set_none_of_them() -> None:
    conn = state.Connection.from_slot_data(slot_data(), team=0, slot=1)
    assert conn.hint_targets == []
    assert not conn.lock_assists and not conn.intel_chip and not conn.death_link_on
    assert conn.hunt is None


@pytest.mark.parametrize("key", ["hints", "intel_chip", "stand_in", "menu", "goal", "mode"])
def test_slot_data_this_client_cannot_read_is_an_unsupported_seed(key) -> None:
    slot = slot_data()
    del slot[key]
    with pytest.raises(state.UnsupportedSeed, match="version of the Zexion world"):
        state.Connection.from_slot_data(slot, team=0, slot=1)


@pytest.mark.parametrize("changes", [{"goal": "boss"}, {"menu": []}, {"start_items": []}])
def test_slot_data_values_this_client_cannot_read_are_an_unsupported_seed(changes) -> None:
    # A goal it does not know, or a value of another kind.
    with pytest.raises(state.UnsupportedSeed):
        state.Connection.from_slot_data(slot_data(**changes), team=0, slot=1)


def test_the_data_storage_keys_are_per_team_and_slot() -> None:
    conn = state.Connection.from_slot_data(slot_data(), team=1, slot=4)
    assert conn.key("found") == conn.found_key == "zexion_found_1_4"
    keys = {conn.shield_key, conn.found_key, conn.popped_key, conn.announced_key, conn.hints_key}
    assert len(keys) == 5 and all(k.endswith("_1_4") for k in keys)


def test_a_new_helper_is_told_everything_again() -> None:
    told = state.HelperState()
    assert told.assists.sent is None and told.intel.sent is None
    assert told.discs_sent is None and told.slots_sent is None and told.offline_sent == {}
    # It may hold an earlier client's labels: taken back unless this seed's are sent.
    assert told.labels_held and told.labels_for is None and not told.labels_refused
    assert not told.client_back_sent and not told.other_save_sent


def test_leaving_the_seeds_save_starts_the_next_load_afresh() -> None:
    load = state.LoadState(seed_loaded=True, save_collected={1, 2}, goal_withheld=True)
    load.return_home.reminded = True
    load.assists_want = load.intel_want = True
    load.leave()
    assert load == state.LoadState()


def test_the_goal_withheld_is_said_again_on_the_next_load() -> None:
    load = state.LoadState(goal_withheld=True)
    load.leave()
    assert not load.goal_withheld


# --- received items' popups -----------------------------------------------------------------


def test_with_no_count_stored_everything_held_counts_as_handed_and_is_stored_at_once() -> None:
    for absent in (None, "x", -1, True):
        assert state.announced_start(absent, 5) == 5
        assert state.announced_to_store(5, absent, None) == 5, "the key is made at once"
    assert state.announced_start(None, 0) == 0
    assert state.announced_to_store(0, None, None) == 0, "even with nothing held"


def test_a_stored_count_starts_the_session_capped_at_the_items_held() -> None:
    assert state.announced_start(3, 5) == 3
    assert state.announced_start(9, 5) == 5
    assert state.announced_to_store(3, 3, None) is None, "nothing new: nothing stored"


def test_a_shield_count_that_is_not_a_count_pauses_shields_rather_than_reading_as_none() -> None:
    assert state.shield_count(None) == 0, "none stored: none given"
    assert state.shield_count(0) == 0 and state.shield_count(4) == 4
    for bad in ("x", -1, True, 2.5, [1]):
        assert state.shield_count(bad) is None, "every received shield would be given again"


def test_a_reconnect_does_not_hand_over_again_what_the_connection_before_did() -> None:
    # Its last count may not have reached the server before it ended.
    assert state.announced_start(3, 5, handed_before=5) == 5
    assert state.announced_start(3, 5, handed_before=9) == 5
    assert state.announced_start(3, 5, handed_before=2) == 3


def test_new_popups_handed_over_are_stored_and_a_failed_store_is_sent_again() -> None:
    assert state.announced_to_store(5, 3, None) == 5
    assert state.announced_to_store(5, 3, 4) == 5
    # The store failed: sent stays as it was, so the next poll sends it again.
    assert state.announced_to_store(5, 3, None) == 5
    # It went through; the server's echo is not here yet: not sent again.
    assert state.announced_to_store(5, 3, 5) is None
    assert state.announced_to_store(5, None, 5) is None
    assert state.announced_to_store(4, 5, None) is None, "another client of the slot is ahead"


# --- the locations found, and what is said once ------------------------------------------------


def test_the_found_record_heals_once_read() -> None:
    assert state.found_to_store({1, 2, 3}, None, {3}, due=False) == {3}, "not read: the new"
    assert state.found_to_store({1, 2, 3}, {1}, {3}, due=False) == {2, 3}
    assert state.found_to_store({1, 2, 3}, {1}, set(), due=False) == set(), "not due yet"
    assert state.found_to_store({1, 2, 3}, {1}, set(), due=True) == {2, 3}
    assert state.found_to_store({1, 2}, {1, 2}, {2}, due=True) == set(), "the record has them"


def test_a_message_is_said_once_until_it_changes() -> None:
    said = state.SayOnce()
    assert said("helper", "a") and not said("helper", "a")
    assert said("game", "a"), "topics are apart"
    assert said("helper", "b") and said("helper", "a")
    said.clear("helper")
    assert said("helper", "a")
    assert said("read", "x") and said("read detail", "y")
    said.clear("read", "read detail")  # a read that worked, or the game closed
    assert said("read", "x") and said("read detail", "y")


def test_the_save_loaded_line_names_the_games_slot() -> None:
    assert state.save_loaded((4).to_bytes(4, "little") + bytes(10)) == (
        "This seed's save is loaded (slot 5): items and checks go through."
    )
    assert "(slot" not in state.save_loaded(b"")  # no stamp read: the slot left out
    assert "(slot" not in state.save_loaded((0xFFFFFFFF).to_bytes(4, "little"))


def test_a_game_the_client_cannot_use_is_said_plainly() -> None:

    unknown = state.attach_failed(game.UnknownBuild("timestamp 0x1234 and image size 0x5678"))
    assert "isn't supported yet" in unknown and "0x" not in unknown
    assert state.REPORT in state.attach_failed(game.PatternError("pattern matched 2 times"))
    assert "0x" not in state.attach_failed(game.GameError("read at 0x1000 failed"))
    denied = state.attach_failed(game.AccessDenied("opening Zexion.exe failed: (error 5)"))
    assert "administrator" in denied and "error 5" not in denied


def test_the_mod_failing_is_said_plainly_unless_the_player_can_fix_it() -> None:

    fixable = state.helper_failed(native.ModFixable("it isn't installed; copy the two files"))
    assert "it isn't installed; copy the two files" in fixable
    off = native.ModDisabled("The mod is turned off.")
    assert state.helper_failed(off) == "The mod is turned off."
    internal = state.helper_failed(native.HelperError("no reply within 2.0 s"))
    assert "no reply" not in internal and "didn't start" in internal
    assert "zexion_ap.dll and version.dll" in internal
    assert state.helper_failed(native.GameExited("the game has exited")) is None


def test_leaving_this_seeds_save_says_items_wait() -> None:
    # The server still sends items: they are given once this seed's save is loaded again.
    for line in (state.SAVE_CLOSED, state.OTHER_SAVE):
        assert "wait until" in line and "received" not in line


# --- reconnects, a lost helper, the goal ------------------------------------------------------


def test_a_reconnect_to_the_same_seed_carries_the_popups_handed_and_the_endings_hold() -> None:
    old = state.Connection.from_slot_data(slot_data(), team=0, slot=1)
    old.session.announced, old.session.holding = 7, True
    new = state.Connection.from_slot_data(slot_data(), team=0, slot=1)
    state.carry_over(new, old, same_seed=True)
    assert new.before is old.session and new.session.holding
    old.session.announced = 9  # its last poll handed more over after Connected
    assert new.before.announced == 9, "read when the new count starts, not at Connected"
    other_seed = state.Connection.from_slot_data(slot_data(), team=0, slot=1)
    state.carry_over(other_seed, old, same_seed=False)
    assert other_seed.before is None and not other_seed.session.holding
    state.carry_over(other_seed, None, same_seed=True)
    assert other_seed.before is None


def test_reload_mod_says_whether_the_mod_unloaded() -> None:
    unloaded, line = state.reload_reply("bye")
    assert unloaded and "loading it again" in line
    unloaded, line = state.reload_reply(None)  # the request failed: it unloads itself
    assert unloaded and "didn't answer" in line
    unloaded, line = state.reload_reply("error a menu is open; not unloading")
    assert not unloaded and "with a menu open" in line
    reply = "error the frame hook was replaced by something else; not unloading"
    unloaded, line = state.reload_reply(reply)
    assert not unloaded and "(the frame hook was replaced by something else)" in line
    assert "restart Zexion" in line, "advice that does not hinge on a menu"


def test_connects_own_disconnect_keeps_the_connection_a_disconnect_left() -> None:
    # Disconnect, then Connect: Archipelago's connect disconnects first, with no connection.
    conn = state.Connection.from_slot_data(slot_data(), team=0, slot=1)
    left = state.left_connection(conn, None)
    assert left is conn
    assert state.left_connection(None, left) is conn, "the one left is not forgotten"
    newer = state.Connection.from_slot_data(slot_data(), team=0, slot=1)
    assert state.left_connection(newer, left) is newer


def test_the_goal_is_the_ending_with_enough_disks_seen_begin_or_not() -> None:
    assert state.goal_reached(True, None, 0), "the core goal: the ending"
    assert state.goal_reached(True, 3, 3)
    assert not state.goal_reached(True, 3, 2), "too few disks"
    assert not state.goal_reached(False, 3, 9), "no ending"


def test_a_feats_guard_not_all_in_place_is_read_from_hello() -> None:
    assert state.feats_unguarded("on") is None
    assert state.feats_unguarded("off") is None, "not on for this save, or not yet"
    assert state.feats_unguarded("") is None, "no field"
    why = "on:the_ending's_call_reads_otherwise;_steam_api64_is_not_loaded"
    assert state.feats_unguarded(why) == why.removeprefix("on:")
    assert "achievements may be earned in this save" in state.FEATS_UNGUARDED


def test_a_load_ended_while_a_reconnect_carried_it_ends_for_both() -> None:
    # Session.look_away ran in a thread while Connected carried the ending over.
    old = state.Connection.from_slot_data(slot_data(), team=0, slot=1)
    old.session.holding, old.session._goal_before = True, True
    new = state.Connection.from_slot_data(slot_data(), team=0, slot=1)
    state.carry_over(new, old, same_seed=True)
    assert new.session.holding and new.session.carried_from is old.session
    state.end_kept_load(old.session, new)
    assert not old.session.holding and not new.session.holding
    assert new.session._goal_before is None
    # One carried from no other (or that has stepped since: step clears it) is its own.
    other = state.Connection.from_slot_data(slot_data(), team=0, slot=1)
    other.session.holding = True
    state.end_kept_load(old.session, other)
    assert other.session.holding
    state.end_kept_load(old.session, None)
