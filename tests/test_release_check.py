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


def test_a_raised_version_passes(monkeypatch, capsys) -> None:
    monkeypatch.setattr(rc, "client_version", lambda: "0.30.0")
    assert rc.main(["--above", "v0.29.0"]) == 0
    assert capsys.readouterr().out.strip() == "0.30.0"


def test_the_same_or_a_lower_version_fails(monkeypatch) -> None:
    monkeypatch.setattr(rc, "client_version", lambda: "0.29.0")
    assert rc.main(["--above", "v0.29.0"]) == 1
    assert rc.main(["--above", "v0.30.1"]) == 1


def test_the_first_release_has_nothing_to_be_above(monkeypatch) -> None:
    monkeypatch.setattr(rc, "client_version", lambda: "0.29.0")
    assert rc.main([]) == 0
