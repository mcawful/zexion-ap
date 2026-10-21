"""PreToolUse hook: block writes into the game install, the real saves, and Steam's cloud cache.

Claude Code sends the tool call as JSON on stdin. Exit 2 blocks it and shows stderr to Claude.

- Write, Edit, MultiEdit, NotebookEdit: blocked when the target file is in a protected folder.
- Bash, PowerShell: a command that names a protected folder is allowed only when every command
  in it is read-only and no output is redirected into a protected folder, or when the whole
  command is one ``.venv`` Python run of a script in ``tools/``. Those scripts enforce their own
  rules through ``tools/protected.py``. Such a command is blocked outright if it contains
  command substitution (``$(``, or a backtick in Bash) or a ``::`` .NET call, or if it
  redirects output to a path built from a variable (``$`` or ``%`` left after expansion),
  because the guard can't see where those lead.

This is a tripwire against mistakes, not a sandbox. It fails closed: if the guard itself breaks
while looking at a call that mentions the saves or a Steam folder, the call is blocked.
"""

from __future__ import annotations

import json
import os
import re
import sys
from pathlib import Path

_TOOLS_DIR = Path(__file__).resolve().parent.parent.parent / "tools"

BLOCK_HINT = (
    "Blocked by .claude/hooks/guard_protected.py: this would write into the Zexion install, the "
    "real save folder, or Steam's cloud cache for the game. Read-only commands are fine. Save "
    "experiments go through tools/save_lab.py (see /save-lab), which backs up first."
)

# Commands that cannot modify files by themselves. Output redirects are checked separately.
READ_ONLY_SH = {
    "ls", "dir", "cat", "head", "tail", "less", "more", "xxd", "od", "hexdump", "wc", "stat",
    "file", "strings", "sha1sum", "sha256sum", "sha512sum", "md5sum", "cksum", "cmp", "diff",
    "grep", "egrep", "fgrep", "rg", "find", "du", "df", "test", "[", "echo", "printf", "cd",
    "pushd", "popd", "pwd", "true", "false", "sort", "uniq", "cut", "tr", "basename", "dirname",
    "realpath", "readlink", "tree", "type", "which", "for", "in", "done", "fi", "esac", "export",
    "fold", "nl", "column", "paste", "comm", "tac", "rev", "seq", "base64",
}  # fmt: skip
READ_ONLY_PS = {
    "get-childitem", "gci", "ls", "dir", "get-content", "gc", "cat", "type", "get-item", "gi",
    "get-itemproperty", "gp", "test-path", "get-filehash", "format-hex", "fhx", "select-string",
    "sls", "measure-object", "measure", "select-object", "select", "where-object", "where", "?",
    "sort-object", "sort", "format-list", "fl", "format-table", "ft", "out-string",
    "write-output", "echo", "write-host", "set-location", "cd", "sl", "chdir", "push-location",
    "pushd", "pop-location", "popd", "resolve-path", "split-path", "join-path", "compare-object",
    "get-location", "pwd", "get-acl", "foreach", "in",
}  # fmt: skip
# Words that only introduce a command and say nothing about what it does.
PREFIX_WORDS = {"do", "then", "else", "elif", "if", "while", "until", "!", "{", "}", "time"}
FIND_WRITE_FLAGS = {"-delete", "-exec", "-execdir", "-ok", "-okdir", "-fprint", "-fprint0",
                    "-fprintf", "-fls"}  # fmt: skip
HARMLESS_TARGETS = {"/dev/null", "$null", "nul", "null"}

_VENV_TOOL = re.compile(
    r"""^\s*(?:&\s*)?["']?(?:[^"'\s]*[\\/])?\.venv[\\/](?:Scripts|bin)[\\/]python(?:\.exe)?["']?"""
    r"""\s+["']?(?:\.[\\/])?tools[\\/][A-Za-z_]\w*\.py["']?(?:\s|$)""",
    re.IGNORECASE,
)


def lex(command: str, powershell: bool) -> tuple[list[list[str]], list[str]]:
    """Split a command into simple commands (lists of words) and the targets of ``>`` redirects.

    Rough but quote-aware. Separators: ``;``, ``&``, ``|``, newlines and parentheses.
    """
    segments: list[list[str]] = [[]]
    targets: list[str] = []
    word: str | None = None
    quote: str | None = None
    expect_target = False
    escape = "`" if powershell else "\\"

    def flush() -> None:
        nonlocal word, expect_target
        if word is None:
            return
        if expect_target:
            targets.append(word)
            expect_target = False
        else:
            segments[-1].append(word)
        word = None

    i, n = 0, len(command)
    while i < n:
        c = command[i]
        if quote == "'":
            if c == "'":
                quote = None
            else:
                word += c
        elif quote == '"':
            if c == '"':
                quote = None
            elif c == escape and i + 1 < n:
                i += 1
                word += command[i]
            else:
                word += c
        elif c in "'\"":
            quote = c
            word = word or ""
        elif c == escape and i + 1 < n:
            i += 1
            word = (word or "") + command[i]
        elif c in " \t\r":
            flush()
        elif c in ";\n()|&":
            if c == "&" and expect_target and word is None:
                # 2>&1 style: the target is a file descriptor.
                j = i + 1
                while j < n and command[j].isdigit():
                    j += 1
                targets.append(command[i:j])
                expect_target = False
                i = j
                continue
            flush()
            segments.append([])
        elif c == ">":
            # A redirect operator, possibly with a descriptor (2>) or * (PowerShell *>).
            if word is not None and re.fullmatch(r"\d|\*|&", word):
                word = None
            else:
                flush()
            while i + 1 < n and command[i + 1] in ">|":
                i += 1
            expect_target = True
        elif c == "<":
            flush()
        else:
            word = (word or "") + c
        i += 1
    flush()
    return [s for s in segments if s], targets


