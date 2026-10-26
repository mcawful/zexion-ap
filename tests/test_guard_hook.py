"""The write guard must block every way of writing into a protected folder we can think of,
and must not get in the way of reading them."""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

import guard_protected
import protected

HOOK = Path(guard_protected.__file__)
SAVES = "C:/Users/someone/AppData/Roaming/Gallant Leaf/Zexion"
GAME = "E:/Program Files (x86)/Steam/steamapps/common/Zexion"
CLOUD = "C:/Program Files (x86)/Steam/userdata/123/3392510/remote"


def run(tool: str, **tool_input) -> str | None:
    return guard_protected.check(tool, tool_input, protected)


@pytest.mark.parametrize(
    "path",
    [
        f"{SAVES}/Save Game 1.sav",
        f"{SAVES}/Mods/Local/Sprites/x.png",
        SAVES.replace("/", "\\") + "\\Save Game 2.sav",
        "%APPDATA%\\Gallant Leaf\\Zexion\\GameUnlocks.sav",
        f"{GAME}/Zexion.exe",
        f"{GAME}/Files/RandoCells",
        "D:/SteamLibrary/steamapps/common/Zexion/zlib.dll",
        f"{CLOUD}/Save Game 1.sav",
    ],
)
def test_file_tools_blocked_in_protected_folders(path: str) -> None:
    for tool in ("Write", "Edit", "MultiEdit"):
        assert run(tool, file_path=path), (tool, path)


@pytest.mark.parametrize(
    "path",
    [
        "D:/code/zexion-ap/tools/save_lab.py",
        "D:/code/zexion-ap/lab/work/slot2.sav",
        "E:/Program Files (x86)/Steam/steamapps/common/ZexionSoundtrack/notes.txt",
        "C:/Users/someone/AppData/Roaming/Gallant Leaf/OtherGame/x.sav",
    ],
)
def test_file_tools_allowed_elsewhere(path: str) -> None:
    assert run("Write", file_path=path) is None


BASH_ALLOWED = [
    f'ls -la "{SAVES}"',
    f'xxd -l 64 "{SAVES}/Save Game 2.sav"',
    f'xxd "{SAVES}/Save Game 2.sav" > lab/slot2.hex',
    f'cmp -l "{SAVES}/Save Game 1.sav" "{SAVES}/Save Game 2.sav" | head -50',
    f'sha256sum "{GAME}/Zexion.exe" 2>&1',
    f'S="{SAVES}"; for f in a b; do xxd -l 64 "$S/$f"; done',
    f'find "{GAME}" -name "*.txt"',
    f'cd "{GAME}" && ls Files',
    'S="$APPDATA/Gallant Leaf/Zexion"; ls "$S"',
    f'.venv/Scripts/python tools/save_lab.py diff "{SAVES}/Save Game 1.sav" 2',
    f'.venv/Scripts/python tools/build_id.py --game-dir "{GAME}" > lab/build.txt',
    "git status",
    "echo hello > lab/x.txt",
]

BASH_BLOCKED = [
    f'cp lab/patched.sav "{SAVES}/Save Game 30.sav"',
    f'mv "{SAVES}/Save Game 2.sav" lab/',
    f'rm "{SAVES}/Save Game 2.sav"',
    f'echo x > "{SAVES}/Save Game 2.sav"',
    f'echo x >> "{GAME}/Files/Flags"',
    f'cat lab/a | tee "{SAVES}/x"',
    f'touch "{GAME}/new"',
    f'find "{SAVES}" -name "*.sav" -delete',
    f'find "{SAVES}" -exec rm {{}} +',
    f'dd if=lab/a of="{SAVES}/Save Game 30.sav"',
    f'S="{SAVES}"; cp lab/a "$S/Save Game 30.sav"',
    f'S="{SAVES}"; echo x > "$S/Save Game 30.sav"',
    f"python -c \"open(r'{SAVES}/Save Game 30.sav', 'wb')\"",
    f'.venv/Scripts/python -c "print(1)" "{SAVES}"',
    f'.venv/Scripts/python tools/save_lab.py diff 1 2; rm "{SAVES}/x"',
    f'ls "{SAVES}" && $(rm -rf "{SAVES}")',
    f'sed -i s/a/b/ "{GAME}/Files/Text"',
    'cp lab/a "$APPDATA/Gallant Leaf/Zexion/Save Game 30.sav"',
    'cp lab/a "${APPDATA}/Gallant Leaf/Zexion/Save Game 30.sav"',
    "cp lab/a ~/AppData/Roaming/Gallant\\ Leaf/Zexion/x.sav",
    f'cp lab/a "{CLOUD}/x"',
    f'git -C "{GAME}" init',
]

