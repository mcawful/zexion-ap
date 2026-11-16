"""A release's message (tools/release_notes.py): the notes, the install guide, the changes link.

The published messages are pinned by hash, so the test needs no network. When one fails, compare
the two by hand (``gh release view`` adds a newline at the end):

    gh release view v1.1.0 --json body -q .body > published.md
    .venv/Scripts/python tools/release_notes.py 1.1.0 --previous v1.0.0 > built.md
"""

from __future__ import annotations

import hashlib
from pathlib import Path

import pytest

import release_notes as rn
from build_apworld import client_version

ROOT = Path(__file__).resolve().parent.parent
WORKFLOW = ROOT / ".github" / "workflows" / "release.yml"

# sha256 of each release's message as published on GitHub (the release's "body"), 2026-11-09.
PUBLISHED = {
    ("1.0.0", None): "01672568b4672d4f5fda6eae420f1317d600a86d1ce88b492e0805d077620a55",
    ("1.1.0", "v1.0.0"): "2e46f2cc8fab48f71331b0a3f662050574359b7784565b511ad6a82becba0677",
}

NOTES = """\
## What's new

- **Something new.** A line about it.

## Fixes

- Something that was wrong no longer is.
"""


def write(tmp_path: Path, text: str, name: str = "1.2.0.md") -> Path:
    path = tmp_path / name
    path.write_text(text, encoding="utf-8")
    return path


@pytest.mark.parametrize(("version", "previous"), list(PUBLISHED))
def test_the_notes_here_rebuild_the_published_messages(version, previous) -> None:
    message = rn.render(version, previous)
    digest = hashlib.sha256(message.encode("utf-8")).hexdigest()
    assert digest == PUBLISHED[version, previous], message


def test_the_workflow_publishes_what_this_builds() -> None:
    text = WORKFLOW.read_text(encoding="utf-8")
    assert "set -euo pipefail" in text
    assert 'previous="$(gh api "repos/$GITHUB_REPOSITORY/releases/latest" -q .tag_name)"' in text
    assert 'python tools/release_notes.py "$VERSION" --previous "$previous" --out "$notes"' in text
    assert '--notes-file "$notes"' in text
    assert "generate-notes" not in text  # no list of pull requests, from the API or from gh
    assert rn.NOTES == ROOT / "docs" / "releases"
    assert rn.FOOTER == ROOT / ".github" / "release-footer.md"


def test_the_repos_version_has_finished_notes() -> None:
    # The pull request that raises the version writes them; CI on develop says so first.
    assert rn.problems(client_version()) == []


def test_every_notes_file_is_named_by_a_version() -> None:
    for path in rn.NOTES.glob("*.md"):
        if path.name not in {"README.md", "TEMPLATE.md"}:
            assert rn.bare(path.stem) == path.stem, path.name


def test_a_release_without_update_steps_gets_the_guides_alone(tmp_path: Path) -> None:
    message = rn.render("1.2.0", "v1.1.0", write(tmp_path, NOTES))
    assert message.startswith(NOTES.strip() + "\n\n## Installing or updating\n")
    assert "**Updating from 1.1.0**" in message
    assert "keep 1.1.0 until it ends" in message
    assert "blob/v1.2.0/worlds/zexion/docs/setup_en.md" in message
    assert "- Then connect the client once with Zexion running.\n\nSomething wrong?" in message
    assert "{{" not in message
    assert message.endswith(
        "**Full list of changes:** [v1.1.0...v1.2.0]"
        "(https://github.com/mcawful/zexion-ap/compare/v1.1.0...v1.2.0)\n"
    )


def test_update_steps_go_under_the_guides_own(tmp_path: Path) -> None:
    steps = "- Delete `old.txt` from Zexion's folder.\n- And then this,\n  over two lines."
    text = NOTES + f"\n## Updating from 1.1.0\n\n{steps}\n"
    message = rn.render("1.2.0", "v1.1.0", write(tmp_path, text))
    assert "## Updating from" not in message
    assert f"- Then connect the client once with Zexion running.\n{steps}\n\nSomething" in message
    assert message.startswith(NOTES.strip() + "\n\n## Installing")


