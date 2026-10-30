"""The version check a pull request into main must pass (tools/release_check.py)."""

from __future__ import annotations

import subprocess
from pathlib import Path

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


@pytest.mark.parametrize(
    ("version", "on_main", "code"),
    [
        ("0.30.0", "0.29.0", 0),  # raised above main
        # Main holds 0.30.0 still waiting for approval (no tag yet): another merge needs a raise.
        ("0.30.0", "0.30.0", 1),
        ("0.29.0", "0.30.0", 1),
    ],
)
def test_the_version_must_be_above_main(monkeypatch, version, on_main, code) -> None:
    monkeypatch.setattr(rc, "client_version", lambda: version)
    monkeypatch.setattr(rc, "version_at", lambda ref: on_main if ref == "origin/main" else None)
    assert rc.main(["--base", "origin/main", "--above", "v0.28.0"]) == code
    assert rc.main(["--base", "origin/main"]) == code  # before the first release too


def _tree_with(repo: Path, native_py: str) -> str:
    """A git tree in ``repo`` holding client/native.py; its id works as a ref for ``git show``."""
    (repo / "client").mkdir(exist_ok=True)
    (repo / "client" / "native.py").write_text(native_py, encoding="utf-8")
    subprocess.run(["git", "add", "client/native.py"], cwd=repo, check=True)
    return subprocess.run(
        ["git", "write-tree"], cwd=repo, check=True, capture_output=True, text=True
    ).stdout.strip()


def test_version_at_reads_main_and_fails_closed(tmp_path: Path) -> None:
    subprocess.run(["git", "init", "-q", str(tmp_path)], check=True)
    tree = _tree_with(tmp_path, 'X = 1\nVERSION = "0.25.3"  # the helper\'s\n')
    assert rc.version_at(tree, repo=tmp_path) == "0.25.3"
    with pytest.raises(SystemExit, match="cannot read"):
        rc.version_at("no-such-ref", repo=tmp_path)
    no_version = _tree_with(tmp_path, "X = 1\n")
    with pytest.raises(SystemExit, match="no VERSION"):
        rc.version_at(no_version, repo=tmp_path)


def test_an_unreadable_main_fails_the_gate(monkeypatch) -> None:
    monkeypatch.setattr(rc, "client_version", lambda: "0.30.0")

    def unreadable(ref: str) -> str:
        raise SystemExit(f"cannot read client/native.py at {ref}")

    monkeypatch.setattr(rc, "version_at", unreadable)
    with pytest.raises(SystemExit):
        rc.main(["--base", "origin/main", "--above", "v0.29.0"])
