"""The write guard must block every way of writing into a protected folder we can think of,
and must not get in the way of reading them."""

from __future__ import annotations

import json
import os
import posixpath
import subprocess
import sys
import time
from pathlib import Path

import pytest

import guard_protected
import protected

HOOK = Path(guard_protected.__file__)
SAVES = "C:/Users/someone/AppData/Roaming/Gallant Leaf/Zexion"
GAME = "E:/Program Files (x86)/Steam/steamapps/common/Zexion"
CLOUD = "C:/Program Files (x86)/Steam/userdata/123/3392510/remote"
REPO = protected.ROOT.as_posix()  # a real checkout: after a cd, tools/ must be the repo's
PS_SAVES = "$env:APPDATA\\Gallant Leaf\\Zexion"
RM_SAVE = f"import os; os.remove(r'{SAVES}/Save Game 2.sav')"
RM_X = f"rm -f '{SAVES}/x'"
GL_ZX = "'Gallant Leaf', 'Zexion'"


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
    # Everyday work that names the folders without writing to them.
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
    f'cd "{REPO}" && .venv/Scripts/python tools/save_lab.py slots --save-dir "{SAVES}"',
    # A scratch script that only names the saves; reads with read, less, sed -n, grep.
    f"cat > lab/probe.py <<'EOF'\nSAVES = r'{SAVES}'\nprint(SAVES)\nEOF",
    f'while read f; do echo "$f"; done < <(ls "{SAVES}")',
    f'less "{SAVES}/log.txt"',
    "sed -n '/Gallant Leaf\\/Zexion/,+5p' docs/re/saves.md",
    f"sed -n '1,4p;s/a/b/g' \"{SAVES}/log.txt\"",
    'grep -rn "steamapps/common/Zexion" docs/ CLAUDE.md',
    # Heredocs that only feed data, and reads that name the folders.
    f"cat <<'EOF' | grep -c Save | head -1\n{SAVES}/Save Game 2.sav\nEOF",
    f"cat <<'EOF' >&2\n{SAVES}\nEOF",
    f"cat <<EOF.x\n{SAVES}\nEOF.x",
    f"git -c user.name=x commit -F - <<'EOF'\nGuard: {SAVES}\nEOF",
    f"gh pr create --title x --body=\"$(cat <<'EOF'\nBlocks {GAME}\nEOF\n)\"",
    f"git commit -m\"$(cat <<'EOF'\nGuard: {SAVES}\nEOF\n)\" && git push",
    f"echo $((1<<4)); cat <<'EOF'\n{SAVES}\nEOF",
    f"cat > lab/notes.md <<'EOF'\n{SAVES}\nEOF\ngit status",
    'grep -n "<<EOF" .claude/hooks/guard_protected.py',
    f'git diff --no-index "{SAVES}/Save Game 1.sav" lab/work/x.sav',
    f'git log --oneline -3 && ls "{SAVES}"',
    f'git show HEAD:docs/re/saves.md | grep "{SAVES}"',
    f'ls "{SAVES}" && git push -u origin claude/steam-achievements',
    "git commit -m \"Docs: os.path.join(appdata, 'Gallant Leaf', 'Zexion')\"",
    # Saved by a heredoc, then only read, posted or removed.
    f"cat > \"$TEMP/pr.md\" <<'EOF'\n## Summary\nBlocks {SAVES} and {GAME}\nEOF\n"
    'gh pr create --base develop --title x --body-file "$TEMP/pr.md"',
    f"cat > \"$TEMP/msg.txt\" <<'EOF'\nGuard: {SAVES}\nEOF\n"
    'git commit -F "$TEMP/msg.txt" && rm "$TEMP/msg.txt"',
    f"cat > lab/notes.md <<'EOF'\n{SAVES}\nEOF\nwc -l lab/notes.md",
    f"cat > lab/notes.md <<'EOF'\n{SAVES}\nEOF\ncat lab/notes.md | head -3",
    f"cat <<'EOF' | git commit -F -\nGuard: {SAVES}\nEOF",
    f"cat <<'EOF' | gh pr create --title x --body-file -\nBlocks {SAVES}\nEOF",
    f"gh api repos/o/r/issues/1/comments -f body=\"$(cat <<'EOF'\nBlocks {SAVES}\nEOF\n)\"",
    # Reads of a parent folder; cleaning build folders.
    'ls -la "$APPDATA/Gallant Leaf"',
    "du -sh E:/SteamLibrary/steamapps/common",
    "rm -rf native/build/* lab/ap/*",
    "rm -rf native/build lab/work/*",
    # The markers at their offsets: xxd, od and hexdump take hex themselves.
    f'xxd -s 0x9360 -l 4 "{SAVES}/Save Game 4.sav"',
    f'od -An -tx1 -j 0x9402 -N 2 "{SAVES}/Save Game 4.sav"',
    f'hexdump -C -s 0x9360 -n 16 "{SAVES}/Save Game 4.sav"',
    # The app id in a URL, or as a number in code.
    "start steam://rungameid/3392510",
    "curl -sL https://steamdb.info/app/3392510/patchnotes/ -o lab/patchnotes.html",
    '.venv/Scripts/python -c "print(3392510 * 1000 + 154)"',
    # Quotes, $'…', $[…] and ${…} that hide no substitution.
    f'echo ${{#arr[@]}}; ls "{SAVES}"',
    f"cat <<'EOF'\n${{HOME}} and $[1+1] and {SAVES}\nEOF",
    f"echo $'tab\\there'; ls \"{SAVES}\"",
    f"printf '%s\\n' \"it's\" 'fine'; ls \"{SAVES}\"",
    f"echo 'a$' '(b)'; ls \"{SAVES}\"",
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
    # Subexpressions that only read a member; Get-Date and DateTime's arithmetic.
    f'Get-ChildItem "{PS_SAVES}" | ForEach-Object {{ "$($_.Name): $($_.Length)" }}',
    f'$f = Get-Item "{PS_SAVES}\\Save Game 4.sav"; "size $($f.Length)"',
    f'Get-Date; Get-ChildItem "{PS_SAVES}" | Select-Object Name, LastWriteTime',
    f'$t = Get-Date; Get-ChildItem "{PS_SAVES}"',
    f'(Get-Date) - (Get-Item "{PS_SAVES}\\Save Game 4.sav").LastWriteTime',
    f'Get-ChildItem "{PS_SAVES}" | ? {{ $_.LastWriteTime -gt (Get-Date).AddMinutes(-10) }}',
    f'Get-ChildItem "{PS_SAVES}" | Select Name, @{{n="Age";e={{(Get-Date) - $_.LastWriteTime}}}}',
    "Start-Process 'steam://rungameid/3392510'",
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
    # Heredoc bodies that end up run, and lexer differences that would hide a command.
    # Piped into an interpreter, xargs, or anything that is not a plain reader.
    f"cat <<'EOF' | .venv/Scripts/python -\n{RM_SAVE}\nEOF",
    f"cat <<'EOF' | python3\n{RM_SAVE}\nEOF",
    f"cat <<'EOF' | bash\ncp lab/x.dll '{GAME}/zlib.dll'\nEOF",
    f"cat <<'EOF' | sh -s\nrm -f '{SAVES}/Save Game 2.sav'\nEOF",
    f"cat <<'EOF' | xargs -d '\\n' rm -f\n{SAVES}/Save Game 2.sav\nEOF",
    f"cat <<'EOF' | powershell -Command -\nRemove-Item '{SAVES}/Save Game 2.sav'\nEOF",
    f"cat <<'EOF' | grep -v '#' | bash\nrm -f '{SAVES}/x'\nEOF",
    f"cat <<'EOF' |\nrm -f '{SAVES}/x'\nEOF\nbash",
    f"(cat <<'EOF'\nrm -f '{SAVES}/x'\nEOF\n) | bash",
    # Run through a substitution.
    f"bash -c \"$(cat <<'EOF'\nrm -f '{SAVES}/Save Game 2.sav'\nEOF\n)\"",
    f"eval \"$(cat <<'EOF'\nrm -f '{SAVES}/Save Game 2.sav'\nEOF\n)\"",
    f".venv/Scripts/python <(cat <<'EOF'\n{RM_SAVE}\nEOF\n)",
    f".venv/Scripts/python -c \"$(cat <<'EOF'\n{RM_SAVE}\nEOF\n)\"",
    f"git log -m \"$(cat <<'EOF'\n{RM_SAVE}\nEOF\n)\"",
    # Saved to a file, then run.
    f"cat > lab/x.py <<'EOF' && .venv/Scripts/python lab/x.py\n{RM_SAVE}\nEOF",
    f"cat > lab/x.py <<'EOF'\n{RM_SAVE}\nEOF\n.venv/Scripts/python lab/x.py",
    f"tee lab/x.sh <<'EOF' >/dev/null\nrm -f '{SAVES}/x'\nEOF\nsh lab/x.sh",
    # git reads a patch, not a message, from stdin.
    f"git apply --unsafe-paths --directory=x <<'EOF'\n--- a/x\n+++ b/{SAVES}/x\nEOF",
    # The delimiter is the whole word; a quoted or shifted << is no heredoc.
    f'cat <<EOF.x\ndata\nEOF.x\ncp lab/a "{SAVES}/Save Game 30.sav"\nEOF',
    f'cat <<EOF-1\ndata\nEOF-1\nrm "{SAVES}/Save Game 2.sav"\nEOF',
    f'grep -c "<<EOF" docs/x.md\nrm "{SAVES}/Save Game 2.sav"\nEOF',
    f'git commit -m "Lexer: cut <<EOF bodies"\nrm "{SAVES}/Save Game 2.sav"\nEOF',
    f'x=$((1<<EOF))\nrm "{SAVES}/Save Game 2.sav"\nEOF',
    # ... nor is one in $[…] or ${…}, an offset or a subscript: a shift there.
    f'cat $[1<<EOF]\ntouch "{SAVES}/t"\nEOF',
    f's=abc; echo ${{s:0:1<<EOF}}\ntouch "{SAVES}/t"\nEOF',
    f'echo ${{a[1<<EOF]}}\ntouch "{SAVES}/t"\nEOF',
    # A $( split by quotes in a value Bash later reads as arithmetic, which runs it.
    *(
        f"x='a[$''(touch \"{SAVES}/t\")]'; {use}"
        for use in (
            "echo $[x]",
            "echo ${s:x}",
            "echo ${s:0:x}",
            "echo ${#a[x]}",
            "echo ${!x}",
            "cat ${a[x]}",
            "ls ${a[x]}",
            "wc ${a[x]}",
        )
    ),
    f"x=$'a[\\x24(touch \"{SAVES}/t\")]'; echo $[x]",
    f"x=$'a[\\044(touch \"{SAVES}/t\")]'; wc ${{a[x]}}",
    f"x=$'a[\\u0024(touch \"{SAVES}/t\")]'; echo ${{s:x}}",
    f'x="a[$"\'(touch "{SAVES}/t")]\'; echo $[x]',
    # -m is a message only where git takes one.
    f'git log -m --output="{SAVES}/x.txt" -1',
    f'git show -m --output="{GAME}/zlib.dll" HEAD',
    f'git diff --output="{SAVES}/x" HEAD',
    f'git -c diff.external=x diff --no-index "{SAVES}/a" lab/b',
    # Piped into a stage that writes a file or runs one.
    f"cat <<'EOF' | tee lab/x.sh\n{RM_X}\nEOF\nsh lab/x.sh",
    f"cat <<'EOF' | sort -o lab/x.sh\n{RM_X}\nEOF\nsh lab/x.sh",
    f"cat <<'EOF' | uniq - lab/x.sh\n{RM_X}\nEOF\nsh lab/x.sh",
    f"cat <<'EOF' | xxd -r - lab/x.sh\n{RM_X}\nEOF\nsh lab/x.sh",
    f"cat <<'EOF' | less -o lab/x.sh\n{RM_X}\nEOF\nsh lab/x.sh",
    f"cat <<'EOF' | rg --pre bash .\n{RM_X}\nEOF",
    f"cat <<'EOF' | git apply\n--- a/x\n+++ b/{SAVES}/x\nEOF",
    # The heredoc's own reader writes a file that is then run.
    f"sort -o lab/x.sh <<'EOF'\n{RM_X}\nEOF\nsh lab/x.sh",
    f"sort --output=lab/x.sh <<'EOF'\n{RM_X}\nEOF\nsh lab/x.sh",
    f"uniq - lab/x.sh <<'EOF'\n{RM_X}\nEOF\nsh lab/x.sh",
    f"xxd -r - lab/x.sh <<'EOF'\n{RM_X}\nEOF\nsh lab/x.sh",
    f"less -o lab/x.sh <<'EOF'\n{RM_X}\nEOF\nsh lab/x.sh",
    f"rg --pre bash . <<'EOF'\n{RM_X}\nEOF",
    # Saved, then run in ways that name the file only in passing.
    f"cat > lab/x.sh <<'EOF'\n{RM_X}\nEOF\ncat lab/x.sh | sh",
    f"cat > lab/x.sh <<'EOF'\n{RM_X}\nEOF\nbash -c \"$(< lab/x.sh)\"",
    f"cat > lab/x.sh <<'EOF'\n{RM_X}\nEOF\n. lab/x.sh",
    f"cat > lab/x.sh <<'EOF'\n{RM_X}\nEOF\necho \"$(sh lab/x.sh)\"",
    f"cat > lab/x.sh <<'EOF'\n{RM_X}\nEOF\ncp lab/x.sh lab/y.sh && sh lab/y.sh",
    f"cat > lab/x.sh <<'EOF'\n{RM_X}\nEOF\nmv lab/x.sh lab/y.sh; bash lab/y.sh",
    f"cat > lab/x.sh <<'EOF'\n{RM_X}\nEOF\nln -s x.sh lab/y.sh; sh lab/y.sh",
    f"cat > lab/x.sh <<'EOF'\n{RM_X}\nEOF\ngit bisect run sh lab/x.sh",
    f"cat > lab/x.sh <<'EOF'\n{RM_X}\nEOF\ngit rebase -x 'sh lab/x.sh' HEAD~1",
    f"cat > lab/x.sh <<'EOF'\n{RM_X}\nEOF\ngit -c alias.r='!sh lab/x.sh' r",
    f"cat > lab/x.sh <<'EOF'\n{RM_X}\nEOF\ngit -c core.pager='sh lab/x.sh' log -1",
    f"cat >lab/x.py <<'EOF'\n{RM_SAVE}\nEOF\n.venv/Scripts/python lab/X.PY",
    f"gh alias set --shell zz - <<'EOF'\n{RM_X}\nEOF\ngh zz",
    # A << in a comment is no heredoc: the rm runs.
    f'cat lab/x # <<EOF\nrm "{SAVES}/x"\nEOF',
    # An environment prefix that names a program to run.
    f"GIT_PAGER='cp lab/a \"{SAVES}/x\"' git log -1",
    f"GIT_EXTERNAL_DIFF='rm -f \"{SAVES}/x\"' git diff --no-index lab/a lab/b",
    f"LESSOPEN='|cp lab/a \"{SAVES}/x\" %s' less lab/a",
    f"PAGER='rm -f \"{SAVES}/x\"' git show HEAD",
    f'export PAGER=x; git show; ls "{SAVES}"',
    f'PATH=/tmp:$PATH git show; ls "{SAVES}"',
    f'BROWSER=x git show; ls "{SAVES}"',
    f'GIT_PROXY_COMMAND=x git show; ls "{SAVES}"',
    f'PYTHONSTARTUP=x git show; ls "{SAVES}"',
    # Paths joined from quoted pieces inside one word of code.
    f".venv/Scripts/python -c \"import os,shutil; shutil.copy2('lab/a', "
    f"os.path.join(os.environ['APPDATA'], {GL_ZX}, 'Save Game 30.sav'))\"",
    f".venv/Scripts/python -c \"import os; os.remove(os.path.join(os.environ['APPDATA'], {GL_ZX}, "
    "'Save Game 2.sav'))\"",
    ".venv/Scripts/python -c \"import os,pathlib,shutil; shutil.copy2('lab/a', "
    "pathlib.Path(os.environ['APPDATA']) / 'Gallant Leaf' / 'Zexion' / 'x.sav')\"",
    'python3 -c \'import os,shutil; shutil.copy2("lab/a", os.path.join(os.environ["APPDATA"], '
    '"Gallant Leaf", "Zexion", "x.sav"))\'',
    "node -e \"require('fs').copyFileSync('lab/a', require('path').join(process.env.APPDATA, "
    f"{GL_ZX}, 'x.sav'))\"",
    ".venv/Scripts/python -c \"import shutil; shutil.copy2('lab/x.dll', "
    "'E:/SteamLibrary/steamapps/common/' + 'Zexion/zlib.dll')\"",
    ".venv/Scripts/python -c \"import os,shutil; shutil.copy2('lab/x.dll', "
    "os.path.join('E:/SteamLibrary/steamapps', 'common', 'Zexion', 'zlib.dll'))\"",
    ".venv/Scripts/python -c \"import os; os.remove(os.environ['APPDATA'] + '/Gallant Leaf/Zex' "
    "'ion/x.sav')\"",
    # A glob for the parent in one word, Zexion after a variable in another.
    'for d in "$APPDATA"/Gall*; do cp lab/a "$d/Zexion/x.sav"; done',
    # A tool run after a cd out of the repo: tools/ is someone else's.
    f'cd "$TEMP" && D:/code/zexion-ap/.venv/Scripts/python tools/w.py "{SAVES}/x"',
    f'cd lab && ../.venv/Scripts/python tools/save_lab.py slots --save-dir "{SAVES}"',
    # Pieces that are not side by side: a nested join, raw strings, +, %, the parent in a name.
    '.venv/Scripts/python -c "import os; os.remove(os.path.join(os.path.join('
    "os.environ['APPDATA'], 'Gallant Leaf'), 'Zexion', 'x.sav'))\"",
    ".venv/Scripts/python -c \"import os; os.remove(os.path.join(os.environ['APPDATA'], "
    "r'Gallant Leaf', r'Zexion', 'x.sav'))\"",
    ".venv/Scripts/python -c \"import os; os.remove(os.path.join(os.environ['APPDATA'], "
    "'Gallant Leaf', 'Zex'+'ion', 'x.sav'))\"",
    ".venv/Scripts/python -c \"import os; os.remove('%s/Gallant Leaf/%s/x.sav' % "
    "(os.environ['APPDATA'], 'Zexion'))\"",
    ".venv/Scripts/python -c \"import os,shutil; base=os.path.join(os.environ['APPDATA'], "
    "'Gallant Leaf'); shutil.copy2('lab/a', os.path.join(base, 'Zexion', 'x.sav'))\"",
    "python - <<'EOF'\nimport os, shutil\n"
    "base = os.path.join(os.environ['APPDATA'], 'Gallant Leaf')\n"
    "shutil.copy2('lab/a', os.path.join(base, 'Zexion', 'x.sav'))\nEOF",
    # Writing into, moving or deleting a parent of a protected folder.
    'cp -r lab/Zexion "$APPDATA/Gallant Leaf/"',
    'cp -r lab/backup/Zexion "C:/Users/someone/AppData/Roaming/Gallant Leaf"',
    'mv lab/Zexion "$APPDATA/Gallant Leaf/"',
    'rsync -a --delete lab/Zexion "$APPDATA/Gallant Leaf/"',
    "cp -r lab/Zexion E:/SteamLibrary/steamapps/common/",
    'rm -rf "$APPDATA/Gallant Leaf"',
    'mv "$APPDATA/Gallant Leaf" lab/old',
    "rm -rf E:/SteamLibrary/steamapps/common",
    'rm -rf "C:/Program Files (x86)/Steam/userdata/123"',
    'rm -rf "C:/Program Files (x86)/Steam/userdata/"*',
    'cd "$APPDATA" && rm -r "Gallant Leaf"',
    'cp -r --target-directory="$APPDATA/Gallant Leaf" lab/Zexion',
    'git -C "$APPDATA/Gallant Leaf" clean -fdx',
    # Arithmetic reads as substitution, even when it only computes.
    f'xxd -s $((0x9360)) -l 4 "{SAVES}/Save Game 4.sav"',
    f'od -An -tx1 -j $((0x9402)) -N 2 "{SAVES}/Save Game 4.sav"',
    f'hexdump -C -s $((0x9360)) -n 16 "{SAVES}/Save Game 4.sav"',
    f'printf "%x\\n" $((0x9360 + 2)); xxd -s 0x9362 -l 2 "{SAVES}/Save Game 4.sav"',
    f'OFF=0x9360; xxd -s "$(( (OFF + 2) * 1 ))" -l 2 "{SAVES}/Save Game 4.sav"',
    # Substitution inside arithmetic, or what Bash reads as $( and a subshell.
    f'x=$(( $(rm -f "{SAVES}/x") ))',
    f'x=$(( `rm -f "{SAVES}/x"` ))',
    f'echo $((a) | (rm -f "{SAVES}/x"))',
    f'echo $(( $((1)) + $(rm -f "{SAVES}/x") ))',
    f'echo $((x[$(rm -f "{SAVES}/x")]))',
    f"echo '$((' ; rm -f \"{SAVES}/x\" ; echo '))'",
    f'$((0)) "{SAVES}/x"',
    f'xxd -s $((0x10)) "{SAVES}/Save Game 4.sav" "{SAVES}/x"',
    # A name the command sets runs as arithmetic, its subscript's substitution included.
    "x='a[$''(rm -f \"$APPDATA/Gallant Leaf/Zexion/Save Game 1.sav\")]'; echo $((x))",
    "x=$'a[\\x24(rm -f \"$APPDATA/Gallant Leaf/Zexion/x\")]'; echo $((x))",
    ": ${x:='a[$''(rm -f \"$APPDATA/Gallant Leaf/Zexion/x\")]'}; echo $((x))",
    "printf -v x '%s' 'a[$''(rm -f \"$APPDATA/Gallant Leaf/Zexion/x\")]'; echo $((x))",
    # Arithmetic text Bash leaves alone still names the folder.
    "p='$((Gallant Leaf/Zexion))'; cp lab/a.sav \"$APPDATA/${p:3:-2}/Save Game 3.sav\"",
    "python3 - <<'EOF'\nimport os, shutil\nshutil.copy2('lab/a.sav', os.path.join("
    "os.environ['APPDATA'], '$((Gallant Leaf/Zexion))'[3:-2], 'Save Game 3.sav'))\nEOF",
    # A .. after a substitution or a variable: Git Bash's cp writes through a missing 1/.. .
    'cp lab/a.sav "$APPDATA/Gallant Leaf/"$(echo 1)"/../Zexion/x.sav"',
    'cp lab/a.sav "$APPDATA/Gallant Leaf/"$((1))"/../Zexion/x.sav"',
    'cp lab/a.sav "$APPDATA/Gallant Leaf/"$x"/../Zexion/x.sav"',
    'cp lab/a.sav "$APPDATA/Gallant Leaf/"$(echo 1/2)"/../../Zexion/x.sav"',
    # The app id as a path, also next to a URL or a product.
    'wget -x -nH -P "D:/Steam" https://example.com/%75serdata/123/3392510/remote/x',
    "rm -rf ab://x/3392510",
    'python3 -c \'import shutil; shutil.rmtree("C:/Program Files (x86)/St" + "eam/user" + '
    '"data/123/" + "xx://3392510"[5:])\'',
    'cd "$UD" && rm -r 3392510',
    'rm -rf "$UD/3392510"',
    'rm -r $((3392510)) && ls "$UD"',
    "start steam://rungameid/3392510; rm -rf 3392510",
    'cp -r lab/x "$UD/x://../3392510"',
    "bash -c 'cd \"$UD\" && rm -rf 3392510 && echo'",
    ".venv/Scripts/python -c \"import shutil; u='http://x'; shutil.rmtree(d + '/3392510')\"",
    '.venv/Scripts/python -c "import shutil, os; shutil.rmtree(os.path.join(d, str(3392510)))"',
    '.venv/Scripts/python -c "import shutil; shutil.rmtree(steam + 3392510 * 1)"',
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
    f"Copy-Item lab\\a ([IO.Path]::Combine($env:APPDATA, {GL_ZX}, 'x.sav'))",
    "Copy-Item lab\\a ([IO.Path]::Combine($env:APPDATA,'Gallant Leaf','Zexion','x.sav'))",
    "Copy-Item lab\\a (Join-Path $env:APPDATA 'Gallant Leaf' "
    "-AdditionalChildPath 'Zexion','x.sav')",
    "Copy-Item lab\\a (Join-Path -Path $env:APPDATA -ChildPath 'Gallant Leaf' "
    "-AdditionalChildPath Zexion,x.sav)",
    "$g = Join-Path $env:APPDATA 'Gallant Leaf'; Copy-Item lab\\a (Join-Path $g 'Zexion','x.sav')",
    'Copy-Item -Recurse lab\\Zexion "$env:APPDATA\\Gallant Leaf"',
    "Copy-Item -Recurse -Force lab\\Zexion E:\\SteamLibrary\\steamapps\\common\\",
    'Remove-Item -Recurse "$env:APPDATA\\Gallant Leaf"',
    # Subexpressions that run something; operators, Get-Date and DateTime around writes.
    f'"$(Remove-Item "{SAVES}/x")"',
    f'$f = Get-Item "{SAVES}/x"; "$($f.Delete())"',
    f'"$(Get-Item "{SAVES}/x" | Remove-Item)"',
    f'(Get-Date) - (Remove-Item "{SAVES}/x")',
    f'(Get-Date) - Remove-Item "{SAVES}/x"',
    f'$f = Get-Item "{SAVES}/x"; (Get-Date) - $f.IsReadOnly = $true',
    f'Get-Date | Remove-Item "{SAVES}/x"',
    f'Set-Date (Get-Date); Get-ChildItem "{SAVES}"',
    f'(Get-Item "{SAVES}/x").LastWriteTime = (Get-Date).AddDays(-1)',
    f'Get-ChildItem "{SAVES}" | % {{ $_.Replace("x", "y") }}',
    "Remove-Item -Recurse (Join-Path $ud 3392510)",
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
    ("tool", "command", "cwd"),
    [
        # A folder named after Steam or a user, not a parent of a protected folder.
        ("Bash", "git status", "D:/code/zexion-ap/.claude/worktrees/steam-achievements-1a2b3c"),
        ("Bash", ".venv/Scripts/python -m pytest -q", "/home/steam/zexion-ap"),
        ("Bash", "ruff check .", "C:/Users/SteamDev/code/zexion-ap"),
        ("PowerShell", "git status", "C:\\Users\\Gallant\\code\\zexion-ap"),
        ("Bash", "cd .claude/worktrees/steam-achievements-1a2b3c && git status", REPO),
        ("Bash", 'cd "D:/Projects [old]/zexion-ap" && cmake --build native/build', REPO),
        ("Bash", f'ls "{SAVES}" && git push -u origin claude/steam-x', "/home/steam/zexion-ap"),
        ("Bash", "../../.venv/Scripts/python MultiServer.py ../../lab/ap/AP_1.zip", "D:/c/.ap/AP"),
        # Names that only end in, or hold, a parent's name.
        ("Bash", "git status", "D:/code/zexion-ap/.claude/worktrees/fix-userdata"),
        ("Bash", "git status", "D:/code/zexion-ap/.claude/worktrees/steam-userdata"),
        ("Bash", "git status", "D:/work/mysteamapps"),
    ],
)
def test_working_folders_named_like_steam_allowed(tool: str, command: str, cwd: str) -> None:
    assert guard_protected.check(tool, {"command": command}, protected, session_cwd=cwd) is None


