"""PreToolUse hook: block writes into the game install, the real saves, and Steam's cloud cache.

Claude Code sends the tool call as JSON on stdin. Exit 2 blocks it and shows stderr to Claude.

- Write, Edit, MultiEdit, NotebookEdit: blocked when the target file is in a protected folder.
- Shell commands: the ``command`` of Bash and Monitor (read as Bash), and of PowerShell and the
  terminal tool ``mcp__terminal__run_in_terminal`` (read as PowerShell). Other tools pass.
  The command runs in the tool's ``cwd`` if it has one, else in the payload's own ``cwd`` (the
  session's working folder, which a ``cd`` in an earlier call may have moved).
  A command that may reach a protected folder is allowed only when every simple command in it
  is recognizably read-only, a ``.venv`` Python run of a script in ``tools/`` (those scripts
  enforce their own rules through ``tools/protected.py``), or a ``git`` or ``gh`` command
  that names no protected folder, and no output is redirected into a protected folder.

  - Text that is only data is left out: the body of a Bash heredoc fed to a reader such as
    ``cat``, ``git`` or ``gh`` (not to a shell or Python), a PowerShell here-string's quotes,
    and the message values of ``git`` (``-m``) and ``gh`` (``--title``, ``--body`` and the
    like). So commit messages and PR text may name the folders.
  - It *may reach* one when its text names one (also with quotes removed, ``/./`` collapsed
    and trailing dots and spaces dropped before a ``/``); when any of its words could name one
    as a path (``..``, wildcards such as ``Gallant*``, 8.3 short names:
    ``protected.word_may_name_protected``) or resolves into one through an existing link;
    when it may build one from pieces (a word that is the app id as a path component; or, in a
    command that mentions ``gallant`` or ``steamapps``, a ``Zexion`` component that starts a
    word or follows a variable or a ``…Leaf``/``…common`` piece, or a ``common`` component
    after a variable); when it makes a link (``mklink``, ``ln``, a junction) next to a folder
    named like ``gallant`` or ``steam``; or when it changes directory (``cd``, ``pushd``,
    ``Set-Location`` and their aliases, or the ``cwd`` above) to a path with ``gallant``,
    ``steam``, ``userdata`` or the app id in a component, or with a wildcard.
  - *Read-only* means the verb is on a short list and nothing in its arguments makes it write:
    ``find`` without ``-delete``, ``-exec`` and the like; ``sort`` without ``-o``, ``-T``,
    ``--output`` or ``--compress-program``; ``uniq`` and ``xxd`` with at most one file operand
    (a second is their output file); ``tree`` without ``-o`` or ``-R``; ``rg`` without
    ``--pre``; ``file`` without ``-C``; ``less`` without a log file; ``sed`` without ``-i`` or
    ``-f`` and with no ``w``, ``W`` or ``e`` command in its script. In PowerShell, a script
    block's contents are checked as commands of their own; ``ForEach-Object`` (``foreach``,
    ``%``) counts only with script blocks (``| foreach Delete`` calls a method); an expression
    (one that starts with a variable, a literal, a ``[Type]``, a member of a parenthesis, or a
    hashtable's ``key=``) counts unless it assigns (``=``, ``++``, ``--``); ``& $variable``
    never counts.
  - Blocked outright: command substitution (``$(``, or a backtick in Bash; the
    ``"$(cat <<'EOF' … EOF)"`` message form is fine), a PowerShell ``::`` static call other
    than a known reader (``[IO.File]::ReadAllBytes``, ``[BitConverter]``, ``[Math]`` and so
    on), a PowerShell method call outside quotes other than a few string ones (``.Delete()``,
    ``.MoveTo()``, ``.'Delete'()``, ``.$name()``), and output redirected (``>``, ``>>``,
    ``>&``, ``&>``) to a path that still holds a variable after the hook's own environment is
    applied (one the command sets itself), because the guard can't see where those lead.

This is a tripwire against mistakes, not a sandbox. It can't see a path assembled entirely from
variables or string arithmetic, a link to a parent folder made outside the command, or the
folder behind a ``cd`` into a variable. It fails closed: if the guard itself breaks while
looking at a call that mentions the saves or a Steam folder, the call is blocked.
"""

