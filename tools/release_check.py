"""The release's version, and the check a pull request into main must pass: it raises it.

    .venv/Scripts/python tools/release_check.py                         # print the version
    .venv/Scripts/python tools/release_check.py --above v0.28.0 v0.29.0 # fail unless higher

The version is the one number the client, the helper and the world share
(``client/native.py`` ``VERSION``; ``tools/build_apworld.py`` refuses a release where they
differ). CI passes every release's tag as ``--above`` (none before the first release); the
version must be above the highest.
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
    parser.add_argument("--above", nargs="*", default=[], help="the releases' tags, if any")
    args = parser.parse_args(argv)
    version = client_version()
    last = max(args.above, key=parse, default="")
    if last and parse(version) <= parse(last):
        print(
            f"version {version} is not above the last release {last}: raise VERSION "
            "(client/native.py), kVersion (native/src/dllmain.cpp) and world_version "
            "(worlds/zexion/archipelago.json) together",
            file=sys.stderr,
        )
        return 1
    print(version)
    return 0


if __name__ == "__main__":
    sys.exit(main())