@pytest.mark.parametrize(
    ("command", "cwd"),
    [
        # In a protected folder, or in a parent of one, relative paths reach it.
        ("git status", SAVES),
        ("rm -r Zexion", "C:/Users/someone/AppData/Roaming/Gallant Leaf"),
        ("rm -rf Zex*", "E:/SteamLibrary/steamapps/common"),
        ("rm -rf x", "E:/SteamLibrary/steamapps"),
        ("cp lab/a ../Zexion/x.dll", "E:/SteamLibrary/steamapps/common/Other"),
        ("cd E:/SteamLibrary/steamapps/common/Other && cp lab/a ../Zexion/x.dll", REPO),
        ("cd ../Gallant\\ Leaf && rm -r Zexion", "C:/Users/someone/AppData/Roaming/Other"),
        ("rm -r 123/3392510", "C:/Program Files (x86)/Steam/userdata"),
        # A parent named by itself, from the folder above it (made-up paths: none exist).
        ('rm -rf "Gallant Leaf"', "C:/Users/someone/AppData/Roaming"),
        ("cp -r lab/Zexion Gall*", "C:/Users/someone/AppData/Roaming"),
    ],
)
def test_working_folders_near_protected_ones_blocked(command: str, cwd: str) -> None:
    assert guard_protected.check("Bash", {"command": command}, protected, session_cwd=cwd)


def test_a_bare_steamapps_counts_only_where_it_exists(tmp_path: Path) -> None:
    # In a Steam library, steamapps is the parent; elsewhere the word may be a test name.
    rm = {"command": "rm -rf steamapps"}
    assert guard_protected.check("Bash", rm, protected, session_cwd=str(tmp_path)) is None
    (tmp_path / "steamapps").mkdir()
    assert guard_protected.check("Bash", rm, protected, session_cwd=str(tmp_path))
    pytest_k = {"command": ".venv/Scripts/python -m pytest -k userdata"}
    assert guard_protected.check("Bash", pytest_k, protected, session_cwd=str(tmp_path)) is None


@pytest.mark.parametrize(
    ("path", "expected"),
    [
        ("C:/Users/x/AppData/Roaming/Gallant Leaf", True),
        ("C:/Users/x/AppData/Roaming/Gallant Leaf.", True),
        ("C:/Users/x/AppData/Roaming/GALLAN~1", True),
        ("C:/Users/x/AppData/Roaming/Gall*", True),
        ("E:/SteamLibrary/steamapps", True),
        ("E:/SteamLibrary/steamapps/common", True),
        ("C:/Program Files (x86)/Steam/userdata", True),
        ("C:/Program Files (x86)/Steam/userdata/123", True),
        ("D:/code/zexion-ap/.claude/worktrees/steam-achievements-1a2b3c", False),
        ("/home/steam/zexion-ap", False),
        ("C:/Users/SteamDev", False),
        ("C:/Program Files (x86)/Steam", False),
        ("E:/SteamLibrary/steamapps/common/Other", False),
        ("D:/Projects [old]", False),
        ("D:/code/zexion-ap/.claude/worktrees/fix-userdata", False),
        ("D:/work/mysteamapps", False),
        ("D:/code/zexion-ap/native/build/*", False),
        ("C:/Program Files (x86)/Steam/userdata/*", True),
        ("E:/SteamLibrary/steamapps/*", True),
    ],
)
def test_may_be_above_protected(path: str, expected: bool) -> None:
    assert protected.may_be_above_protected(path) is expected


@pytest.mark.parametrize(
    ("text", "expected"),
    [
        (f"os.path.join(os.environ['APPDATA'], {GL_ZX}, 'x.sav')", True),
        ("Path(appdata) / 'Gallant Leaf' / 'Zexion'", True),
        ("[IO.Path]::Combine($a,'Gallant Leaf','Zexion')", True),
        ("'E:/Steam/steamapps/common/' + 'Zexion/zlib.dll'", True),
        ("join('C:/Steam/userdata', '123', '3392510')", True),
        ('"$APPDATA/Gallant Leaf/"$(echo 1)"/../Zexion/x"', True),
        ("'Gallant Leaf/a b/c/../../Zexion'", True),
        ("Gallant Leaf/Zexion/../Other", True),  # names it on the way
        ("Gallant Leaf/x/../../Zexion", False),  # one level too many
        ("Gallant Leaf/../../Zexion", False),
        ("'Gallant Leaf/Zex' 'ion'", True),
        ("os.path.join(appdata, 'Gallant Leaf', 'Other')", False),
        ("os.path.join(root, 'worlds', 'zexion')", False),
        ("'steamapps', 'common', 'ZexionSoundtrack'", False),
        ("join(join(appdata, 'Gallant Leaf'), 'Zexion')", True),
        ("join(appdata, r'Gallant Leaf', R'Zexion')", True),
        ("'Gallant Leaf/' + 'Zex' + 'ion'", True),
        ("Join-Path $a 'Gallant Leaf' -AdditionalChildPath 'Zexion','x'", True),
        ("'Gallant Leaf/Zex' + 'ionSoundtrack'", False),
    ],
)
def test_mentions_protected_joins_quoted_pieces(text: str, expected: bool) -> None:
    assert protected.mentions_protected(text) is expected


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


# The mod's offline files, %LOCALAPPDATA%\ZexionAP: only the helper writes them.
LOCAL = "C:/Users/someone/AppData/Local"
OFFLINE = f"{LOCAL}/ZexionAP"
SCRATCH = f"{LOCAL}/Temp/claude/2745d331536e48bf/7c00b8f6-7f48-4f26-95c9-decf1adcccd1/scratchpad"


@pytest.mark.parametrize(
    "path",
    [
        f"{OFFLINE}/config-B1.txt",
        f"{OFFLINE}/seeds/Player1.txt",
        f"{OFFLINE}/labels/x.txt",
        OFFLINE.replace("/", "\\") + "\\seeds\\Player1.txt",
        "%LOCALAPPDATA%\\ZexionAP\\config-B1.txt",
        "$env:LOCALAPPDATA\\ZexionAP\\seeds\\x.txt",
        "C:/Users/someone/AppData/Local/zexionap/x",
        "C:/Users/someone/AppData/Local/ZexionAP./x",
        "C:/Users/someone/AppData/Local/Other/../ZexionAP/x",
    ],
)
def test_file_tools_blocked_in_the_offline_folder(path: str) -> None:
    for tool in ("Write", "Edit", "MultiEdit", "NotebookEdit"):
        assert run(tool, file_path=path), (tool, path)


@pytest.mark.parametrize(
    "path",
    [
        f"{LOCAL}/Temp/x.txt",
        f"{LOCAL}/Claude-Data/x.txt",
        f"{LOCAL}/ZexionAPnotes/x.txt",
        f"{LOCAL}/ZexionAP-old/x.txt",
        "C:/Users/someone/AppData/Roaming/ZexionAP/x",
        "D:/code/zexion-ap/client/offline.py",
        "D:/code/zexion-ap/docs/ZexionAP.md",
    ],
)
def test_file_tools_allowed_beside_the_offline_folder(path: str) -> None:
    assert run("Write", file_path=path) is None


