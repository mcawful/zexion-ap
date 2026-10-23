"""client/aplabels.py: names for other players' items (no game)."""

from __future__ import annotations

import aplabels as al


def test_a_label_names_the_item_and_its_player_in_the_fonts_letters() -> None:
    assert al.label("Master Sword", "Alice") == "MASTER SWORD FOR ALICE"
    assert al.label("Pokédex", "Bob_2") == "POKDEX FOR BOB2"
    assert al.label("Hookshot [L2]", "Zoë") == "HOOKSHOT L2 FOR ZO"


def test_a_label_is_at_most_49_bytes() -> None:
    text = al.label("A" * 60, "Player")
    assert len(text) == 49 and text == "A" * 49


def test_characters_the_font_lacks_are_dropped() -> None:
    assert al.label("", "") == "FOR"
    assert al.label("€", "€")[:3] == "FOR"


def test_foreign_locations_are_the_kept_stand_ins() -> None:
    items = [3, 47, 46, 47, 47]
    assert al.foreign_locations(items, kept=[0, 1, 3]) == [1, 3]


def test_the_request_lists_each_cell_with_its_label_in_hex() -> None:
    text = al.labels_request({(5, 2): "SWORD FOR AL", (1, 0): "X"})
    assert text.splitlines() == [
        "ap_labels",
        "label 1 0 58",
        "label 5 2 " + b"SWORD FOR AL".hex(),
    ]
