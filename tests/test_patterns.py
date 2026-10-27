"""client/patterns.py against a digest of every build and pattern (FROZEN_SHA256), so a change to
the patterns, accidental or not, fails here until the digest is updated in the same change:
``.venv/Scripts/python tests/test_patterns.py`` prints the new one.
"""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

if __name__ == "__main__":
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from client.patterns import KNOWN_BUILDS, TARGETS, Pattern, at, rip  # noqa: E402

FROZEN_SHA256 = "ebded6421dc4d96ddacee47e191eac29883d3f58588fda49e9ee52a3326126d8"


def digest() -> str:
    """The SHA-256 of every build, and every target's name, description, RVAs and patterns."""
    builds = [[b.name, b.sha256, b.timestamp, b.size_of_image] for b in KNOWN_BUILDS]
    targets = [
        {
            "name": t.name,
            "description": t.description,
            "rvas": dict(t.rvas),
            "patterns": [[p.text, p.disp_at, p.kind, p.rip_end, p.addend] for p in t.patterns],
        }
        for t in TARGETS
    ]
    text = json.dumps({"builds": builds, "targets": targets}, sort_keys=True)
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def test_the_builds_and_patterns_are_the_frozen_ones() -> None:
    assert digest() == FROZEN_SHA256, "patterns.py changed: update FROZEN_SHA256 (module doc)"


def test_the_constructors_build_each_kind() -> None:
    assert rip("48 8D 05 ?? ?? ?? ??", 3, 7) == Pattern("48 8D 05 ?? ?? ?? ??", 3, "rip", 7, 0)
    assert rip("C6 05 ?? ?? ?? ?? 01", 2, 7, -2).addend == -2
    assert at("4C 8B DC") == Pattern("4C 8B DC", 0, "at", 0, 0)
    assert at("4C 8B DC", 7) == Pattern("4C 8B DC", 0, "at", 0, 7)


def test_every_target_has_an_rva_for_each_known_build_and_no_other() -> None:
    names = {b.name for b in KNOWN_BUILDS}
    for t in TARGETS:
        assert set(t.rvas) == names, t.name


if __name__ == "__main__":
    print(digest())