from __future__ import annotations

import fnmatch
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

# The tools that run a shell command, and whether it is read as PowerShell.
SHELL_TOOLS = {
    "Bash": False,
    "Monitor": False,
    "PowerShell": True,
    "mcp__terminal__run_in_terminal": True,
}

# Commands that cannot modify files by themselves, given the argument checks in
# ``_bash_args_write``. Output redirects are checked separately.
READ_ONLY_SH = {
    "ls", "dir", "cat", "head", "tail", "less", "more", "xxd", "od", "hexdump", "wc", "stat",
    "file", "strings", "sha1sum", "sha256sum", "sha512sum", "md5sum", "cksum", "cmp", "diff",
    "grep", "egrep", "fgrep", "rg", "find", "du", "df", "test", "[", "echo", "printf", "cd",
    "pushd", "popd", "pwd", "true", "false", "sort", "uniq", "cut", "tr", "basename", "dirname",
    "realpath", "readlink", "tree", "type", "which", "for", "in", "done", "fi", "esac", "export",
    "fold", "nl", "column", "paste", "comm", "tac", "rev", "seq", "base64", "read", "sed",
}  # fmt: skip
READ_ONLY_PS = {
    "get-childitem", "gci", "ls", "dir", "get-content", "gc", "cat", "type", "get-item", "gi",
    "get-itemproperty", "gp", "test-path", "get-filehash", "format-hex", "fhx", "select-string",
    "sls", "measure-object", "measure", "select-object", "select", "where-object", "where", "?",
    "sort-object", "sort", "format-list", "fl", "format-table", "ft", "format-wide", "fw",
    "out-string", "out-null", "out-host", "oh", "write-output", "echo", "write-host",
    "set-location", "cd", "sl", "chdir", "push-location", "pushd", "pop-location", "popd",
    "resolve-path", "split-path", "join-path", "compare-object", "get-location", "pwd",
    "get-acl", "in", "group-object", "group", "get-member", "gm", "convertto-json",
    "convertto-csv",
}  # fmt: skip
# Heredoc readers whose stdin is only data (not a shell, an interpreter, or sed's script).
HEREDOC_READERS = {
    "cat", "git", "gh", "tee", "head", "tail", "grep", "egrep", "fgrep", "rg", "wc", "sort",
    "uniq", "cut", "tr", "base64", "xxd", "od", "hexdump", "diff", "cmp", "comm", "nl", "tac",
    "rev", "column", "fold", "paste", "less", "more", "strings",
}  # fmt: skip
# ForEach-Object counts as read-only only with script blocks: a bare word is a method to call.
PS_FOREACH = {"foreach", "%", "foreach-object"}
PS_SCRIPT_BLOCK_PARAMS = {"-process", "-begin", "-end", "-parallel"}
# Method calls that only read. Any other ``.Name(`` (``.Delete()``) is blocked.
PS_SAFE_METHODS = {
    "tostring", "trim", "trimstart", "trimend", "substring", "split", "tolower", "toupper",
    "startswith", "endswith", "contains", "indexof", "gettype", "padleft", "padright", "equals",
    "getstring", "getbytes",
}  # fmt: skip
# ``[Type]::Member`` calls that only read, by type (``System.`` dropped); None: any member.
PS_STATIC_READERS: dict[str, set[str] | None] = {
    "io.file": {"readallbytes", "readalltext", "readalllines", "exists", "getattributes",
                "getlastwritetime", "getcreationtime"},
    "io.directory": {"exists", "getfiles", "getdirectories", "getfilesystementries",
                     "enumeratefiles", "getcurrentdirectory"},
    "io.path": None, "bitconverter": None, "math": None, "text.encoding": None, "string": None,
    "int": None, "int32": None, "uint32": None, "int64": None, "uint64": None, "byte": None,
    "char": None, "datetime": None, "convert": {"to*"},
    "environment": {"getenvironmentvariable", "getfolderpath", "newline", "currentdirectory"},
}  # fmt: skip
# Commands that change directory, in either shell.
CD_VERBS = {"cd", "pushd", "chdir", "set-location", "sl", "push-location"}
# Words that make a link; next to a folder like ``Gallant Leaf`` the link may lead inside.
LINK_WORDS = {"mklink", "ln", "junction", "symboliclink", "hardlink"}
# Words that only introduce a command and say nothing about what it does.
PREFIX_WORDS = {"do", "then", "else", "elif", "if", "while", "until", "!", "{", "}", "time"}
FIND_WRITE_FLAGS = {"-delete", "-exec", "-execdir", "-ok", "-okdir", "-fprint", "-fprint0",
                    "-fprintf", "-fls"}  # fmt: skip
