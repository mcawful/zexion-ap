"""client/aplabels.py: names for other players' items (no game)."""

from __future__ import annotations

import aplabels as al


def lines(text: str) -> list[str]:
    """The popup's lines as the helper gets them: the logo glyph ("@" here) starts the first."""
    return ["@" + line if i == 0 else line for i, line in enumerate(text.split("\n"))]


def test_a_label_says_what_kind_of_item_then_whom_it_is_for() -> None:
    assert lines(al.label(True, "Alice")) == ["@PROGRESSION ITEM", "FOR ALICE"]
    assert lines(al.label(False, "Bob_2")) == ["@AP ITEM", "FOR BOB2"]
    assert lines(al.label(False, "Zoë")) == ["@AP ITEM", "FOR ZO"]


def test_long_player_names_are_cut_at_a_word() -> None:
    assert lines(al.label(True, "Alice in Wonderland Forever")) == [
        "@PROGRESSION ITEM",
        "FOR ALICE IN WONDERLAND",
    ]
    long = al.label(False, "LinksAwakenerTheGreat")
    assert lines(long) == ["@AP ITEM", "FOR LINKSAWAKENERTHEGREA"]


def test_another_zexion_players_item_is_named_by_its_record() -> None:
    assert al.zexion_label(1, "Zexion2") == "@1\nFOR ZEXION2"


def test_another_zexion_players_intel_chip_is_the_area_map_by_another_name() -> None:
    assert al.zexion_label(49, "Zexion2", "Intel Chip") == "@49 INTEL CHIP\nFOR ZEXION2"


def test_another_zexion_players_items_keep_their_names_whatever_the_settings() -> None:
    # The Area Map by name, as this player's own seed may rename the record INTEL CHIP; the
    # disk under its Archipelago name, not the game's DATA DISC; the Intel Chip as the Area Map.
    assert al.zexion_look(49) == (49, "AREA MAP")
    assert al.zexion_look(44) == (44, "OS INSTALL DISK")
    assert al.zexion_look(101) == (49, "INTEL CHIP")
    assert al.zexion_look(16) == (16, "")
    assert al.zexion_look(100) is None
    assert al.zexion_look(-1) is None


def test_each_line_fits_the_popup() -> None:
    text = al.label(True, "P" * 60)
    assert lines(text) == ["@PROGRESSION ITEM", "FOR " + "P" * 20]
    assert all(len(line) <= 24 for line in lines(text))


def test_characters_the_font_lacks_are_dropped() -> None:
    assert lines(al.label(False, "€")) == ["@AP ITEM", "FOR"]


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


def test_labels_are_wanted_only_while_this_seeds_save_is_loaded() -> None:
    assert al.labels_wanted(seed_matches=True, left_seed=False, wanted=False)
    assert not al.labels_wanted(seed_matches=False, left_seed=True, wanted=True)
    # A poll that could not tell (a load in progress) keeps what was wanted.
    assert al.labels_wanted(seed_matches=False, left_seed=False, wanted=True)
    assert not al.labels_wanted(seed_matches=False, left_seed=False, wanted=False)


def test_an_own_disc_is_named_by_its_record_for_no_one() -> None:
    assert al.own_label(44) == "@44"
    assert al.own_label(44, "OS Install Disk") == "@44 OS INSTALL DISK"