def test_an_updating_section_may_come_between_others(tmp_path: Path) -> None:
    text = "## What's new\n\n- A.\n\n## Updating from 1.1.0\n\n- Step.\n\n## Fixes\n\n- B.\n"
    body, since, steps = rn.split(text)
    assert body == "## What's new\n\n- A.\n\n## Fixes\n\n- B."
    assert (since, steps) == ("1.1.0", "- Step.")


@pytest.mark.parametrize(
    ("text", "complaint"),
    [
        ("", "is empty"),
        ("  \n\n", "is empty"),
        (NOTES + "- TODO: one more.\n", "TODO"),
        (NOTES + "- {{VERSION}}\n", "placeholder"),
        (NOTES + "\n## Updating from 1.0.0\n\n- Step.\n", "the last release is 1.1.0"),
        (NOTES + "\n## Updating from soon\n\n- Step.\n", "does not name a version"),
        (NOTES + "\n## Updating from 1.1.0\n\nDo this first.\n", "must be a list"),
        (NOTES + "\n## Updating from 1.1.0\n", "must be a list"),
        (NOTES + "\n## Updating from 1.1.0\n\n- A.\n\n## Updating from 1.1.0\n\n- B.\n", "more"),
        ("## Updating from 1.1.0\n\n- Step.\n", "only an Updating section"),
    ],
)
def test_unfinished_notes_are_refused(tmp_path: Path, text, complaint) -> None:
    path = write(tmp_path, text)
    found = rn.problems("1.2.0", "v1.1.0", path)
    assert any(complaint in problem for problem in found), found
    with pytest.raises(SystemExit, match=complaint):
        rn.render("1.2.0", "v1.1.0", path)


def test_missing_notes_are_refused(tmp_path: Path, monkeypatch) -> None:
    monkeypatch.setattr(rn, "NOTES", tmp_path)
    assert "1.2.0.md is missing" in rn.problems("1.2.0")[0]
    with pytest.raises(SystemExit, match="missing"):
        rn.render("1.2.0", "v1.1.0")


def test_the_template_is_refused_as_it_is() -> None:
    found = rn.problems("1.2.0", "v1.1.0", rn.NOTES / "TEMPLATE.md")
    assert any("TODO" in problem for problem in found), found


def test_the_updating_section_is_checked_only_against_a_known_release(tmp_path: Path) -> None:
    path = write(tmp_path, NOTES + "\n## Updating from 1.0.0\n\n- Step.\n")
    assert rn.problems("1.2.0", None, path) == []
    assert rn.problems("1.2.0", "1.0.0", path) == []


def test_the_first_release_has_nothing_to_update_from(tmp_path: Path) -> None:
    path = write(tmp_path, NOTES)
    message = rn.render("1.2.0", None, path)
    assert message == (
        NOTES.strip() + "\n\n**Full list of changes:** [all commits up to v1.2.0]"
        "(https://github.com/mcawful/zexion-ap/commits/v1.2.0)\n"
    )
    path = write(tmp_path, NOTES + "\n## Updating from 1.1.0\n\n- Step.\n")
    with pytest.raises(SystemExit, match="nothing to update from"):
        rn.render("1.2.0", None, path)


def test_the_command_writes_the_message(tmp_path: Path, capsys) -> None:
    notes = write(tmp_path, NOTES)
    out = tmp_path / "message.md"
    assert rn.main(["1.2.0", "--previous", "v1.1.0", "--notes", str(notes), "--out", str(out)]) == 0
    assert out.read_bytes() == rn.render("1.2.0", "v1.1.0", notes).encode("utf-8")
    assert rn.main(["1.2.0", "--previous", "v1.1.0", "--notes", str(notes)]) == 0
    assert capsys.readouterr().out == rn.render("1.2.0", "v1.1.0", notes)
