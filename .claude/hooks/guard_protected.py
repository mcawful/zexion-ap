"""PreToolUse hook: block writes into the game install, the real saves, and Steam's cloud cache.

Claude Code sends the tool call as JSON on stdin. Exit 2 blocks it and shows stderr to Claude.

- Write, Edit, MultiEdit, NotebookEdit: blocked when the target file is in a protected folder.
- Shell commands: the ``command`` of Bash and Monitor (read as Bash), and of PowerShell and the
  terminal tool ``mcp__terminal__run_in_terminal`` (read as PowerShell). Other tools pass.
  The command runs in the tool's ``cwd`` if it has one, else in the payload's own ``cwd`` (the
  session's working folder, which a ``cd`` in an earlier call may have moved).
  A command that may reach a protected folder is allowed only when every simple command in it
  is recognizably read-only, a ``.venv`` Python run of a script in ``tools/`` (those scripts
  enforce their own rules through ``tools/protected.py``; after a ``cd``, only when every
  ``cd`` goes to a checkout of this repo), or a ``git`` or ``gh`` command that names no
  protected folder or parent of one, and no output is redirected into a protected folder.

  - Text that is only data is left out, so commit messages and PR text may name the folders:
    a PowerShell here-string's quotes; the message values of ``git`` and ``gh`` (``-m``,
    ``--title``, ``--body`` and the like: ``_text_words``); and the body of a Bash heredoc
    that nothing can run (``_heredoc_is_data``): its reader and any command it pipes into are
    data readers (``_data_reader``: ``cat``, ``grep``, … that write no file; ``git commit -F
    -``; ``gh`` but not ``gh alias`` or ``extension``), nothing else follows the ``<<`` on its
    line, no parenthesis or substitution is around it but a git or gh message value (the
    ``"$(cat <<'EOF' … EOF)"`` form), and a file it is saved to is not run later in the
    command (``_saved_and_used``: ``git commit -F x`` and ``wc x`` are fine, ``sh x``, ``.
    x``, ``cat x | sh`` and ``cp x y && sh y`` are not). Heredocs are found as Bash finds them
    (``_heredocs``). Every other body is judged as commands.
  - It *may reach* one (``_may_reach_protected``) when its text names one, also with quotes
    removed and pieces joined (``protected.mentions_protected``); when a word could name one as
    a path (``..``, wildcards, 8.3 short names: ``protected.word_may_name_protected``), leads
    into one from where the command runs, also through an existing link, or names a parent of
    one (``"$APPDATA/Gallant Leaf"``, ``…/steamapps/common``, ``…/userdata/123``:
    ``protected.may_be_above_protected``, ``_names_parent``), so copying into, moving or
    deleting the parent blocks while reading it does not; when it may build one from pieces
    (``_builds_from_pieces``; the app id counts as a path component, but not in a URL such as
    ``steam://rungameid/3392510`` or as a number in code: ``_names_app_id``); when it makes a
    link next to a folder named like ``gallant`` or ``steam``; or when it runs in or changes
    directory to a folder in a protected one or a parent of one. Other names in the folder's
    path don't count, so a worktree named ``steam-achievements`` works as any other.
  - *Read-only* means the verb is on a short list and nothing in its arguments makes it write
    (``_bash_args_write``: ``find -delete``, ``sort -o``, ``sed -i`` and the like); ``git
    diff``, ``log`` and ``show`` count unless an argument starts with ``--output``,
    ``--ext-diff``, ``--exec-path``, ``--config-env`` or ``-c``. PowerShell's rules for script
    blocks, ``ForEach-Object``, expressions and ``&``: ``_ps_segment_is_read_only``.
  - Blocked outright, when the command may reach a protected folder: command substitution
    (``$(``, or a backtick in Bash) but the message form above, Bash arithmetic that only
    computes (``xxd -s $((0x9360))``: ``_arithmetic_to_numbers``) and PowerShell's
    ``"$($_.Name)"``, which only reads a member; a PowerShell ``::`` call other
    than a known reader or a method call other than a few string ones (``_ps_calls_block``);
    output redirected to a path that still holds a variable after the hook's own environment is
    applied (the guard can't see where it leads); and in Bash an environment prefix or
    ``export`` of a variable that names a program (``_PROGRAM_VAR``: ``GIT_PAGER``,
    ``LESSOPEN``, ``PATH``, …), even before a read-only command.

This is a tripwire against mistakes, not a sandbox. It can't see a path assembled entirely from
variables or string arithmetic, a link to a parent folder made outside the command, the
folder behind a ``cd`` into a variable, a Steam library folder by its own name (only its
``steamapps`` and what is below), or what a script file does when it runs (the file's
contents are not read). A scratch script written by a heredoc and run in the same command is
code, like ``python - <<'EOF'``, so it is blocked when it names the saves, even to read them
or in a comment; but the run is tied to the heredoc only by the file's name, so ``sh *.sh``
or a variable holding the name is missed, as is a program variable that is not on the list,
set with ``env`` before a command that is otherwise allowed, or set in PowerShell
(``$env:GIT_PAGER = …``). A ``tools/`` script is trusted in the session's own ``cwd``
without checking that it is a checkout of this repo. The joined reading may take
prose such as ``print('Gallant Leaf, Zexion')`` for the folder; that blocks only in a
command that is not read-only. It fails closed: if the guard itself breaks while looking at a
call that mentions the saves or a Steam folder, the call is blocked.
"""