# Options that take the next word as their value, so it is not a file operand.
UNIQ_VALUED = {"-f", "-s", "-w", "--skip-fields", "--skip-chars", "--check-chars"}
XXD_VALUED = {"-c", "-cols", "-g", "-groupsize", "-l", "-len", "-n", "-name", "-o", "-offset",
              "-s", "-seek"}  # fmt: skip
# Options of git and gh whose value is text, not a path.
GIT_TEXT_OPTS = {"-m", "--message"}
GH_TEXT_OPTS = {"-t", "--title", "-b", "--body", "-n", "--notes", "--subject", "-f",
                "--raw-field", "-F", "--field", "-q", "--jq", "-S", "--search"}  # fmt: skip
HARMLESS_TARGETS = {"/dev/null", "$null", "nul", "null"}

_VENV_PYTHON = re.compile(r"(?:.*[\\/])?\.venv[\\/](?:scripts|bin)[\\/]python(?:\.exe)?", re.I)
_TOOL_SCRIPT = re.compile(r"(?:\.[\\/])?tools[\\/][A-Za-z_]\w*\.py", re.I)
_PS_METHOD_CALL = re.compile(r"\.\s*([A-Za-z_]\w*)\s*\(")
_PS_ODD_METHOD_CALL = re.compile(r"\.\s*(?:(['\"])\w+\1|\$\w+)\s*\(")  # .'Delete'(), .$m()
_PS_STATIC = re.compile(r"\[\s*(?:system\.)?([\w.]+)\s*\]\s*::\s*(\w+)", re.I)
_PS_QUOTED = re.compile(r"'[^']*'|\"(?:`.|[^\"`])*\"", re.DOTALL)
_PS_HERE_STRING = re.compile(r"@(['\"])[ \t]*\r?\n(.*?)\r?\n\1@", re.DOTALL)
_HEREDOC = re.compile(r"(?<!<)<<(?!<)(-?)[ \t]*(?:'([^'\n]+)'|\"([^\"\n]+)\"|(\\?)([A-Za-z_]\w*))")
_CAT_HEREDOC = re.compile(r"\$\(\s*cat\s*<<-?\s*(?:'[^'\n]+'|\"[^\"\n]+\"|\\?\w+)[ \t]*\n\s*\)")
# A ``>&`` that duplicates or closes a descriptor (2>&1, >&-) rather than naming a file.
_FD_DUP = re.compile(r"\d*-?(?=$|[\s;&|()<>])")
_ENV_REF = re.compile(r"\$\{env:(\w+)\}|\$env:(\w+)|\$\{(\w+)\}|\$(\w+)|%(\w+)%", re.I)


class Word(str):
    """A lexed word: its text, ``bare`` (the characters that were not quoted), and its span."""

    bare: str = ""
    start: int = 0
    end: int = 0


def _word(text: str, bare: str, start: int, end: int) -> Word:
    w = Word(text)
    w.bare, w.start, w.end = bare, start, end
    return w


