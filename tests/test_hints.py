"""Area Maps as hints: which item each one hints, and its popup text (no game, no server)."""

from __future__ import annotations

from client import hints

TARGETS = [[2, 900, 16], [1, 501, 26], [2, 905, 27], [3, 77, 19]]  # player, location, item


def test_area_maps_take_the_worlds_order_skipping_used_and_received() -> None:
    first = hints.next_targets(TARGETS, used=[], owned=set(), count=2)
    assert first == [(2, 900), (1, 501)]
    # The Beta Key at (2, 905) came in meanwhile: the third Area Map skips it.
    after = hints.next_targets(TARGETS, used=first, owned={(2, 905)}, count=1)
    assert after == [(3, 77)]


def test_area_maps_past_the_list_hint_nothing() -> None:
    used = [(2, 900), (1, 501), (2, 905)]
    assert hints.next_targets(TARGETS, used, {(3, 77)}, count=2) == [hints.NONE_LEFT] * 2


def test_the_notice_is_worded_as_the_servers_hint() -> None:
    assert hints.popup_note("Slide Mod", "Guardia 22,7") == "HINT: SLIDE MOD IS AT GUARDIA 22,7"
    assert hints.popup_note("S-Blade", "Exodia 27,0", "Zexion2") == (
        "HINT: S-BLADE IS AT EXODIA 27,0 IN ZEXION2'S WORLD"
    )


def test_another_games_long_location_is_shown_whole() -> None:
    note = hints.popup_note("Gamma Key", "Hyrule Castle - Big Key Chest (Upper Floor)", "Alice")
    assert (
        note == "HINT: GAMMA KEY IS AT HYRULE CASTLE - BIG KEY CHEST UPPER FLOOR IN ALICE'S WORLD"
    )
    assert set(note) <= hints.POPUP_CHARS  # the parentheses would be dropped by the helper


def test_past_the_helpers_limit_the_words_between_the_names_go() -> None:
    long_place = "Room " * 38  # 190 characters
    note = hints.popup_note("Slide Mod", long_place, "Alice")
    assert len(note) <= hints.POPUP_MAX
    assert note.startswith("HINT: SLIDE MOD - ROOM ROOM")


def test_each_area_map_has_a_key_received_or_found_here() -> None:
    location_items = [3, 49, 17, 49]  # this world's own Area Maps at locations 1 and 3
    keys = hints.area_maps([17, 49, 3, 49], location_items, checked=[3, 0])
    assert keys == ["r0", "r1", "l3"]
    assert hints.received_area_maps([17, 49, 3, 49]) == {1: "r0", 3: "r1"}


def test_only_area_maps_without_an_entry_get_one_in_order() -> None:
    stored = {"r0": (2, 900)}
    new = hints.assign(TARGETS, stored, ["r0", "l3", "r1"], owned={(1, 501)})
    assert new == {"l3": (2, 905), "r1": (3, 77)}
    assert hints.assign(TARGETS, stored | new, ["r0", "l3", "r1"], owned=set()) == {}