from __future__ import annotations

import contextlib
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
    "convertto-csv", "get-date",
}  # fmt: skip
# Readers whose stdin is only data (not a shell, an interpreter, or sed's script) and that write
# no file a later command could run: a heredoc may go to one, and pipe on into more.
DATA_READERS = {
    "cat", "head", "tail", "grep", "egrep", "fgrep", "rg", "wc", "sort", "uniq", "cut", "tr",
    "base64", "xxd", "od", "hexdump", "diff", "cmp", "comm", "nl", "tac", "rev", "column",
    "fold", "paste", "less", "more", "strings",
}  # fmt: skip
# gh subcommands whose stdin or file argument is a script to run.
GH_RUN_SUBCOMMANDS = {"alias", "extension", "ext"}
# Verbs that move or remove a file a heredoc saved, but never run it. The copy verbs give it a
# new name, which is followed too.
COPY_VERBS = {"mv", "cp", "ln", "install"}
FILE_VERBS = COPY_VERBS | {"rm", "touch", "chmod", "mkdir", "unlink"}
# gh options whose value is key=value text.
GH_FIELD_OPTS = {"-f", "--raw-field", "-F", "--field"}
# Environment variables that name a program for a command to run (GIT_PAGER, LESSOPEN, …): a
# prefix setting one makes even a read-only command run something.
_PROGRAM_VAR = re.compile(
    r"(?:[A-Z_]*_)?(?:PAGER|EDITOR|VISUAL|DIFF|COMMAND|SSH|ASKPASS|PROGRAM|SHELL|STARTUP)"
    r"|PYTHONSTARTUP"
    r"|LESSOPEN|LESSCLOSE|BASH_ENV|ENV|GIT_CONFIG\w*|GIT_EXEC_PATH|LD_PRELOAD|PAGER|EDITOR"
    r"|PATH|BROWSER",
    re.I,
)
# ForEach-Object counts as read-only only with script blocks: a bare word is a method to call.
PS_FOREACH = {"foreach", "%", "foreach-object"}
PS_SCRIPT_BLOCK_PARAMS = {"-process", "-begin", "-end", "-parallel"}
# Method calls that only read. Any other ``.Name(`` (``.Delete()``) is blocked.
PS_SAFE_METHODS = {
    "tostring", "trim", "trimstart", "trimend", "substring", "split", "tolower", "toupper",
    "startswith", "endswith", "contains", "indexof", "gettype", "padleft", "padright", "equals",
    "getstring", "getbytes",
    "addseconds", "addminutes", "addhours", "adddays",  # DateTime's; no file object has them
}  # fmt: skip
# Operators that continue an expression after a parenthesis: ``(Get-Date) - $t``. After one,
# PowerShell reads a value, never a command, so the rest is an expression too.
PS_OPERATORS = {"+", "-", "*", "/", "%", "-eq", "-ne", "-gt", "-ge", "-lt", "-le"}
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
# Options of git and gh whose value is text, not a path. For git, only in the subcommands that
# take a message (in ``git log -m`` and ``git revert -m`` it is something else).
GIT_TEXT_OPTS = {"-m", "--message"}
GIT_MESSAGE_SUBCOMMANDS = {"commit", "merge", "tag", "stash", "notes", "commit-tree"}
# git subcommands that only read, record or post a file (git bisect run, rebase -x run one).
GIT_FILE_SUBCOMMANDS = GIT_MESSAGE_SUBCOMMANDS | {
    "add", "rm", "mv", "diff", "log", "show", "status", "restore", "blame", "grep", "ls-files",
    "hash-object", "check-ignore",
}  # fmt: skip
# git's global options that take the next word as their value.
GIT_VALUED_GLOBALS = {"-C", "-c", "--git-dir", "--work-tree", "--namespace", "--config-env"}
# git subcommands that only read, unless told to write a file (``--output``) or to run a
# program (``-c``, ``--config-env``, ``--ext-diff``).
GIT_READERS = {"diff", "log", "show"}
GIT_READER_WRITE_FLAGS = ("--output", "-c", "--config-env", "--ext-diff", "--exec-path")
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
# A heredoc operator and its delimiter word (quotes and backslashes not yet removed).
_DELIM_WORD = r"(?:[^\s;&|<>()'\"\\]|\\.|'[^'\n]*'|\"[^\"\n]*\")+"
_HEREDOC = re.compile(rf"<<(-?)[ \t]*({_DELIM_WORD})")
_CAT_HEREDOC = re.compile(rf"\$\(\s*cat\s*<<-?[ \t]*{_DELIM_WORD}[ \t]*\n\s*\)")
# A ``>&`` that duplicates or closes a descriptor (2>&1, >&-) rather than naming a file.
_FD_DUP = re.compile(r"\d*-?(?=$|[\s;&|()<>])")
# A URL, up to a space or a quote: at the start of a word or a quoted piece of one (not
# ``$d/x://``), with a scheme of two letters or more (``C://x`` is a path), and no ``..``
# (Windows reads ``x://../y`` as ``y``).
_URL = re.compile(r"(?<![^\s\"'(=,])[a-z][\w+.-]+://(?![^\s\"']*\.\.)[^\s\"']*", re.I)
# A Bash ``$((`` and a PowerShell ``$( $name.Member )``: neither runs a command.
_ARITH_START = re.compile(r"(?<!\\)\$\(\(")
_ARITH_BODY = re.compile(r"[\w \t+\-*/%<>=!&|^~?:,.\[\]$()]*")
_PS_MEMBER_SUBEXPR = re.compile(r"\$\(\s*\$(?:env:)?\w+(?:\.\w+)*\s*\)", re.I)
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