def _strip_prefix(words: list[str], powershell: bool) -> list[str]:
    out = list(words)
    while out:
        first = out[0]
        if first.lower() in PREFIX_WORDS:
            out.pop(0)
        elif not powershell and re.fullmatch(r"[A-Za-z_]\w*=.*", first, re.DOTALL):
            out.pop(0)  # NAME=value
        elif powershell and re.fullmatch(r"\$[\w:]+", first) and len(out) > 1 and out[1] == "=":
            out = out[2:]  # $name = <command>
            if len(out) == 1 and re.search(r"[\\/ ]", out[0]):
                return []  # $name = "a path literal"
        elif powershell and re.fullmatch(r"\$[\w:]+=", first):
            out.pop(0)
        else:
            break
    return out


def _segment_is_read_only(words: list[str], powershell: bool) -> bool:
    words = _strip_prefix(words, powershell)
    if not words:
        return True
    verb = re.split(r"[\\/]", words[0])[-1].lower()
    verb = verb.removesuffix(".exe")
    if powershell:
        if verb.startswith("$") and len(words) == 1:
            return True  # a bare variable is just output
        allowed = verb in READ_ONLY_PS
    else:
        allowed = verb in READ_ONLY_SH
    if not allowed:
        return False
    args = {w.lower() for w in words[1:]}
    if verb == "find" and args & FIND_WRITE_FLAGS:
        return False
    return not (verb == "sort" and any(a == "-o" or a.startswith("--output") for a in args))


def check(tool_name: str, tool_input: dict, protected) -> str | None:
    """Return a reason to block, or None to allow. ``protected`` is the tools/protected module."""
    if tool_name in {"Write", "Edit", "MultiEdit", "NotebookEdit"}:
        path = tool_input.get("file_path") or tool_input.get("notebook_path") or ""
        if path and protected.is_protected_path(path):
            return f"{BLOCK_HINT}\nTarget: {path}"
        return None

    if tool_name not in {"Bash", "PowerShell"}:
        return None
    command = str(tool_input.get("command", ""))
    if not protected.mentions_protected(command):
        return None
    powershell = tool_name == "PowerShell"

    if "$(" in command or (not powershell and "`" in command) or "::" in command:
        return f"{BLOCK_HINT}\nCommand substitution or .NET calls next to a protected path."

    segments, targets = lex(command, powershell)
    for target in targets:
        if target.startswith("&") or target.lower() in HARMLESS_TARGETS:
            continue
        expanded = protected.normalize(target)
        if protected.is_protected_path(target) or "$" in expanded or "%" in expanded:
            return f"{BLOCK_HINT}\nRedirect target: {target}"

    if _VENV_TOOL.match(command) and len(segments) == 1:
        return None
    for words in segments:
        if not _segment_is_read_only(words, powershell):
            return f"{BLOCK_HINT}\nNot a read-only command: {' '.join(words)}"
    return None


def _mentions_sensitive(text: str) -> bool:
    low = text.lower()
    return any(s in low for s in ("gallant", "steamapps", "3392510"))


def main() -> int:
    raw = sys.stdin.read()
    # Judge a failure by the tool's own input only. The rest of the payload carries session
    # paths (the transcript lives under AppData) that would make every failure block everything.
    try:
        subject = json.dumps(json.loads(raw).get("tool_input") or {})
    except Exception:
        subject = raw
    try:
        sys.path.insert(0, str(_TOOLS_DIR))
        import protected

        data = json.loads(raw)
        reason = check(data.get("tool_name", ""), data.get("tool_input") or {}, protected)
    except Exception as exc:  # fail closed, but only where it matters
        if _mentions_sensitive(subject):
            print(
                f"{BLOCK_HINT}\nThe guard itself failed ({exc!r}), so it blocked.", file=sys.stderr
            )
            return 2
        return 0
    if reason:
        print(reason, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    os.environ.setdefault("PYTHONIOENCODING", "utf-8")
    sys.exit(main())
