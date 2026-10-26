"""client/state.py: the client's state built from slot data, and what each part resets (no AP)."""

from __future__ import annotations

from test_save_patch import AMOUNTS
from test_session import CELLS

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
    }
    slot.update(changes)
    return slot


def test_a_connection_reads_the_seed_from_its_slot_data() -> None:
    conn = state.Connection.from_slot_data(
        slot_data(
            stand_in=47,
            stand_in_progression=48,
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
    assert conn.plan.start == (5, 326, 449)
    assert conn.session.plan is conn.plan
    assert conn.foreign == [3], "location 9 is not kept"
    assert conn.hint_targets == [[1, 3392510_520, 16]]
    assert conn.lock_assists and conn.intel_chip and conn.death_link_on
    assert conn.hunt == discs.DiscHunt(3, 5, 0, frozenset({12}))
    assert not conn.items_listed and conn.awaiting_backlog
    assert conn.newgame is None and not conn.goal_sent and not conn.labels_want


def test_a_world_from_before_the_options_sets_none_of_them() -> None:
    conn = state.Connection.from_slot_data(slot_data(), team=0, slot=1)
    assert conn.foreign == [], "no stand_in in the slot data: no labels"
    assert conn.hint_targets == []
    assert not conn.lock_assists and not conn.intel_chip and not conn.death_link_on
    assert conn.hunt is None


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