class Heredoc:
    """A Bash heredoc: its ``<<`` operator, its body, and the parenthesis it sits in."""

    def __init__(self, op: re.Match, body_start: int, body_end: int, after: int, opener) -> None:
        self.op = op
        self.body_start, self.body_end = body_start, body_end
        self.after = after  # the end of the delimiter line
        # The innermost ``(``, ``$(``, ``<(``, ``>(``, ``$((`` or backtick around the operator,
        # as (kind, where it starts, where it ends), or None at the top level.
        self.opener: tuple[str, int, int] | None = opener
        token = op.group(2)
        self.expands = not re.search(r"['\"\\]", token)  # an unquoted delimiter expands $(…)
        self.delim = re.sub(r"\\(.)|'([^']*)'|\"([^\"]*)\"", lambda m: "".join(m.groups("")), token)


def _heredocs(command: str) -> list[Heredoc]:
    """The Bash heredocs in ``command``, found as Bash finds them.

    A ``<<`` inside quotes or a comment is text, and ``<<`` inside ``$((…))`` is a shift. The
    delimiter is the whole shell word after ``<<`` (``EOF.x``), quotes removed. A body runs from
    the line after its operator to the delimiter line, or to the end of the command.
    """
    found: list[Heredoc] = []
    pending: list[tuple[re.Match, tuple | None]] = []
    stack: list[tuple[str, int, int]] = []  # quotes, parentheses and backticks we are inside
    i, n = 0, len(command)
    while i < n:
        c = command[i]
        top = stack[-1][0] if stack else ""
        if top == "'":
            if c == "'":
                stack.pop()
            i += 1
            continue
        if c == "\\":
            i += 2
            continue
        if top == '"':
            if c == '"':
                stack.pop()
            elif command.startswith("$(", i):
                kind = "$((" if command.startswith("$((", i) else "$("
                stack.append((kind, i, i + len(kind)))
                i += len(kind)
                continue
            elif c == "`":
                stack.append(("`", i, i + 1))
            i += 1
            continue
        arithmetic = any(k in ("$((", "((") for k, _, _ in stack)
        if c in "'\"":
            stack.append((c, i, i + 1))
        elif c == "`":
            if top == "`":
                stack.pop()
            else:
                stack.append(("`", i, i + 1))
        elif command.startswith("((", i) or command.startswith("$((", i):
            kind = "$((" if c == "$" else "(("
            stack.append((kind, i, i + len(kind)))
            i += len(kind)
            continue
        elif c == ")" and top in ("$((", "((") and command.startswith("))", i):
            stack.pop()
            i += 2
            continue
        elif c == "(":
            kind = command[i - 1] + "(" if i and command[i - 1] in "$<>" else "("
            stack.append((kind, i - len(kind) + 1, i + 1))
        elif c == ")":
            if top.endswith("("):
                stack.pop()
        elif c == "#" and (i == 0 or command[i - 1] in " \t\n;&|()"):
            newline = command.find("\n", i)
            i = n if newline < 0 else newline
            continue
        elif command.startswith("<<<", i):
            i += 3
            continue
        elif command.startswith("<<", i):
            op = _HEREDOC.match(command, i)
            if op and not arithmetic:
                pending.append((op, stack[-1] if stack else None))
                i = op.end()
                continue
            i += 2
            continue
        elif c == "\n" and pending:
            line_start = i + 1
            for op, opener in pending:
                strip = bool(op.group(1))
                doc = Heredoc(op, line_start, n, n, opener)
                while line_start < n:
                    line_end = command.find("\n", line_start)
                    line_end = n if line_end < 0 else line_end
                    line = command[line_start:line_end].rstrip("\r")
                    if (line.lstrip("\t") if strip else line) == doc.delim:
                        doc.body_end, doc.after = line_start, line_end
                        line_start = line_end + 1
                        break
                    line_start = line_end + 1
                found.append(doc)
            pending = []
            i = found[-1].after
            continue
        i += 1
    return found


