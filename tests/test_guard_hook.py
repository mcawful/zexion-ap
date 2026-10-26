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
        "E:/SteamLibrary/steamapps/COMMON~1/Zexion/x",
        "C:/Users/someone/AppData/Roaming/Gallant Leaf./Zexion/x",
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
    # Reads the stricter rules must still let through.
    f'xxd -s 16 -l 64 "{SAVES}/Save Game 2.sav"',
    f'xxd -r lab/a.hex | cmp - "{SAVES}/Save Game 2.sav"',
    f'sort -k2 "{SAVES}/x" | uniq -c',
    f'cat "{SAVES}/Save Game 2.sav" >&2',
    f'tree -L 2 "{GAME}"',
    f'ls "{SAVES}"/*.sav',
    'ls "$APPDATA"/Gallant*/Zexion',
    f'cat "{SAVES}/GameUnlocks.sav" | xxd | head',
    f'cd "{SAVES}" && cat "Save Game 2.sav" | xxd | head -4',
    f".venv/Scripts/python tools/save_lab.py slots --save-dir '{SAVES}'",
    ".venv/Scripts/python tools/save_lab.py slots",
    ".venv/Scripts/python tools/build_id.py",
    ".venv/Scripts/python -m pytest -q",
    ".venv/Scripts/ruff check .",
    "git diff --stat",
    "git log --oneline -5",
    "cp lab/a worlds/zexion/x.py",
    "cd lab && rm -r work",
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
    # Read-only verbs whose arguments make them write.
    f'xxd -r lab/patched.hex "{SAVES}/Save Game 30.sav"',
    f'xxd lab/a "{SAVES}/Save Game 30.sav"',
    f'uniq lab/a "{SAVES}/Save Game 1.sav"',
    f'sort lab/a -o "{SAVES}/x"',
    f'sort lab/a -o"{SAVES}/x"',
    f'sort -uo"{SAVES}/x" lab/a',
    f'sort --output="{SAVES}/x" lab/a',
    f'sort --out "{SAVES}/x" lab/a',
    f'tree -o "{SAVES}/x" lab',
    f'rg --pre ./x.sh pattern "{SAVES}"',
    f'file -C -m "{SAVES}/x"',
    # Redirects to a file with >& and &>.
    f'cat lab/a >& "{SAVES}/Save Game 30.sav"',
    f'cat lab/a >&"{SAVES}/x"',
    f'cat lab/a &> "{SAVES}/x"',
    'echo x > "$APPDATA"/Gallant*/Zexion/x.sav',
    # Path spellings.
    'cp lab/a "E:/Program Files (x86)/Steam/steamapps/common/./Zexion/x.dll"',
    "cp lab/a /e/Steam/steamapps/common/Other/../Zexion/x.dll",
    'cp lab/a "E:/Steam/steamapps/common/"Zexion/x.dll',
    'cp lab/a "$APPDATA/Gallant Leaf/"Zexion/x.sav',
    'cp lab/a "$APPDATA"/Gallant*/Zexion/x.sav',
    "cp lab/a /e/Steam/steamapps/common/Zex*/x.dll",
    "cp lab/a /e/Steam/steamapps/c?mmon/[Zz]exion/x.dll",
    "cp lab/a /e/Steam/steamapps/common/{Zexion,Other}/x.dll",
    "cp lab/a ~/AppData/Roaming/GALLAN~1/Zexion/x.sav",
    'cd "C:/Program Files (x86)/Steam" && cp lab/a userdata/123/3392510/remote/x',
    'cd "$APPDATA/Gallant Leaf" && cp lab/a Zexion/x.sav',
    "cd ~/AppData/Roaming/Gall* && cp lab/a Zexion/x.sav",
]

