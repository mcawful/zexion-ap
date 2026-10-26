"""The release's version, and the check a pull request into main must pass: it raises it.

    .venv/Scripts/python tools/release_check.py                 # print the version
    .venv/Scripts/python tools/release_check.py --above v0.29.0 # fail unless it is higher

The version is the one number the client, the helper and the world share
(``client/native.py`` ``VERSION``; ``tools/build_apworld.py`` refuses a release where they
differ). CI passes the newest release tag as ``--above`` (none before the first release).
"""

from __future__ import annotations

import argparse
import re
import sys

from build_apworld import client_version


def parse(text: str) -> tuple[int, ...]:
    """``0.29.0`` or ``v0.29.0`` as a tuple of numbers; ValueError for anything else."""
    match = re.fullmatch(r"v?(\d+)\.(\d+)\.(\d+)", text.strip())
    if not match:
        raise ValueError(f"not a version: {text!r}")
    return tuple(int(part) for part in match.groups())


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--above", default="", help="the newest release's tag, if any")
    args = parser.parse_args(argv)
    version = client_version()
    if args.above and parse(version) <= parse(args.above):
        print(
            f"version {version} is not above the last release {args.above}: raise VERSION "
            "(client/native.py), kVersion (native/src/dllmain.cpp) and world_version "
            "(worlds/zexion/archipelago.json) together",
            file=sys.stderr,
        )
        return 1
    print(version)
    return 0


if __name__ == "__main__":
    sys.exit(main())
