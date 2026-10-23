"""client/aplabels.py: names for other players' items (no game)."""

from __future__ import annotations

import aplabels as al


def lines(text: str) -> list[str]:
    """The popup's lines as the helper gets them: the logo glyph ("@" here) starts the first."""
    return ["@" + line if i == 0 else line for i, line in enumerate(text.split("\n"))]


def test_a_label_names_the_item_then_its_player_on_the_next_line() -> None:
    assert lines(al.label("Master Sword", "Alice")) == ["@MASTER SWORD", "FOR ALICE"]
    assert lines(al.label("Pokédex", "Bob_2")) == ["@POKDEX", "FOR BOB2"]
    assert lines(al.label("Hookshot [L2]", "Zoë")) == ["@HOOKSHOT L2", "FOR ZO"]


def test_long_names_are_cut_at_a_word() -> None:
    text = al.label("Progressive Hookshot of Great Length", "LinksAwakenerTheGreat")
    assert lines(text) == ["@PROGRESSIVE HOOKSHOT OF", "FOR LINKSAWAKENERTHEGREA"]
    assert lines(al.label("Sword", "Alice in Wonderland Forever")) == [
        "@SWORD",
        "FOR ALICE IN WONDERLAND",
    ]


def test_each_line_fits_the_popup() -> None:
    text = al.label("A" * 60, "P" * 60)
    assert lines(text) == ["@" + "A" * 23, "FOR " + "P" * 20]
    assert all(len(line) <= 24 for line in lines(text))


def test_characters_the_font_lacks_are_dropped() -> None:
    assert al.label("", "") == al.GENERAL
    assert al.label("€", "Alice") == al.GENERAL
    assert lines(al.label("Sword", "€")) == ["@SWORD", "FOR"]


def test_foreign_locations_are_the_kept_stand_ins() -> None:
    items = [3, 47, 46, 47, 47, 48]
    assert al.foreign_locations(items, kept=[0, 1, 3]) == [1, 3]
    assert al.foreign_locations(items, kept=[1, 5], codes={47, 48}) == [1, 5]


def test_the_stand_ins_come_from_slot_data() -> None:
    assert al.stand_ins({"stand_in": 47, "stand_in_progression": 48}) == {47, 48}
    assert al.stand_ins({"stand_in": 47}) == {47}
    assert al.stand_ins({}) == set()
    assert al.stand_ins({"stand_in_progression": 48}) == set()


def test_the_request_lists_each_cell_with_its_label_in_hex() -> None:
    text = al.labels_request({(5, 2): "SWORD FOR AL", (1, 0): "X"})
    assert text.splitlines() == [
        "ap_labels",
        "label 1 0 58",
        "label 5 2 " + b"SWORD FOR AL".hex(),
    ]