OFFLINE_BLOCKED = [
    f'cp lab/a "{OFFLINE}/seeds/Player1.txt"',
    f'echo x > "{OFFLINE}/config-B1.txt"',
    f'echo x >> "{OFFLINE}/seeds/Player1.txt"',
    f'rm "{OFFLINE}/seeds/Player1.txt"',
    f'rm -rf "{OFFLINE}"',
    f'mv "{OFFLINE}/config-B1.txt" lab/',
    f'touch "{OFFLINE}/x"',
    f'cat lab/a | tee "{OFFLINE}/x"',
    f'sed -i s/a/b/ "{OFFLINE}/config-B1.txt"',
    f'find "{OFFLINE}" -name "*.txt" -delete',
    f"python -c \"open(r'{OFFLINE}/x', 'w')\"",
    f'ls "{OFFLINE}" && $(rm -rf "{OFFLINE}")',
    'rm -rf "$LOCALAPPDATA/ZexionAP"',
    'rm -rf "${LOCALAPPDATA}/ZexionAP/seeds"',
    'echo x > "$LOCALAPPDATA/ZexionAP/x"',
    'rm -rf "$LOCALAPPDATA"/Zexion*',
    'rm -rf "$LOCALAPPDATA/Z*/seeds"',
    "rm -rf ~/AppData/Local/ZexionAP",
    'rm -rf "$LOCALAPPDATA"',  # the parent: deleting it deletes the folder
    'cp -r lab/x "$LOCALAPPDATA"',
    "python -c \"import os; os.remove(os.environ['LOCALAPPDATA'] + '/ZexionAP/x')\"",
    "python -c \"import os; open(os.path.join(os.environ['LOCALAPPDATA'], 'ZexionAP', 'x'), 'w')\"",
    "python -c \"import os; d = os.environ['LOCALAPPDATA']; open(d + '/' + 'ZexionAP', 'w')\"",
    'python -c "from pathlib import Path; import os; '
    "Path(os.environ['LOCALAPPDATA']).joinpath('ZexionAP', 'x').unlink()\"",
]
OFFLINE_PS_BLOCKED = [
    f'Remove-Item "{OFFLINE}\\seeds\\Player1.txt"',
    f'Copy-Item lab\\a "{OFFLINE}\\config-B1.txt"',
    'Remove-Item -Recurse "$env:LOCALAPPDATA\\ZexionAP"',
    'Set-Content -Path "$env:LOCALAPPDATA\\ZexionAP\\x" -Value 1',
    'Remove-Item -Recurse -Force "${env:LOCALAPPDATA}\\ZexionAP"',
    'Remove-Item -Recurse "%LOCALAPPDATA%\\ZexionAP"',
    "Remove-Item -Recurse $env:LOCALAPPDATA\\Zexion*",
    "Remove-Item -Recurse (Join-Path $env:LOCALAPPDATA 'ZexionAP')",
    "$d = $env:LOCALAPPDATA; Remove-Item -Recurse (Join-Path $d 'ZexionAP')",
    "Remove-Item -Recurse "
    "([IO.Path]::Combine([Environment]::GetFolderPath('LocalApplicationData'), 'ZexionAP'))",
    'echo x > "$env:LOCALAPPDATA\\ZexionAP\\x"',
    'Remove-Item -Recurse "$env:LOCALAPPDATA"',
]
OFFLINE_ALLOWED = [
    f'ls "{OFFLINE}"',
    f'cat "{OFFLINE}/config-B1.txt"',
    f'head -5 "{OFFLINE}/seeds/Player1.txt" | cat',
    f'xxd "{OFFLINE}/config-B1.txt" | head',
    f'grep -rn address "{OFFLINE}"',
    f'find "{OFFLINE}" -name "*.txt"',
    f'wc -c "{OFFLINE}"/seeds/*.txt',
    f'xxd "{OFFLINE}/x" > lab/x.hex',
    f'ls -la "{LOCAL}"',
    'ls "$LOCALAPPDATA/ZexionAP"',
    'cat "$LOCALAPPDATA/ZexionAP/config-B1.txt"',
    'du -sh "$LOCALAPPDATA"/Zexion*',
    # Elsewhere in LOCALAPPDATA, and the repo's own names.
    f'rm -rf "{LOCAL}/Temp/x"',
    f'echo x > "{LOCAL}/Temp/x.txt"',
    f'cp lab/a "{LOCAL}/Temp/x.txt"',
    'rm -rf "$LOCALAPPDATA/Temp/x"',
    'rm -f "$LOCALAPPDATA/Temp/"*',
    'echo x > "$LOCALAPPDATA/Temp/x.txt"',
    'cp lab/a "$LOCALAPPDATA/Temp/claude/x.txt"',
    "python -c \"import os; open(os.environ['LOCALAPPDATA'] + '/Temp/x', 'w')\"",
    "rm -rf native/build/* lab/ap/*",
    "cat client/offline.py",
    'git commit -m "Docs: the offline files in %LOCALAPPDATA%\\ZexionAP"',
    "git commit -m \"$(cat <<'EOF'\nGuard: protect %LOCALAPPDATA%\\ZexionAP\nEOF\n)\"",
    'gh pr create --title "Guard" --body "Blocks writes to %LOCALAPPDATA%\\ZexionAP"',
    'grep -rn "ZexionAP" docs/ client/ native/src',
    f"cat > lab/notes.md <<'EOF'\n{OFFLINE}\nEOF",
    ".venv/Scripts/python tools/build_id.py",
]
OFFLINE_PS_ALLOWED = [
    f'Get-ChildItem "{OFFLINE}"',
    'Get-ChildItem "$env:LOCALAPPDATA\\ZexionAP"',
    'Get-Content "$env:LOCALAPPDATA\\ZexionAP\\config-B1.txt" -TotalCount 5',
    'Get-ChildItem "$env:LOCALAPPDATA\\ZexionAP" | Select-Object Name, Length',
    'Test-Path "$env:LOCALAPPDATA\\ZexionAP\\seeds"',
    "(Get-Item $env:LOCALAPPDATA\\ZexionAP\\config-B1.txt).Length",
    'Remove-Item -Recurse "$env:LOCALAPPDATA\\Temp\\x"',
    'Set-Content -Path "$env:LOCALAPPDATA\\Temp\\x" -Value 1',
    'Copy-Item lab\\a "$env:LOCALAPPDATA\\Temp\\claude\\x.txt"',
    "git commit -m @'\nDocs: %LOCALAPPDATA%\\ZexionAP\n'@",
]


@pytest.fixture
def local_appdata(monkeypatch):
    """LOCALAPPDATA set the way Windows sets it, so the env var spellings expand."""
    monkeypatch.setenv("LOCALAPPDATA", LOCAL)


@pytest.mark.usefixtures("local_appdata")
@pytest.mark.parametrize(
    ("tool", "command"),
    [("Bash", c) for c in OFFLINE_BLOCKED] + [("PowerShell", c) for c in OFFLINE_PS_BLOCKED],
)
def test_writes_into_the_offline_folder_blocked(tool: str, command: str) -> None:
    assert run(tool, command=command)


@pytest.mark.usefixtures("local_appdata")
@pytest.mark.parametrize(
    ("tool", "command"),
    [("Bash", c) for c in OFFLINE_ALLOWED] + [("PowerShell", c) for c in OFFLINE_PS_ALLOWED],
)
def test_reads_of_the_offline_folder_and_other_local_paths_allowed(tool: str, command: str) -> None:
    assert run(tool, command=command) is None


# On Linux: $XDG_DATA_HOME/ZexionAP, else ~/.local/share/ZexionAP (Snap Steam's too), or Flatpak
# Steam's, as the Linux helper sees it (native/src/offline.cpp, offline_folder).
LINUX_HOME = "/home/someone"
DATA_HOME = f"{LINUX_HOME}/.local/share"
FLATPAK_DATA = f"{LINUX_HOME}/.var/app/com.valvesoftware.Steam/data"
LINUX_OFFLINE_FILES = [
    f"{DATA_HOME}/ZexionAP/seeds/S-0-1.txt",
    f"{FLATPAK_DATA}/ZexionAP/seeds/S-0-1.txt",
    f"{LINUX_HOME}/snap/steam/common/.local/share/ZexionAP/x",
    "$XDG_DATA_HOME/ZexionAP/x",
    "${XDG_DATA_HOME}/ZexionAP/seeds/x.txt",
    "/data/someone/ZexionAP/x",  # XDG_DATA_HOME set to a folder of its own (below)
]
LINUX_OFFLINE_BLOCKED = [
    f'rm -rf "{DATA_HOME}/ZexionAP"',
    f'echo x > "{FLATPAK_DATA}/ZexionAP/seeds/S-0-1.txt"',
    f'cp lab/a "{DATA_HOME}/ZexionAP/seeds/x.txt"',
    "rm -rf ~/.local/share/ZexionAP",
    'rm -rf "$XDG_DATA_HOME/ZexionAP/seeds"',
    "rm -rf /data/someone/ZexionAP",
    'python -c "import os; '
    "open(os.path.join(os.environ['XDG_DATA_HOME'], 'ZexionAP', 'x'), 'w')\"",
    "python -c \"import os; d = os.path.expanduser('~/.local/share'); "
    "os.remove(os.path.join(d, 'ZexionAP', 'x'))\"",
]
LINUX_OFFLINE_ALLOWED = [
    f'ls "{DATA_HOME}/ZexionAP"',
    f'cat "{FLATPAK_DATA}/ZexionAP/seeds/S-0-1.txt"',
    'cat "$XDG_DATA_HOME/ZexionAP/seeds/x.txt"',
    f'rm -rf "{DATA_HOME}/uv/x"',
    f'echo x > "{DATA_HOME}/ZexionAP-notes.md"',
    f'rm -rf "{FLATPAK_DATA}/other"',
    'grep -rn "XDG_DATA_HOME/ZexionAP" docs/ native/src',
]


@pytest.fixture
def xdg_data_home(monkeypatch):
    """XDG_DATA_HOME set to a folder other than the default, so its expansion is protected too."""
    monkeypatch.setenv("XDG_DATA_HOME", "/data/someone")


@pytest.mark.usefixtures("xdg_data_home")
@pytest.mark.parametrize("path", LINUX_OFFLINE_FILES)
def test_file_tools_blocked_in_the_linux_offline_folder(path: str) -> None:
    for tool in ("Write", "Edit", "MultiEdit", "NotebookEdit"):
        assert run(tool, file_path=path), (tool, path)


@pytest.mark.usefixtures("xdg_data_home")
@pytest.mark.parametrize("command", LINUX_OFFLINE_BLOCKED)
def test_writes_into_the_linux_offline_folder_blocked(command: str) -> None:
    assert run("Bash", command=command)


@pytest.mark.usefixtures("xdg_data_home")
@pytest.mark.parametrize("command", LINUX_OFFLINE_ALLOWED)
def test_reads_of_the_linux_offline_folder_and_beside_it_allowed(command: str) -> None:
    assert run("Bash", command=command) is None


# Another process's folders through /proc (the game's install is the game's cwd, its saves in its
# root's view): a command that isn't read-only is blocked, whatever the pid, worked out by the
# command or not; reads, and the shell's own (/proc/self), are fine.
PROC_BLOCKED = [
    "cp native/build/libzexion_ap.so /proc/$(pgrep -x Zexion)/cwd/",
    "cp x /proc/`pgrep Zexion`/cwd/",
    "cp x /proc/*/cwd/",
    'pid=$(pgrep Zexion); cp x "/proc/$pid/cwd/lib.so"',
    'cp x "/proc/${pid}/root/tmp/x"',
    "rm -rf /proc/$(pgrep Zexion)/root/home",
    "ln -s /proc/$(pgrep Zexion)/cwd here",
    "tee /proc/*/cwd/x < y",
    "cp x /proc/12345/cwd/y",  # a literal pid's too
    # The path spelled otherwise, or built from pieces.
    "cp x /proc//$(pgrep Zexion)/cwd/lib.so",
    "cp x /proc/./$pid/cwd/",
    "cp x /proc/1/../$pid/cwd/",
    "cp x '/proc'/$pid/cwd/y",
    'cp x "/proc"/$(pgrep Zexion)/cwd/y',
    "cd /proc && cp x */cwd/",
    "cd /proc; cp x $(pgrep Zexion)/cwd/y",
    "P=/proc; cp x $P/$pid/cwd/y",
    "find /proc -maxdepth 2 -name cwd -exec cp x {} \\;",
    "python3 -c \"import os,shutil; shutil.copy('x', os.path.join('/proc', pid, 'cwd'))\"",
]
PROC_ALLOWED = [
    "readlink /proc/*/exe",
    "ls -l /proc/12345/cwd",
    "cat /proc/self/status",
    "cp x /proc/self/cwd/y",
    "cat /proc/cpuinfo",
    "pid=12; grep zexion /proc/$pid/maps",
    "ps aux",
    "echo root > notes.txt",
]


@pytest.mark.parametrize("command", PROC_BLOCKED)
def test_writes_through_another_processs_proc_view_blocked(command: str) -> None:
    assert run("Bash", command=command)


@pytest.mark.parametrize("command", PROC_ALLOWED)
def test_reads_through_proc_and_the_shells_own_allowed(command: str) -> None:
    assert run("Bash", command=command) is None


def test_the_linux_offline_folder_is_blocked_with_the_variable_missing(monkeypatch) -> None:
    monkeypatch.delenv("XDG_DATA_HOME", raising=False)
    assert run("Write", file_path="$XDG_DATA_HOME/ZexionAP/x")
    assert run("Bash", command='rm -rf "${XDG_DATA_HOME}/ZexionAP"')
    assert run("Bash", command='ls "$XDG_DATA_HOME/ZexionAP"') is None


@pytest.mark.parametrize("spelling", ["%LOCALAPPDATA%", "$env:LOCALAPPDATA", "${LOCALAPPDATA}"])
def test_the_offline_folder_is_blocked_with_the_variable_missing(
    spelling: str, monkeypatch
) -> None:
    # Linux and macOS, or a stripped environment: nothing to expand, nothing may crash or pass.
    monkeypatch.delenv("LOCALAPPDATA", raising=False)
    assert protected.normalize(f"{spelling}/x") == f"{spelling.lower()}/x"
    assert run("Write", file_path=f"{spelling}/ZexionAP/x")
    assert run("Bash", command=f'rm -rf "{spelling}/ZexionAP"')
    assert run("PowerShell", command=f'Remove-Item -Recurse "{spelling}\\ZexionAP\\seeds"')
    assert run("Bash", command=f'ls "{spelling}/ZexionAP"') is None
    assert run("Bash", command=f'rm -rf "{spelling}/Temp/x"') is None


@pytest.mark.parametrize("name", ["TEMP", "TMP"])
def test_the_offline_folder_through_temp_blocked(name: str, monkeypatch) -> None:
    # TEMP and TMP are AppData\Local\Temp: one ``..`` leads to the folder.
    monkeypatch.setenv(name, f"{LOCAL}/Temp")
    for spelling in (f"${name}", f"${{{name}}}", f"%{name}%"):
        assert run("Bash", command=f'rm -rf "{spelling}/../ZexionAP"'), spelling
        assert run("Bash", command=f'echo x > "{spelling}/../ZexionAP/x"'), spelling
        assert run("Write", file_path=f"{spelling}/../ZexionAP/x"), spelling
    assert run("PowerShell", command=f'Remove-Item -Recurse "$env:{name}\\..\\ZexionAP"')
    assert run("PowerShell", command=f'Remove-Item -Recurse "${{env:{name}}}\\..\\ZexionAP"')
    # Writes under it, and a read of the folder through it, stay allowed.
    assert run("Bash", command=f'rm -rf "${name}/x"') is None
    assert run("Bash", command=f'echo x > "${name}/x.txt"') is None
    assert run("Bash", command=f'ls "${name}/../ZexionAP"') is None
    assert run("PowerShell", command=f'Remove-Item -Recurse "$env:{name}\\x"') is None
    assert run("Write", file_path=f"${name}/x.txt") is None


def test_reading_the_offline_folder_through_a_folder_number_allowed() -> None:
    get = "[Environment]::GetFolderPath(28)"
    assert run("PowerShell", command=f"Get-ChildItem (Join-Path ({get}) 'ZexionAP')") is None


@pytest.mark.parametrize("name", ["TEMP", "APPDATA"])
def test_a_variable_the_command_sets_itself_is_not_the_environments(name: str, monkeypatch) -> None:
    # Else ``$TEMP`` would be read as the real Temp folder and the redirect let through.
    monkeypatch.setenv(name, f"{LOCAL}/Temp")
    for folder in (SAVES, OFFLINE):
        assert run("Bash", command=f'{name}="{folder}"; ls lab > "${name}/x"')
        assert run("Bash", command=f'export {name}="{folder}"; echo x >> "${{{name}}}/x"')


@pytest.mark.parametrize("number", ["28", "0x1c", "0x1C", " 28 "])
def test_the_offline_folder_through_a_folder_number_blocked(number: str) -> None:
    # GetFolderPath(28) is LocalApplicationData.
    get = f"[Environment]::GetFolderPath({number})"
    assert run("PowerShell", command=f"Remove-Item -Recurse (Join-Path ({get}) 'ZexionAP')")
    assert run("PowerShell", command=f'Remove-Item -Recurse "$({get})\\ZexionAP"')
    assert run("PowerShell", command=f"$d = {get}; Remove-Item -Recurse (Join-Path $d 'ZexionAP')")
    assert run("PowerShell", command=f"Set-Content -Path ({get} + '\\ZexionAP\\x') -Value 1")
    other = "[Environment]::GetFolderPath(26)"  # ApplicationData: not this folder
    assert (
        run("PowerShell", command=f"Remove-Item -Recurse (Join-Path ({other}) 'ZexionAP')") is None
    )
    assert run("PowerShell", command=f"Remove-Item -Recurse (Join-Path ({get}) 'Temp\\x')") is None


def test_the_offline_folder_with_a_different_local_appdata(monkeypatch) -> None:
    monkeypatch.setenv("LOCALAPPDATA", "D:/Profiles/someone/Local")
    assert run("Bash", command='rm -rf "$LOCALAPPDATA/ZexionAP"')
    assert run("Write", file_path="%LOCALAPPDATA%/ZexionAP/config-B1.txt")
    assert run("Write", file_path="D:/Profiles/someone/Local/ZexionAP/config-B1.txt")
    assert run("Bash", command='rm -rf "$LOCALAPPDATA"/Zexion*')
    assert protected.word_may_name_protected("D:/Profiles/someone/Local/Zex*/x")
    assert run("Bash", command='rm -rf "$LOCALAPPDATA/Temp/x"') is None
    assert run("Bash", command='ls "$LOCALAPPDATA/ZexionAP"') is None