def lex(command: str, powershell: bool) -> tuple[list[list[Word]], list[Word]]:
    """Split a command into simple commands (lists of words) and the targets of ``>`` redirects.

    Rough but quote-aware. Separators: ``;``, ``&``, ``|``, newlines and parentheses, and in
    PowerShell the braces of a script block (``${name}`` stays one word). PowerShell's call
    operator ``&`` starts its command with the word ``&``.
    """
    segments: list[list[Word]] = [[]]
    targets: list[Word] = []
    text: str | None = None
    bare = ""
    start = 0
    quote: str | None = None
    expect_target = False
    escape = "`" if powershell else "\\"
    separators = ";\n()|&{}" if powershell else ";\n()|&"

    def add(chars: str, is_bare: bool, at: int) -> None:
        nonlocal text, bare, start
        if text is None:
            text, bare, start = "", "", at
        text += chars
        if is_bare:
            bare += chars

    def flush(end: int) -> None:
        nonlocal text, expect_target
        if text is None:
            return
        w = _word(text, bare, start, end)
        if expect_target:
            targets.append(w)
            expect_target = False
        else:
            segments[-1].append(w)
        text = None

    i, n = 0, len(command)
    while i < n:
        c = command[i]
        if quote == "'":
            if c == "'":
                quote = None
            else:
                add(c, False, i)
        elif quote == '"':
            if c == '"':
                quote = None
            elif c == escape and i + 1 < n:
                i += 1
                add(command[i], False, i)
            else:
                add(c, False, i)
        elif c in "'\"":
            quote = c
            add("", False, i)
        elif c == escape and i + 1 < n:
            add(command[i + 1], False, i)
            i += 1
        elif c in " \t\r":
            flush(i)
        elif powershell and c == "$" and command.startswith("{", i + 1):
            end = command.find("}", i)
            end = n - 1 if end < 0 else end
            add(command[i : end + 1], True, i)
            i = end + 1
            continue
        elif c in separators:
            if c == "&" and expect_target and text is None:
                dup = _FD_DUP.match(command, i + 1)
                if dup and dup.group():  # 2>&1, >&-: a descriptor
                    targets.append(_word("&" + dup.group(), "&" + dup.group(), i, dup.end()))
                    expect_target = False
                    i = dup.end()
                else:  # >& FILE: the next word is the target
                    i += 1
                continue
            flush(i)
            segments.append([])
            if powershell and c == "&":
                if command.startswith("&", i + 1):  # &&
                    i += 2
                    continue
                segments[-1].append(_word("&", "&", i, i + 1))  # the call operator
        elif c == ">":
            # A redirect operator, possibly with a descriptor (2>) or * (PowerShell *>).
            if text is not None and text == bare and re.fullmatch(r"\d|\*|&", text):
                text = None
            else:
                flush(i)
            while i + 1 < n and command[i + 1] in ">|":
                i += 1
            expect_target = True
        elif c == "<":
            flush(i)
        else:
            add(c, True, i)
        i += 1
    flush(n)
    return [s for s in segments if s], targets


def _cut_heredocs(command: str) -> str:
    """The command without the bodies of Bash heredocs fed to a data reader.

    The readers are ``HEREDOC_READERS``. A body fed to anything else (``python -``, ``bash``)
    stays in, and so does one that substitutes a command (``$(`` or a backtick under an
    unquoted delimiter).
    """
    out: list[str] = []
    pos = 0
    while (m := _HEREDOC.search(command, pos)) is not None:
        delim = m.group(2) or m.group(3) or m.group(5)
        expands = m.group(5) is not None and not m.group(4)
        newline = command.find("\n", m.end())
        if newline < 0:
            break
        body_start = line_start = newline + 1
        body_end = after = len(command)
        while line_start < len(command):
            line_end = command.find("\n", line_start)
            line_end = len(command) if line_end < 0 else line_end
            line = command[line_start:line_end].rstrip("\r")
            if (line.lstrip("\t") if m.group(1) else line) == delim:
                body_end, after = line_start, line_end
                break
            line_start = line_end + 1
        piece = re.split(r"[;&|\n(]|\$\(", command[: m.start()])[-1].split()
        while piece and re.fullmatch(r"[A-Za-z_]\w*=\S*", piece[0]):
            piece.pop(0)
        reader = _verb(piece[0]) if piece else ""
        body = command[body_start:body_end]
        runs = expands and ("$(" in body or "`" in body)  # <<EOF bodies substitute commands
        if reader in HEREDOC_READERS and not runs:
            out.append(command[pos:body_start])
            pos = after
        else:
            out.append(command[pos:after])
            pos = after
    out.append(command[pos:])
    return "".join(out)