PS_ALLOWED = [
    f'Get-ChildItem "{SAVES}"',
    f'Get-FileHash "{GAME}/Zexion.exe" | Format-List',
    f'Format-Hex -Path "{SAVES}/Save Game 2.sav" | Select-Object -First 4',
    '$s = "$env:APPDATA\\Gallant Leaf\\Zexion"; Get-ChildItem $s',
    f'& .venv\\Scripts\\python.exe tools\\save_lab.py slots --save-dir "{SAVES}"',
    f'Get-ChildItem "{SAVES}" | Where-Object {{ $_.Length -gt 0 }} | ForEach-Object {{ $_.Name }}',
    f'(Get-Item "{SAVES}/Save Game 2.sav").Length',
    f'Get-Content "{SAVES}/x" -TotalCount 5',
    f'$s = "{SAVES}"; foreach ($f in Get-ChildItem $s) {{ $f.Name.ToUpper() }}',
    f'Get-FileHash "{GAME}/Zexion.exe" 2>&1',
    f'Get-ChildItem "{SAVES}" | Select-Object Name, Length | Sort-Object Name',
    "Get-ChildItem $env:APPDATA/Gallant*/Zexion",
    "git status",
    ".venv\\Scripts\\python.exe -m pytest -q",
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
    # Method calls on items.
    f'$f = Get-Item "{SAVES}/Save Game 2.sav"; $f.Delete()',
    f'(Get-Item "{SAVES}/Save Game 2.sav").Delete()',
    f'$f = Get-Item "{SAVES}/x"; $f.MoveTo("lab/x.sav")',
    f'Get-Item "{SAVES}/x" | % {{ $_.Delete() }}',
    f'Get-ChildItem "{SAVES}" | foreach Delete',
    f'Get-ChildItem "{SAVES}" | ForEach-Object -MemberName Delete',
    f'$f = Get-Item "{SAVES}/x"; $f.IsReadOnly = $false',
    # Script blocks after a read-only verb.
    f'Get-ChildItem "{SAVES}" | foreach {{ Remove-Item $_ }}',
    f'Get-ChildItem "{SAVES}" | Where-Object {{ Remove-Item $_.FullName }}',
    f'Get-ChildItem "{SAVES}" | Sort-Object {{ Remove-Item $_ }}',
    f'(Get-ChildItem "{SAVES}").ForEach{{ Remove-Item $_ }}',
    # The call operator on a variable.
    f'$s = "{SAVES}"; & $cmd "$s/x"',
    # Path spellings.
    "$p = Join-Path $env:APPDATA 'Gallant Leaf' 'Zexion'; Copy-Item lab/a $p",
    'Copy-Item lab/a "E:/Steam/steamapps/common/./Zexion/x.dll"',
    'Remove-Item "$env:APPDATA/Gallant*/Zexion/x.sav"',
    "Set-Location $env:APPDATA/Gallant*; Remove-Item Zexion/x.sav",
]

PS_SAVES = "$env:APPDATA\\Gallant Leaf\\Zexion"

# Everyday work that names the folders without writing to them.
BASH_DAILY = [
    # Commit and PR text is data.
    'git commit -m "Guard: block steamapps/common/Zexion and Gallant Leaf/Zexion writes"',
    "git commit -m \"$(cat <<'EOF'\nGuard: protect %APPDATA%/Gallant Leaf/Zexion\n\n"
    'Co-Authored-By: Claude\nEOF\n)"',
    "git commit -F - <<'EOF'\nGuard: protect steamapps/common/Zexion\nEOF",
    'git commit -m "$(cat <<EOF\nGuard: protect $USER Gallant Leaf/Zexion\nEOF\n)" && git push',
    'gh pr create --title "Guard" --body "$(cat <<\'EOF\'\n- blocks `cp` into '
    'steamapps/common/Zexion\nEOF\n)"',
    'git add worlds/zexion && git commit -m "Docs: the Gallant Leaf save folder"',
    'git commit -am "Docs: Steam app 3392510"',
    'git stash push -m "wip Gallant Leaf/Zexion paths"',
    'gh pr create --base develop --title "Guard" --body "Blocks steamapps/common/Zexion writes"',
    "gh pr create --title \"Zexion: Gallant Leaf saves\" --body-file - <<'EOF'\n"
    "steamapps/common/Zexion\nEOF",
    # Tool runs piped, with an env prefix, or after a cd into the repo.
    f'.venv/Scripts/python tools/save_lab.py diff "{SAVES}/Save Game 1.sav" lab/w/x.sav | head -40',
    f'.venv/Scripts/python tools/check_patterns.py "{GAME}/Zexion.exe" 2>&1 | tail -5',
    f'ZEXION_SAVE_DIR="{SAVES}" .venv/Scripts/python tools/save_lab.py slots',
    f'cd D:/code/zexion-ap && .venv/Scripts/python tools/save_lab.py slots --save-dir "{SAVES}"',
    # A scratch script that only names the saves; reads with read, less, sed -n, grep.
    f"cat > lab/probe.py <<'EOF'\nSAVES = r'{SAVES}'\nprint(SAVES)\nEOF",
    f'while read f; do echo "$f"; done < <(ls "{SAVES}")',
    f'less "{SAVES}/log.txt"',
    "sed -n '/Gallant Leaf\\/Zexion/,+5p' docs/re/saves.md",
    f"sed -n '1,4p;s/a/b/g' \"{SAVES}/log.txt\"",
    'grep -rn "steamapps/common/Zexion" docs/ CLAUDE.md',
]