def _git_subcommand(words: list[str]) -> str:
    """The subcommand of a ``git`` command (its words, ``git`` first), past global options."""
    skip = False
    for w in words[1:]:
        if skip:
            skip = False
        elif w in GIT_VALUED_GLOBALS:
            skip = True
        elif not w.startswith("-"):
            return w.lower()
    return ""


def _is_message_option(words: list[str], option: str, sub: str | None = None) -> bool:
    """Whether ``option`` of the git or gh command ``words`` takes message text. ``sub``: the
    git subcommand, when the caller has it."""
    verb = _verb(words[0]) if words else ""
    if verb == "gh":
        return option in GH_TEXT_OPTS
    sub = _git_subcommand(words) if sub is None and verb == "git" else sub
    if verb == "git" and sub in GIT_MESSAGE_SUBCOMMANDS:
        return option in GIT_TEXT_OPTS or re.fullmatch(r"-[a-zA-Z]*m", option) is not None
    return False


def _in_message(command: str, at: int) -> bool:
    """Whether the ``$(`` at ``at`` is the value of a git or gh message option."""
    head = command[:at]
    start = max(head.rfind(c) for c in ";&|\n(") + 1
    segments, _ = lex(head[start:], False)
    words = _strip_prefix(segments[-1], False) if segments else []
    if not words or len(words) < 2:
        return False
    last = words[-1]
    if last == "":  # -m "$(…: the quote opened a new word
        return _is_message_option(words[:-1], words[-2])
    if last.endswith("="):  # --body="$(…, or gh api -f body="$(…
        if _verb(words[0]) == "gh" and words[-2] in GH_FIELD_OPTS:
            return re.fullmatch(r"[\w.\[\]-]+=", last) is not None
        return _is_message_option(words, last[:-1])
    return _is_message_option(words, last)  # -m $(… or -m"$(…


def _subcommand(words: list[str]) -> str:
    """The subcommand of a git or gh command (its words, the verb first)."""
    if _verb(words[0]) == "git":
        return _git_subcommand(words)
    return next((w.lower() for w in words[1:] if not w.startswith("-")), "")


def _handles_without_running(words: list[str], powershell: bool = False) -> bool:
    """Whether a command only reads, moves or posts the files it names, never runs them:
    read-only, a file verb such as ``rm``, ``git`` (not ``apply`` or ``am``), or ``gh`` (not
    ``alias`` or ``extension``)."""
    words = _strip_prefix(words, powershell)
    if not words:
        return True
    verb = _verb(words[0])
    if verb == "git":  # not git bisect run, rebase -x, or an alias given with -c
        return _subcommand(words) in GIT_FILE_SUBCOMMANDS and not any(
            w.startswith(("-c", "--config-env")) for w in words[1:]
        )
    if verb == "gh":
        return _subcommand(words) not in GH_RUN_SUBCOMMANDS
    return verb in FILE_VERBS or _segment_is_read_only(words, powershell)


def _base_name(path: str) -> str:
    return re.split(r"[\\/]", path.strip("\"'").rstrip("\\/"))[-1]