def _here_strings_to_quotes(command: str) -> str:
    """PowerShell here-strings (``@'…'@``) as ordinary quoted strings, for the lexer."""

    def sub(m: re.Match) -> str:
        q, body = m.group(1), m.group(2)
        return q + (body.replace("'", "''") if q == "'" else body.replace('"', '`"')) + q

    return _PS_HERE_STRING.sub(sub, command)


def _strip_prefix(words: list[Word], powershell: bool) -> list[Word]:
    out = list(words)
    if powershell and out[:1] == ["&"]:
        out.pop(0)
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


def _verb(word: str) -> str:
    return re.split(r"[\\/]", word)[-1].lower().removesuffix(".exe")


def _operands(args: list[Word], valued: set[str]) -> list[str]:
    """The words of ``args`` that are not options or option values."""
    out: list[str] = []
    skip = ended = False
    for a in args:
        if skip:
            skip = False
        elif ended or a == "-" or not a.startswith("-"):
            out.append(a)
        elif a == "--":
            ended = True
        elif a in valued:
            skip = True
    return out


def _sed_writes(args: list[Word]) -> bool:
    """Whether sed may write: in place, a script from a file, or a w, W or e command."""
    scripts: list[str] = []
    take_next = False
    for a in args:
        if take_next:
            scripts.append(a)
            take_next = False
        elif a.startswith("--"):
            if a.startswith(("--in-place", "--file")):
                return True
            if a.startswith("--expression"):
                name, eq, value = a.partition("=")
                if eq:
                    scripts.append(value)
                take_next = not eq
        elif a.startswith("-") and len(a) > 1:
            flags = a[1:]
            if "i" in flags or "f" in flags:
                return True
            if "e" in flags:
                rest = flags[flags.index("e") + 1 :]
                if rest:
                    scripts.append(rest)
                take_next = not rest
    if not scripts:
        scripts = _operands(args, {"-l"})[:1]
    for script in scripts:
        s = re.sub(r"\\.", "", script)
        s = re.sub(r"([sy])(.)(?:(?!\2).)*\2(?:(?!\2).)*\2", r"\1", s)  # s/re/new/ keeps flags
        s = re.sub(r"/[^/]*/", "", s)  # addresses
        if re.search(r"[wWe]", s):
            return True
    return False


def _bash_args_write(verb: str, args: list[Word]) -> bool:
    """Whether a read-only-listed command writes a file because of its arguments."""
    if verb == "find":
        return bool({a.lower() for a in args} & FIND_WRITE_FLAGS)
    if verb == "sort":  # -o FILE, -oFILE, -uo FILE, --output, --o; -T DIR; --compress-program
        return any(re.match(r"-[^-]*[oT]", a) or a.startswith(("--o", "--t", "--co")) for a in args)
    if verb == "uniq":  # uniq [INPUT [OUTPUT]]
        return len(_operands(args, UNIQ_VALUED)) > 1
    if verb == "xxd":  # xxd [INFILE [OUTFILE]], and -r patches OUTFILE
        return len(_operands(args, XXD_VALUED)) > 1
    if verb == "tree":  # -o FILE; -R writes 00Tree.html into every folder
        return any(re.match(r"-[^-]*[oR]", a) or a.startswith("--o") for a in args)
    if verb == "rg":
        return any(a.startswith("--pre") for a in args)
    if verb == "file":
        return any(re.match(r"-[^-]*C", a) or a.startswith("--comp") for a in args)
    if verb == "less":
        return any(re.match(r"-[^-]*[oO]", a) or a.lower().startswith("--log") for a in args)
    if verb == "sed":
        return _sed_writes(args)
    return False


def _segment_is_read_only(words: list[Word], powershell: bool) -> bool:
    if powershell:
        return _ps_segment_is_read_only(words)
    words = _strip_prefix(words, powershell)
    if not words:
        return True
    verb = _verb(words[0])
    return verb in READ_ONLY_SH and not _bash_args_write(verb, words[1:])


