"""The release's version, and versions as the release tools read them.

The version is ``VERSION`` in ``client/native.py`` (``client_version``; ``version_in`` reads it
from that file's text, here or at a commit). A version as given to the release tools, a tag
(``v1.2.0``) or a bare one (``1.2.0``), is read once, by ``_VERSION``: ``parse`` gives its
numbers (``tools/release_check.py`` compares them), ``bare`` the version without its ``v``
(``tools/release_notes.py`` names the notes' files by it). Kept apart from
``tools/build_apworld.py``, so that the release tools need not import the builder.
"""

from __future__ import annotations

import re

from protected import ROOT

_VERSION = re.compile(r"v?(\d+)\.(\d+)\.(\d+)")


def version_in(text: str, where: str) -> str:
    """The ``VERSION`` that ``text``, client/native.py here or at a commit (``where``), sets."""
    match = re.search(r'^VERSION = "([0-9.]+)"', text, re.M)
    if not match:
        raise SystemExit(f"{where} has no VERSION")
    return match.group(1)


def client_version() -> str:
    """The repo's version: ``client/native.py``'s ``VERSION``."""
    native = ROOT / "client" / "native.py"
    return version_in(native.read_text(encoding="utf-8"), "client/native.py")


def _groups(text: str) -> tuple[str, str, str]:
    match = _VERSION.fullmatch(text.strip())
    if not match:
        raise ValueError(f"not a version: {text!r}")
    return match.groups()


def parse(text: str) -> tuple[int, ...]:
    """``0.29.0`` or ``v0.29.0`` as a tuple of numbers; ValueError for anything else."""
    return tuple(int(part) for part in _groups(text))


def bare(text: str) -> str:
    """``v1.1.0`` or ``1.1.0`` as ``1.1.0``; ValueError for anything else."""
    return ".".join(_groups(text))
