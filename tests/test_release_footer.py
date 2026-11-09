"""The install guide at the bottom of each release's notes (``.github/release-footer.md``).

``.github/workflows/release.yml`` appends it to GitHub's generated notes, with ``{{VERSION}}``
filled in. These keep it honest: the workflow reads it, every file it names is one the release
has, its links lead somewhere in the repo, and its game build is the setup guide's.
"""

from __future__ import annotations

import re
from pathlib import Path

import build_apworld as bap

ROOT = Path(__file__).resolve().parent.parent
FOOTER = ROOT / ".github" / "release-footer.md"
WORKFLOW = ROOT / ".github" / "workflows" / "release.yml"
REPO = "https://github.com/mcawful/zexion-ap/"


def footer() -> str:
    return FOOTER.read_text(encoding="utf-8")


def released_names() -> set[str]:
    """Every file name build_apworld.py puts into dist/, so into the release."""
    names = {"zexion.apworld", bap.HELPER.name, bap.PROXY.name}
    names |= set(bap.GUIDES.values())
    names |= {path.name for path in bap.SAMPLES.glob("*.yaml")}
    return names


def test_the_workflow_appends_the_footer_to_generated_notes() -> None:
    text = WORKFLOW.read_text(encoding="utf-8")
    assert "releases/generate-notes" in text
    assert "previous_tag_name=" in text
    assert 'sed "s/{{VERSION}}/$VERSION/g" .github/release-footer.md >> "$notes"' in text
    assert '--notes-file "$notes"' in text
    assert "--generate-notes" not in text  # gh would put our text above GitHub's


def test_every_file_it_names_is_in_the_release() -> None:
    named = set(re.findall(r"`([\w.-]+\.(?:apworld|dll|md|yaml))`", footer()))
    assert {"zexion.apworld", "zexion_ap.dll", "version.dll", "zexion-setup-guide.md"} <= named
    assert named <= released_names(), named - released_names()


def test_its_only_placeholder_is_the_version() -> None:
    assert set(re.findall(r"\{\{(\w+)\}\}", footer())) == {"VERSION"}


def test_its_links_into_the_repo_lead_to_tracked_paths() -> None:
    links = re.findall(r"\]\((https://[^)\s]+)\)", footer())
    assert links
    for link in links:
        assert link.startswith(REPO), link
        rest = link.removeprefix(REPO)
        if rest.startswith("blob/v{{VERSION}}/"):
            assert (ROOT / rest.removeprefix("blob/v{{VERSION}}/")).is_file(), link
        else:
            assert rest in {"issues", "releases"}, link


def test_its_game_build_is_the_setup_guides() -> None:
    builds = set(re.findall(r"Steam build\s+(\d+)", footer()))
    assert len(builds) == 1
    setup = (ROOT / "worlds" / "zexion" / "docs" / "setup_en.md").read_text(encoding="utf-8")
    assert re.search(rf"Steam build\s+{builds.pop()}\b", setup)