def _assigns(words: list[Word]) -> bool:
    return any("=" in w.bare or "++" in w.bare or w.bare.endswith("--") for w in words)


def _ps_segment_is_read_only(words: list[Word]) -> bool:
    called = words[:1] == ["&"]
    words = _strip_prefix(words, True)
    if not words:
        return True
    first = words[0]
    if called:
        # & $variable runs whatever it holds; & "path\tool.exe" is judged by its name.
        return not first.startswith("$") and _verb(first) in READ_ONLY_PS
    key = re.match(r"[A-Za-z_]\w*=", first.bare)
    if key and first.startswith(key.group()):  # a hashtable entry: @{n="KB"; e={...}}
        rest = _word(first[key.end() :], first.bare[key.end() :], first.start, first.end)
        if not rest and len(words) == 1:
            return True
        return _ps_segment_is_read_only(([rest] if rest else []) + words[1:])
    if (
        first.startswith("$")
        or first.bare == ""  # a quoted literal: "{0,-24}" -f $_.Name
        or re.fullmatch(r"-?[\d.,]+", first)  # a number or a list: (0, 4)
        or re.fullmatch(r"(?:\.[A-Za-z_]\w*|\[\d+\])+", first)  # (Get-Item x).Length
        or re.match(r"\[[\w.]+(?:\[\])?\]", first)  # [BitConverter]::ToUInt32, [int]$x
    ):
        return not _assigns(words)  # an expression: harmless unless it assigns
    verb = _verb(first)
    if verb in PS_FOREACH:
        return all(w.lower() in PS_SCRIPT_BLOCK_PARAMS for w in words[1:])
    return verb in READ_ONLY_PS


def _text_words(segments: list[list[Word]], powershell: bool) -> set[int]:
    """The ids of words that are message text to git or gh, not paths.

    A word that substitutes a command (``"$(rm …)"``, a backtick in Bash) is not text: the
    shell runs it. The ``"$(cat <<'EOF' … EOF)"`` form is.
    """
    out: set[int] = set()
    runs = re.compile(r"\$\(" if powershell else r"\$\(|`")
    for words in segments:
        words = _strip_prefix(words, powershell)
        if not words or _verb(words[0]) not in ("git", "gh"):
            continue
        git = _verb(words[0]) == "git"
        opts = GIT_TEXT_OPTS if git else GH_TEXT_OPTS
        take_next = False
        for a in words[1:]:
            if take_next:
                take_next = False
                is_text = True
            elif a in opts or (git and re.fullmatch(r"-[a-zA-Z]*m", a)):
                take_next = True
                continue
            else:
                is_text = any(a.startswith(o + "=") for o in opts if o.startswith("--")) or (
                    git and re.match(r"-m.", a) is not None
                )
            if is_text and not runs.search(_CAT_HEREDOC.sub("", a)):
                out.add(id(a))
    return out


def _dir_is_suspect(text: str, protected) -> bool:
    """A folder above or near a protected one (by name), or one spelled with a wildcard."""
    low = protected.normalize(text)
    if re.search(r"[*?\[]|~\d", low):
        return True
    app_id = str(protected.STEAM_APP_ID)
    return any("gallant" in c or "steam" in c or c in ("userdata", app_id) for c in low.split("/"))


def _changes_directory_to_suspect(segments: list[list[Word]], cwd: str, protected) -> bool:
    dirs = [cwd] if cwd else []
    for words in segments:
        words = _strip_prefix(words, False)
        if words and _verb(words[0]) in CD_VERBS:
            dirs.append(" ".join(words[1:]))
    return any(_dir_is_suspect(d, protected) for d in dirs)


def _resolves_protected(word: str, cwd: str, protected) -> bool:
    """Whether a path-like word leads into a protected folder once links are resolved."""
    if (
        word.startswith("-")
        or "://" in word
        or not re.search(r"[\\/]", word)
        or re.search(r"[*?\[\n]", word)
        or len(word) > 400
    ):
        return False
    path = protected._expand(word)
    if not (os.path.isabs(path) or re.match(r"[A-Za-z]:", path)):
        path = os.path.join(cwd or os.getcwd(), path)
    return protected.is_protected_path(path)


