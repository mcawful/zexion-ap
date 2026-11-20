"""The helper's own address scan (native/src/findaddr.cpp) against the client's (client/game.py),
on a copy of the game's exe.

Never in CI: there is no game there. With ZEXION_EXE naming a copy (``tools/game_data.py --exe``
puts one in lab/data/<exe-hash-12>/) and the native tests built (docs/DEVELOPING.md), it runs the
helper's scanner on that exe's .text and checks it finds every address the client does, at the
same places. ``ZEXION_REQUIRE=scan`` makes a skipped run fail.
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import pytest

from client import game, native, pe

EXE = os.environ.get("ZEXION_EXE", "")
TESTS = Path(__file__).resolve().parent.parent / "native" / "build" / "Release"
RUNNER = TESTS / "zap_findaddr_tests.exe"
REQUIRED = {e.strip() for e in os.environ.get("ZEXION_REQUIRE", "").split(",")} - {""}


@pytest.mark.skipif(
    sys.platform != "win32" or not EXE or not RUNNER.is_file(),
    reason="needs ZEXION_EXE (a copy of the game's exe) and the built native tests",
)
def test_the_helpers_scan_finds_what_the_clients_does(tmp_path: Path) -> None:
    exe = Path(EXE)
    data = exe.read_bytes()
    text_rva, size = game.code_section(data[:0x1000])
    pe_text = _section_bytes(data, text_rva, size)
    client = game.locate_all(pe_text, text_rva, optional=())
    out = tmp_path / "scan.txt"
    run = subprocess.run(
        [str(RUNNER)],
        env={**os.environ, "ZEXION_EXE": str(exe), "ZEXION_SCAN_OUT": str(out)},
        capture_output=True,
        text=True,
        timeout=120,
        check=False,
    )
    assert run.returncode == 0, run.stdout + run.stderr
    ours = {
        key: int(value, 16)
        for key, value in (line.split("=", 1) for line in out.read_text().splitlines())
    }
    assert ours == {key: client[key] for key in native.CONFIG_KEYS}


def _section_bytes(data: bytes, rva: int, size: int) -> bytes:
    """The loaded section: its raw bytes, zeros past them up to its virtual size."""
    header = pe.parse(data[:0x1000])
    section = next(s for s in header.sections if s.va == rva)
    raw = data[section.raw : section.raw + min(section.rsize, size)]
    return raw + bytes(size - len(raw))


@pytest.mark.skipif("scan" not in REQUIRED, reason="ZEXION_REQUIRE does not list scan")
def test_the_real_exe_check_ran_where_it_was_required() -> None:
    assert EXE and RUNNER.is_file()