def _saved_and_used(command: str, doc: Heredoc, piece: str, reader: str) -> bool:
    """Whether a heredoc's reader saves it to a file that a later command may run.

    A later command may run it when it names the file (any case) and does not only handle it
    (``_handles_without_running``: ``wc``, ``git commit -F``, ``gh pr create --body-file``,
    ``rm``), names it inside a substitution, or pipes it into a command that does not.
    """
    rest = command[doc.op.end() : command.find("\n", doc.op.end())]
    files = re.findall(r"\d?>>?\|?[ \t]*(\"[^\"]*\"|'[^']*'|[^\s;&|<>()]+)", piece + " " + rest)
    if reader == "tee":
        files += [w for w in piece.split()[1:] if not w.startswith("-")]
    names = {_base_name(f) for f in files} - {""}
    names = {n for n in names if n.lower() not in HARMLESS_TARGETS}
    if not names:
        return False

    def pattern(names: set[str]) -> re.Pattern:
        alts = "|".join(rf"(?<![\w.-]){re.escape(n)}(?![\w.-])" for n in sorted(names))
        return re.compile(alts, re.IGNORECASE)

    named = pattern(names)
    later = command[doc.after :]
    if not named.search(later):
        return False
    segments, _ = lex(later, False)
    for k, words in enumerate(segments):
        mentions = [w for w in words if named.search(w)]
        if not mentions:
            continue
        stripped = _strip_prefix(words, False)
        if stripped and _verb(stripped[0]) in COPY_VERBS:  # cp x.sh y.sh: follow y.sh too
            names |= {_base_name(w) for w in stripped[1:] if not w.startswith("-")} - {""}
            named = pattern(names)
        if not _handles_without_running(words) or any(re.search(r"\$\(|`", w) for w in mentions):
            return True
        while k + 1 < len(segments):  # piped on: cat x.sh | sh
            gap = later[segments[k][-1].end : segments[k + 1][0].start]
            if not re.search(r"(?<!\|)\|(?!\|)", gap):
                break
            k += 1
            if not _handles_without_running(segments[k]):
                return True
    return False


def _data_reader(text: str, first: bool) -> str:
    """The verb of a command that takes a heredoc (``first``) or a pipe from one as data, or "".

    That is a reader in ``DATA_READERS`` whose arguments write no file (not ``sort -o x.sh``,
    ``uniq - x.sh`` or ``rg --pre``); ``tee`` only first (``_saved_and_used`` follows its
    files); ``git`` with a subcommand that takes a message (``commit -F -``; ``apply`` reads a
    patch); ``gh`` but not ``gh alias`` or ``extension`` (a script).
    """
    words = text.split()
    while words and re.fullmatch(r"[A-Za-z_]\w*=\S*", words[0]):
        words.pop(0)
    verb = _verb(words[0]) if words else ""
    if verb == "git":
        ok = _git_subcommand(words) in GIT_MESSAGE_SUBCOMMANDS
    elif verb == "gh":
        ok = _subcommand(words) not in GH_RUN_SUBCOMMANDS
    else:
        reader = verb in DATA_READERS or (first and verb == "tee")
        ok = reader and not _bash_args_write(verb, words[1:])
    return verb if ok else ""


def _heredoc_is_data(command: str, doc: Heredoc) -> bool:
    """Whether nothing can run a heredoc's body: it goes to a data reader and stays data.

    The reader (the command the ``<<`` belongs to) and every command it pipes into must be data
    readers (``_data_reader``). Its output must go only to the terminal, a file or such a pipe:
    no ``||``, ``|&``, ``&``, ``;``, backtick, ``<(``, ``>(``, ``$(`` or trailing ``\\``
    after the operator on its line (a descriptor copy such as ``2>&1`` is fine); no ``(``,
    ``<(``, ``>(`` or backtick around it; around a ``$(``, only as a git or gh message value;
    and a file it is saved to is not run later in the command (``_saved_and_used``). A body
    under an unquoted delimiter that substitutes a command (``$(`` or a backtick) is code too.
    """
    op = doc.op
    lo = doc.opener[2] if doc.opener else 0
    head = command[lo : op.start()]
    piece = head[max(head.rfind(c) for c in ";&|\n()") + 1 :]
    reader = _data_reader(piece, first=True)
    if not reader:
        return False
    body = command[doc.body_start : doc.body_end]
    if doc.expands and ("$(" in body or "`" in body):
        return False
    line_end = command.find("\n", op.end())
    rest = re.sub(r"\d*>&(?:\d+|-)|&>>?", " ", command[op.end() : line_end])
    if re.search(r"[&;`]|\|\||\|&|[<>]\(|\$\(|\\\s*$", rest):
        return False
    if not all(_data_reader(stage, first=False) for stage in rest.split("|")[1:]):
        return False
    if doc.opener and (doc.opener[0] != "$(" or not _in_message(command, doc.opener[1])):
        return False
    return not _saved_and_used(command, doc, piece, reader)


def _cut_heredocs(command: str) -> str:
    """The command without the bodies of Bash heredocs that are only data
    (``_heredoc_is_data``). Every other body stays in, to be judged as code."""
    out: list[str] = []
    pos = 0
    for doc in _heredocs(command):
        if _heredoc_is_data(command, doc):
            out.append(command[pos : doc.body_start])
            pos = doc.after
    out.append(command[pos:])
    return "".join(out)