@pytest.mark.parametrize(
    "command",
    [
        f'cmake --build "{LOCAL}/Temp/scratch/build" --config Release # ZexionAP',
        f'ctest --test-dir "{LOCAL}/Temp/scratch/b" -C Release -R offline # ZexionAP',
        f'pytest tests -k ZexionAP --basetemp "{LOCAL}/Temp/scratch/pt"',
        f'Q="{LOCAL}/Temp/scratch"; grep -rn ZexionAP . > "$Q/hits.txt"',
        f'python -c "print(1)" > "{LOCAL}/Temp/scratch/out.txt" # ZexionAP',
        # A ``..`` or a parent word beside a scratch path is not TEMP's parent.
        "mkdir -p /tmp/x && ls .. > /tmp/x/l # ZexionAP",
        f'python tools/x.py "{SCRATCH}/../x" ZexionAP',
        f'cd "{SCRATCH}/b" && ctest -C Release -R ZexionAP && cd ..',
        f'python -m pytest tests -k ZexionAP --basetemp "{SCRATCH}/pt" && cd ..',
        f'cd native && cmake -S . -B "{SCRATCH}/b" && cmake --build "{SCRATCH}/b" '
        "&& cd .. # ZexionAP",
        f'cp "{SCRATCH}/../x.txt" lab/ # ZexionAP',
        f'python "{SCRATCH}/p.py" --root "{SCRATCH}/.." ZexionAP',
        f'python -c "import os; print(os.path.dirname(\'a/b\'))" > "{SCRATCH}/o.txt" # ZexionAP',
        f'rm -rf "{SCRATCH}/r155/old" # ZexionAP notes in ../',
    ],
)
def test_a_scratch_path_beside_the_word_is_not_the_offline_folder(command: str) -> None:
    # ``AppData/Local/Temp/…`` holds every scratch, TEMP and test folder: the word alone is
    # no build of ``AppData/Local/ZexionAP``.
    assert run("Bash", command=command) is None


@pytest.mark.parametrize(
    ("tool", "command"),
    [
        ("Bash", f'Q="{LOCAL}"; rm -rf "$Q/ZexionAP"'),
        ("Bash", f'Q="{LOCAL}"; rm -rf "$Q/Temp/../ZexionAP"'),
        ("Bash", 'cd "$LOCALAPPDATA" && rm -rf ZexionAP'),
        ("Bash", 'rm "$LOCALAPPDATA/ZexionAP/seeds.txt"'),
        ("PowerShell", "$d = $env:LOCALAPPDATA; Remove-Item -Recurse (Join-Path $d 'ZexionAP')"),
        ("PowerShell", 'Remove-Item -Recurse "$env:TEMP\\..\\ZexionAP"'),
        # TEMP's parent is the folder: a ``..`` piece or a parent function takes it.
        ("PowerShell", "Remove-Item -Recurse (Join-Path $env:TEMP '..\\ZexionAP')"),
        ("Bash", 'TMP=..; cd "$LOCALAPPDATA/Temp" && rm -rf $TMP/ZexionAP'),
        ("PowerShell", "Remove-Item -Recurse (Join-Path $env:TEMP '..' 'ZexionAP')"),
        ("PowerShell", "Remove-Item -Recurse (Join-Path (Split-Path $env:TEMP) 'ZexionAP')"),
        (
            "PowerShell",
            "Remove-Item -Recurse (Join-Path (Split-Path $env:TEMP -Parent) 'ZexionAP')",
        ),
        ("PowerShell", '$d = Split-Path $env:TEMP; Remove-Item -Recurse "$d\\ZexionAP"'),
        (
            "PowerShell",
            "Remove-Item -Recurse ([IO.Path]::Combine($env:TEMP, '..', 'ZexionAP'))",
        ),
        (
            "PowerShell",
            "Remove-Item -Recurse ((Get-Item $env:TEMP).Parent.FullName + '\\ZexionAP')",
        ),
        ("Bash", 'rm -rf "$(dirname "$TEMP")/ZexionAP"'),
        ("Bash", 'd=$(dirname "$TEMP"); rm -rf "$d/ZexionAP"'),
        ("Bash", 'p=..; rm -rf "$TEMP/$p/ZexionAP"'),
        (
            "Bash",
            'python -c "import os,shutil; '
            "shutil.rmtree(os.path.join(os.environ['TEMP'], '..', 'ZexionAP'))\"",
        ),
        (
            "Bash",
            'python -c "import os,shutil; '
            "shutil.rmtree(os.path.join(os.path.dirname(os.environ['TEMP']), 'ZexionAP'))\"",
        ),
        (
            "Bash",
            'python -c "import os,shutil,tempfile; '
            "shutil.rmtree(os.path.join(os.path.dirname(tempfile.gettempdir()), 'ZexionAP'))\"",
        ),
        (
            "Bash",
            'python -c "import shutil,tempfile,pathlib; '
            "shutil.rmtree(pathlib.Path(tempfile.gettempdir()).parent / 'ZexionAP')\"",
        ),
    ],
)
def test_the_offline_folder_built_from_pieces_still_blocked(tool: str, command: str) -> None:
    assert run(tool, command=command), command


@pytest.mark.parametrize(
    ("cwd", "blocked"),
    [
        (OFFLINE, True),
        (f"{OFFLINE}/seeds", True),
        (LOCAL, True),  # a parent: relative paths reach the folder
        ("C:/Users/someone/AppData", True),
        (f"{LOCAL}/Temp", False),
        (f"{LOCAL}/Claude-Data/x", False),
        ("D:/code/zexion-ap", False),
    ],
)
def test_the_offline_folder_as_the_working_folder(cwd: str, blocked: bool) -> None:
    rm = {"command": "rm -rf ZexionAP"}
    assert bool(guard_protected.check("Bash", rm, protected, session_cwd=cwd)) is blocked


@pytest.mark.parametrize(
    ("word", "expected"),
    [
        (f"{OFFLINE}/x", True),
        (f"{LOCAL}/ZexionAP", True),
        (f"{LOCAL}/Zexion*/x", True),
        (f"{LOCAL}/ZEXIONAP/x", True),
        (f"{LOCAL}/Other/../ZexionAP/x", True),
        (f"{LOCAL}/Temp/x", False),
        (f"{LOCAL}/Zexions/x", False),
        ("C:/Users/someone/AppData/Roaming/ZexionAP/x", False),
        ("/usr/local/*", False),
        ("native/build/*", False),
    ],
)
def test_word_may_name_the_offline_folder(word: str, expected: bool) -> None:
    assert protected.word_may_name_protected(word) is expected


@pytest.mark.parametrize(
    ("text", "expected"),
    [
        (OFFLINE, True),
        ("%LOCALAPPDATA%\\ZexionAP\\seeds", True),
        ("$env:LOCALAPPDATA\\ZexionAP", True),
        ("os.path.join(os.environ['LOCALAPPDATA'], 'ZexionAP')", True),
        ("Path(os.environ['LOCALAPPDATA']) / 'ZexionAP' / 'x'", True),
        ("[Environment]::GetFolderPath('LocalApplicationData') + '\\ZexionAP'", True),
        ("%LOCALAPPDATA%\\ZexionAP-notes", False),
        ("%LOCALAPPDATA%\\ZexionAP.md", False),
        ("%LOCALAPPDATA%\\Temp", False),
        ("D:/code/zexion-ap/client/offline.py", False),
        ("ZexionAP", False),  # the name alone, away from LOCALAPPDATA, is prose
    ],
)
def test_mentions_the_offline_folder(text: str, expected: bool) -> None:
    assert protected.mentions_protected(text) is expected


def test_assert_writable_refuses_the_offline_folder() -> None:
    with pytest.raises(protected.ProtectedPathError):
        protected.assert_writable(f"{OFFLINE}/seeds/x.txt")
    assert protected.assert_writable(f"{LOCAL}/Temp/x.txt") == Path(f"{LOCAL}/Temp/x.txt")


# A folder named only by its own name, from wherever the search starts (#172): the command names
# neither the parent path nor the data-home variable. ``find``'s name predicates and PowerShell's
# -Filter and -Include count when what the command does with the matches can write.
BY_NAME_BLOCKED = [
    ("Bash", "find ~ -name ZexionAP -delete"),
    ("Bash", 'find ~ -name "ZexionAP" -exec rm -r {} +'),
    ("Bash", "find ~ -type d -name ZexionAP -execdir rm -rf {} +"),
    ("Bash", "find ~ -name ZexionAP -ok rm -r {} \\;"),
    ("Bash", "find ~ -name ZexionAP -okdir rm -r {} \\;"),
    ("Bash", "find ~ -name ZexionAP -fprint /tmp/x"),
    ("Bash", "find ~ -name ZexionAP -a -type d -delete"),
    ("Bash", "find ~ ! -name x -name ZexionAP -delete"),
    ("Bash", "cd ~ && find . -name ZexionAP -delete"),
    # The other names: the save folder's parent and its own, the cloud cache's, the library's.
    ("Bash", 'find / -name "Gallant Leaf" -exec rm -r {} +'),
    ("Bash", "find / -name Gallant\\ Leaf -delete"),
    ("Bash", "find / -name 3392510 -delete"),
    ("Bash", "find ~ -type d -name steamapps -exec rm -rf {} +"),
    ("Bash", "find ~ -type d -name userdata -delete"),
    # Case, wildcards and characters classes, and quotes that split the name.
    ("Bash", "find ~ -iname '*zexionap*' -delete"),
    ("Bash", "find ~ -type d -iname ZEXIONAP -exec rm -r {} +"),
    ("Bash", "find ~ -name 'Zexion*' -exec rm -rf {} \\;"),
    ("Bash", "find ~ -name 'Z*AP' -delete"),
    ("Bash", "find ~ -name 'Z?xionAP' -delete"),
    ("Bash", "find ~ -name 'ZexionA[P]' -delete"),
    ("Bash", "find ~ -name 'Gall*' -delete"),
    ("Bash", 'find ~ -name "Zexion"AP -delete'),
    ("Bash", "find ~ -name 'Zex''ionAP' -delete"),
    # Path and regular-expression predicates.
    ("Bash", "find ~ -path '*/ZexionAP' -delete"),
    ("Bash", "find ~ -ipath '*/zexionap/*' -delete"),
    ("Bash", "find ~ -wholename '*/ZexionAP' -delete"),
    ("Bash", "find ~ -path '*/Gallant Leaf/*' -delete"),
    ("Bash", "find ~ -regex '.*/ZexionAP' -delete"),
    ("Bash", "find ~ -iregex '.*/zexion.*' -delete"),
    ("Bash", "find ~ -regex '.*/(foo|ZexionAP)$' -delete"),
    ("Bash", "find ~ -regex '.*/Zexion.P' -delete"),
    # What it finds handed to another command.
    ("Bash", "find ~ -name ZexionAP -print0 | xargs -0 rm -rf"),
    ("Bash", "find ~ -name ZexionAP | xargs rm -r"),
    ("Bash", 'find ~ -name ZexionAP | while read d; do rm -r "$d"; done'),
    # Inside another command's word, or behind a prefix.
    ("Bash", 'sh -c "find ~ -name ZexionAP -delete"'),
    ("Bash", "bash -c 'find ~ -name ZexionAP -exec rm -r {} +'"),
    ("Bash", 'eval "find ~ -name ZexionAP -delete"'),
    ("Bash", "sudo find ~ -name ZexionAP -delete"),
    ("Bash", "env find ~ -name ZexionAP -delete"),
    ("Bash", "xargs find ~ -name ZexionAP -delete"),
    # PowerShell: Get-ChildItem's name filters, in full or abbreviated.
    ("PowerShell", "Get-ChildItem -Recurse -Filter ZexionAP | Remove-Item -Recurse -Force"),
    ("PowerShell", "gci ~ -r -Include ZexionAP | Remove-Item -Recurse"),
    ("PowerShell", "Get-ChildItem -Recurse -Filter 'Gallant Leaf' | Remove-Item -Recurse"),
    ("PowerShell", "gci -r -fi Zexion* | rm -r -fo"),
    ("PowerShell", "Get-ChildItem -Recurse -Include '*zexionap*' | Remove-Item -Recurse -Force"),
    (
        "PowerShell",
        "Get-ChildItem -Recurse -Filter ZexionAP | ForEach-Object { Remove-Item -Recurse $_ }",
    ),
    # What the first version of the fix missed (review of #178): find's default regular
    # expression syntax, a substitution's backticks, PowerShell's ``-Param:value`` and
    # positional filter, escapes, classes of letters, and a ``*`` that spans folders.
    ("Bash", "find ~ -regex '.*/\\(foo\\|ZexionAP\\)' -delete"),
    ("Bash", "find ~ -regex '.*/[^/]*nAP' -delete"),
    ("Bash", "find ~ -regex '.*/Gallant\\sLeaf' -exec rm -rf {} +"),
    ("Bash", "find ~ -regex '.*Local.ZexionAP' -delete"),
    ("Bash", "rm -rf `find ~ -name ZexionAP`"),
    ("Bash", "rm -rf `find ~ -name ZexionAP -print`"),
    ("Bash", "find ~ -name 'Zexi\\onAP' -delete"),
    ("Bash", "find ~ -name $'ZexionAP' -delete"),
    ("Bash", "find ~ -name '[Z][e][x][i][o][n][A][P]' -delete"),
    ("Bash", "find ~ -name '[Zz]exionAP' -delete"),
    ("Bash", "find ~ -name '[[:upper:]]exionAP' -delete"),
    ("Bash", "find ~ -path '*Local*ZexionAP*' -delete"),
    ("PowerShell", "gci ~ -r -Filter:ZexionAP | ri -r"),
    ("PowerShell", "gci ~ -r -Include:'ZexionAP' | ri -r"),
    (
        "PowerShell",
        "pwsh -Command 'Get-ChildItem -Recurse -Filter:ZexionAP | Remove-Item -Recurse'",
    ),
    ("PowerShell", "gci ~ ZexionAP -r | Remove-Item -Recurse"),
    ("PowerShell", "gci -Path ~ ZexionAP -r | Remove-Item -Recurse"),
    ("PowerShell", "gci -Path ~ -Recurse –Filter ZexionAP | Remove-Item -Recurse"),
    # Round 3 of the review: ``Zexion`` is the install and the real saves as written, and the
    # repo's own folder in lower case; a PowerShell list; groups, braces; the positional filter.
    ("Bash", "find / -name Zexion -exec rm -rf {} +"),
    ("Bash", "find ~ -name Zexion -type d -delete"),
    ("Bash", "find / -iname zexion -exec rm -rf {} +"),
    ("Bash", "find / -ipath '*/zexion' -exec rm -rf {} +"),
    ("PowerShell", "gci $env:APPDATA -r -Filter Zexion | ri -r"),
    ("PowerShell", "gci C:\\ -r -Filter zexion -Directory | ri -r"),
    ("PowerShell", "gci ~ -r -Include foo,ZexionAP | ri -r"),
    ("PowerShell", "gci ~ -r -Include 'ZexionAP','x' | ri -r"),
    ("PowerShell", "gci ~ -r -Include:foo,ZexionAP | ri -r"),
    ("PowerShell", "gci ~ -r -Include @('x','ZexionAP') | ri -r"),
    ("PowerShell", "gci ~ foo,ZexionAP -r | ri -r"),
    ("PowerShell", "gci -Path:~ ZexionAP -r | ri -r"),
    ("PowerShell", "gci ~ -ErrorAction SilentlyContinue ZexionAP -r | ri -r"),
    ("PowerShell", "gci -ea 0 ~ ZexionAP -r | ri -r"),
    ("PowerShell", "gci \u2013Path ~ ZexionAP -r | ri -r"),
    ("PowerShell", ". gci ~ ZexionAP -r | ri -r"),
    ("Bash", 'pwsh -c "gci ~ ZexionAP -r | ri -r"'),
    ("Bash", "find ~ -regex '.*/Zexio\\(n\\)AP' -delete"),
    ("Bash", "find ~ -regextype posix-extended -regex '.*/(Zexion)AP' -delete"),
    ("Bash", "find ~ -regex '.*/Z(exionAP)' -delete"),
    ("Bash", "find ~ -name {ZexionAP,} -delete"),
    ("Bash", "rm -rf `find ~ -name ZexionAP`/x"),
    ("Bash", "rm -rf $(find ~ -name ZexionAP)/x"),
    ("Bash", "ls `find ~ -name ZexionAP -delete`"),
    ("Bash", 'ls "$(find ~ -name ZexionAP -delete)"'),
    # Round 4: a command that runs in the folder a substitution found by name, a substitution
    # split by quotes, and PowerShell lists with spaces, parentheses or a continued line.
    ("Bash", 'cd "$(find ~ -name ZexionAP)" && git init -q && git clean -fdx'),
    ("Bash", "cd $(find ~ -name ZexionAP); git clean -fdx"),
    ("Bash", "cd `find ~ -name ZexionAP` && git checkout -- ."),
    ("Bash", 'pushd "$(find ~ -name ZexionAP)" && git clean -fdx'),
    ("Bash", 'git -C "$(find ~ -name ZexionAP)" init'),
    ("Bash", 'git --git-dir=/tmp/g --work-tree="$(find ~ -name ZexionAP)" clean -fdx'),
    ("Bash", 'cd "$(find / -name Zexion -type d -print -quit)" && git init -q && git clean -fdx'),
    ("PowerShell", "cd $(gci ~ -r -Filter ZexionAP); git init; git clean -fdx"),
    ("Bash", "x=$'a[\\x24(find ~ -name ZexionAP -delete)]'; echo ${s:x}"),
    ("Bash", "x='a[$''(find ~ -name ZexionAP -delete)]'; echo $[x]"),
    ("Bash", 'x="a[\\`find ~ -name ZexionAP -delete\\`]"; echo $[x]'),
    ("Bash", 'x="a[\\$(find ~ -name ZexionAP -delete)]"; echo $[x]'),
    ("PowerShell", "Remove-Item ~\\* -Recurse -Include x, ZexionAP"),
    ("PowerShell", "Get-Item ~\\* -Include 'x', 'ZexionAP' | Remove-Item -Recurse"),
    ("PowerShell", "Move-Item ~\\* -Include x, ZexionAP C:\\tmp\\x"),
    ("PowerShell", "gci ~ -r -Include @('x', 'ZexionAP') | ri -r"),
    ("PowerShell", "gci ~ -r -Include ('x', 'ZexionAP') | ri -r"),
    ("PowerShell", "gci ~ -r -Include:@('x','ZexionAP') | ri -r"),
    ("PowerShell", "gci ~ -r -Include x,@('ZexionAP') | ri -r"),
    ("PowerShell", "gci ~ -r -Include x,`\nZexionAP | ri -r"),
    ("PowerShell", "gci ~ ('ZexionAP') -r | ri -r"),
    ("PowerShell", 'pwsh -c "Remove-Item ~\\* -Include x, ZexionAP"'),
]
BY_NAME_ALLOWED = [
    # Looking: nothing here can write.
    ("Bash", "find ~ -name ZexionAP"),
    ("Bash", "find ~ -name ZexionAP -print"),
    ("Bash", "find ~ -iname '*zexionap*' -print"),
    ("Bash", 'find ~ -name "Gallant Leaf" -ls'),
    ("Bash", "find / -name 3392510 -type d -print"),
    ("Bash", "find ~ -path '*/ZexionAP/*' -print"),
    ("Bash", "find ~ -regex '.*/ZexionAP' -ls"),
    ("Bash", "find ~ -name ZexionAP -type d -printf '%p\\n'"),
    ("Bash", "find ~ -name ZexionAP -prune -o -name x -print"),
    ("Bash", "find ~ -name ZexionAP | wc -l"),
    ("Bash", "find ~ -name ZexionAP | head"),
    ("Bash", "find / -name userdata 2>/dev/null"),
    ("PowerShell", "Get-ChildItem -Recurse -Filter ZexionAP | Select-Object FullName"),
    # Other names, wherever they are deleted: this repo's own work.
    ("Bash", "find . -name '*.pyc' -delete"),
    ("Bash", "find . -name __pycache__ -exec rm -r {} +"),
    ("Bash", "find . -name 'zexion*.py' -delete"),
    ("Bash", "find . -name 'zx*.py' -delete"),
    ("Bash", "find . -iname '*.log' -exec rm {} +"),
    ("Bash", "find . -path './build/*' -delete"),
    ("Bash", "find . -regex '.*\\.py[co]' -delete"),
    ("Bash", "find ~/AppData/Local/Temp -path '*/AppData/Local/Temp/x' -delete"),
    ("Bash", "find . -name 'test*' -exec ls {} \\;"),
    ("PowerShell", "Get-ChildItem -Recurse -Filter '*.pyc' | Remove-Item"),
    ("PowerShell", 'Set-Content -Path "$env:LOCALAPPDATA\\Temp\\x" -Value 1'),
    # The world's own folder is named like the game's: by itself ``zexion`` is no name (#178).
    ("Bash", "find . -path './worlds/zexion/*' -name '*.pyc' -delete"),
    ("Bash", "find . -type d -name zexion -prune -o -name '*.tmp' -delete"),
    ("Bash", "find .ap -path '*/worlds/zexion' -prune -o -name '*.pyc' -delete"),
    ("Bash", "find . -name zexion -exec ls -d {} \\;"),
    ("PowerShell", "gci . ZexionNot -r | Remove-Item"),
    ("PowerShell", "gci . '*.pyc' -r | Remove-Item"),
    ("PowerShell", "Get-ChildItem -Recurse -Filter:'*.pyc' | Remove-Item"),
    ("Bash", "find . -regex '.*/[^/]*\\.pyc' -delete"),
    ("Bash", "find . -name '[0-9].tmp' -delete"),
    # Round 3: the repo's lower-case names, which case-comparing predicates can't match.
    ("Bash", "find dist -name 'zexion*' -delete"),
    ("Bash", "find lab -name 'zexion*' -delete"),
    ("Bash", "find . -path '*zexion*.pyc' -delete"),
    ("Bash", "find . -regex '.*zexion.*\\.pyc' -delete"),
    ("Bash", "find . -path '*zexion*' -name '*.pyc' -delete"),
    ("Bash", "find . -path './worlds/zexion*' -name '*.pyc' -delete"),
    ("Bash", "find worlds -name zexion -type d -exec rm -rf {} +"),
    ("PowerShell", "gci . '*.pyc',*.pyo -r | Remove-Item"),
    ("PowerShell", "gci worlds\\zexion -Filter *.pyc | Remove-Item"),
    ("PowerShell", "Get-ChildItem -Recurse -Include *.pyc,*.pyo | Remove-Item"),
    # A substitution that only reads, in a command that only reads.
    ("Bash", "ls -ld $(find ~ -name ZexionAP)"),
    ("Bash", "ls -ld `find ~ -name ZexionAP`"),
    ("Bash", 'du -sh "$(find ~ -name ZexionAP -print)"'),
    # Round 4: reads only, around a substitution that reads.
    ("Bash", 'cd "$(find ~ -name ZexionAP)" && ls'),
    ("Bash", "cat $(find ~ -name ZexionAP -type f)"),
    ("PowerShell", "Get-Item .\\* -Include '*.pyc', '*.pyo' | Remove-Item"),
    ("PowerShell", "Remove-Item ~\\lab\\* -Recurse -Include x, y"),
    ("PowerShell", "gci . ('*.pyc') -r | Remove-Item"),
    # The folders' names are text in a message, not a command.
    ("Bash", 'git commit -m "find ~ -name ZexionAP -delete"'),
    ("Bash", 'gh pr create --title "find -name ZexionAP -delete" --body "x"'),
    ("Bash", "grep -rn 'find -name ZexionAP -delete' docs"),
]


