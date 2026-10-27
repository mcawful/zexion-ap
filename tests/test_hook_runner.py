"""The hooks run on any machine: ``.claude/hooks/run_python.sh`` finds a Python 3.11+ (or
fails closed where it matters), and the hook scripts read their payload as UTF-8."""

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
ROOT = HOOKS.parent.parent
RUNNER = HOOKS / "run_python.sh"
SH = shutil.which("sh")
needs_sh = pytest.mark.skipif(SH is None, reason="no sh on PATH (on Windows, Git Bash's)")
# CI's ruff and pytest job sets ZEXION_REQUIRE=sh: there a missing sh fails
# test_sh_is_on_path_where_ci_requires_it, where the cases that need it would only skip.


def required(value: str) -> set[str]:
    """ZEXION_REQUIRE's entries: comma-separated, spaces around them ignored."""
    return {entry.strip() for entry in value.split(",")} - {""}


REQUIRED = required(os.environ.get("ZEXION_REQUIRE", ""))
REAL = Path(sys.executable).as_posix()

SAVES = "C:/Users/someone/AppData/Roaming/Gallant Leaf/Zexion"
PROBE = """\
import json, sys
data = json.loads(sys.stdin.buffer.read())
print("probe out")
print("probe ran", file=sys.stderr)
sys.exit(data["code"])
"""


def _sh(repo: Path, script: str, payload: str, bin_dir: Path | None = None):
    env = dict(os.environ)
    if bin_dir is not None:
        env["PATH"] = f"{bin_dir}{os.pathsep}{env.get('PATH', '')}"
    return subprocess.run(
        [SH, str(repo / ".claude" / "hooks" / "run_python.sh"), script],
        input=payload.encode("utf-8"),
        capture_output=True,
        env=env,
    )


@pytest.mark.skipif("sh" not in REQUIRED, reason="ZEXION_REQUIRE does not list sh")
def test_sh_is_on_path_where_ci_requires_it() -> None:
    assert SH is not None, "no sh on PATH, so the runner's cases skipped"


def test_zexion_require_ignores_spaces_around_entries() -> None:
    assert required("client, sh") == {"client", "sh"}
    assert required(" sh ,client,") == {"client", "sh"}
    assert required("") == set()


