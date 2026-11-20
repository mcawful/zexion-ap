"""tools/gen_native_patterns.py: native/src/findaddr_patterns.h is what it writes from
client/patterns.py, so the helper never scans with a pattern the client does not."""

from __future__ import annotations

import re

import pytest

import gen_native_patterns as gnp
from client import native, patterns


def test_the_header_is_up_to_date() -> None:
    # Reads bytes, not text: the file has LF line ends on every platform.
    current = gnp.OUT.read_bytes().decode("utf-8")
    assert current == gnp.render(), "run tools/gen_native_patterns.py"


def test_it_holds_the_targets_the_helper_takes_in_the_clients_order() -> None:
    text = gnp.render()
    names = re.findall(r'^    \{"(\w+)", k_\w+, \d+\},$', text, re.M)
    assert names == list(native.CONFIG_KEYS)
    assert not patterns.CLIENT_ONLY_TARGETS & set(names)


def test_it_holds_each_pattern_as_the_client_reads_it() -> None:
    text = gnp.render()
    for target in patterns.TARGETS:
        if target.name in patterns.CLIENT_ONLY_TARGETS:
            continue
        block = re.search(rf"k_{target.name}\[\] = \{{\n(.*?)\n\}};", text, re.S)
        assert block, target.name
        rows = [
            re.fullmatch(r'    \{"([^"]+)", (\d+), Kind::k(Rip|At), (\d+), (-?\w+)\},', line)
            for line in block.group(1).split("\n")
        ]
        assert all(rows), target.name
        got = [(r[1], int(r[2]), r[3].lower(), int(r[4]), int(r[5], 0)) for r in rows]
        want = [(p.text, p.disp_at, p.kind, p.rip_end, p.addend) for p in target.patterns]
        assert got == want, target.name
        assert f'{{"{target.name}", k_{target.name}, {len(target.patterns)}}},' in text


def test_it_holds_no_resolved_address() -> None:
    # Target.rvas is for checks only: a build's addresses never go into the helper.
    assert "rvas" not in gnp.render()
    text = gnp.render()
    for target in patterns.TARGETS:
        for rva in target.rvas.values():
            assert f"{rva:#x}" not in text.lower() or rva < 0x100, target.name


@pytest.mark.parametrize(
    ("value", "text"),
    [(0, "0"), (9, "9"), (10, "0xa"), (0x3D3, "0x3d3"), (-0x10, "-0x10"), (-3, "-3")],
)
def test_numbers_are_written_as_c_literals(value: int, text: str) -> None:
    assert gnp.number(value) == text


@pytest.mark.parametrize(
    "pattern",
    [
        patterns.Pattern("48 8D 0x ??", 0, "at", 0, 0),
        patterns.Pattern("48 8d 05", 0, "at", 0, 0),
        patterns.Pattern("", 0, "at", 0, 0),
        patterns.Pattern("48 8D 05 ?? ?? ?? ??", 0, "rva", 0, 0),
        patterns.Pattern("48 8D 05 ?? ?? ?? ??", 4, "rip", 7, 0),
        patterns.Pattern("48 8D 05 ?? ?? ?? ??", -1, "rip", 7, 0),
    ],
)
def test_a_pattern_the_helper_could_not_read_is_refused_when_generated(pattern) -> None:
    with pytest.raises(SystemExit):
        gnp.pattern_line(pattern)


def test_check_says_when_the_file_is_stale(tmp_path, monkeypatch, capsys) -> None:
    stale = tmp_path / "findaddr_patterns.h"
    stale.write_bytes(b"// old\n")
    monkeypatch.setattr(gnp, "OUT", stale)
    assert gnp.main(["--check"]) == 1
    assert "out of date" in capsys.readouterr().err
    assert stale.read_bytes() == b"// old\n"  # --check writes nothing
    stale.write_bytes(gnp.render().encode("utf-8"))
    assert gnp.main(["--check"]) == 0
