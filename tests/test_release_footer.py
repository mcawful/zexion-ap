"""The install guide in each release's message (``.github/release-footer.md``).

``tools/release_notes.py`` puts it after the version's notes, with ``{{VERSION}}`` and
``{{PREVIOUS}}`` filled in and the notes' own update steps at ``{{UPDATE_STEPS}}``
(``tests/test_release_notes.py``: the workflow publishes that). These keep it honest: every file
it names is one the release has, as an asset (the archive for each system, and the apworld on its
own) or in an archive (the Linux helper, ``libzexion_ap.so``, among them), its links lead
somewhere in the repo, and its game build is the setup guide's.
"""

from __future__ import annotations

import re
from pathlib import Path

import build_apworld as bap

ROOT = Path(__file__).resolve().parent.parent
FOOTER = ROOT / ".github" / "release-footer.md"
REPO = "https://github.com/mcawful/zexion-ap/"


def footer() -> str:
    return FOOTER.read_text(encoding="utf-8")


def released_names() -> set[str]:
    """Every file name in the release: its assets (build_apworld.py's dist/, the archives named
    for the version placeholder) and what the archives hold."""
    names = {"zexion.apworld", bap.README}
    names |= {bap.archive_name(platform, "{{VERSION}}") for platform in bap.PLATFORMS}
    names |= {path.name for platform in bap.PLATFORMS for path in bap.helpers(platform)}
    names |= set(bap.GUIDES.values())
    names |= {path.name for path in bap.SAMPLES.glob("*.yaml")}
    return names


def test_every_file_it_names_is_in_the_release() -> None:
    named = set(re.findall(r"`([\w.{}-]+\.(?:apworld|dll|so|md|yaml|zip|tar\.gz))`", footer()))
    assert {
        "zexion.apworld",
        "zexion_ap.dll",
        "version.dll",
        "libzexion_ap.so",
        "zexion-setup-guide.md",
        "zexion-ap-{{VERSION}}-windows.zip",
        "zexion-ap-{{VERSION}}-linux.tar.gz",
    } <= named
    assert named <= released_names(), named - released_names()


def test_its_placeholders_are_the_ones_release_notes_fills() -> None:
    assert set(re.findall(r"\{\{(\w+)\}\}", footer())) == {"VERSION", "PREVIOUS", "UPDATE_STEPS"}
    assert "\n{{UPDATE_STEPS}}\n" in footer()  # a line of its own, after the guide's last step


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