PS_DAILY = [
    'git commit -m "Guard: block steamapps/common/Zexion writes"',
    "git commit -m @'\nGuard: protect Gallant Leaf/Zexion\n'@",
    f'.venv\\Scripts\\python.exe tools\\save_lab.py slots --save-dir "{PS_SAVES}" | Select -Fi 9',
    f'Get-ChildItem "{PS_SAVES}" | ForEach-Object {{ "{{0,-24}} {{1,8}}" -f $_.Name, $_.Size }}',
    f'Get-ChildItem "{PS_SAVES}" | Select-Object Name, @{{n="KB";e={{$_.Length/1KB}}}}',
    f'Get-ChildItem "{PS_SAVES}" | Format-Table Name, @{{n="KB";e={{[math]::Round($_.Length)}}}}',
    f'(Get-Item "{PS_SAVES}\\Save Game 2.sav").LastWriteTime.ToString("o")',
    f'(Get-Item "{PS_SAVES}\\Save Game 2.sav").Name.Substring(0, 4)',
    f'(Get-Item "{PS_SAVES}\\Save Game 2.sav").Name.Split(" ")',
    f'$b = [IO.File]::ReadAllBytes("{PS_SAVES}\\Save Game 2.sav"); $b.Length',
    f'$b = Get-Content "{PS_SAVES}\\x.sav" -AsByteStream; [BitConverter]::ToUInt32($b, 16)',
    f'Get-ChildItem "{PS_SAVES}" | Out-Null',
    f'Get-ChildItem "{PS_SAVES}" | ConvertTo-Json',
    f'Get-ChildItem "{PS_SAVES}" | Group-Object Extension',
    f'Get-ChildItem "{PS_SAVES}" | Get-Member',
    f'Get-ChildItem "{PS_SAVES}" | Format-Wide',
    f'Get-ChildItem "{PS_SAVES}" | Out-Host',
    f'if (Test-Path "{PS_SAVES}") {{ Get-ChildItem "{PS_SAVES}" }}',
]

BASH_BLOCKED_MORE = [
    # Text that the shell runs is not data.
    f'git commit -m "$(rm -rf {SAVES})"',
    f'git commit -m "$(cat <<EOF\n$(rm -rf "{SAVES}")\nEOF\n)"',
    f'cat <<EOF\n`rm "{SAVES}/x"`\nEOF',
    "bash <<'EOF'\nrm \"" + SAVES + '/x"\nEOF',
    f"cat <<'EOF' > \"{SAVES}/x\"\nhello\nEOF",
    # A variable the command sets itself, in a redirect.
    f'TEMP="{SAVES}"; ls lab > "$TEMP/x"',
    # sed, less, tree that write.
    f'sed -n "w {SAVES}/x" lab/a',
    f'sed -i s/a/b/ "{SAVES}/x"',
    f"sed 's/a/b/w {SAVES}/x' lab/a",
    f'less -o "{SAVES}/x" lab/a',
    f'tree -R -L 1 -H . "{GAME}"',
    # git naming a protected folder outside its message; a tool run inside one.
    f'git -C "{SAVES}" init',
    f'cd "{SAVES}" && /d/code/zexion-ap/.venv/Scripts/python tools/save_lab.py slots',
    # The parent folder in a variable.
    'S="$APPDATA/Gallant Leaf"; cp lab/a "$S/Zexion/Save Game 30.sav"',
    'L=/e/SteamLibrary/steamapps; cp lab/x.dll "$L/common/Zexion/zlib.dll"',
    'L="E:/SteamLibrary/steamapps/common"; rm "$L/Zexion/zlib.dll"',
    # Links to a parent folder.
    'cmd //c mklink //J lab\\gl "%APPDATA%\\Gallant Leaf"',
    'ln -s "E:/SteamLibrary/steamapps/common" lab/common',
]

