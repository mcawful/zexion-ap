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
