"""Shared fixtures for the tests."""

from __future__ import annotations

import pytest

from client import ticks


@pytest.fixture(autouse=True)
def no_stored_linux_seed(monkeypatch) -> None:
    """The Linux helper's seed files are the machine's, not a test's: none is read (a test of
    ticks.linux_stored_seed itself keeps the function it took at import)."""
    monkeypatch.setattr(ticks, "linux_stored_seed", lambda mod, name: None)
