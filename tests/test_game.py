"""client/game.py on synthetic code and a fake process (no game, no exe)."""

from __future__ import annotations

import struct

import pytest
from fakes import FakeGame, FakeProcess, pe_image

from client import game
from client.patterns import CLIENT_TARGETS, TARGETS, TOOL_TARGETS, Build, Pattern, Target

# lea rax, [rip + disp32] ; add rax, rdx ; ret  -- a stand-in for a referring instruction
LEA = Pattern("48 8D 05 ?? ?? ?? ?? 48 01 D0 C3", disp_at=3, kind="rip", rip_end=7, addend=0)


def code_with(*chunks: bytes, pad: int = 0x40) -> bytes:
    return b"\x90" * pad + b"".join(chunks) + b"\x90" * pad


def lea(disp: int) -> bytes:
    return b"\x48\x8d\x05" + struct.pack("<i", disp) + b"\x48\x01\xd0\xc3"


# --- pattern resolution --------------------------------------------------------------------


def test_rip_pattern_resolves_to_its_target() -> None:
    code = code_with(lea(0x1000))
    # The instruction sits at code offset 0x40; code is loaded at RVA 0x2000.
    assert game.resolve(code, 0x2000, LEA) == 0x2000 + 0x40 + 7 + 0x1000


def test_negative_displacement_and_addend() -> None:
    p = Pattern(LEA.text, 3, "rip", 7, addend=-2)
    assert game.resolve(code_with(lea(-0x20)), 0x100, p) == 0x100 + 0x40 + 7 - 0x20 - 2


def test_at_patterns_resolve_to_the_match_itself() -> None:
    p = Pattern("55 48 89 E5 48 83 EC 20 89 7D FC 8B 45 FC", 0, "at", 0, addend=0)
    code = code_with(bytes.fromhex(p.text.replace(" ", "")))
    assert game.resolve(code, 0x3000, p) == 0x3000 + 0x40


def test_image_base_patterns_are_refused() -> None:
    p = Pattern("43 C6 84 11 ?? ?? ?? ?? 00 C3", 4, "rva", 9, 0)
    code = code_with(b"\x43\xc6\x84\x11" + struct.pack("<i", 0x3E469D) + b"\x00\xc3")
    with pytest.raises(game.PatternError, match="not accepted"):
        game.resolve(code, 0x1000, p)


def test_no_match_or_two_matches_is_refused() -> None:
    with pytest.raises(game.PatternError, match="0 times"):
        game.resolve(code_with(b"\xcc" * 16), 0, LEA)
    with pytest.raises(game.PatternError, match="2 times"):
        game.resolve(code_with(lea(1), lea(2)), 0, LEA)


def test_overlapping_matches_count_as_two() -> None:
    # "AA ?? AA ?? AA" matches at offsets 0 and 2 of AA AA AA AA AA AA: overlapping.
    p = Pattern("AA ?? AA ?? AA", 0, "rip", 4, 0)
    with pytest.raises(game.PatternError, match="2 times"):
        game.find_unique(b"\xaa" * 6, p)


def test_patterns_that_disagree_are_refused() -> None:
    other = Pattern("48 8D 05 ?? ?? ?? ?? 48 01 D0 CC", 3, "rip", 7, 0)
    code = code_with(lea(0x100), b"\x48\x8d\x05" + struct.pack("<i", 0x900) + b"\x48\x01\xd0\xcc")
    with pytest.raises(game.PatternError, match="disagree"):
        game.locate_all(code, 0, [Target("t", "test", {}, (LEA, other))])
    agree = Pattern(other.text, 3, "rip", 7, 0x100 - 0x900 - 11)
    found = game.locate_all(code, 0, [Target("t", "test", {}, (LEA, agree))])
    assert found["t"] == 0x40 + 7 + 0x100


def test_the_client_does_not_locate_the_targets_only_the_tools_read() -> None:
    # A pattern of the pickup counter breaking with an update cannot stop the client attaching.
    assert {"pickup_counter"} == TOOL_TARGETS
    assert {t.name for t in CLIENT_TARGETS} == {t.name for t in TARGETS} - TOOL_TARGETS
    assert game.Game.attach.__func__.__defaults__ == (CLIENT_TARGETS,)


# --- PE header and build identity ----------------------------------------------------------


def test_code_section_reads_the_pe_header() -> None:
    assert game.code_section(pe_image()) == (0x1000, 0x2000)
    with pytest.raises(game.GameError):
        game.code_section(bytes(0x1000))