@pytest.mark.parametrize(("tool", "command"), BY_NAME_BLOCKED)
def test_a_deletion_by_a_protected_folders_bare_name_blocked(tool: str, command: str) -> None:
    assert run(tool, command=command), command


@pytest.mark.parametrize(("tool", "command"), BY_NAME_ALLOWED)
def test_reads_by_a_protected_folders_bare_name_and_other_names_allowed(
    tool: str, command: str
) -> None:
    assert run(tool, command=command) is None, command


@pytest.mark.parametrize(
    ("pattern", "kind", "expected"),
    [
        ("ZexionAP", "name", True),
        ("ZexionAP", "name", True),
        ("Zexion", "name", True),
        ("3392510", "name", True),
        ("Gallant Leaf", "name", True),
        ("Gallant\\ Leaf", "name", True),
        ("steamapps", "name", True),
        ("userdata", "name", True),
        ("Zexion*", "name", True),
        ("*ZexionAP*", "name", True),
        ("Z*AP", "name", True),
        ("Zexion[AB]P", "name", True),
        ("[Zz]exionAP", "name", True),
        ("[Z][e][x][i][o][n][A][P]", "name", True),
        ("[[:upper:]]exionAP", "name", True),
        ("Zexi\\onAP", "name", True),
        ("GALLAN~1", "name", True),
        ("*/ZexionAP/*", "path", True),
        ("*/Gallant Leaf/*", "path", True),
        ("*/steamapps/common/Zexion", "path", True),
        ("*Local*ZexionAP*", "path", True),
        (".*/ZexionAP$", "regex", True),
        (".*/(a|ZexionAP)", "regex", True),
        (".*/\\(a\\|ZexionAP\\)", "regex", True),
        (".*/Zexion.P", "regex", True),
        (".*/[^/]*nAP", "regex", True),
        (".*Local.ZexionAP", "regex", True),
        (".*/Gallant\\sLeaf", "regex", True),
        # Too little of a name, or another name. ``Zexion`` alone is the world's own folder too.
        ("zexion", "path", False),
        ("zexion*", "name", False),
        ("*zexionap*", "name", False),
        ("*zexion*.pyc", "path", False),
        (".*zexion.*\\.pyc", "regex", False),
        ("*zexion", "name", False),
        ("*P", "name", False),
        ("*.py", "name", False),
        ("zexion*.py", "name", False),
        ("zexion_client.py", "name", False),
        ("ZexionAP-notes", "name", False),
        ("common", "name", False),
        ("*/AppData/Local/Temp/x", "path", False),
        ("./build/*", "path", False),
        ("./worlds/zexion/*", "path", False),
        (r".*/Zexion\.AP", "regex", False),
        (r".*\.py[co]", "regex", False),
        ("", "name", False),
    ],
)
def test_pattern_may_name_protected(pattern: str, kind: str, expected: bool) -> None:
    assert protected.pattern_may_name_protected(pattern, kind) is expected


@pytest.mark.parametrize(
    ("pattern", "kind", "ignore_case", "expected"),
    [
        ("zexion", "name", False, False),  # worlds/zexion
        ("zexion", "name", True, True),  # -iname: any case
        ("zexion*", "name", False, False),
        ("zexion*", "name", True, True),
        ("zexionap", "name", False, False),
        ("zexionap", "name", True, True),
        ("Zexion", "name", False, True),  # the install and the saves
        ("zexion", "ps", False, True),  # PowerShell ignores case, whatever is asked
        ("*/zexion", "path", True, True),
        (".*/zexion", "regex", True, True),
        ("worlds/zexion", "path", False, False),
        ("Gallant Leaf", "name", False, True),  # no other name is case-compared
        ("gallant leaf", "name", False, True),
        ("GALLAN~1", "name", False, True),
    ],
)
def test_the_repos_own_names_are_matched_by_case(
    pattern: str, kind: str, ignore_case: bool, expected: bool
) -> None:
    assert protected.pattern_may_name_protected(pattern, kind, ignore_case) is expected


