"""client/state.py: the client's state built from slot data, and what each part resets (no AP)."""

from __future__ import annotations

import pytest
from fakes import AMOUNTS, CELLS

from client import discs, state
from client import save_patch as sp

ITEMS = [46] * sp.LOCATIONS
ITEMS[3], ITEMS[9], ITEMS[12] = 47, 48, 44  # a stand-in, a progression stand-in, a disk


def slot_data(**changes) -> dict:
    slot = {
        "location_items": ITEMS,
        "start_items": {"0": 3},
        "start_index": 5,
        "start_column": 326,
        "start_row": 449,
        "second_start": None,
        "amounts": list(AMOUNTS),
        "options": {"42": 2},
        "locations": [list(c) for c in CELLS],
        "kept_locations": [i for i in range(sp.LOCATIONS) if i != 9],
        "mode": "Standard",
        "menu": {
            "seed_text": "0123456789ABCDEF",
            "start": [0] * 27,
            "pool": [1] * 27,
            "settings": [0] * 64,
            "second_target": [0, 20, 509],
        },
        "stand_in": 47,
        "stand_in_progression": 48,
        "hints": [],
        "allow_assists": True,
        "intel_chip": False,
        "death_link": False,
        "goal": "core",
        "discs_required": 0,
        "discs_placed": 0,
        "discs_start": 0,
        "disc_locations": [],
    }
    slot.update(changes)
    return slot


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
    with pytest.raises(state.UnsupportedSeed):
        state.Connection.from_slot_data(slot_data(goal="boss"), team=0, slot=1)
    for wrong_form in ({"menu": []}, {"start_items": []}):  # a value of another kind
        with pytest.raises(state.UnsupportedSeed):
            state.Connection.from_slot_data(slot_data(**wrong_form), team=0, slot=1)


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
    load = state.LoadState(seed_loaded=True, save_collected={1, 2}, ending_unseen=True)
    load.return_home.reminded = True
    load.assists_want = load.intel_want = True
    load.leave()
    assert load == state.LoadState()


def test_the_goal_withheld_is_said_again_on_the_next_load() -> None:
    load = state.LoadState(goal_withheld=True)
    load.leave()
    assert not load.goal_withheld


# --- received items' popups -----------------------------------------------------------------


def test_with_no_count_stored_everything_held_counts_as_shown_and_is_stored_at_once() -> None:
    for absent in (None, "x", -1, True):
        assert state.announced_start(absent, 5) == (5, 5)
        assert state.announced_to_store(5, absent, None) == 5, "the key is made at once"
    assert state.announced_start(None, 0) == (0, 0)
    assert state.announced_to_store(0, None, None) == 0, "even with nothing held"


def test_a_stored_count_starts_the_session_capped_at_the_items_held() -> None:
    assert state.announced_start(3, 5) == (3, 3)
    assert state.announced_start(9, 5) == (5, 5)
    assert state.announced_to_store(3, 3, None) is None, "nothing new: nothing stored"


def test_a_reconnect_does_not_queue_again_what_the_same_helper_holds() -> None:
    assert state.announced_start(3, 5, queued_before=5) == (5, 3)
    assert state.announced_start(3, 5, queued_before=9) == (5, 3)
    assert state.announced_start(3, 5, queued_before=2) == (3, 3)


def test_popups_shown_are_those_queued_less_those_waiting() -> None:
    assert state.popups_unstored(5, 3, None)
    assert not state.popups_unstored(3, 3, None) and not state.popups_unstored(5, 3, 5)
    assert state.popups_shown(5, 2, 3) == 3
    assert state.popups_shown(5, 0, 3) == 5
    # The helper's count takes in follow-up popups too: never below what was known.
    assert state.popups_shown(5, 4, 3) == 3
    assert state.popups_shown(5, 9, 0) == 0


def test_new_popups_shown_are_stored_and_a_failed_store_is_sent_again() -> None:
    assert state.announced_to_store(5, 3, None) == 5
    assert state.announced_to_store(5, 3, 4) == 5
    # The store failed: sent stays as it was, so the next poll sends it again.
    assert state.announced_to_store(5, 3, None) == 5
    # It went through; the server's echo is not here yet: not sent again.
    assert state.announced_to_store(5, 3, 5) is None
    assert state.announced_to_store(5, None, 5) is None
    assert state.announced_to_store(4, 5, None) is None, "another client of the slot is ahead"


def test_a_lost_helper_queues_again_what_it_had_not_shown() -> None:
    conn = state.Connection.from_slot_data(slot_data(), team=0, slot=1)
    conn.helper_lost()
    assert conn.session.announced is None, "nothing known yet: nothing to queue again"
    conn.session.mark_announced(5)
    conn.announced_shown, conn.queued_before = 3, 5
    conn.helper_lost()
    assert conn.session.announced == 3 and conn.queued_before is None


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


def test_the_save_loaded_line_names_the_games_slot() -> None:
    assert state.save_loaded((4).to_bytes(4, "little") + bytes(10)) == (
        "This seed's save is loaded (slot 5): items and checks go through."
    )
    assert "(slot" not in state.save_loaded(b"")  # no stamp read: the slot left out
    assert "(slot" not in state.save_loaded((0xFFFFFFFF).to_bytes(4, "little"))