def test_known_build_is_identified_and_others_refused() -> None:
    b1 = Build("B1", "0" * 64, 0x6A3F82DE, 0x1204000)
    assert game.identify_build(pe_image(), [b1]) is b1
    with pytest.raises(game.UnknownBuild):
        game.identify_build(pe_image(stamp=0x6A400000), [b1])
    with pytest.raises(game.UnknownBuild):
        game.identify_build(pe_image(size_of_image=0x1205000), [b1])
    with pytest.raises(game.GameError):
        game.identify_build(bytes(0x1000), [b1])


# --- the seed's randomizer section ---------------------------------------------------------


def blob(cells=None, items=None) -> bytearray:
    cells = cells or [(x, y) for y in range(3) for x in range(54)][: game.LOCATIONS]
    items = items or [46] * game.LOCATIONS
    b = bytearray(game.RANDO_BLOB_SIZE)
    for i, (x, y) in enumerate(cells):
        struct.pack_into("<HH", b, 4 * i, x, y)
    b[game.ITEM_TABLE_AT : game.ITEM_TABLE_AT + len(items)] = bytes(items)
    return b


def test_a_well_formed_seed_parses() -> None:
    seed = game.parse_seed(bytes(blob()))
    assert len(seed.locations) == game.LOCATIONS and seed.locations[0] == (0, 0)
    assert seed.items == bytes([46]) * game.LOCATIONS


@pytest.mark.parametrize(
    "broken",
    [
        lambda b: struct.pack_into("<HH", b, 0, 60, 0),  # a cell off the map
        lambda b: struct.pack_into("<HH", b, 4, 0, 0),  # a duplicate cell
        lambda b: b.__setitem__(4 * game.LOCATIONS, 1),  # no zero gap
        lambda b: b.__setitem__(game.ITEM_TABLE_AT, 51),  # an item ID out of range
    ],
)
def test_a_malformed_seed_is_refused(broken) -> None:
    b = blob()
    broken(b)
    with pytest.raises(game.GameError):
        game.parse_seed(bytes(b))


# --- reads and writes through a fake process ------------------------------------------------


def fake_game(**rva: int) -> FakeGame:
    """A fake with the inventory at 0x1100, room for the player state, which starts 0x108E
    before it, and ``rva`` for other targets."""
    rva = {"inventory": 0x1100, **rva}
    return FakeGame(pm=FakeProcess(0x2000), base=0, rva=rva, pid=1, build=None)


def test_set_item_is_compare_and_set() -> None:
    g = fake_game()
    g.set_item(17, expected=0, value=1)
    assert g.inventory()[17] == 1
    with pytest.raises(game.GameError, match="not the expected"):
        g.set_item(17, expected=0, value=1)  # it holds 1 now: refused, nothing written


def test_set_item_refuses_items_and_values_it_should_not_write() -> None:
    g = fake_game()
    with pytest.raises(ValueError, match="not an item"):
        g.set_item(50, 0, 1)  # Energy Shield: its pickup counts in byte 46, not 50
    with pytest.raises(ValueError, match="fit a byte"):
        g.set_item(3, 0, 256)


def test_a_run_of_flags_is_read_at_once() -> None:
    g = fake_game(flags=0x10)
    g.pm.memory[0x10 + 39] = 1
    g.pm.memory[0x10 + 250] = 2
    run = g.flag_bytes(39, 212)
    assert len(run) == 212 and run[0] == 1 and run[-1] == 2 and run[1:-1] == bytes(210)
    assert g.flag_bytes(250, 1) == b"\x02"
    for first, count in ((-1, 2), (game.FLAGS - 1, 2), (0, 0)):
        with pytest.raises(ValueError, match="outside"):
            g.flag_bytes(first, count)


def test_a_failed_write_says_its_state_is_unknown() -> None:
    g = fake_game()
    g.pm.fail_write = True
    with pytest.raises(game.WriteStateUnknown):
        g.set_item(17, 0, 1)


def test_a_read_back_that_differs_is_its_own_error() -> None:
    g = fake_game()
    g.pm.overwrite_with = 7  # the game puts the byte back straight after
    with pytest.raises(game.ReadBackMismatch) as info:
        g.set_item(17, expected=0, value=1)
    assert info.value.read_back == bytes([7])


def put_supplies(g: FakeGame, values: tuple[int, ...]) -> None:
    for field, v in zip(game.SUPPLY_FIELDS, values, strict=True):
        at = 0x1100 - game.INVENTORY_IN_PLAYER_STATE + field
        g.pm.memory[at : at + 2] = struct.pack("<h", v)


def test_the_player_state_read_at_once_agrees_with_each_read() -> None:
    g = fake_game()
    put_supplies(g, (30, 48, 2, 3, 1, 4))
    g.pm.memory[0x1100 + 17] = 1
    g.pm.memory[0x1100 + 50] = 2
    at = 0x1100 - game.INVENTORY_IN_PLAYER_STATE + game.PLAYER_CELL
    g.pm.memory[at : at + 2] = bytes([12, 34])
    player = g.player_state()
    assert player.inventory == g.inventory()
    assert player.supplies == g.supplies()
    assert player.energy == g.energy() == 30
    assert player.cell == (12, 34)