def _builds_from_pieces(text: str, words: list[Word], protected) -> bool:
    low = protected.normalize(text)
    app_id = str(protected.STEAM_APP_ID)
    parts = [[c for c in protected.normalize(w).split("/") if c] for w in words]
    if any(app_id in p for p in parts):
        return True
    if "gallant" not in low and "steamapps" not in low:
        return False
    for p in parts:
        for i, c in enumerate(p):
            before = p[i - 1] if i else ""
            variable = bool(re.search(r"[$%~]", before))
            if fnmatch.fnmatchcase("zexion", c) and (
                i == 0 or variable or before.endswith(("leaf", "common"))
            ):
                return True
            if "steamapps" in low and variable and fnmatch.fnmatchcase("common", c):
                return True
    return False


def _may_reach_protected(text, words, targets, segments, cwd: str, protected) -> bool:
    if protected.mentions_protected(text) or (cwd and protected.mentions_protected(cwd)):
        return True
    every = words + targets + ([_word(cwd, cwd, 0, 0)] if cwd else [])
    if any(protected.word_may_name_protected(w) for w in every):
        return True
    if _builds_from_pieces(text, words + targets, protected):
        return True
    if any(w.lower() in LINK_WORDS for w in words) and any(
        _dir_is_suspect(w, protected) for w in words
    ):
        return True
    if _changes_directory_to_suspect(segments, cwd, protected):
        return True
    return any(_resolves_protected(w, cwd, protected) for w in (words + targets)[:64])


def _assigned_names(code: str, segments: list[list[Word]]) -> set[str]:
    """Variables the command sets itself, which the hook's environment says nothing about."""
    names = re.findall(r"(?:^|[\s;&|(])(?:export\s+|local\s+)?([A-Za-z_]\w*)\+?=", code)
    names += re.findall(r"\bfor\s+([A-Za-z_]\w*)\s+in\b", code)
    names += re.findall(r"\$(?:env:)?(\w+)\s*(?:[-+*/%]|\?\?)?=(?!=)", code, re.I)
    names += re.findall(r"foreach\s*\(\s*\$(\w+)", code, re.I)
    for words in segments:
        if words and _verb(words[0]) == "read":
            names += [w for w in words[1:] if not w.startswith("-")]
    return {n.lower() for n in names}


def _expand_env(text: str, assigned: set[str]) -> str:
    def sub(m: re.Match) -> str:
        name = next(g for g in m.groups() if g)
        value = None if name.lower() in assigned else os.environ.get(name)
        return m.group(0) if value is None else value

    return _ENV_REF.sub(sub, text)


def _is_venv_tool(words: list[Word], powershell: bool) -> bool:
    words = _strip_prefix(words, powershell)
    return (
        len(words) >= 2
        and _VENV_PYTHON.fullmatch(words[0]) is not None
        and _TOOL_SCRIPT.fullmatch(words[1]) is not None
    )


def _is_repo_vcs(words: list[Word], text_ids: set[int], cwd: str, protected, ps: bool) -> bool:
    """A git or gh command whose non-message words name no protected or suspect folder."""
    words = _strip_prefix(words, ps)
    if not words or _verb(words[0]) not in ("git", "gh"):
        return False
    args = [a for a in words[1:] if id(a) not in text_ids]
    if _verb(words[0]) == "gh" and {a.lower() for a in args} & {"clone", "download"}:
        return False
    return not any(
        protected.mentions_protected(a)
        or protected.word_may_name_protected(a)
        or _dir_is_suspect(a, protected)
        or _resolves_protected(a, cwd, protected)
        for a in args
    )


