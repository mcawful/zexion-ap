"""client/game.py on synthetic code and a fake process (no game, no exe)."""

from __future__ import annotations

import struct

import pytest

import game
from patterns import TARGETS, Build, Pattern, Target

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
        game.locate_all(code, 0, [Target("t", "test", 0, (LEA, other))])
    agree = Pattern(other.text, 3, "rip", 7, 0x100 - 0x900 - 11)
    assert game.locate_all(code, 0, [Target("t", "test", 0, (LEA, agree))])["t"] == 0x40 + 7 + 0x100


# --- PE header and build identity ----------------------------------------------------------


def pe_header(stamp: int = 0x6A3F82DE, size_of_image: int = 0x1204000) -> bytes:
    header = bytearray(0x1000)
    header[0x3C:0x40] = struct.pack("<I", 0x80)
    header[0x80:0x84] = b"PE\0\0"
    header[0x86:0x88] = struct.pack("<H", 2)  # two sections
    header[0x88:0x8C] = struct.pack("<I", stamp)
    header[0x94:0x96] = struct.pack("<H", 0xF0)  # optional header size
    header[0x80 + 24 + 56 : 0x80 + 24 + 60] = struct.pack("<I", size_of_image)
    first = 0x80 + 24 + 0xF0
    header[first : first + 8] = b".rdata\0\0"
    header[first + 40 : first + 48] = b".text\0\0\0"
    header[first + 48 : first + 56] = struct.pack("<II", 0x2000, 0x1000)  # vsize, va
    return bytes(header)


def test_code_section_reads_the_pe_header() -> None:
    assert game.code_section(pe_header()) == (0x1000, 0x2000)
    with pytest.raises(game.GameError):
        game.code_section(bytes(0x1000))


def test_known_build_is_identified_and_others_refused() -> None:
    b1 = Build("B1", "0" * 64, 0x6A3F82DE, 0x1204000)
    assert game.identify_build(pe_header(), [b1]) is b1
    with pytest.raises(game.UnknownBuild):
        game.identify_build(pe_header(stamp=0x6A400000), [b1])
    with pytest.raises(game.UnknownBuild):
        game.identify_build(pe_header(size_of_image=0x1205000), [b1])


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


class FakeProcess:
    def __init__(self, size: int = 0x100):
        self.memory = bytearray(size)
        self.fail_write = False

    def read_bytes(self, address: int, size: int) -> bytes:
        return bytes(self.memory[address : address + size])

    def write_bytes(self, address: int, data: bytes, size: int) -> None:
        if self.fail_write:
            raise OSError("access denied")
        self.memory[address : address + size] = data


class FakeGame(game.Game):
    alive = True

    def _alive(self) -> bool:
        return self.alive


def fake_game() -> FakeGame:
    return FakeGame(pm=FakeProcess(), base=0, rva={"inventory": 0x10}, pid=1, build=None)


def test_set_item_is_compare_and_set() -> None:
    g = fake_game()
    g.set_item(17, expected=0, value=1)
    assert g.inventory()[17] == 1
    with pytest.raises(game.GameError, match="not the expected"):
        g.set_item(17, expected=0, value=1)  # it holds 1 now: refused, nothing written


def test_set_item_refuses_items_and_values_it_should_not_write() -> None:
    g = fake_game()
    with pytest.raises(ValueError, match="not an item"):
        g.set_item(50, 0, 1)  # Energy Shield: not written until its byte is understood
    with pytest.raises(ValueError, match="fit a byte"):
        g.set_item(3, 0, 256)


def test_a_failed_write_says_its_state_is_unknown() -> None:
    g = fake_game()
    g.pm.fail_write = True
    with pytest.raises(game.WriteStateUnknown):
        g.set_item(17, 0, 1)


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
    for p in target.patterns:
        toks = p.text.split()
        assert all(t == "??" or len(t) == 2 for t in toks)
        assert toks[p.disp_at : p.disp_at + 4] == ["??"] * 4, "the address field must be wild"
        assert sum(t != "??" for t in toks) >= 12, "too few fixed bytes to trust"
        assert p.kind == "rip"
        assert p.disp_at + 4 <= p.rip_end <= len(toks)
        game.compile_pattern(p.text)
