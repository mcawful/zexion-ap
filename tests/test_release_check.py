"""The version check a pull request into main must pass (tools/release_check.py)."""

from __future__ import annotations

import pytest

import release_check as rc


def test_versions_are_read_with_or_without_a_v() -> None:
    assert rc.parse("0.29.0") == (0, 29, 0)
    assert rc.parse("v1.2.10") == (1, 2, 10)
    for bad in ("0.29", "v0.29.0-beta", "latest", ""):
        with pytest.raises(ValueError):
            rc.parse(bad)


def test_versions_compare_as_numbers() -> None:
    assert rc.parse("0.10.0") > rc.parse("0.9.9")


@pytest.mark.parametrize(
    ("version", "released", "code"),
    [
        ("0.30.0", ["v0.29.0"], 0),  # raised
        ("0.29.0", ["v0.29.0"], 1),  # the same
        ("0.29.0", ["v0.30.1"], 1),  # lower
        ("0.30.0", ["v0.30.0", "v0.9.0"], 1),  # the highest release counts, whatever the order
        ("0.30.0", ["v0.9.0", "v0.29.0"], 0),
        ("0.29.0", [], 0),  # the first release has nothing to be above
    ],
)
def test_the_version_must_be_above_every_release(monkeypatch, version, released, code) -> None:
    monkeypatch.setattr(rc, "client_version", lambda: version)
    assert rc.main(["--above", *released] if released else []) == code


def test_a_passing_check_prints_the_version(monkeypatch, capsys) -> None:
    monkeypatch.setattr(rc, "client_version", lambda: "0.30.0")
    assert rc.main(["--above", "v0.29.0"]) == 0
    assert capsys.readouterr().out.strip() == "0.30.0"
