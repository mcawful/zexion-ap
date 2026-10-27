"""The hooks run on any machine: ``.claude/hooks/run_python.sh`` finds a Python 3 (or fails
closed where it matters), and the hook scripts read their payload as UTF-8."""

from __future__ import annotations

import io
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

import guard_protected
import ruff_on_edit

HOOKS = Path(guard_protected.__file__).parent
RUNNER = HOOKS / "run_python.sh"
SH = shutil.which("sh")
needs_sh = pytest.mark.skipif(SH is None, reason="no sh on PATH (on Windows, Git Bash's)")

SAVES = "C:/Users/someone/AppData/Roaming/Gallant Leaf/Zexion"
PROBE = """\
import json, sys
data = json.loads(sys.stdin.buffer.read())
print("probe ran", file=sys.stderr)
sys.exit(data["code"])
"""


def _sh(runner: Path, script: str, payload: str, path_first: Path | None = None):
    env = dict(os.environ)
    if path_first is not None:
        env["PATH"] = f"{path_first}{os.pathsep}{env.get('PATH', '')}"
    return subprocess.run(
        [SH, str(runner), script],
        input=payload.encode("utf-8"),
        capture_output=True,
        env=env,
    )


def _fake(bin_dir: Path, name: str, body: str) -> None:
    path = bin_dir / name
    path.write_bytes(f"#!/bin/sh\n{body}\n".encode())
    path.chmod(0o755)


@pytest.fixture
def repo(tmp_path: Path) -> Path:
    """A repo with the runner and a probe hook in it, and no .venv."""
    hooks = tmp_path / "repo" / ".claude" / "hooks"
    hooks.mkdir(parents=True)
    shutil.copy(RUNNER, hooks / RUNNER.name)
    (hooks / "probe.py").write_text(PROBE, encoding="utf-8")
    return tmp_path / "repo"


@pytest.fixture
def fake_bin(tmp_path: Path) -> Path:
    """python3 and py that cannot run anything (a Store stub, say), ahead of the real ones."""
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    _fake(bin_dir, "python3", 'echo "Python was not found" >&2; exit 49')
    _fake(bin_dir, "py", "exit 9009")
    return bin_dir


@needs_sh
@pytest.mark.parametrize("code", [0, 2])
def test_the_first_python_that_runs_the_hook_decides(repo, fake_bin, code: int) -> None:
    real = Path(sys.executable).as_posix()
    _fake(fake_bin, "python", f'exec "{real}" "$@"')
    result = _sh(
        repo / ".claude/hooks/run_python.sh", "probe.py", json.dumps({"code": code}), fake_bin
    )
    assert result.returncode == code
    stderr = result.stderr.decode()
    assert "probe ran" in stderr
    assert "not found" not in stderr  # the stub's complaint is dropped


@needs_sh
def test_the_repos_venv_comes_first(repo, fake_bin) -> None:
    real = Path(sys.executable).as_posix()
    venv = repo / ".venv" / "bin"
    venv.mkdir(parents=True)
    _fake(venv, "python", f'echo venv >&2; exec "{real}" "$@"')
    _fake(fake_bin, "python", "exit 1")
    result = _sh(
        repo / ".claude/hooks/run_python.sh", "probe.py", json.dumps({"code": 0}), fake_bin
    )
    assert result.returncode == 0
    assert result.stderr.decode().split() == ["venv", "probe", "ran"]


@needs_sh
@pytest.mark.parametrize(
    ("command", "code"),
    [
        (f"cp lab/a '{SAVES}/x.sav'", 2),  # near the saves: blocked
        ("ls", 1),  # elsewhere: goes ahead, with a hook error notice
    ],
)
def test_with_no_python_it_fails_closed_near_the_saves(repo, fake_bin, command, code) -> None:
    _fake(fake_bin, "python", 'echo "SyntaxError (Python 2)" >&2; exit 1')
    payload = json.dumps({"tool_name": "Bash", "tool_input": {"command": command}})
    result = _sh(repo / ".claude/hooks/run_python.sh", "probe.py", payload, fake_bin)
    assert result.returncode == code
    stderr = result.stderr.decode()
    assert "No Python 3 could run .claude/hooks/probe.py" in stderr
    assert "SyntaxError" not in stderr


@needs_sh
@pytest.mark.parametrize(("file_path", "code"), [(f"{SAVES}/x.sav", 2), ("lab/x.sav", 0)])
def test_the_guard_through_the_runner(file_path: str, code: int) -> None:
    payload = json.dumps({"tool_name": "Write", "tool_input": {"file_path": file_path}})
    result = _sh(RUNNER, "guard_protected.py", payload)
    assert result.returncode == code
    assert (code == 0) == (guard_protected.BLOCK_HINT not in result.stderr.decode())


def test_the_guard_reads_and_answers_in_utf8() -> None:
    # A name whose UTF-8 bytes a Windows code page reads as other characters (or none).
    command = f"cp lab/a '{SAVES}/\u00c1\u0436.sav'"
    payload = json.dumps(
        {"tool_name": "Bash", "tool_input": {"command": command}}, ensure_ascii=False
    )
    result = subprocess.run(
        [sys.executable, str(HOOKS / "guard_protected.py")],
        input=payload.encode("utf-8"),
        capture_output=True,
        env={**os.environ, "PYTHONIOENCODING": "", "PYTHONUTF8": "0"},
    )
    assert result.returncode == 2
    assert "\u00c1\u0436.sav" in result.stderr.decode("utf-8")


def test_ruff_on_edit_reads_its_payload_as_utf8(tmp_path: Path, monkeypatch) -> None:
    source = tmp_path / "\u00c1\u0436.py"
    source.write_text("x = 1\n", encoding="utf-8")
    calls: list[list[str]] = []

    def run(args, **_kwargs):
        calls.append(args)
        return subprocess.CompletedProcess(args, 0, "", "")

    monkeypatch.setattr(ruff_on_edit, "ROOT", tmp_path.resolve())
    monkeypatch.setattr(ruff_on_edit, "find_ruff", lambda: "ruff")
    monkeypatch.setattr(ruff_on_edit.subprocess, "run", run)
    payload = json.dumps({"tool_input": {"file_path": str(source)}}, ensure_ascii=False)
    # A stdin whose text layer is a code page that cannot read the name's bytes.
    monkeypatch.setattr(
        sys, "stdin", io.TextIOWrapper(io.BytesIO(payload.encode("utf-8")), encoding="cp1252")
    )
    assert ruff_on_edit.main() == 0
    assert [args[-1] for args in calls] == [str(source.resolve())] * 2
