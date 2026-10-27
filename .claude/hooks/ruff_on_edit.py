"""PostToolUse hook: ``ruff format`` and ``ruff check --fix`` on a Python file Claude just edited.

Only files inside this repo are touched. Lint errors ruff cannot fix go back to Claude (exit 2).
It uses ``.venv``'s ruff, or one on ``PATH``; with neither, it does nothing.
"""

from __future__ import annotations

import contextlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent


def find_ruff() -> str | None:
    for candidate in (ROOT / ".venv" / "Scripts" / "ruff.exe", ROOT / ".venv" / "bin" / "ruff"):
        if candidate.is_file():
            return str(candidate)
    return shutil.which("ruff")


def main() -> int:
    with contextlib.suppress(Exception):
        sys.stderr.reconfigure(encoding="utf-8", errors="backslashreplace")
    try:
        # UTF-8, as Claude Code sends it, whatever this machine's locale (a path's non-ASCII
        # name would otherwise not be found, and the file left unformatted).
        data = json.loads(sys.stdin.buffer.read().decode("utf-8", errors="replace"))
    except json.JSONDecodeError:
        return 0
    path_text = (data.get("tool_input") or {}).get("file_path") or ""
    if not path_text.endswith(".py"):
        return 0
    path = Path(path_text).resolve()
    if not path.is_file() or not path.is_relative_to(ROOT):
        return 0
    ruff = find_ruff()
    if ruff is None:
        return 0

    run = {"cwd": ROOT, "capture_output": True, "text": True, "encoding": "utf-8"}
    subprocess.run([ruff, "format", "--force-exclude", str(path)], **run)
    result = subprocess.run(
        [ruff, "check", "--fix", "--force-exclude", "--output-format", "concise", str(path)], **run
    )
    if result.returncode != 0:
        print(f"ruff check found problems it could not fix:\n{result.stdout}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