@pytest.mark.parametrize("kind", ["name", "path", "regex", "ps"])
@pytest.mark.parametrize("piece", ["[[:a:", "[", "[[", "[a", "*[", "\\(", "(["])
def test_a_long_pattern_is_read_quickly(kind: str, piece: str) -> None:
    started = time.monotonic()
    for length in (200, 2000, 10000):
        protected.pattern_may_name_protected(piece * (length // len(piece)) + "x", kind)
    assert time.monotonic() - started < 2


def test_a_long_pattern_in_a_command_is_read_quickly() -> None:
    started = time.monotonic()
    run("PowerShell", command="gci ~ -Include " + "a," * 25000 + "x | ri")
    run("PowerShell", command="gci ~ " + "a " * 2500 + "| ri")
    run("PowerShell", command="Remove-Item x -Include " + "('a')," * 800 + "x")
    run("Bash", command="find ~ -name '" + "[[:a:" * 2000 + "x' -delete")
    run("PowerShell", command="gci ~ -Filter '" + "[[:a:" * 2000 + "x' | ri")
    assert time.monotonic() - started < 2


TERMINAL = "mcp__terminal__run_in_terminal"
SLOTS_PS = f'.venv\\Scripts\\python.exe tools\\save_lab.py slots --save-dir "{SAVES}"'

# A redirect's target is read as an argument is: a substitution in it that finds a protected
# folder by its name blocks, whatever writes (a redirect, tee, Out-File, Set-Content).
FIND_OFFLINE = "$(find ~ -name ZexionAP)"
GCI_OFFLINE = "$(gci ~ -r -Filter ZexionAP)"
JOIN_OFFLINE = "(Join-Path (gci ~ -r -Filter ZexionAP).FullName seeds.json)"

REDIRECT_BY_SUBSTITUTION_BLOCKED = [
    ("Bash", f'echo x > "{FIND_OFFLINE}/seeds.json"'),
    ("Bash", f'echo x >> "{FIND_OFFLINE}/seeds.json"'),
    ("Bash", f'echo x 2> "{FIND_OFFLINE}/seeds.json"'),
    ("Bash", f'echo x &> "{FIND_OFFLINE}/seeds.json"'),
    ("Bash", f'echo x >| "{FIND_OFFLINE}/seeds.json"'),
    ("Bash", f'echo x >& "{FIND_OFFLINE}/seeds.json"'),
    ("Bash", f'echo x 2>&1 > "{FIND_OFFLINE}/seeds.json"'),
    ("Bash", f"echo x > {FIND_OFFLINE}/seeds.json"),
    ("Bash", 'echo x > "`find ~ -name ZexionAP`/seeds.json"'),
    ("Bash", "echo x >> `find ~ -name ZexionAP`/seeds.json"),
    ("Bash", 'echo x > "${f:-$(find ~ -name ZexionAP)}/seeds.json"'),
    ("Bash", """{ echo x; } > "$(find ~ -iname '*zexionap*')/seeds.json\""""),
    ("Bash", f'(echo x) > "{FIND_OFFLINE}/seeds.json"'),
    ("Bash", f'exec 3> "{FIND_OFFLINE}/seeds.json"'),
    ("Bash", f'cd lab && echo y > "{FIND_OFFLINE}/seeds.json"'),
    ("Bash", """echo x > "$(sh -c 'find ~ -name ZexionAP')/seeds.json\""""),
    ("Bash", """echo x > "$(find ~ -path '*/ZexionAP')/seeds.json\""""),
    ("Bash", """echo x > "$(find ~ -regex '.*/ZexionAP')/seeds.json\""""),
    ("Bash", 'echo x > "$(find ~ -name Zexion)/Save Game 1.sav"'),
    ("Bash", """cat a > "$(find ~ -name 'Gallant Leaf')/Zexion/Save Game 1.sav\""""),
    ("Bash", f'echo x | tee "{FIND_OFFLINE}/seeds.json"'),
    ("Bash", 'echo x | tee -a "`find ~ -name ZexionAP`/seeds.json"'),
    ("Monitor", f'echo x > "{FIND_OFFLINE}/seeds.json"'),
    ("PowerShell", f'"x" > "{GCI_OFFLINE}\\seeds.json"'),
    ("PowerShell", f'"x" >> "{GCI_OFFLINE}\\seeds.json"'),
    ("PowerShell", f'"x" *> "{GCI_OFFLINE}\\seeds.json"'),
    ("PowerShell", f'"x" 2> "{GCI_OFFLINE}\\seeds.json"'),
    ("PowerShell", f'"x" > {JOIN_OFFLINE}'),
    ("PowerShell", '"x" >> (Get-ChildItem ~ -Recurse -Filter ZexionAP).FullName'),
    ("PowerShell", '"x" > (gci ~ -r -Filter ZexionAP | select -f 1).FullName'),
    ("PowerShell", '"x" > $(gci ~ -r -Filter ZexionAP).FullName'),
    (
        "PowerShell",
        """"x" > (Join-Path (gci ~ -r -Filter 'Gallant Leaf').FullName Zexion\\x.sav)""",
    ),
    ("PowerShell", f'"x" | Out-File -Path "{GCI_OFFLINE}\\seeds.json"'),
    ("PowerShell", f'"x" | Out-File {JOIN_OFFLINE}'),
    ("PowerShell", f'"x" | Out-File -Append -FilePath {JOIN_OFFLINE}'),
    ("PowerShell", f'Set-Content -Path "{GCI_OFFLINE}\\seeds.json" -Value x'),
    ("PowerShell", f"Set-Content -LiteralPath {JOIN_OFFLINE} -Value x"),
    ("PowerShell", f"Add-Content -Path {JOIN_OFFLINE} -Value x"),
    (TERMINAL, f'"x" > {JOIN_OFFLINE}'),
]
# The same forms where no protected folder is named stay as they were; so does reading what a
# substitution found (``ls $(find ~ -name ZexionAP)``).
REDIRECT_BY_SUBSTITUTION_ALLOWED = [
    ("Bash", 'echo x > "$(mktemp)"'),
    ("Bash", 'echo x > "$(pwd)/out.txt"'),
    ("Bash", 'echo "$(date)" > lab/x.txt'),
    ("Bash", 'git log > "$(mktemp)"'),
    ("Bash", 'echo x | tee "$(mktemp)"'),
    ("Bash", f"ls {FIND_OFFLINE}"),
    ("Bash", f'ls -ld "{FIND_OFFLINE}"'),
    ("PowerShell", '"x" > (Join-Path $PWD out.txt)'),
    ("PowerShell", '"x" > "$(Get-Date -f yyyy).txt"'),
    ("PowerShell", '"x" | Out-File -Path (Join-Path $PWD out.txt)'),
    ("PowerShell", "Set-Content -Path (Join-Path $PWD out.txt) -Value x"),
    ("PowerShell", '"x" > out.txt'),
]


@pytest.mark.parametrize(("tool", "command"), REDIRECT_BY_SUBSTITUTION_BLOCKED)
def test_a_redirect_target_built_by_a_substitution_is_read(tool: str, command: str) -> None:
    assert run(tool, command=command), command


@pytest.mark.parametrize(("tool", "command"), REDIRECT_BY_SUBSTITUTION_ALLOWED)
def test_a_redirect_target_built_by_a_substitution_elsewhere_allowed(
    tool: str, command: str
) -> None:
    assert run(tool, command=command) is None, command


def test_a_redirect_into_a_parenthesis_is_one_target() -> None:
    _, targets = guard_protected.lex('"x" > (Join-Path $PWD out.txt)', True)
    assert targets == ["("]
    _, targets = guard_protected.lex('"x" > out.txt (1)', True)
    assert targets == ["out.txt"]
    _, targets = guard_protected.lex('"x" > "out (1).txt"', True)
    assert targets == ["out (1).txt"]
    assert "(" not in targets[0].bare  # a name with a parenthesis in quotes is no expression


@pytest.mark.parametrize(
    ("tool", "command"),
    [
        # A command that reaches a protected folder by name only: its target is a name.
        ("Bash", 'find ~ -name ZexionAP -print > "out (1).txt"'),
        ("PowerShell", 'gci ~ -r -Filter ZexionAP > "out (1).txt"'),
        ("Bash", "find ~ -name ZexionAP -print > 'out (1).txt'"),
    ],
)
def test_a_parenthesis_in_a_quoted_redirect_target_is_a_name(tool: str, command: str) -> None:
    assert run(tool, command=command) is None, command


@pytest.mark.parametrize(
    ("tool", "command"),
    [
        # Not into a protected folder, but the target is worked out when the command runs, in a
        # command that names one: blocked all the same.
        ("Bash", "ls ~/AppData/Local/ZexionAP > >(cat)"),
        ("PowerShell", "gci $env:LOCALAPPDATA\\ZexionAP > (Join-Path $env:TEMP x.txt)"),
    ],
)
def test_a_computed_redirect_target_beside_a_protected_name_is_blocked(
    tool: str, command: str
) -> None:
    assert run(tool, command=command), command


# A hook that times out lets the call through, so the guard answers in about a second whatever
# the command: past a budget (``MAX_*``) it does not read word by word but takes the command to
# reach a protected folder, and then only a read-only one passes.
SLOW = 4  # seconds; the guard needs a fraction of one, a CI machine more
Z_AP = "Zexion" + "AP"  # the name the hostile commands below are built around
CLASSES = "[" * 988  # a run of ``[``: every one is tried as the start of a bracket expression


@pytest.fixture(autouse=True)
def _generous_deadline(monkeypatch) -> None:
    """The guard's own deadline (``DEADLINE``, one second) is the subject of its own tests; the
    others must not fail on a slow machine."""
    monkeypatch.setattr(guard_protected, "DEADLINE", 60.0)


HOSTILE = {
    "a heredoc saved, copied 2000 times, then a write": (
        "Bash",
        "cat <<E > f.sh\nx\nE\n" * 4
        + "cp f.sh g0.sh; "
        + "".join(f"cp g{i}.sh g{i + 1}.sh; " for i in range(2000))
        + f"rm -rf ~/AppData/Local/{Z_AP}",
    ),
    "regex name patterns": (
        "Bash",
        "find ~ " + " -o ".join(f"-regex '{CLASSES}q{i}'" for i in range(96)) + " -delete",
    ),
    "regex name patterns, then a name": (
        "Bash",
        "find ~ "
        + " -o ".join(f"-regex '{CLASSES}q{i}'" for i in range(96))
        + f" -o -name {Z_AP} -delete",
    ),
    "name patterns in sh -c": (
        "Bash",
        "sh -c 'find ~ " + " -o ".join(f'-name "{CLASSES}q{i}"' for i in range(96)) + "'",
    ),
    "PowerShell filters": (
        "PowerShell",
        "".join(f"gci ~ -r -Filter '{CLASSES}q{i}' | Out-Null; " for i in range(90)) + "gci | ri",
    ),
    "name patterns": ("Bash", "find ~ " + " -o ".join(f"-name '{CLASSES}q{i}'" for i in range(98))),
    "dollar-paren run": ("Bash", "$(" * 25000),
    "dollar-paren and a name": ("Bash", "$(" * 25000 + f" -name {Z_AP}"),
    "doubled quotes and parentheses": ("Bash", '"$(' * 16000),
    "many redirects": ("Bash", "echo x > f;" * 4000),
    "different redirects": ("Bash", "".join(f"echo x > f{i};" for i in range(3000))),
    "many cds": ("Bash", "cd a;" * 10000),
    "different cds": ("Bash", "".join(f"cd d{i};" for i in range(4000))),
    "many heredoc operators": ("Bash", "cat <<A\nx\nA\n" * 4000),
    "many heredocs saved and run": ("Bash", "cat <<E > f.sh\nx\nE\nsh f.sh\n" * 1500),
    "many words": ("Bash", "echo a " * 14000),
    "different words": ("Bash", "echo " + " ".join(f"w{i}" for i in range(9000))),
    "different wildcards": ("Bash", "ls " + " ".join(f"a*b{i}?" for i in range(6000))),
    "different relative paths": ("Bash", "ls " + " ".join(f"a{i}/../b" for i in range(7000))),
    "one long wildcard word": ("Bash", "ls " + "q1" * 40000 + "*"),
    "one long word with short-name tildes": ("Bash", "ls " + "find~" * 15000),
    "network paths": ("Bash", "ls " + " ".join(f"//host{i}/share" for i in range(5000))),
    "network cds": ("Bash", "".join(f"cd //host{i}/x;" for i in range(5000))),
    "many names": ("Bash", "find . -name n -o " * 5000),
    "different names": ("PowerShell", "".join(f"gci . -Filter f{i};" for i in range(4000))),
    "many positional filters": ("PowerShell", "gci . a; " * 11000),
    "a list of filters": ("PowerShell", "gci -Filter " + ",".join(f"f{i}" for i in range(14000))),
    "many sub-expressions": ("PowerShell", "$(" * 45000),
    "many methods": ("PowerShell", "$a.ToString();" * 7000),
    "many here-strings": ("PowerShell", "@'\nx\n'@ " * 12000),
    "many pipes": ("Bash", "a|" * 45000),
    "many escapes": ("Bash", "\\" * 99000),
    "many backticks": ("Bash", "`" * 99000),
    "a name repeated": ("Bash", "echo gallant " * 7000),
    "the app id repeated": ("Bash", "echo 3392510 " * 7000),
}


@pytest.mark.parametrize("name", HOSTILE)
def test_a_hostile_command_is_read_in_about_a_second(name: str) -> None:
    tool, command = HOSTILE[name]
    assert len(command) <= guard_protected.MAX_COMMAND
    started = time.monotonic()
    run(tool, command=command)
    assert time.monotonic() - started < SLOW


def test_a_command_over_the_length_limit_is_blocked_unread() -> None:
    assert run("Bash", command="echo " + "a" * guard_protected.MAX_COMMAND)
    assert run("PowerShell", command="echo " + "a" * guard_protected.MAX_COMMAND)
    assert run("Bash", command="echo " + "a" * (guard_protected.MAX_COMMAND - 10)) is None


def test_a_command_over_the_work_budget_is_read_as_reaching_a_protected_folder() -> None:
    many = " ".join(f"w{i}" for i in range(guard_protected.MAX_WORK + 1))
    assert run("Bash", command=f"ls {many}") is None  # only reads
    assert run("Bash", command=f"echo {many} | sort") is None
    assert run("Bash", command=f"rm {many}")
    assert run("Bash", command=f"git add {many}")  # not trusted as the repo's own
    assert run("Bash", command=f"echo x > f; ls {many}")  # a redirect
    assert run("Bash", command=f"ls {many} $(date)")  # a substitution
    assert run("PowerShell", command=f"gci {many} | ri")
    assert run("PowerShell", command=f"gci {many}") is None
    assert run("Bash", command=f"rm {many[:2000]}") is None  # under it: read as before


def test_a_command_with_too_many_folders_or_commands_is_read_as_reaching_one() -> None:
    cds = "".join(f"cd d{i}; " for i in range(guard_protected.MAX_FOLDERS + 1))
    assert run("Bash", command=cds + "ls") is None
    assert run("Bash", command=cds + "rm x")
    assert run("Bash", command="cd d; " * 100 + "ls") is None  # one folder again and again
    assert run("Bash", command="ls;" * (guard_protected.MAX_SEGMENTS + 1) + "rm x")


def test_many_heredocs_are_not_cut_out_as_data() -> None:
    n = guard_protected.MAX_HEREDOCS
    one = "cat <<'EOF'\nsee Gallant Leaf/Zexion\nEOF\n"
    assert run("Bash", command=one) is None
    assert run("Bash", command=one * n) is None
    assert run("Bash", command=one * (n + 1))  # the bodies are code now: ``see`` is no read


@pytest.mark.parametrize(
    "command",
    [
        # The text of a message may name the folders, however long.
        "git commit -F - <<'EOF'\n"
        + "Fix the save in Gallant Leaf/Zexion [a] * ?\n" * 2000
        + "EOF",
        "git commit -m \"$(cat <<'EOF'\n"
        + "Fix the save in Gallant Leaf/Zexion.\n" * 2000
        + 'EOF\n)"',
        "gh pr create --title t --body-file - <<'EOF'\n"
        + "ZexionAP and Gallant Leaf\n" * 3000
        + "EOF",
        'gh pr create --title t --body "' + "ZexionAP, Gallant Leaf/Zexion, [a] * ?. " * 1500 + '"',
        # A long script that names no folder.
        "python - <<'EOF'\n"
        + "x = a[0] + b['k'] * 2  # fix {y} ?\nfor i in range(9): print(i)\n" * 400
        + "EOF",
        "git add " + " ".join(f"worlds/zexion/file{i}.py" for i in range(1500)),
        "ls " + " ".join(f"tools/f{i}.py" for i in range(1500)),
    ],
    ids=["commit -F", "commit -m", "gh body-file", "gh body", "script", "git add", "ls"],
)
def test_normal_long_commands_still_pass(command: str) -> None:
    started = time.monotonic()
    assert run("Bash", command=command) is None
    assert time.monotonic() - started < SLOW


def test_what_is_a_network_path(monkeypatch) -> None:
    monkeypatch.setattr(protected, "_on_windows", lambda: True)
    for path in ("//host/share", "\\\\host\\share", "\\\\host\\share\\x", "//?/UNC/host/share"):
        assert protected.is_network_path(path), path
    assert protected.is_network_path("\\\\?\\UNC\\host\\share\\x")
    # A local path with the extended-length or device prefix is not one, nor are the rest.
    for path in (
        "//?/C:/x",
        "\\\\?\\C:\\x",
        "//./C:/x",
        "\\\\.\\C:\\x",
        "/host/share",
        "C:/host/x",
    ):
        assert not protected.is_network_path(path), path
    monkeypatch.setattr(protected, "_on_windows", lambda: False)  # Linux and macOS: never
    assert not protected.is_network_path("//host/share")


def test_nothing_asks_the_network_about_a_network_path(monkeypatch) -> None:
    # Windows resolves ``//host/share`` over the network, seconds for each host.
    monkeypatch.setattr(protected, "_on_windows", lambda: True)
    real = os.path.realpath
    asked: list[str] = []

    def realpath(path, *args, **kwargs):
        if str(path).replace("\\", "/").startswith("//"):
            asked.append(str(path))
        return real(path, *args, **kwargs)

    monkeypatch.setattr(os.path, "realpath", realpath)
    assert not protected.is_protected_path("//host/share/x")
    run("Bash", command="ls //host1/a > //host2/b; cd //host3/c && ls")
    run("PowerShell", command="gci \\\\host4\\a | Out-File \\\\host5\\b")
    assert not asked


LINK = "X:/lab/lnk"  # a link to the offline folder, simulated: nothing is created
LOCAL_PREFIXES = ["//?/", "\\\\?\\", "//./", "\\\\.\\"]


@pytest.fixture
def linked(monkeypatch, local_appdata):
    """``X:/lab/lnk`` resolves to the offline folder, and ``X:/Steam/steamapps`` and
    ``…/userdata`` exist, whichever way a path to them is written."""
    real_path, real_isdir = os.path.realpath, os.path.isdir

    def plain(path) -> str:
        text = str(path).replace("\\", "/")
        text = text[4:] if text.startswith(("//?/", "//./")) else text
        return text.lower()  # the guard folds case before it asks

    def realpath(path, *args, **kwargs):
        text = plain(path)
        if LINK.lower() in text:  # also where the guard has folded the prefix's slashes
            return OFFLINE + text.split(LINK.lower(), 1)[1]
        return real_path(path, *args, **kwargs)

    def isdir(path):
        return plain(path).startswith("x:/steam/") or real_isdir(path)

    monkeypatch.setattr(os.path, "realpath", realpath)
    monkeypatch.setattr(os.path, "isdir", isdir)


LINKED_BLOCKED = [
    ("Write", {"file_path": "\\\\?\\X:\\lab\\lnk\\seeds.json"}),
    ("Write", {"file_path": "//?/X:/lab/lnk/seeds.json"}),
    ("Write", {"file_path": "\\\\.\\X:\\lab\\lnk\\seeds.json"}),
    ("Write", {"file_path": "X:/lab/lnk/seeds.json"}),
    ("Bash", {"command": "echo x > '//?/X:/lab/lnk/seeds.json'"}),
    ("Bash", {"command": "echo x > '//./X:/lab/lnk/seeds.json'"}),
    ("Bash", {"command": "rm -r '//?/X:/lab/lnk'"}),
    ("Bash", {"command": "cp a '//?/X:/lab/lnk/seeds.json'"}),
    ("PowerShell", {"command": '"x" > \\\\?\\X:\\lab\\lnk\\seeds.json'}),
    ("PowerShell", {"command": "Remove-Item -Recurse \\\\?\\X:\\lab\\lnk"}),
    ("Bash", {"command": "cd '//?/X:/Steam' && rm -rf steamapps"}),
    ("Bash", {"command": "cd '//?/X:/Steam' && rm -rf userdata"}),
    pytest.param(
        "PowerShell",
        {"command": "Set-Location \\\\?\\X:\\Steam; Remove-Item -Recurse steamapps"},
        marks=pytest.mark.skipif(os.name != "nt", reason="a backslash path is a Windows path"),
    ),
]


@pytest.mark.parametrize(
    ("tool", "tool_input"),
    [c if hasattr(c, "marks") else pytest.param(*c) for c in LINKED_BLOCKED],
)
@pytest.mark.parametrize("windows", [True, False])
def test_a_link_behind_a_local_device_path_is_still_followed(
    tool: str, tool_input: dict, windows: bool, linked, monkeypatch
) -> None:
    # On Windows the prefixes are local paths; on Linux and macOS ``//`` is never the network.
    monkeypatch.setattr(protected, "_on_windows", lambda: windows)
    assert run(tool, **tool_input), (tool, tool_input)


POSIX_HOME = "/home/u"
POSIX_DATA = f"{POSIX_HOME}/.local/share"
TWO_BACKSLASHES = "\\\\"  # a folder name on Linux and macOS, a separator on Windows


@pytest.fixture
def posix(monkeypatch):
    """Linux's paths and environment, on any machine: ``os.path`` is ``posixpath``, nothing is
    Windows, and the offline folder is under ``$XDG_DATA_HOME``."""
    monkeypatch.setattr(os, "path", posixpath)
    monkeypatch.setattr(protected, "_on_windows", lambda: False)
    for name in ("LOCALAPPDATA", "APPDATA", "USERPROFILE"):
        monkeypatch.delenv(name, raising=False)
    monkeypatch.setenv("HOME", POSIX_HOME)
    monkeypatch.setenv("XDG_DATA_HOME", POSIX_DATA)


POSIX_BLOCKED = [
    # ``\\`` is an ordinary folder there, so ``'\\'/../..`` is two levels up from the working
    # folder, not from the root (an absolute-looking path is read from ``/`` on Windows).
    (f"rm -rf '{TWO_BACKSLASHES}'/../../ZexionAP", f"{POSIX_DATA}/t"),
    (f"cp e.json '{TWO_BACKSLASHES}'/../../ZexionAP/s.json", f"{POSIX_DATA}/t"),
    (f"cd {POSIX_DATA}/t && rm -rf '{TWO_BACKSLASHES}'/../../ZexionAP", POSIX_HOME),
    (
        f"mkdir -p {POSIX_DATA}/t/'{TWO_BACKSLASHES}' && cd {POSIX_DATA}/t"
        f" && rm -rf '{TWO_BACKSLASHES}'/../../ZexionAP",
        POSIX_HOME,
    ),
    (
        f"cd ~/.steam/steam/steamapps/common/x && rm -rf '{TWO_BACKSLASHES}'/../../Zexion",
        POSIX_HOME,
    ),
    (f"rm -rf '{TWO_BACKSLASHES}'/../../Zexion", f"{POSIX_HOME}/.steam/steam/steamapps/common/x"),
    ("rm -rf ../ZexionAP", f"{POSIX_DATA}/t"),
    ("rm -rf ../../share/ZexionAP", f"{POSIX_DATA}/t"),
]


@pytest.mark.parametrize(("command", "cwd"), POSIX_BLOCKED)
def test_on_linux_a_backslash_folder_name_is_no_root(command: str, cwd: str, posix) -> None:
    assert run("Bash", command=command, cwd=cwd), (command, cwd)


def test_a_slow_command_is_blocked_at_the_deadline(monkeypatch) -> None:
    command = "find lab " + " -o ".join(f"-name 'q{i}'" for i in range(200)) + " -delete"
    assert run("Bash", command=command) is None  # read in full: no protected name
    monkeypatch.setattr(guard_protected, "DEADLINE", 0.0)
    reason = run("Bash", command=command)
    assert reason and "reading it took over" in reason
    assert run("Bash", command="ls lab")  # even a read, when the time is gone
    assert run("PowerShell", command="gci lab | ri")


def test_the_clock_is_read_before_the_first_word_is_read_as_a_path(monkeypatch) -> None:
    # The lexer, the heredoc pass and each word's reading look at it, not only ``protected``.
    ticks = iter(range(100_000))
    monkeypatch.setattr(guard_protected.time, "monotonic", lambda: next(ticks) / 1000)
    monkeypatch.setattr(guard_protected, "DEADLINE", 0.5)  # 500 reads of the clock
    words = " ".join(f"w{i}" for i in range(2000))
    assert "reading it took over" in (run("Bash", command=f"ls {words}") or "")
    ticks = iter(range(100_000))
    assert run("Bash", command="ls lab tools") is None
    monkeypatch.setattr(guard_protected, "_due", 0.0)  # time's up before the pass starts
    for stage in (
        lambda: guard_protected._cut_heredocs("cat <<E > f.sh\nx\nE\nwc f.sh\n"),
        lambda: guard_protected.lex("ls a b", False),
    ):
        with pytest.raises(guard_protected._OutOfTime):
            stage()


def _copy_chain(n: int, heredocs: int, tail: str, body: str = "x") -> str:
    saved = f"cat <<E > f.sh\n{body}\nE\n" * heredocs
    chain = "cp f.sh g0.sh; " + "".join(f"cp g{i}.sh g{i + 1}.sh; " for i in range(n))
    return saved + chain + tail


RM_OFFLINE = "rm -rf ~/AppData/Local/ZexionAP"


@pytest.mark.parametrize(("n", "heredocs"), [(100, 1), (1000, 1), (2000, 4), (1000, 8)])
def test_a_chain_of_copies_of_a_saved_heredoc_is_read_quickly(n: int, heredocs: int) -> None:
    # Each copy used to rebuild a pattern of every name so far, before any clock was read.
    for command in (
        _copy_chain(n, heredocs, RM_OFFLINE),  # a write after the chain
        _copy_chain(n, heredocs, "wc g0.sh", body=RM_OFFLINE),  # a body that is a write
    ):
        started = time.monotonic()
        assert run("Bash", command=command)
        assert time.monotonic() - started < SLOW


def test_a_heredoc_copied_to_a_few_files_is_followed_as_before() -> None:
    body = RM_OFFLINE
    assert run("Bash", command=_copy_chain(3, 1, "sh g3.sh", body))  # run after the copies: code
    assert run("Bash", command=_copy_chain(3, 1, "wc g3.sh", body)) is None  # only read


def test_normal_long_commands_pass_under_the_real_deadline(monkeypatch) -> None:
    monkeypatch.setattr(guard_protected, "DEADLINE", 1.0)  # the one the hook runs with
    script = "x = a[0] + b['k'] * 2  # fix {y} ?\nfor i in range(9): print(i)\n" * 300
    for command in (
        "git commit -F - <<'EOF'\n" + "Fix the save in Gallant Leaf/Zexion [a] * ?\n" * 500 + "EOF",
        'gh pr create --title t --body "' + "ZexionAP, Gallant Leaf/Zexion, [a] * ?. " * 500 + '"',
        "python - <<'EOF'\n" + script + "EOF",
        "python -c '" + "{" + ", ".join(f'"k{i}": [{i}]' for i in range(1500)) + "}'",  # one word
    ):
        assert run("Bash", command=command) is None, command[:80]


def test_a_long_quoted_word_costs_the_budget_a_bounded_amount() -> None:
    word = "x[0]" * 22000  # 88,000 characters, one quoted word
    assert guard_protected._work(word) == guard_protected.MAX_NAME_TEXT // 8
    assert guard_protected._work("plain") == 1
    assert guard_protected._work("a*") == 8
    assert run("Bash", command=f"python -c '{word}'") is None


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
        (_crashing_payload({"file_path": f"{OFFLINE}/config-B1.txt"}), 2),
    ],
)
def test_the_hook_process_blocks_with_exit_2(payload: str, code: int) -> None:
    result = _hook(payload)
    assert result.returncode == code
    assert code == 0 or "guard_protected" in result.stderr


