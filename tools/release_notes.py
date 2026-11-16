"""A release's message on GitHub: its notes for players, the install guide, the list of changes.

    .venv/Scripts/python tools/release_notes.py 1.2.0 --previous v1.1.0      # print it
    .venv/Scripts/python tools/release_notes.py --previous v1.1.0 --notes draft.md
                                                     # preview a draft, for the repo's version
    .venv/Scripts/python tools/release_notes.py 1.2.0 --previous v1.1.0 --out FILE

The message is, in order:

1. The version's notes, ``docs/releases/<version>.md``: "What's new" and "Fixes", written by
   hand for players in the pull request that raises the version (``docs/releases/README.md``).
   An "## Updating from <previous>" section, which only a release that needs an extra step from
   the player has, is not printed here but in 2.
2. The install guide, ``.github/release-footer.md``, with ``{{VERSION}}`` and ``{{PREVIOUS}}``
   filled in, and the notes' update steps, if any, at ``{{UPDATE_STEPS}}`` after its own.
3. "**Full list of changes:**" and the compare link from the previous release.

``.github/workflows/release.yml`` publishes it, with ``--previous`` the latest release's tag.
Without ``--previous`` it is the first release's message: 1.0.0 had nothing to update from, so
its notes carry their own install steps, and the message has no install guide and links every
commit up to the release. ``tests/test_release_notes.py`` checks that the files here rebuild
1.0.0's and 1.1.0's messages exactly as published.

``tools/release_check.py`` refuses a release whose notes ``problems`` names: missing, empty,
the template's ``TODO`` still in them, or an Updating section that is malformed or not from the
last release.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from build_apworld import client_version
from protected import ROOT, assert_writable

NOTES = ROOT / "docs" / "releases"  # <version>.md for each release; README.md is the guide
FOOTER = ROOT / ".github" / "release-footer.md"
REPO_URL = "https://github.com/mcawful/zexion-ap"
PLACEHOLDER = "TODO"  # what docs/releases/TEMPLATE.md marks every line to replace with

_VERSION = re.compile(r"v?(\d+\.\d+\.\d+)")
_UPDATING = re.compile(r"^## Updating from (\S+)[ \t]*$", re.MULTILINE)
_HEADING = re.compile(r"^## ", re.MULTILINE)
_STEP = re.compile(r"(- |  )\S")  # a list item, or a line continuing one


def bare(version: str) -> str:
    """``v1.1.0`` or ``1.1.0`` as ``1.1.0``; ValueError for anything else."""
    match = _VERSION.fullmatch(version.strip())
    if not match:
        raise ValueError(f"not a version: {version!r}")
    return match.group(1)


def notes_path(version: str) -> Path:
    return NOTES / f"{bare(version)}.md"


def _rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def split(text: str) -> tuple[str, str | None, str]:
    """The notes without their Updating section, the version it updates from, and its steps.

    ``(notes, None, "")`` when they have none. ValueError for more than one.
    """
    found = list(_UPDATING.finditer(text))
    if not found:
        return text.strip(), None, ""
    if len(found) > 1:
        raise ValueError('more than one "## Updating from" section')
    match = found[0]
    after = _HEADING.search(text, match.end())
    end = after.start() if after else len(text)
    rest = (text[: match.start()].rstrip() + "\n\n" + text[end:].lstrip()).strip()
    return rest, match.group(1), text[match.end() : end].strip()


def problems(version: str, previous: str | None = None, path: Path | None = None) -> list[str]:
    """Why the notes for ``version`` can't be published (none: they can).

    ``previous`` is the last release, when known: an Updating section must be from it.
    ``path`` is the notes file, ``docs/releases/<version>.md`` by default.
    """
    path = path or notes_path(version)
    name = _rel(path)
    if not path.is_file():
        return [
            f"{name} is missing: write {bare(version)}'s notes for players, from "
            "docs/releases/TEMPLATE.md (docs/releases/README.md)"
        ]
    text = path.read_text(encoding="utf-8")
    if not text.strip():
        return [f"{name} is empty: write {bare(version)}'s notes for players"]
    found = []
    if PLACEHOLDER in text:
        found.append(f"{name} still has the template's {PLACEHOLDER}: replace or delete each one")
    if "{{" in text:
        found.append(f"{name} has a {{{{...}}}} placeholder: only the install guide takes those")
    try:
        body, since, steps = split(text)
    except ValueError as exc:
        return [*found, f"{name}: {exc}"]
    if not body:
        found.append(f"{name} has only an Updating section: say what's new or fixed too")
    if since is not None:
        try:
            since = bare(since)
        except ValueError:
            found.append(f'{name}: "## Updating from {since}" does not name a version')
        else:
            if previous is not None and since != bare(previous):
                found.append(
                    f'{name}: "## Updating from {since}", but the last release is {bare(previous)}'
                )
        lines = steps.splitlines()
        if not lines or not all(_STEP.match(line) for line in lines):
            found.append(
                f'{name}: its "## Updating from" section must be a list ("- ...") of the extra '
                "steps, to go under the install guide's own"
            )
    return found


def render(version: str, previous: str | None, path: Path | None = None) -> str:
    """The release's whole message; SystemExit naming what's wrong if its notes have problems.

    ``previous`` is the last release's tag or version; None only for the first release.
    """
    version = bare(version)
    path = path or notes_path(version)
    wrong = problems(version, previous, path)
    if wrong:
        raise SystemExit("\n".join(wrong))
    body, since, steps = split(path.read_text(encoding="utf-8"))
    parts = [body]
    if previous is None:
        if since is not None:
            raise SystemExit(f"{_rel(path)}: the first release has nothing to update from")
        link = f"[all commits up to v{version}]({REPO_URL}/commits/v{version})"
    else:
        previous = bare(previous)
        footer = FOOTER.read_text(encoding="utf-8")
        footer = footer.replace("{{VERSION}}", version).replace("{{PREVIOUS}}", previous)
        footer = footer.replace("{{UPDATE_STEPS}}\n", f"{steps}\n" if steps else "")
        if "{{" in footer:
            raise SystemExit(f"{_rel(FOOTER)}: a placeholder is left after filling it in")
        parts.append(footer.strip())
        link = f"[v{previous}...v{version}]({REPO_URL}/compare/v{previous}...v{version})"
    parts.append(f"**Full list of changes:** {link}")
    return "\n\n".join(parts) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("version", nargs="?", help="the release's version (default: the repo's)")
    parser.add_argument("--previous", help="the last release's tag; omit only for the first")
    parser.add_argument("--notes", type=Path, help="a notes file other than docs/releases'")
    parser.add_argument("--out", type=Path, help="write the message here, not to stdout")
    args = parser.parse_args(argv)
    message = render(args.version or client_version(), args.previous, args.notes)
    if args.out is None:
        sys.stdout.reconfigure(encoding="utf-8", newline="\n")
        sys.stdout.write(message)
    else:
        assert_writable(args.out).write_text(message, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