PS_ALLOWED = [
    f'Get-ChildItem "{SAVES}"',
    f'Get-FileHash "{GAME}/Zexion.exe" | Format-List',
    f'Format-Hex -Path "{SAVES}/Save Game 2.sav" | Select-Object -First 4',
    '$s = "$env:APPDATA\\Gallant Leaf\\Zexion"; Get-ChildItem $s',
    f'& .venv\\Scripts\\python.exe tools\\save_lab.py slots --save-dir "{SAVES}"',
]

PS_BLOCKED = [
    f'Copy-Item lab/a "{SAVES}/Save Game 30.sav"',
    f'Remove-Item "{SAVES}/Save Game 2.sav"',
    f'Set-Content -Path "{GAME}/Files/Flags" -Value x',
    f'Get-Content lab/a | Out-File "{SAVES}/x"',
    f'"x" > "{SAVES}/x"',
    f'[IO.File]::WriteAllBytes("{SAVES}/x", $b)',
    'Copy-Item lab/a "$env:APPDATA\\Gallant Leaf\\Zexion\\x.sav"',
    'Copy-Item lab/a "${env:APPDATA}\\Gallant Leaf\\Zexion\\x.sav"',
    f'New-Item -ItemType File "{GAME}/x"',
    f'Invoke-Expression "rm {SAVES}/x"',
]


@pytest.mark.parametrize(
    ("tool", "command"),
    [("Bash", c) for c in BASH_ALLOWED] + [("PowerShell", c) for c in PS_ALLOWED],
)
def test_read_only_commands_allowed(tool: str, command: str) -> None:
    assert run(tool, command=command) is None


@pytest.mark.parametrize(
    ("tool", "command"),
    [("Bash", c) for c in BASH_BLOCKED] + [("PowerShell", c) for c in PS_BLOCKED],
)
def test_writing_commands_blocked(tool: str, command: str) -> None:
    assert run(tool, command=command)


@pytest.mark.parametrize(
    "spelling", ["%APPDATA%", "$APPDATA", "${APPDATA}", "$env:APPDATA", "${env:APPDATA}"]
)
def test_every_shells_env_var_spelling_expands(spelling: str, monkeypatch) -> None:
    monkeypatch.setenv("APPDATA", "C:/Users/someone/AppData/Roaming")
    assert protected.normalize(f"{spelling}/x") == "c:/users/someone/appdata/roaming/x"


def test_other_tools_pass() -> None:
    assert run("Read", file_path=f"{SAVES}/Save Game 1.sav") is None
    assert run("Grep", pattern="x", path=GAME) is None


def _hook(payload: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(HOOK)],
        input=payload,
        capture_output=True,
        text=True,
        env={**os.environ, "APPDATA": "C:/Users/someone/AppData/Roaming"},
    )


def _crashing_payload(tool_input: dict) -> str:
    # An unhashable tool_name makes check() raise, which exercises the failure path.
    return json.dumps(
        {
            "tool_name": ["not", "a", "name"],
            "tool_input": tool_input,
            "transcript_path": "C:/Users/someone/AppData/Roaming/Claude/session.jsonl",
        }
    )


@pytest.mark.parametrize(
    ("payload", "code"),
    [
        (json.dumps({"tool_name": "Write", "tool_input": {"file_path": f"{SAVES}/x.sav"}}), 2),
        (json.dumps({"tool_name": "Write", "tool_input": {"file_path": "lab/x.sav"}}), 0),
        ("not json, but Gallant Leaf", 2),  # garbage near the saves: fails closed
        ("not json", 0),  # garbage elsewhere: fails open
        # The payload's transcript path (under AppData) must not make a guard failure block
        # everything, only a failure on a call naming the saves.
        (_crashing_payload({"file_path": "D:/code/zexion-ap/tools/x.py"}), 0),
        (_crashing_payload({"file_path": f"{SAVES}/x.sav"}), 2),
    ],
)
def test_the_hook_process_blocks_with_exit_2(payload: str, code: int) -> None:
    result = _hook(payload)
    assert result.returncode == code
    assert code == 0 or "guard_protected" in result.stderr