# Folders above the protected ones, and the names of the files in them (round 7). A command that
# starts from Roaming, the home folder or the root, and deletes or moves what it finds, reaches
# the saves whatever name it looks for.

FAKE_ENV = {
    "APPDATA": "C:\\Users\\someone\\AppData\\Roaming",
    "LOCALAPPDATA": "C:\\Users\\someone\\AppData\\Local",
    "USERPROFILE": "C:\\Users\\someone",
    "HOME": "C:/Users/someone",
}


@pytest.fixture(params=["set", "unset", "posix"])
def home_env(request, monkeypatch) -> str:
    """The three ways the environment tells the guard where Roaming and the home folder are: set
    as on Windows, not set at all (the variables are read as written), and Linux's."""
    if request.param == "posix":
        request.getfixturevalue("posix")
    for name, value in FAKE_ENV.items():
        if request.param == "set":
            monkeypatch.setenv(name, value)
        elif request.param == "unset":
            monkeypatch.delenv(name, raising=False)
    return request.param


ABOVE_BLOCKED = [
    # Allowed on v1.2.0 and develop before round 7.
    ("Bash", 'find "$APPDATA" -name "Save Game*.sav" -delete'),
    ("Bash", 'find ~ -name "ZexionAP-seed-*.sav" -exec rm {} +'),
    ("Bash", 'find "$APPDATA" -name "*seed*" -delete'),
    ("PowerShell", "gci $env:APPDATA -r -Filter ZexionAP-seed-* | ri"),
    ("Bash", 'find "$APPDATA" -delete'),
    ("Bash", 'rm -rf "$APPDATA"'),
    ("Bash", 'mv "$APPDATA" x'),
    ("Bash", "rm -rf ~"),
    # find with a write action from a folder above, whatever it looks for
    ("Bash", "find ~ -delete"),
    ("Bash", 'find "$HOME" -delete'),
    ("Bash", "find $USERPROFILE -delete"),
    ("Bash", "find %APPDATA% -delete"),
    ("Bash", 'find "$LOCALAPPDATA" -delete'),
    ("Bash", 'find "${APPDATA}" -ok rm {} \\;'),
    ("Bash", "find ~ -execdir rm {} +"),
    ("Bash", "find ~ -name x -fprint out.txt"),
    ("Bash", "find ~ -name '*.pyc' -delete"),
    ("Bash", "find ~ -name x -exec cat {} +"),  # any -exec counts as a write: a false positive
    ("Bash", "find -L ~ -delete"),
    ("Bash", 'find -H "$APPDATA" -type f -delete'),
    ("Bash", "find ~ -type f -print0 | xargs -0 rm"),
    ("Bash", "find ~ -name '*.sav' | xargs rm"),
    ("Bash", "find ~ -mindepth 1 -maxdepth 1 -exec mv {} /tmp/x \\;"),
    ("Bash", "find / -delete"),
    ("Bash", "find / -name token -delete"),
    ("Bash", "find C:/ -delete"),
    ("Bash", "find 'C:\\' -delete"),
    ("Bash", "find /c/Users -exec rm {} +"),
    ("Bash", "find /c/Users/someone -delete"),
    ("Bash", "find /mnt/c/Users/someone/AppData/Roaming -delete"),
    ("Bash", "find ~/.. -delete"),
    ("Bash", "find ~/.var -delete"),
    ("Bash", "find ~/AppData -delete"),
    ("Bash", "sh -c 'find ~ -delete'"),
    ("Bash", 'bash -c "find \\"$APPDATA\\" -delete"'),
    ("Bash", "eval 'find ~ -delete'"),
    ("Bash", "cd ~ && find . -delete"),
    ("Bash", 'cd "$APPDATA" && find . -delete'),
    ("Bash", 'cd "$APPDATA" && find -delete'),
    ("Bash", "cd ~ && rm -rf *"),
    ("Bash", "cd ~ && rm -rf .*"),
    ("Bash", "cd $LOCALAPPDATA && rm -rf ZexionAP"),
    ("Bash", "cd /c/Users/someone && rm -rf AppData"),
    ("Bash", "cd / && rm -rf *"),
    # the same in PowerShell
    ("PowerShell", "gci $env:APPDATA -r | ri"),
    ("PowerShell", "Get-ChildItem ~ -Recurse | Remove-Item -Force"),
    ("PowerShell", "Get-ChildItem $env:USERPROFILE -Recurse -Filter *seed* | Remove-Item"),
    ("PowerShell", "gci $env:LOCALAPPDATA -rec -Include ZexionAP-seed-* | Remove-Item -Force"),
    ("PowerShell", "Remove-Item -Recurse $env:APPDATA"),
    ("PowerShell", 'Remove-Item -Recurse -Force "$env:APPDATA"'),
    ("PowerShell", "Move-Item $env:APPDATA x"),
    ("PowerShell", "mi $env:APPDATA x"),
    ("PowerShell", "ri -r -fo ~"),
    ("PowerShell", "rm -r C:\\Users\\someone"),
    ("PowerShell", "Remove-Item C:\\ -Recurse"),
    ("PowerShell", "gci C:\\ -r -Include token | ri"),
    ("PowerShell", "gci ~ -r | where Name -like '*seed*' | ri"),
    ("PowerShell", "Get-ChildItem -Path $env:APPDATA -Recurse | Remove-Item -Recurse"),
    ("PowerShell", "Get-ChildItem -Path $env:APPDATA -Recurse | % { Remove-Item $_.FullName }"),
    ("PowerShell", "Copy-Item $env:APPDATA x -Recurse"),
    ("PowerShell", "Set-Location $env:APPDATA; Remove-Item * -Recurse"),
    ("PowerShell", "cd ~; ri * -r"),
    ("PowerShell", "Get-Item ~\\* -Include '*.pyc', '*.pyo' | Remove-Item"),
    # an ancestor deleted, moved or copied whole
    ("Bash", "rm -rf $HOME"),
    ("Bash", 'rm -rf "$HOME"'),
    ("Bash", "rm -rf ${HOME}"),
    ("Bash", "rm -rf /"),
    ("Bash", "rm -rf /*"),
    ("Bash", "rm -rf /c/Users/someone"),
    ("Bash", "rm -rf /c/Users"),
    ("Bash", "rm -rf /c"),
    ("Bash", "rm -rf /mnt/c"),
    ("Bash", "rm -rf /cygdrive/c/Users"),
    ("Bash", "rm -rf C:/"),
    ("Bash", "rm -rf 'C:\\Users'"),
    ("Bash", "rm -rf C:/Users/someone"),
    ("Bash", "rm -rf /home"),
    ("Bash", "rm -rf /home/u"),
    ("Bash", "rm -rf /Users/u"),
    ("Bash", "rm -rf ~/*"),
    ("Bash", "rm -rf ~/.*"),
    ("Bash", 'rm -rf "$APPDATA/*"'),
    ("Bash", 'rm -rf "$APPDATA"/*'),
    ("Bash", "rm -rf %APPDATA%\\.."),
    ("Bash", 'rm -rf "$APPDATA/.."'),
    ("Bash", 'rm -rf "$LOCALAPPDATA"'),
    ("Bash", 'rm -rf "$LOCALAPPDATA/.."'),
    ("Bash", 'rm -rf "$XDG_DATA_HOME"'),
    ("Bash", "rm -rf ~/.local"),
    ("Bash", "rm -rf ~/.local/share"),
    ("Bash", "rm -rf ~/.var"),
    ("Bash", "rm -rf ~/.var/app"),
    ("Bash", "rm -rf ~/.steam"),
    ("Bash", "rm -rf ~/.local/share/Steam"),
    ("Bash", 'rm -rf "C:/Program Files (x86)/Steam"'),
    ("Bash", 'rm -rf "C:/Program Files (x86)"'),
    ("Bash", "rm -rf C:/Users/someone/AppData/Roaming"),
    ("Bash", "rm -rf C:/Users/someone/AppData"),
    ("Bash", "rm -rf C:/Users/*/AppData/Roaming"),
    ("Bash", "cp -r ~ /tmp/x"),  # copying one out reads it, but is blocked with the rest
    ("Bash", 'cp -r "$APPDATA" x'),
    ("Bash", "mv ~ x"),
    ("Bash", 'mv "$HOME" x'),
    ("Bash", "rmdir ~"),
    ("Bash", "chmod -R 000 ~"),
    ("Bash", "tar czf x.tgz ~"),
    ("Bash", "rsync -a --delete x/ ~"),
    ("Bash", "git -C ~ clean -fdx"),
    ("Bash", "git clean -fdx ~"),
    ("Bash", "cmd /c rd /s /q %APPDATA%"),
    ("Bash", "cmd /c rd /s /q %USERPROFILE%"),
    ("Bash", "rm -rf --one-file-system ~"),
    ("Bash", "rm -rf -- ~"),
    ("Bash", "rm -rf --no-preserve-root /"),
    ("Bash", "sh -c 'rm -rf /'"),
    ("Bash", "find / -name x -delete"),
    ("Bash", "mv / /tmp/x"),
    ("PowerShell", "ri -r -fo /"),
    ("PowerShell", "Remove-Item -Recurse /c"),
    ("Bash", "rm -rf /c"),
    ("Bash", "cmd /c rd /s /q C:\\"),
    ("Bash", 'rm -r "$APPDATA" "$HOME"'),
    ("Bash", "python -c \"import shutil; shutil.rmtree('/c/Users/someone')\""),
    # names of the files in the protected folders, wherever the search starts
    ("Bash", "find lab -name 'Save Game*.sav' -delete"),
    ("Bash", "find . -name 'GameUnlocks.sav' -delete"),
    ("Bash", "find . -name 'ZexionAP-seed-*' -delete"),
    ("Bash", "find . -name 'ZexionAP-seed-*.sav' -exec rm {} +"),
    ("Bash", "find . -name 'Pref*' -delete"),
    ("Bash", "find . -name 'Preferences*' -delete"),
    ("Bash", "find . -name 'PreferencesLocal' -delete"),
    ("Bash", "find . -name 'steam_autocloud.vdf' -exec rm {} +"),
    ("Bash", "find . -name token -delete"),
    ("Bash", "find . -name seeds -exec rm -r {} +"),
    ("Bash", "find . -name 'config-*.txt' -delete"),
    ("Bash", "find . -name '*.sav' -delete"),
    ("Bash", "find lab -name '*.sav' -delete"),
    ("Bash", "find . -name '*.vdf' -delete"),
    ("Bash", "find . -name '*' -delete"),
    ("Bash", "find . -name '*.*' -delete"),
    ("Bash", "find . -name '?????' -delete"),
    ("Bash", "find . -iname '*seed*' -delete"),
    ("Bash", "find . -iname 'SAVE*' -delete"),
    ("Bash", "find . -iname 'zexionap-seed-*' -delete"),
    ("Bash", "find . -name 'Save?Game*' -delete"),
    ("Bash", "find . -name 'Sav[e]*' -delete"),
    ("Bash", "find . -name '{token,x}' -delete"),
    ("Bash", "find . -regex '.*\\.sav' -delete"),
    ("Bash", "find . -regex '.*/Save Game.*\\.sav' -delete"),
    ("Bash", "find . -path '*/Save Game*' -delete"),
    ("Bash", "find . -path './lab/*.sav' -delete"),
    ("Bash", "find . -wholename '*/token' -delete"),
    ("Bash", "find . -name 'Save*' -exec rm {} +"),
    ("Bash", "find . -name '*.sav' -o -name '*.tmp' | xargs rm"),
    ("Bash", "find lab -type f -name '*.sav' -exec shred {} +"),
    ("Bash", "find . -name 'sav*' -exec mv {} /tmp/x \\;"),
    ("PowerShell", "gci -r -Filter Save* | ri"),
    ("PowerShell", "gci . -Recurse -Filter 'ZexionAP-seed-*' | Remove-Item"),
    ("PowerShell", "Get-ChildItem -Recurse -Include *.sav | Remove-Item"),
    ("PowerShell", "gci -r *.sav | ri"),
    ("PowerShell", "gci -r -Filter * | ri"),
    ("PowerShell", "gci -r -Filter GameUnlocks.sav | ri"),
    ("PowerShell", "Get-ChildItem . -Recurse -Include token,seeds | Remove-Item -Recurse"),
]
ABOVE_ALLOWED = [
    # Reads from a folder above, and everyday work that names none.
    ("Bash", 'find "$APPDATA" -name x -print'),
    ("Bash", 'find "$APPDATA" -name "Save Game*.sav" -print'),
    ("Bash", 'find "$APPDATA" -name "*seed*" -ls'),
    ("Bash", 'find "$APPDATA" -maxdepth 2'),
    ("Bash", 'find ~ -name "*.sav" -print'),
    ("Bash", "find ~ -name token"),
    ("Bash", "find / -name token 2>/dev/null"),
    ("Bash", "find ~ -maxdepth 1 -type f | head"),
    ("Bash", "find . -name '*.sav' -print"),
    ("Bash", "find . -name '*' -type f | wc -l"),
    ("Bash", "find lab -name 'Save Game*.sav' | sort"),
    ("Bash", "ls -R ~"),
    ("Bash", "ls -la ~"),
    ("Bash", "ls ~"),
    ("Bash", 'ls "$APPDATA"'),
    ("Bash", 'ls "$APPDATA/*"'),
    ("Bash", "ls ~/*"),
    ("Bash", "ls /"),
    ("Bash", "ls /c/Users"),
    ("Bash", "ls C:/"),
    ("Bash", 'ls "$HOME"'),
    ("Bash", 'ls "$LOCALAPPDATA"'),
    ("Bash", "du -sh ~"),
    ("Bash", 'du -sh "$APPDATA"/*'),
    ("Bash", "df -h /"),
    ("Bash", "stat ~"),
    ("Bash", "tree -L 1 ~"),
    ("Bash", "cat ~/.bashrc"),
    ("Bash", "grep -rn foo ~/work"),
    ("Bash", "grep -r foo ~"),
    ("Bash", "rg foo ~"),
    ("Bash", "cd ~ && ls"),
    ("Bash", "cd ~ && cat .bashrc"),
    ("Bash", 'cd "$APPDATA" && ls'),
    ("Bash", "cd / && ls"),
    ("Bash", 'cd "$APPDATA" && find . -name x -print'),
    ("Bash", "wc -l ~/.bashrc"),
    ("Bash", "echo ~"),
    ("Bash", "echo $HOME"),
    ("Bash", 'echo "$APPDATA"'),
    ("Bash", 'git commit -m "docs: ~ and $APPDATA, rm -rf ~ is blocked"'),
    ("Bash", 'git commit -m "find ~ -delete"'),
    ("Bash", 'gh pr create --title "x" --body "find ~ -delete and rm -rf $APPDATA"'),
    ("Bash", "git commit -F - <<'EOF'\nGuard: rm -rf ~ and find \"$APPDATA\" -delete\nEOF"),
    ("Bash", "git diff HEAD~ -- tools"),
    ("Bash", "git log HEAD~3..HEAD"),
    ("Bash", "git reset --soft HEAD~1"),
    # A lone ``/`` or ``/c`` is a root only for a command that deletes, moves or copies.
    ("Bash", "cmd /c dir"),
    ("Bash", "cmd /c echo hi > lab/x.txt"),
    ("Bash", 'cmd /c "type lab\\x.txt"'),
    ("Bash", 'python -c "print(1 / 2)"'),
    ("Bash", "robocopy lab lab2 /MIR /E"),
    ("Bash", "findstr /s /i foo lab"),
    ("Bash", "xcopy lab lab2 /s /e"),
    ("Bash", "cmake --build native/build /m"),
    ("PowerShell", "cmd /c dir"),
    ("PowerShell", "robocopy lab lab2 /MIR /E"),
    ("PowerShell", "gci $env:APPDATA -r | select Name"),
    ("PowerShell", "gci $env:APPDATA -r -Filter *seed* | select -First 3"),
    ("PowerShell", "Get-ChildItem ~ -Recurse | Measure-Object"),
    ("PowerShell", "Get-ChildItem C:\\ | Sort-Object Name"),
    ("PowerShell", "ls ~"),
    ("PowerShell", "dir $env:APPDATA"),
    ("PowerShell", "gci -r -Filter *.sav | select Name"),
    ("PowerShell", "Get-Content ~/.bashrc"),
    ("PowerShell", "Set-Location ~"),
    ("PowerShell", "cd ~; ls"),
    # Work in the repo and in the project folders under the home folder.
    ("Bash", "rm -rf build"),
    ("Bash", "rm -rf ./build/*"),
    ("Bash", "rm lab/*.txt"),
    ("Bash", "rm -rf lab/work"),
    ("Bash", "rm -rf ~/work/x"),
    ("Bash", "rm ~/notes.txt"),
    ("Bash", "cp x ~/work/"),
    ("Bash", "cp x ~/.bashrc"),
    ("Bash", "mkdir -p ~/work"),
    ("Bash", "mkdir ~/work && cd ~/work && git clone https://example.com/x.git"),
    ("Bash", "echo x > ~/work/y"),
    ("Bash", "echo x > ~/.bashrc"),
    ("Bash", "touch ~/x"),
    ("Bash", "cd ~/work && git pull"),
    ("Bash", "cd ~/work && rm -rf build"),
    ("Bash", "rm -rf /tmp/x"),
    ("Bash", "rm -rf /tmp/*"),
    ("Bash", 'rm -rf "$TEMP/foo"'),
    ("Bash", "rm -rf /var/tmp/x"),
    ("Bash", "find . -name '*.pyc' -delete"),
    ("Bash", "find . -name '__pycache__' -exec rm -rf {} +"),
    ("Bash", "find . -name '*.o' -delete"),
    ("Bash", "find . -name '*.py' -exec wc -l {} +"),
    ("Bash", "find . -name '*.md' -exec sed -i s/a/b/ {} +"),
    ("Bash", "find build -name '*.tmp' -delete"),
    ("Bash", "find . -name '*~' -delete"),
    ("Bash", "find . -name '.DS_Store' -delete"),
    ("Bash", "find . -name '*.log' -delete"),
    ("Bash", "find . -name '*.json' -exec cat {} +"),
    ("Bash", "find ~/work -name x -delete"),
    ("Bash", "find ./build -type f -delete"),
    ("Bash", "find . -path './lab/*' -delete"),
    ("Bash", "find . -path './build/*' -delete"),
    ("Bash", "find . -regex '.*/[^/]*\\.pyc' -delete"),
    ("Bash", "find . -type f -name '*.orig' -delete"),
    ("Bash", "find . -path './node_modules' -prune -o -name '*.js' -print"),
    ("Bash", "find dist -name 'zexion*' -delete"),
    ("Bash", "find worlds -name 'test_*.py' -exec ruff check {} +"),
    ("Bash", ".venv/Scripts/python -m pytest -q"),
    ("Bash", "cmake -S native -B native/build"),
    ("Bash", "cmake --build native/build --config Release"),
    ("PowerShell", "Remove-Item build -Recurse"),
    ("PowerShell", "gci -r -Filter *.pyc | ri"),
    ("PowerShell", "gci lab -r -Filter *.tmp | ri"),
    ("PowerShell", "Remove-Item $env:TEMP\\foo -Recurse"),
    ("PowerShell", "Remove-Item ~\\work\\x -Recurse"),
    ("PowerShell", "New-Item -ItemType Directory ~\\work"),
    # The repo's own build folders: the helpers' files there are plain work.
    ("Bash", "rm native/build/version.dll"),
    ("Bash", "rm native/build/libzexion_ap.so"),
    ("Bash", "rm -f native/build/version.dll native/build/libzexion_ap.so"),
    ("Bash", "cp native/build/version.dll dist/version.dll"),
    ("Bash", "cp native/build/libzexion_ap.so dist/libzexion_ap.so"),
    ("Bash", "cp -f native/build/Release/zexion_ap.dll dist/"),
    ("Bash", "rm dist/version.dll dist/libzexion_ap.so dist/zexion_ap.dll"),
    ("Bash", "rm -rf native/build"),
    ("Bash", "cd native/build && rm version.dll libzexion_ap.so"),
    ("Bash", "cd native/build && cp libzexion_ap.so ../../dist/"),
    ("Bash", "rm native/build/proxy/Release/version.dll"),
    ("Bash", "rm native/build/libzexion_ap.so && cmake --build native/build --clean-first"),
    ("Bash", "gh run download 123 -n libzexion_ap-runtime -D native/build"),
    ("PowerShell", "Remove-Item native\\build\\version.dll"),
    ("PowerShell", "Copy-Item native\\build\\libzexion_ap.so dist\\"),
    ("PowerShell", "Remove-Item native\\build\\proxy\\Release\\version.dll -Force"),
]


