"""client/patterns.py against patterns_frozen.json, a copy of every build and pattern as they
were before the file was rewritten to one line per pattern (``rip`` and ``at``): the rewrite
changed none. A change to the patterns updates the copy in the same change, so it shows in
review: ``.venv/Scripts/python tests/test_patterns.py`` writes it from client/patterns.py.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

if __name__ == "__main__":
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from client.patterns import KNOWN_BUILDS, TARGETS, Pattern, at, rip  # noqa: E402

FROZEN_PATH = Path(__file__).parent / "patterns_frozen.json"


def builds() -> list[list]:
    return [[b.name, b.sha256, b.timestamp, b.size_of_image] for b in KNOWN_BUILDS]


def targets() -> list[dict]:
    return [
        {
            "name": t.name,
            "description": t.description,
            "b1_rva": t.b1_rva,
            "patterns": [[p.text, p.disp_at, p.kind, p.rip_end, p.addend] for p in t.patterns],
        }
        for t in TARGETS
    ]


def frozen_text() -> str:
    """The copy as written: one line per build, target and pattern."""
    lines = ["{", f' "builds": [{", ".join(json.dumps(b) for b in builds())}],', ' "targets": [']
    listed = targets()
    for i, t in enumerate(listed):
        head = {k: t[k] for k in ("name", "description", "b1_rva")}
        lines.append(f"  {json.dumps(head)[:-1]}, " + '"patterns": [')
        pats = [f"   {json.dumps(p)}" for p in t["patterns"]]
        lines.append(",\n".join(pats))
        lines.append("  ]}" + ("," if i < len(listed) - 1 else ""))
    return "\n".join(lines + [" ]", "}"]) + "\n"


def test_the_builds_are_the_frozen_ones() -> None:
    assert builds() == json.loads(FROZEN_PATH.read_text(encoding="utf-8"))["builds"]


def test_every_target_and_pattern_is_the_frozen_one() -> None:
    frozen = json.loads(FROZEN_PATH.read_text(encoding="utf-8"))["targets"]
    got = targets()
    assert [t["name"] for t in got] == [t["name"] for t in frozen]
    for mine, theirs in zip(got, frozen, strict=True):
        assert mine == theirs, mine["name"]


def test_the_constructors_build_each_kind() -> None:
    assert rip("48 8D 05 ?? ?? ?? ??", 3, 7) == Pattern("48 8D 05 ?? ?? ?? ??", 3, "rip", 7, 0)
    assert rip("C6 05 ?? ?? ?? ?? 01", 2, 7, -2).addend == -2
    assert at("4C 8B DC") == Pattern("4C 8B DC", 0, "at", 0, 0)
    assert at("4C 8B DC", 7) == Pattern("4C 8B DC", 0, "at", 0, 7)


if __name__ == "__main__":
    FROZEN_PATH.write_bytes(frozen_text().encode("utf-8"))
    print(f"wrote {FROZEN_PATH}")