def _arithmetic_to_numbers(command: str, protected) -> str:
    """Bash ``$((…))`` expansions that only compute (``xxd -s $((0x9360))``) as the word ``0``,
    padded to the same length, so they read as a number and not as command substitution.

    Only when the body has numbers, names, ``$name`` and operators alone (no ``$(``, backtick,
    quote, brace, ``;`` or newline), and closes as Bash closes arithmetic: on a ``))`` where
    its own parentheses balance. Bash reads ``$((a) | (b))`` as ``$(`` and two subshells, so
    that one stays. So does a body holding the app id, a piece of a path.
    """
    out: list[str] = []
    pos = 0
    for m in _ARITH_START.finditer(command):
        if m.start() < pos:
            continue
        depth, i, end = 0, m.end(), None
        while i < len(command):
            if command[i] == "(":
                depth += 1
            elif command[i] == ")":
                if depth == 0:
                    end = i + 2 if command.startswith("))", i) else None
                    break
                depth -= 1
            i += 1
        body = command[m.end() : i]
        if (
            end is None
            or not _ARITH_BODY.fullmatch(body)
            or "$(" in body
            or str(protected.STEAM_APP_ID) in body
        ):
            continue
        out.append(command[pos : m.start()] + "0".ljust(end - m.start()))
        pos = end
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


def _git_reads(words: list[Word]) -> bool:
    """``git diff``, ``log`` or ``show`` with nothing that makes it write a file or run one."""
    return (
        _verb(words[0]) == "git"
        and _git_subcommand(words) in GIT_READERS
        and not any(w.startswith(GIT_READER_WRITE_FLAGS) for w in words[1:])
    )


def _segment_is_read_only(words: list[Word], powershell: bool) -> bool:
    stripped = _strip_prefix(words, powershell)
    if not stripped or _git_reads(stripped):
        return True
    if powershell:
        return _ps_segment_is_read_only(words)
    verb = _verb(stripped[0])
    return verb in READ_ONLY_SH and not _bash_args_write(verb, stripped[1:])


def _sets_program(words: list[Word]) -> str:
    """A ``NAME=value`` prefix of a Bash command, or an ``export`` of one, that names a program
    for a command to run (``GIT_PAGER=…``, ``export LESSOPEN=…``), or ""."""
    if words and words[0] in ("export", "declare", "readonly", "local", "typeset"):
        words = [w for w in words[1:] if not w.startswith("-")]
    for w in words:
        m = re.match(r"([A-Za-z_]\w*)=", w)
        if not m:
            break
        if _PROGRAM_VAR.fullmatch(m.group(1)):
            return m.group(1)
    return ""


def _assigns(words: list[Word]) -> bool:
    return any("=" in w.bare or "++" in w.bare or w.bare.endswith("--") for w in words)


def _ps_segment_is_read_only(words: list[Word]) -> bool:
    """A read-only verb (a script block's contents are checked as commands of their own);
    ``ForEach-Object`` (``foreach``, ``%``) only with script blocks (``| foreach Delete`` calls
    a method); an expression (it starts with a variable, a literal, a ``[Type]``, a member of a
    parenthesis, or a hashtable's ``key=``) unless it assigns; after an operator that
    continues an expression (``(Get-Date) - $t``: ``PS_OPERATORS``), what follows it; never
    ``& $variable``."""
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
    if first.bare.lower() in PS_OPERATORS:  # (Get-Date) - $_.LastWriteTime: a value follows
        return _ps_segment_is_read_only(words[1:])
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
        sub = _git_subcommand(words) if git else ""
        if git and sub not in GIT_MESSAGE_SUBCOMMANDS:
            continue
        opts = GIT_TEXT_OPTS if git else GH_TEXT_OPTS
        take_next = False
        for a in words[1:]:
            if take_next:
                take_next = False
                is_text = True
            elif _is_message_option(words, a, sub):
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
    """A folder near a protected one by name (for links), or one spelled with a wildcard."""
    low = protected.normalize(text)
    if re.search(r"[*?\[]|~\d", low):
        return True
    app_id = str(protected.STEAM_APP_ID)
    return any("gallant" in c or "steam" in c or c in ("userdata", app_id) for c in low.split("/"))


def _is_absolute(path: str) -> bool:
    return os.path.isabs(path) or re.match(r"[A-Za-z]:", path) is not None


def _cd_targets(segments: list[list[Word]], cwd: str, code: str, protected) -> list[str | None]:
    """The folders the command's ``cd``s go to, resolved against ``cwd`` once variables the
    hook's environment knows are expanded. None: a target that still holds a variable, so its
    folder is unknown."""
    out: list[str | None] = []
    base = cwd or os.getcwd()
    assigned = _assigned_names(code, segments)
    for words in segments:
        words = _strip_prefix(words, False)
        if not words or _verb(words[0]) not in CD_VERBS:
            continue
        args = [w for w in words[1:] if not w.startswith("-")]
        if not args:
            continue
        target = _expand_env(protected._expand(args[0]), assigned)
        if re.search(r"[$%]", target):
            out.append(None)
        else:
            out.append(target if _is_absolute(target) else os.path.join(base, target))
    return out


