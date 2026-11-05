"""The release's version, and the check a pull request into main must pass: it raises it.

    .venv/Scripts/python tools/release_check.py                         # print the version
    .venv/Scripts/python tools/release_check.py --base origin/main --above v0.28.0 v0.29.0
                                                                        # fail unless higher

The version is the one number the client, the helper and the world share
(``client/native.py`` ``VERSION``; ``tools/build_apworld.py`` refuses a release where they
differ). CI passes every release's tag as ``--above`` (none before the first release), and
``--base`` names main as fetched: the version must be above the highest release and above the
version on main. Main always holds the last release's version or one still waiting for approval,
which no tag shows yet; without ``--base`` a second merge under that number would pass. Any
error reading main's version fails the check.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

from build_apworld import client_version, version_in
from protected import ROOT


def parse(text: str) -> tuple[int, ...]:
    """``0.29.0`` or ``v0.29.0`` as a tuple of numbers; ValueError for anything else."""
    match = re.fullmatch(r"v?(\d+)\.(\d+)\.(\d+)", text.strip())
    if not match:
        raise ValueError(f"not a version: {text!r}")
    return tuple(int(part) for part in match.groups())


def version_at(ref: str, repo: Path = ROOT) -> str:
    """``client/native.py``'s ``VERSION`` at git ``ref``; SystemExit if it can't be read."""
    try:
        shown = subprocess.run(
            ["git", "show", f"{ref}:client/native.py"],
            cwd=repo,
            capture_output=True,
            check=True,
            text=True,
            encoding="utf-8",
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        detail = (getattr(exc, "stderr", None) or str(exc)).strip()
        raise SystemExit(f"cannot read client/native.py at {ref}: {detail}") from exc
    return version_in(shown.stdout, f"client/native.py at {ref}")


def _refuse(version: str, what: str) -> int:
    print(
        f"version {version} is not above {what}: raise VERSION (client/native.py), kVersion "
        "(native/src/dllmain.cpp) and world_version (worlds/zexion/archipelago.json) together",
        file=sys.stderr,
    )
    return 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--above", nargs="*", default=[], help="the releases' tags, if any")
    parser.add_argument("--base", help="a git ref of main: the version must be above main's too")
    args = parser.parse_args(argv)
    version = client_version()
    last = max(args.above, key=parse, default="")
    if last and parse(version) <= parse(last):
        return _refuse(version, f"the last release {last}")
    if args.base is not None:
        on_base = version_at(args.base)
        if parse(version) <= parse(on_base):
            return _refuse(version, f"{on_base}, the version on {args.base}")
    print(version)
    return 0


if __name__ == "__main__":
    sys.exit(main())