@pytest.mark.parametrize(("tool", "command"), ABOVE_BLOCKED)
def test_a_write_that_reaches_a_folder_above_a_protected_one_blocked(
    tool: str, command: str, home_env: str
) -> None:
    assert run(tool, command=command), (home_env, command)


@pytest.mark.parametrize(("tool", "command"), ABOVE_ALLOWED)
def test_reads_from_a_folder_above_and_everyday_work_allowed(
    tool: str, command: str, home_env: str
) -> None:
    assert run(tool, command=command) is None, (home_env, command)


def test_a_folder_above_in_a_commands_cwd_blocks_a_write_not_a_read(home_env: str) -> None:
    for cwd in (FAKE_ENV["USERPROFILE"], FAKE_ENV["APPDATA"], "C:/", "/", "/home/u"):
        assert run("Bash", command="rm -rf x", cwd=cwd), cwd
        assert run("Bash", command="find . -delete", cwd=cwd), cwd
        assert run("Bash", command="git pull", cwd=cwd), cwd
        assert run("Bash", command="ls -R", cwd=cwd) is None, cwd
        assert run("Bash", command="find . -name x -print", cwd=cwd) is None, cwd
    assert run("Bash", command="rm -rf x", cwd="D:/code/zexion-ap") is None
    assert run("Bash", command="rm -rf x", cwd=f"{FAKE_ENV['USERPROFILE']}/work") is None


@pytest.mark.parametrize(
    "path",
    [
        "~", "$HOME", "${HOME}", "%USERPROFILE%", "$env:USERPROFILE", "$env:APPDATA", "${APPDATA}",
        "%LOCALAPPDATA%", "$XDG_DATA_HOME", "/", "//", "C:/", "C:\\", "d:/", "/c", "/mnt/c",
        "/cygdrive/c", "//?/C:/", "/home", "/home/someone", "/Users/someone", "C:/Users",
        "C:/Users/someone", "C:/Users/someone/AppData/Roaming", "~/AppData/Roaming/", "~/.var",
        "~/.var/app", "~/.steam", "~/.steam/steam", "~/.local/share/Steam",
        "C:/Program Files (x86)", "C:/Program Files (x86)/Steam", "~/*", "$APPDATA/*",
        "$APPDATA/.*", "$APPDATA/..", "$HOME/../..", "C:/Users/*", "/home/*", "/*",
        "C:/Users/someone/AppData/Roaming/..", "//?/C:/Users/someone", "PROGRA~2",
        "C:/PROGRA~2/Steam", "~/snap/steam", "C:/Users/someone/AppData", "~/AppData/Local",
    ],
)  # fmt: skip
def test_a_folder_above_a_protected_one(path: str, home_env: str) -> None:
    assert protected.holds_protected(path), (home_env, path)


@pytest.mark.parametrize(
    "path",
    [
        "lab", ".", "", "./build", "x/y", "C:/Users/someone/work", "C:/Users/someone/Documents",
        "C:/Users/someone/AppData/Local/Temp/x", "C:/Users/someone/AppData/Roaming/Code",
        "C:/Users/someone/AppData/Roaming/Code/*", "~/work", "~/work/*", "~/x/*", "/tmp", "/tmp/x",
        "/home/someone/work", "/home/someone/work/zexion-ap", "D:/code/zexion-ap",
        "D:/code/zexion-ap/native/build", "/c/Users/someone/work", "snap", "steam",
        "C:/Program Files", "$TEMP", "$TEMP/x", "/usr/local", "C:/Users/someone/*.txt",
        "~/*.txt", "/c/Users/someone/work/x/..", "worlds/zexion", "../x",
    ],
)  # fmt: skip
def test_a_folder_that_holds_no_protected_one(path: str, home_env: str) -> None:
    assert not protected.holds_protected(path), (home_env, path)


FILE_NAME_PATTERNS = [
    # (pattern, kind, ignore_case, may it name a file in a protected folder)
    ("*", "name", False, True),
    ("Z*", "name", False, True),
    ("[Z]*", "name", False, True),
    ("[A-Z][a-z]*", "name", False, True),
    ("*.sav", "name", False, True),
    ("Save*", "name", False, True),
    ("save*", "name", False, True),
    ("Save Game*.sav", "name", False, True),
    ("Save Game ??.sav", "name", False, True),
    ("Save Game 7.sav", "name", False, True),
    ("Save Game 31.sav", "name", False, False),
    ("GameUnlocks.sav", "name", False, True),
    ("gameunlocks.SAV", "name", False, True),
    ("Pref*", "name", False, True),
    ("Preferences*", "name", False, True),
    ("PreferencesLocal", "name", False, True),
    ("ZexionAP-seed-*", "name", False, True),
    ("ZexionAP-seed-12345-0-1.sav", "name", False, True),
    ("zexionap-seed-*", "name", False, False),  # the repo's own name in lower case
    ("zexionap-seed-*", "name", True, True),  # -iname
    ("*seed*", "name", False, True),
    ("steam_autocloud.vdf", "name", False, True),
    ("*.vdf", "name", False, True),
    ("token", "name", False, True),
    ("TOKEN", "name", False, True),
    ("t*n", "name", False, True),
    ("?????", "name", False, True),
    ("seeds", "name", False, True),
    ("config-*.txt", "name", False, True),
    ("config-B1.txt", "name", False, True),
    ("*.txt", "name", False, True),
    ("*.*", "name", False, True),
    ("{token,x}", "name", False, True),
    ("Sav[e]*", "name", False, True),
    ("Save\\ Game*", "name", False, True),
    ("*/Save Game*", "path", False, True),
    ("./lab/*.sav", "path", False, True),
    ("*/token", "path", False, True),
    (".*\\.sav", "regex", False, True),
    (".*/Save Game.*\\.sav", "regex", False, True),
    ("*.sav", "ps", False, True),
    ("ZexionAP-seed-*", "ps", False, True),
    ("zexionap-seed-*", "ps", False, True),  # PowerShell ignores case
    ("Save*", "ps", False, True),
    ("*", "ps", False, True),
    # Not files of theirs.
    ("*.py", "name", False, False),
    ("*.pyc", "name", False, False),
    ("*.pyo", "name", False, False),
    ("*.md", "name", False, False),
    ("*.json", "name", False, False),
    ("*.log", "name", False, False),
    ("*.tmp", "name", False, False),
    ("*.o", "name", False, False),
    ("*~", "name", False, False),
    ("*foo*", "name", False, False),
    ("test_*.py", "name", False, False),
    ("x", "name", False, False),
    ("tokens", "name", False, False),
    ("Save Game.txt", "name", False, False),
    ("worlds", "name", False, False),
    ("./lab/*", "path", False, False),
    ("./build/*", "path", False, False),
    ("*/x/*", "path", False, False),
    (".*\\.pyc", "regex", False, False),
    (".*/[^/]*\\.pyc", "regex", False, False),
    ("*.pyc", "ps", False, False),
    ("*.py", "ps", False, False),
]


@pytest.mark.parametrize(("pattern", "kind", "ignore_case", "expected"), FILE_NAME_PATTERNS)
def test_a_name_pattern_that_may_match_a_file_in_a_protected_folder(
    pattern: str, kind: str, ignore_case: bool, expected: bool
) -> None:
    assert protected.pattern_may_name_protected(pattern, kind, ignore_case) is expected


@pytest.mark.parametrize("length", [201, 500, 999])
def test_a_long_part_of_a_name_pattern_is_read_as_a_file_name(length: int) -> None:
    assert protected.pattern_may_name_protected("x" * length, "name")
    assert not protected.pattern_may_name_protected("x" * 200, "name")


def test_the_glob_reader_agrees_with_fnmatch() -> None:
    import fnmatch
    import random

    rnd = random.Random(7)
    for _ in range(3000):
        pattern = "".join(rnd.choice("ab*?") for _ in range(rnd.randint(0, 8)))
        name = "".join(rnd.choice("ab") for _ in range(rnd.randint(0, 7)))
        tokens = protected._wildcard_tokens(pattern)
        assert protected._glob_matches(tokens, name) is fnmatch.fnmatchcase(name, pattern), (
            pattern,
            name,
        )


@pytest.mark.parametrize(
    ("tool", "command"),
    [
        ("Bash", "rm -rf ~"),
        ("Bash", 'find "$APPDATA" -delete'),
        ("PowerShell", "gci $env:APPDATA -r | ri"),
        ("Bash", "find lab -name '*.sav' -delete"),
    ],
)
def test_the_hook_process_blocks_a_folder_above_and_a_file_name(tool: str, command: str) -> None:
    result = _hook(json.dumps({"tool_name": tool, "tool_input": {"command": command}}))
    assert result.returncode == 2, command
    assert "guard_protected" in result.stderr


def _quick(tool: str, command: str, limit: float = 2.5) -> str | None:
    started = time.monotonic()
    result = run(tool, command=command)
    assert time.monotonic() - started < limit, command[:80]
    return result


def test_the_reads_of_folders_above_and_of_file_names_answer_quickly() -> None:
    long_star = "*a" * 450
    spaced = "x " * 900
    for tool, command in [
        ("Bash", "rm -rf " + " ".join(f"~/x{i}" for i in range(3000))),
        ("Bash", "rm -rf " + " ".join(f"../../x{i}/.." for i in range(3000))),
        ("Bash", "rm -rf " + " ".join(f"$V{i}/.." for i in range(3000))),
        ("Bash", "rm -rf " + " ".join(f"/{i}/.." for i in range(3000))),
        ("Bash", "rm " + "~ " * 6000),
        ("Bash", "rm " + "* " * 6000),
        ("Bash", "sh -c " + " ".join(f"'{spaced}{i}'" for i in range(40))),
        ("Bash", "sh -c " + " ".join(f"'find . -name n{i} -x y z'" for i in range(1500))),
        ("Bash", "find ~ " + " -o ".join(f"-name 'n{i}*'" for i in range(1500)) + " -delete"),
        ("Bash", f"find ~ -name '{long_star}' -delete"),
        ("Bash", "find . " + " -o ".join(f"-name 'q{i}{'*a' * 100}'" for i in range(40))),
        ("Bash", "find . -name '" + "{a,b}*" * 150 + "' -delete"),
        ("Bash", "find . -regex '" + ".*a" * 300 + "' -delete"),
        ("Bash", "find . -path '" + "*/a*" * 200 + "' -delete"),
        ("PowerShell", "gci -r -Filter '" + "*a" * 450 + "' | ri"),
        ("PowerShell", "gci -r -Filter " + ",".join(f"*a{i}*" for i in range(2000)) + " | ri"),
        ("PowerShell", ";".join(f"gci . -Filter 'n{i} a b c'" for i in range(2000))),
        ("Bash", ";".join(f"cd ~/d{i}" for i in range(2000)) + "; rm x"),
        ("Bash", "eval " + "'eval " * 10 + "find ~ -delete" + "'" * 10),
        ("Bash", "rm -rf " + "../" * 190),
        ("Bash", "rm -rf " + "/a" * 190),
    ]:
        _quick(tool, command)


def test_a_long_command_that_reads_only_still_passes_quickly() -> None:
    words = " ".join(f"w{i}" for i in range(3000))
    assert _quick("Bash", f"ls -R ~ {words}") is None
    assert _quick("Bash", f"find ~ {words} -name x -print") is None