def _ps_calls_block(code: str) -> str | None:
    """A reason when PowerShell code calls a method or a static member that may write."""
    unquoted = _PS_QUOTED.sub(" ", code)
    odd = _PS_ODD_METHOD_CALL.search(code)
    if odd:
        return odd.group(0)
    for call in _PS_METHOD_CALL.finditer(unquoted):
        if call.group(1).lower() not in PS_SAFE_METHODS:
            return call.group(0)

    def drop_reader(m: re.Match) -> str:
        members = PS_STATIC_READERS.get(m.group(1).lower(), set())
        member = m.group(2).lower()
        ok = members is None or any(fnmatch.fnmatchcase(member, p) for p in members)
        return "" if ok else m.group(0)

    rest = _PS_STATIC.sub(drop_reader, unquoted)
    return "::" if "::" in rest else None


def check(tool_name: str, tool_input: dict, protected, session_cwd: str = "") -> str | None:
    """Return a reason to block, or None to allow. ``protected`` is the tools/protected module.

    ``session_cwd`` is the payload's top-level ``cwd``: where a shell tool without its own
    ``cwd`` runs.
    """
    if tool_name in {"Write", "Edit", "MultiEdit", "NotebookEdit"}:
        path = tool_input.get("file_path") or tool_input.get("notebook_path") or ""
        if path and protected.is_protected_path(path):
            return f"{BLOCK_HINT}\nTarget: {path}"
        return None

    if tool_name not in SHELL_TOOLS:
        return None
    powershell = SHELL_TOOLS[tool_name]
    command = str(tool_input.get("command") or "")
    cwd = str(tool_input.get("cwd") or session_cwd or "")
    code = _here_strings_to_quotes(command) if powershell else _cut_heredocs(command)
    segments, targets = lex(code, powershell)
    text_ids = _text_words(segments, powershell)
    words = [w for s in segments for w in s if id(w) not in text_ids]
    text = code
    for w in sorted((w for s in segments for w in s if id(w) in text_ids), key=lambda w: -w.start):
        text = text[: w.start] + " " * (w.end - w.start) + text[w.end :]
    if not _may_reach_protected(text, words, targets, segments, cwd, protected):
        return None

    substitution = _CAT_HEREDOC.sub("heredoc", code)
    if "$(" in substitution or (not powershell and "`" in substitution):
        return f"{BLOCK_HINT}\nCommand substitution next to a protected path."
    if powershell and (call := _ps_calls_block(code)):
        return f"{BLOCK_HINT}\nA method or .NET call next to a protected path: {call}"

    assigned = _assigned_names(code, segments)
    for target in targets:
        if target.startswith("&") or target.lower() in HARMLESS_TARGETS:
            continue
        expanded = protected.normalize(_expand_env(target, assigned))
        if (
            protected.is_protected_path(expanded)
            or protected.word_may_name_protected(expanded)
            or _resolves_protected(expanded, cwd, protected)
            or "$" in expanded
            or "%" in expanded
        ):
            return f"{BLOCK_HINT}\nRedirect target: {target}"

    suspect_dir = _changes_directory_to_suspect(segments, cwd, protected)
    for words in segments:
        if _segment_is_read_only(words, powershell):
            continue
        if not suspect_dir and (
            _is_venv_tool(words, powershell)
            or _is_repo_vcs(words, text_ids, cwd, protected, powershell)
        ):
            continue
        shown = " ".join(w for w in words if id(w) not in text_ids)
        return f"{BLOCK_HINT}\nNot a read-only command: {shown}"
    return None


def _mentions_sensitive(text: str) -> bool:
    low = text.lower()
    return any(s in low for s in ("gallant", "steamapps", "3392510"))


def main() -> int:
    raw = sys.stdin.read()
    # Judge a failure by the tool's own input only. The rest of the payload carries session
    # paths (the transcript lives under AppData) that would make every failure block everything.
    try:
        data = json.loads(raw)
        subject = json.dumps(data.get("tool_input") or {})
    except Exception:
        data, subject = None, raw  # data None makes the check below fail
    try:
        sys.path.insert(0, str(_TOOLS_DIR))
        import protected

        reason = check(
            data.get("tool_name", ""),
            data.get("tool_input") or {},
            protected,
            session_cwd=str(data.get("cwd") or ""),
        )
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