PS_BLOCKED_MORE = [
    '$d = "$env:APPDATA\\Gallant Leaf"; Copy-Item lab\\a "$d\\Zexion\\Save Game 30.sav"',
    "$d = Join-Path $env:APPDATA 'Gallant Leaf'; Remove-Item \"$d\\Zexion\\x.sav\"",
    '$g = "E:\\SteamLibrary\\steamapps\\common"; Copy-Item lab\\x.dll "$g\\Zexion\\zlib.dll"',
    'Copy-Item lab\\a ("$env:APPDATA\\Gallant" + " Leaf\\Zexion\\x.sav")',
    'New-Item -ItemType Junction -Path lab\\gl -Target "$env:APPDATA\\Gallant Leaf"',
    'cmd /c mklink /J lab\\common "E:\\SteamLibrary\\steamapps\\common"',
    # Trailing dots and spaces on a parent folder.
    'Copy-Item lab\\a "$env:APPDATA\\Gallant Leaf.\\Zexion\\x"',
    'Copy-Item lab\\a "$env:APPDATA\\Gallant Leaf \\Zexion\\x"',
    'Copy-Item lab\\a "E:\\SteamLibrary\\steamapps\\common.\\Zexion\\x"',
    # Quoted or computed method names; static calls that write.
    f"$f = Get-Item \"{SAVES}/x\"; $f.'Delete'()",
    f'Get-Item "{SAVES}/x" | % {{ $_."Delete"() }}',
    f'$m = "Delete"; (Get-Item "{SAVES}/x").$m()',
    f'[IO.File]::Delete("{SAVES}/x")',
    f'[System.IO.Directory]::Delete("{SAVES}", $true)',
    # Text that the shell runs; calls through & and assignments.
    f'git commit -m "$(Remove-Item {SAVES}/x)"',
    f'& "Remove-Item" "{SAVES}/x"',
    f'(Get-Item "{SAVES}/x").Attributes += "Hidden"',
]


@pytest.mark.parametrize(
    ("tool", "command"),
    [("Bash", c) for c in BASH_ALLOWED + BASH_DAILY]
    + [("PowerShell", c) for c in PS_ALLOWED + PS_DAILY],
)
def test_read_only_commands_allowed(tool: str, command: str) -> None:
    assert run(tool, command=command) is None


@pytest.mark.parametrize(
    ("tool", "command"),
    [("Bash", c) for c in BASH_BLOCKED + BASH_BLOCKED_MORE]
    + [("PowerShell", c) for c in PS_BLOCKED + PS_BLOCKED_MORE],
)
def test_writing_commands_blocked(tool: str, command: str) -> None:
    assert run(tool, command=command)


@pytest.mark.parametrize(
    "command",
    [
        f'ls -la "{SAVES}" > "$TEMP/listing.txt"',
        "cat > \"$TEMP/probe.py\" <<'EOF'\nimport os\n"
        "print(os.listdir(os.environ['APPDATA'] + '/Gallant Leaf/Zexion'))\nEOF",
    ],
)
def test_redirects_into_a_known_env_folder_allowed(command: str, monkeypatch) -> None:
    monkeypatch.setenv("TEMP", "C:/Users/someone/AppData/Local/Temp")
    assert run("Bash", command=command) is None


def test_the_sessions_working_folder_counts() -> None:
    # A cd in an earlier call leaves the shell in the saves; the payload's cwd says so.
    rm = {"command": "rm 'Save Game 2.sav'"}
    assert guard_protected.check("Bash", rm, protected, session_cwd=SAVES)
    assert guard_protected.check("PowerShell", rm, protected, session_cwd=f"{GAME}/Files")
    assert guard_protected.check("Bash", rm, protected, session_cwd="D:/code/zexion-ap") is None


def test_a_link_to_a_parent_folder_is_followed(tmp_path: Path) -> None:
    parent = tmp_path / "real" / "Gallant Leaf"
    (parent / "Zexion").mkdir(parents=True)
    link = tmp_path / "gl"
    try:
        if sys.platform == "win32":
            import _winapi

            _winapi.CreateJunction(str(parent), str(link))
        else:
            link.symlink_to(parent, target_is_directory=True)
    except OSError as exc:
        pytest.skip(f"cannot make a link here: {exc}")
    cp = {"command": "cp lab/a gl/Zexion/x.sav"}
    assert guard_protected.check("Bash", cp, protected, session_cwd=str(tmp_path))
    ls = {"command": "ls gl/Zexion"}
    assert guard_protected.check("Bash", ls, protected, session_cwd=str(tmp_path)) is None