def _folder_is_suspect(folder: str | None, protected) -> bool:
    """A folder in a protected one, or one whose last names could hold one (``Gallant Leaf``,
    ``steamapps``, …: ``protected.may_be_above_protected``). Not judged by other names in it,
    so a worktree named ``steam-achievements`` or a user named Steam is fine."""
    return folder is not None and (
        protected.is_protected_path(folder) or protected.may_be_above_protected(folder)
    )


def _as_paths(word: str, folders: list[str | None], protected) -> list[str]:
    """The paths a word may stand for: itself when absolute, else read from each of ``folders``
    (the hook's own when none is known). None for an option, a URL or a block of text."""
    if word.startswith("-") or "://" in word or "\n" in word or len(word) > 400 or not word:
        return []
    path = protected._expand(word)
    if _is_absolute(path):
        return [path] if re.search(r"[\\/]", path) else []
    bases = [f for f in folders if f is not None] or [os.getcwd()]
    return [os.path.join(b, path) for b in bases]


def _reaches_protected(word: str, folders: list[str | None], protected) -> bool:
    """Whether a word, read as a path, leads into a protected folder, also through a link."""
    return any(protected.is_protected_path(p) for p in _as_paths(word, folders, protected))


def _names_parent(word: str, folders: list[str | None], protected) -> bool:
    """Whether a word names a parent of a protected folder (``"$APPDATA/Gallant Leaf"``,
    ``…/steamapps/common``, ``rm -r "Gallant Leaf"`` or ``Gall*`` in Roaming): copying into,
    moving or deleting it reaches the folder. A bare ``steamapps`` or ``userdata`` counts only
    when that folder exists where the command runs (it may be a test name)."""
    word = re.sub(r"^--?[\w-]+=", "", word)  # --target-directory=…
    bare = word.lower() in ("steamapps", "userdata")
    return any(
        protected.may_be_above_protected(p) and (not bare or os.path.isdir(p))
        for p in _as_paths(word, folders, protected)
    )


def _repo_checkout(folder: str | None) -> bool:
    """Whether a folder is a checkout of this repo, so its ``tools/`` are the repo's tools."""
    return folder is not None and os.path.isfile(os.path.join(folder, "tools", "protected.py"))


def _names_app_id(text: str, words: list[Word], protected) -> bool:
    """Whether a word holds the app id as a path component (the cloud cache is
    ``userdata/<account>/3392510``), split as ``_builds_from_pieces`` splits words.

    URLs are left out (``steam://rungameid/3392510``, ``…/app/3392510/``). So is the id
    multiplied by a number inside one word, as code builds a location id (``python -c
    "print(3392510 * 1000 + 154)"``), with no slash or quote right next to it, unless the
    command, URLs left out, names ``steam``, ``userdata`` or ``compatdata``. Any other use
    counts: ``str(3392510)``, ``d + 3392510``, ``rm -r 3392510 && …`` in a ``sh -c`` string.
    """
    app_id = str(protected.STEAM_APP_ID)
    steam = re.search(r"steam|userdata|compatdata", protected.normalize(_URL.sub(" ", text)))
    for w in words:
        norm = protected.normalize(_URL.sub(" ", w))
        for m in re.finditer(rf"(?<![^\"'\s,+()/]){app_id}(?![^\"'\s,+()/])", norm):
            near = norm[m.start() - 1 : m.start()] + norm[m.end() : m.end() + 1]
            product = re.match(r"[ \t]*\*[ \t]*\d", norm[m.end() :]) or re.search(
                r"\d[ \t]*\*[ \t]*$", norm[: m.start()]
            )
            if steam or not product or re.search(r"[/\"']", near):
                return True
    return False


def _builds_from_pieces(text: str, words: list[Word], protected) -> bool:
    """Whether words may build a protected path from pieces: the app id as a path component
    (``_names_app_id``); or, in a command that mentions ``gallant`` or ``steamapps``, a
    ``Zexion`` piece that starts a word or a quoted or comma-separated piece of one, or follows
    a variable or a ``…Leaf`` or ``…common`` piece (``base = join(…, 'Gallant Leaf');
    join(base, 'Zexion')``), or a ``common`` component after a variable."""
    low = protected.normalize(text)
    if _names_app_id(text, words, protected):
        return True
    # Each word's pieces, split at quotes, commas, plus signs, parentheses and spaces as well
    # (code: join(base, 'Zexion', …)), then each piece's path components.
    pieces = [s for w in words for s in re.split(r"[\"'\s,+()]+", protected.normalize(w))]
    parts = [[c for c in s.split("/") if c] for s in pieces if s]
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