def test_set_supplies_is_compare_and_set_and_writes_only_changes() -> None:
    g = fake_game()
    put_supplies(g, (30, 48, 2, 3, 0, 0))  # in memory directly: no writes counted
    g.set_supplies((30, 48, 2, 3, 0, 0), (64, 64, 2, 3, 0, 0))
    assert g.supplies() == (64, 64, 2, 3, 0, 0)
    assert g.pm.writes == 2
    with pytest.raises(game.GameError, match="not the expected"):
        g.set_supplies((30, 48, 2, 3, 0, 0), (80, 80, 2, 3, 0, 0))
    assert g.supplies() == (64, 64, 2, 3, 0, 0)


def test_set_supplies_refuses_values_that_do_not_fit() -> None:
    g = fake_game()
    with pytest.raises(ValueError, match="16 bits"):
        g.set_supplies((0,) * 6, (0x8000, 0, 0, 0, 0, 0))


SHIELD_AT = 0x1400  # the fake's shield object


def put_shield(
    g: FakeGame, pointer: int = SHIELD_AT, serial: int = 0x8DD1, live: bool = True
) -> None:
    """The player's link to a shield object at ``pointer``, and that object, as measured."""
    link = 0x1100 - game.INVENTORY_IN_PLAYER_STATE + game.SHIELD_LINK
    handle = struct.pack("<IHH", serial, game.SHIELD_POOL_TYPE, 0)
    g.pm.memory[link : link + 24] = struct.pack("<Q", pointer) + handle + struct.pack("<Q", 4)
    own = struct.pack("<IHH", serial if live else 0, game.SHIELD_POOL_TYPE, 0)
    at = SHIELD_AT + game.OBJECT_HANDLE
    g.pm.memory[at : at + 8] = own
    at = SHIELD_AT + game.OBJECT_SELF
    g.pm.memory[at : at + 8] = struct.pack("<Q", SHIELD_AT)


def test_a_shield_is_up_while_the_player_links_to_its_live_object() -> None:
    g = fake_game()
    assert not g.shield_up()  # no link
    put_shield(g)
    assert g.shield_up()
    put_shield(g, pointer=0)  # broken: the game clears the pointer
    assert not g.shield_up()


def test_a_link_to_an_object_that_does_not_match_is_no_shield() -> None:
    g = fake_game()
    put_shield(g, live=False)  # the object's slot was freed
    assert not g.shield_up()
    put_shield(g)
    at = SHIELD_AT + game.OBJECT_HANDLE
    g.pm.memory[at : at + 4] = struct.pack("<I", 0x9999)  # the slot now holds another object
    assert not g.shield_up()
    put_shield(g)
    at = SHIELD_AT + game.OBJECT_SELF
    g.pm.memory[at : at + 8] = struct.pack("<Q", 0x1800)  # not an object of the pool
    assert not g.shield_up()
    put_shield(g, pointer=0x1C00)  # points somewhere else entirely
    assert not g.shield_up()


def test_a_link_pointing_outside_the_game_is_an_error() -> None:
    g = fake_game()
    put_shield(g, pointer=0x10_0000)  # past the fake's memory: a short read
    with pytest.raises(game.GameError, match="short read"):
        g.shield_up()


def test_an_exited_game_is_reported_as_not_running() -> None:
    g = fake_game()
    g.alive = False
    with pytest.raises(game.GameNotRunning):
        g.inventory()


# --- the shipped patterns ------------------------------------------------------------------


@pytest.mark.parametrize("target", TARGETS, ids=lambda t: t.name)
def test_shipped_patterns_are_well_formed(target: Target) -> None:
    assert len(target.patterns) >= 2, "each target needs a second pattern to cross-check"
    if target.name == "rando_blob":  # fields deep inside it must be cross-checked by its start
        assert any(p.addend == 0 for p in target.patterns)
    assert any(p.kind == "rip" for p in target.patterns), "a start pattern alone is not enough"
    for p in target.patterns:
        toks = p.text.split()
        assert all(t == "??" or len(t) == 2 for t in toks)
        assert sum(t != "??" for t in toks) >= 12, "too few fixed bytes to trust"
        assert p.kind in ("rip", "at")
        if p.kind == "rip":
            assert toks[p.disp_at : p.disp_at + 4] == ["??"] * 4, "the address field must be wild"
            assert p.disp_at + 4 <= p.rip_end <= len(toks)
        game.compile_pattern(p.text)