@pytest.mark.parametrize(
    "spelling", ["%APPDATA%", "$APPDATA", "${APPDATA}", "$env:APPDATA", "${env:APPDATA}"]
)
def test_every_shells_env_var_spelling_expands(spelling: str, monkeypatch) -> None:
    monkeypatch.setenv("APPDATA", "C:/Users/someone/AppData/Roaming")
    assert protected.normalize(f"{spelling}/x") == "c:/users/someone/appdata/roaming/x"


TERMINAL = "mcp__terminal__run_in_terminal"
SLOTS_PS = f'.venv\\Scripts\\python.exe tools\\save_lab.py slots --save-dir "{SAVES}"'


@pytest.mark.parametrize(
    ("tool", "tool_input"),
    [
        ("Monitor", {"command": f'cp lab/a "{SAVES}/Save Game 30.sav"', "description": "x"}),
        ("Monitor", {"command": f'tail -f lab/log | tee "{SAVES}/x"', "description": "x"}),
        (TERMINAL, {"command": f'Copy-Item lab\\x.sav "{SAVES}\\Save Game 30.sav"'}),
        (
            TERMINAL,
            {"command": 'Copy-Item lab\\x.sav "$env:APPDATA\\Gallant Leaf\\Zexion\\x.sav"'},
        ),
        (TERMINAL, {"command": "Remove-Item 'Save Game 2.sav'", "cwd": SAVES}),
        (TERMINAL, {"command": "Remove-Item -Recurse x", "cwd": "D:/SteamLibrary/steamapps"}),
        (TERMINAL, {"command": ".venv\\Scripts\\python.exe tools\\x.py", "cwd": SAVES}),
    ],
)
def test_monitor_and_terminal_commands_blocked(tool: str, tool_input: dict) -> None:
    assert run(tool, **tool_input)


@pytest.mark.parametrize(
    ("tool", "tool_input"),
    [
        ("Monitor", {"command": f'tail -f "{SAVES}/log.txt" | grep --line-buffered x'}),
        ("Monitor", {"ws": {"url": "wss://example.com"}, "description": "x"}),
        (TERMINAL, {"command": f'Get-ChildItem "{SAVES}"'}),
        (TERMINAL, {"command": "npm run dev", "cwd": "D:/code/zexion-ap"}),
        (TERMINAL, {"command": SLOTS_PS, "cwd": "D:/code/zexion-ap"}),
    ],
)
def test_monitor_and_terminal_reads_allowed(tool: str, tool_input: dict) -> None:
    assert run(tool, **tool_input) is None


@pytest.mark.parametrize(
    ("word", "expected"),
    [
        (f"{GAME}/x.dll", True),
        ("E:/Steam/steamapps/common/./Zexion/x.dll", True),
        ("E:/Steam/steamapps/common/Other/../Zexion", True),
        ("E:/Steam/steamapps/common/Zex*/x.dll", True),
        ("E:/Steam/steam*/*/zexion", True),
        ("E:/Steam/STEAMA~1/common/Zexion", True),
        ("C:/Users/x/AppData/Roaming/Gallant*/Zexion", True),
        ("C:/Users/x/AppData/Roaming/GALLAN~1/Zexion/x.sav", True),
        ("userdata/123/3392510/remote", True),
        ("userdata/*/3392510", True),
        ("E:/Steam/steamapps/common/Zexion.", True),
        ("C:/Users/x/AppData/Roaming/Gallant Leaf./Zexion/x", True),
        ("E:/Steam/steamapps/common /Zexion/x", True),
        ("worlds/zexion/items.py", False),
        ("E:/Steam/steamapps/common/ZexionSoundtrack", False),
        ("E:/Steam/steamapps/common/Zexions*", False),
        ("E:/Steam/steamapps/common/Zexion/../Other/x", False),
        ("D:/code/zexion-ap/lab/x.sav", False),
        ("userdata/123/4000000", False),
    ],
)
def test_word_may_name_protected(word: str, expected: bool) -> None:
    assert protected.word_may_name_protected(word) is expected


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
        (json.dumps({"tool_name": "Monitor", "tool_input": {"command": f"rm '{SAVES}/x'"}}), 2),
        (json.dumps({"tool_name": TERMINAL, "tool_input": {"command": f"rm '{SAVES}/x'"}}), 2),
        # The payload's own cwd: a cd in an earlier call left the shell in the saves.
        (json.dumps({"tool_name": "Bash", "tool_input": {"command": "rm x"}, "cwd": SAVES}), 2),
        (json.dumps({"tool_name": "Bash", "tool_input": {"command": "rm x"}, "cwd": "D:/c"}), 0),
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