def _fake(path: Path, body: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(f"#!/bin/sh\n{body}\n".encode())
    path.chmod(0o755)


def _works(path: Path, name: str) -> None:
    """A Python that runs (the real one), saying which candidate it is."""
    _fake(path, f'echo "via {name}" >&2; exec "{REAL}" "$@"')


def _payload(command: str, code: int = 0) -> str:
    return json.dumps({"tool_name": "Bash", "tool_input": {"command": command}, "code": code})


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
    """python3, python and py ahead of the real ones, none able to run anything: a Store
    stub, Python 2's SyntaxError, the launcher with no Python."""
    bin_dir = tmp_path / "bin"
    _fake(bin_dir / "python3", 'echo "Python was not found" >&2; exit 49')
    _fake(bin_dir / "python", 'echo "SyntaxError (Python 2)" >&2; exit 1')
    _fake(bin_dir / "py", "exit 9009")
    return bin_dir


@needs_sh
@pytest.mark.parametrize("code", [0, 2])
def test_the_first_python_that_runs_the_hook_decides(repo, fake_bin, code: int) -> None:
    _works(fake_bin / "python", "python")
    result = _sh(repo, "probe.py", _payload("ls", code), fake_bin)
    assert result.returncode == code
    assert result.stdout.decode().split() == ["probe", "out"]  # stdout passes through
    # The stub's complaint is dropped; the hook's own stderr is kept.
    assert result.stderr.decode().split() == ["via", "python", "probe", "ran"]


@needs_sh
@pytest.mark.parametrize(
    ("working", "chosen"),
    [
        ([".venv/Scripts/python.exe", ".venv/bin/python", "python3"], ".venv/Scripts/python.exe"),
        ([".venv/bin/python", "python3"], ".venv/bin/python"),
        (["python3", "python"], "python3"),
        (["py"], "py"),
    ],
)
def test_candidates_in_order(repo, fake_bin, working: list[str], chosen: str) -> None:
    for name in working:
        if name == "py":  # the launcher, which must be asked for Python 3
            _fake(
                fake_bin / "py",
                f'[ "$1" = -3 ] || exit 9; shift; echo "via py" >&2; exec "{REAL}" "$@"',
            )
        else:
            _works(repo / name if name.startswith(".venv") else fake_bin / name, name)
    result = _sh(repo, "probe.py", _payload("ls"), fake_bin)
    assert result.returncode == 0
    assert result.stderr.decode().split()[:2] == ["via", chosen]


@needs_sh
@pytest.mark.parametrize(
    ("script", "command", "code"),
    [
        ("guard_protected.py", f"cp lab/a '{SAVES}/x.sav'", 2),  # near the saves: blocked
        ("guard_protected.py", "rm STEAMAPPS/x", 2),
        ("guard_protected.py", "rm SteamApps/x", 2),
        ("guard_protected.py", "rm userdata/1/3392510/x", 2),
        ("guard_protected.py", "rm gAlLaNt/x", 2),
        ("guard_protected.py", "ls", 1),  # elsewhere: goes ahead, with a hook error notice
        ("ruff_on_edit.py", f"cp lab/a '{SAVES}/x.sav'", 1),  # after the call: never "blocked"
    ],
)
def test_with_no_python_it_fails_closed_near_the_saves(repo, fake_bin, script, command, code):
    result = _sh(repo, script, _payload(command), fake_bin)
    assert result.returncode == code
    stderr = result.stderr.decode()
    assert f"No Python 3.11 or later could run .claude/hooks/{script}" in stderr
    assert "SyntaxError" not in stderr and "not found" not in stderr
    assert ("so it is blocked" in stderr) == (code == 2)


@needs_sh
def test_a_script_python_cannot_open_is_no_verdict(repo, fake_bin) -> None:
    # Python exits 2 itself for a file it can't open (a Windows .venv seen from WSL, say);
    # through the runner that is a traceback instead, so the call is not blocked or let
    # through on Python's word.
    _works(fake_bin / "python", "python")
    result = _sh(repo, "missing.py", _payload("ls"), fake_bin)
    assert result.returncode == 1
    assert "No Python 3.11 or later could run" in result.stderr.decode()


@needs_sh
@pytest.mark.parametrize(("file_path", "code"), [(f"{SAVES}/x.sav", 2), ("lab/x.sav", 0)])
def test_the_guard_as_claude_code_runs_it(tmp_path: Path, file_path: str, code: int) -> None:
    # The hook command as settings.json would give it, in a folder whose name has a space,
    # with the Pythons this machine has.
    repo = tmp_path / "a repo"
    (repo / ".claude" / "hooks").mkdir(parents=True)
    (repo / "tools").mkdir()
    for name in ("run_python.sh", "guard_protected.py"):
        shutil.copy(HOOKS / name, repo / ".claude" / "hooks" / name)
    shutil.copy(ROOT / "tools" / "protected.py", repo / "tools" / "protected.py")
    payload = json.dumps({"tool_name": "Write", "tool_input": {"file_path": file_path}})
    result = subprocess.run(
        [SH, "-c", 'sh "${CLAUDE_PROJECT_DIR}/.claude/hooks/run_python.sh" guard_protected.py'],
        input=payload.encode("utf-8"),
        capture_output=True,
        env={**os.environ, "CLAUDE_PROJECT_DIR": str(repo)},
    )
    assert result.returncode == code
    assert (code == 2) == (guard_protected.BLOCK_HINT in result.stderr.decode())


def test_the_guard_reads_and_answers_in_utf8() -> None:
    # A name whose UTF-8 bytes a Windows code page reads as other characters. Where the
    # locale is UTF-8 (Linux, macOS) this passes either way; it guards Windows.
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
    # A stdin whose text layer is a code page that cannot read the name's bytes, and a
    # stderr of its own (main reconfigures it).
    stdin = io.TextIOWrapper(io.BytesIO(payload.encode("utf-8")), encoding="cp1252")
    monkeypatch.setattr(sys, "stdin", stdin)
    monkeypatch.setattr(sys, "stderr", io.TextIOWrapper(io.BytesIO(), encoding="cp1252"))
    assert ruff_on_edit.main() == 0
    assert [args[-1] for args in calls] == [str(source.resolve())] * 2