def _may_reach_protected(text, words, targets, folders, protected) -> bool:
    """``folders``: where the command runs (the ``cwd``) and its ``cd`` targets."""
    every = words + targets
    links = any(w.lower() in LINK_WORDS for w in words)
    return (
        protected.mentions_protected(text)
        or any(_folder_is_suspect(f, protected) for f in folders)
        or any(protected.word_may_name_protected(w) for w in every)
        or _builds_from_pieces(text, every, protected)
        or (links and any(_dir_is_suspect(w, protected) for w in words))
        or any(
            _reaches_protected(w, folders, protected) or _names_parent(w, folders, protected)
            for w in every[:64]
        )
    )


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


def _is_repo_vcs(words: list[Word], text_ids: set[int], folders, protected, ps: bool) -> bool:
    """A git or gh command whose non-message words name no protected folder or a parent of
    one."""
    words = _strip_prefix(words, ps)
    if not words or _verb(words[0]) not in ("git", "gh"):
        return False
    args = [a for a in words[1:] if id(a) not in text_ids]
    if _verb(words[0]) == "gh" and {a.lower() for a in args} & {"clone", "download"}:
        return False
    return not any(
        protected.mentions_protected(a)
        or protected.word_may_name_protected(a)
        or protected.may_be_above_protected(a)
        or _reaches_protected(a, folders, protected)
        for a in args
    )


def _ps_calls_block(code: str) -> str | None:
    """A reason when PowerShell code calls a method or a static member that may write: a ``::``
    call other than a known reader (``[IO.File]::ReadAllBytes``, ``[BitConverter]``, ``[Math]``),
    or a method outside quotes other than a few string ones (``.Delete()``, ``.'Delete'()``,
    ``.$name()``)."""
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
    if powershell:
        code = _here_strings_to_quotes(command)
    else:
        code = _arithmetic_to_numbers(_cut_heredocs(command), protected)
    segments, targets = lex(code, powershell)
    text_ids = _text_words(segments, powershell)
    words = [w for s in segments for w in s if id(w) not in text_ids]
    text = code
    for w in sorted((w for s in segments for w in s if id(w) in text_ids), key=lambda w: -w.start):
        text = text[: w.start] + " " * (w.end - w.start) + text[w.end :]
    cds = _cd_targets(segments, cwd, code, protected)
    folders = ([cwd] if cwd else []) + cds
    if not _may_reach_protected(text, words, targets, folders, protected):
        return None

    substitution = _CAT_HEREDOC.sub("heredoc", code)
    if powershell:  # "$($_.Name)" only reads a member
        substitution = _PS_MEMBER_SUBEXPR.sub("member", substitution)
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
            protected.is_protected_path(expanded)  # also by its words: Gallant*, GALLAN~1
            or _reaches_protected(expanded, folders, protected)
            or re.search(r"[$%]", expanded)
        ):
            return f"{BLOCK_HINT}\nRedirect target: {target}"

    suspect_dir = any(_folder_is_suspect(f, protected) for f in folders)
    # After a cd, tools/ is the repo's own only in a checkout of the repo.
    repo_tools = all(_repo_checkout(f) for f in cds)
    for words in segments:
        if not powershell and (program := _sets_program(words)):
            return f"{BLOCK_HINT}\nAn environment prefix that names a program: {program}"
        if _segment_is_read_only(words, powershell):
            continue
        if not suspect_dir and (
            (repo_tools and _is_venv_tool(words, powershell))
            or _is_repo_vcs(words, text_ids, folders, protected, powershell)
        ):
            continue
        shown = " ".join(w for w in words if id(w) not in text_ids)
        return f"{BLOCK_HINT}\nNot a read-only command: {shown}"
    return None


def _mentions_sensitive(text: str) -> bool:
    low = text.lower()
    return any(s in low for s in ("gallant", "steamapps", "3392510"))


def read_payload() -> str:
    """The hook's stdin as UTF-8, which Claude Code sends, whatever this machine's locale.

    Messages go out as UTF-8 too, so a command's non-ASCII text reads right in the reason. A
    stream that can't do either (none at all, say) gives no payload, never an exception: one
    here would exit 1, which lets the call through unjudged.
    """
    with contextlib.suppress(Exception):
        sys.stderr.reconfigure(encoding="utf-8", errors="backslashreplace")
    try:
        return sys.stdin.buffer.read().decode("utf-8", errors="replace")
    except Exception:
        return ""


def main() -> int:
    raw = read_payload()
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
    sys.exit(main())
