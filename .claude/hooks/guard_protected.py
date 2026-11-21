"""PreToolUse hook: block writes into the game install, the real saves, Steam's cloud cache for
the game, and the mod's offline files (``%LOCALAPPDATA%\\ZexionAP``: the seed list, labels and
disk counts the helper keeps; on Linux the seeds' files and the helper's socket token, in
``$XDG_DATA_HOME/ZexionAP``, else ``~/.local/share/ZexionAP``, and Flatpak's default data
folder; only the helper writes them). ``tools/protected.py`` defines these folders, and says why
the helpers' own ``ZexionAP-seed-<NAME>.sav`` copies in the save folder change nothing here.

Claude Code sends the tool call as JSON on stdin. Exit 2 blocks it and shows stderr to Claude.
``check`` is the entry point, ``_check`` the rules, in this order:

- Write, Edit, MultiEdit, NotebookEdit: blocked when the target file is in a protected folder.
- Shell commands: the ``command`` of Bash and Monitor (read as Bash), and of PowerShell and the
  terminal tool ``mcp__terminal__run_in_terminal`` (read as PowerShell). Other tools pass. The
  command runs in the tool's ``cwd`` if it has one, else in the payload's own ``cwd`` (the
  session's working folder, which a ``cd`` in an earlier call may have moved).

  1. ``_parse`` splits the command into simple commands, redirect targets and words. Text that is
     only data is left out, so commit messages and PR text may name the folders: a PowerShell
     here-string's quotes, the message values of ``git`` and ``gh`` (``_text_words``) and the
     body of a Bash heredoc that nothing can run (``_heredoc_is_data``). The script of a
     ``sed`` that writes nothing is no path (``_sed_data_words``): the text of it is still read
     for a protected folder's name, as any command's is. Nothing is protected by file name
     outside the four folders: ``native/build``, ``dist``, a scratch folder and a worktree are
     the repo's own, whatever they hold (``libzexion_ap.so``, ``version.dll``, ``zexion_ap.dll``,
     ``zexion-ap-<version>-windows.zip``).
  2. ``_reach``: whether the command *may reach* a protected folder (``_may_reach_protected``,
     ``_selects_protected_name``), or deletes, moves out, changes or searches-and-acts on a folder
     *above* one (``_ancestor_violation``). One that cannot is allowed.
  3. One that may is allowed only when every simple command in it is recognizably read-only
     (``_segment_is_read_only``; ``unzip -l``, ``-v``, ``-t``, ``tar -t`` and ``zipinfo`` only
     list an archive: ``_unzip_lists``, ``_tar_lists``), a ``.venv`` Python run of a script in
     ``tools/`` (those enforce their own rules through ``tools/protected.py``; after a ``cd``,
     only when every ``cd`` goes to a checkout of this repo), or a ``git`` or ``gh`` command
     that names no protected folder or parent of one; and when it holds no command substitution
     or PowerShell method call (``_substitution_violation``) and no output is redirected into a
     protected folder or to a path the guard cannot see (``_redirect_violation``,
     ``_command_violation``).

Past a budget (the ``MAX_*`` limits and ``DEADLINE``, with their comments) the guard does not read
a command word by word: it counts as reaching a protected folder, so only a read-only one passes.
That keeps its answer within about a second, as a hook that times out lets the call through.

Documented false positives (name the folder in the command instead):

- A ``cd`` to a variable, a substitution or a temporary folder is unknown (a variable may be
  empty, and then ``cd`` goes home): ``d=$(mktemp -d) && cd "$d" && rm -rf *``, ``cd "$(git
  rev-parse --show-toplevel)" && git clean -fdx`` and ``cd "$RUNNER_TEMP" && rm -rf *`` block.
  ``rm -rf "$d"/*`` and ``git -C "$d" clean -fdx`` pass (a variable the environment knows is
  expanded, and judged by its real path).
- ``find . -iname '*zexion*' -delete`` and ``gci . zexion | ri`` in this repo block: they can't
  be told from ``ZexionAP``. ``find dist -name 'zexion*' -delete`` passes: ``-name`` is read as
  written.
- A name pattern that may match a file in the protected folders blocks wherever the search starts:
  ``find lab -name '*.sav' -delete`` and ``find . -name '*' -delete`` (``*.py`` passes).
- The joined reading of a command may take prose such as ``print('Gallant Leaf, Zexion')`` for
  the folder; that blocks only in a command that is not read-only.
- ``ls``, ``dir`` or ``gci`` inside a quoted PowerShell string (``Write-Host "run ls zexion";
  Remove-Item …``) counts as a call.
- A scratch script written by a heredoc and run in the same command is code, like ``python -
  <<'EOF'``, so it blocks when it names the saves, even to read them or in a comment.

Known gaps. This is a tripwire against mistakes, not a sandbox. It fails closed: if the guard
itself breaks while looking at a call that mentions the saves, a Steam folder or ``ZexionAP``, the
call is blocked. It can't see:

- a path assembled entirely from variables or string arithmetic; the folder behind a ``cd`` into a
  variable; a link to a parent folder made outside the command; a link to the home folder
  followed by an ``rm``; ``/proc/self/root``; the folder a script file's contents name (a scratch
  script written by a heredoc and run in the same command is code, but the run is tied to the
  heredoc only by the file's name: ``sh *.sh`` or a variable holding it is missed);
- links in a command's words past the first ``MAX_LINK_WORDS`` different ones (the file system is
  asked about each, which costs time a word; only the following of links is cut, a word that names
  a parent of a protected folder is read wherever it comes): ``cp`` with ``MAX_LINK_WORDS`` filler
  words before an operand in a link in the working folder that leads into a protected folder
  passes; nor a link in a UNC path on Windows (``protected.is_network_path``: asking would
  resolve it over the network, seconds a host; ``//?/C:/x`` and ``//./C:/x`` are local and are
  followed);
- a Steam library folder by its own name (only its ``steamapps`` and what is below, and Steam's
  own folders in the usual places: a Steam folder at ``D:/Steam`` is missed), a home folder that
  neither the environment nor a ``/home/name``, ``/Users/name`` or ``C:/Users/name`` path gives,
  and the folders above a protected one that other variables name: ``$PUBLIC/..``,
  ``$HOMEDRIVE$HOMEPATH``, ``%HOMEDRIVE%%HOMEPATH%``, ``$SYSTEMDRIVE``, ``$ProgramData/..``,
  ``$PWD/../..``, ``$env:SystemDrive/``, ``~+``, a variable set by another variable (``H=$X``),
  and UNC admin shares (``//localhost/C$/Users/…``);
- wrappers it does not know (``chrt``, ``taskset``, ``flock``, ``strace``, ``watch``,
  ``unbuffer``, ``script -qc``, ``parallel``, ``wsl``), which hide the verb; so do ``command cd``
  and ``builtin cd``, ``bash -c --`` and ``-o … -c``, a script piped or here-stringed into a shell
  with no ``-c``, ``xargs … sh -c`` with the listing, ``pwsh -co`` and a script word of more than
  2000 characters (``sh -c``, ``eval``, ``cmd /c``, which is not read again); cmd forms beyond the
  common ones (``@rd``, ``rd/s/q`` with no spaces, ``^"`` escapes, ``if /i a==a``, ``if
  errorlevel``); a subshell in the middle of a pipeline (``ls ~ | (cat) | xargs rm -rf``), which
  cuts the link between ``ls ~`` and ``xargs``; a ``xargs rm`` whose list comes from a file
  (``xargs rm < list``); a variable that holds an item (``$h = gi ~``) and ``foreach (… in
  @($HOME))``;
- verbs that change a folder that ``_ancestor_violation`` does not list: ``Clear-Item``,
  ``Clear-Content``, ``takeown``, ``icacls``, ``chattr``, ``setfacl -R``, ``rm -rf $(dirname
  ~/x)``; archives and tree copies into the home folder (``tar -x -C ~``, ``rsync -a … ~/``, ``cp
  -r … ~``, ``Expand-Archive`` without ``-Force``) pass: they add files, and overwrite only what
  the archive holds;
- code in another language (``python -c "shutil.rmtree('/')"``, ``Path.home()``,
  ``os.path.expanduser('~')``) and PowerShell's .NET calls and methods (``[IO.Directory]::Delete(
  $HOME, $true)``, ``gci ~ -r | % Delete``), unless the command names a protected folder itself;
- a name picked by another tool than ``find`` and PowerShell's ``-Filter`` and ``-Include``
  (``locate``, ``find lab/x | grep ZexionAP | xargs rm`` from a folder that is not above a
  protected one: from ``~`` it blocks; ``Where-Object Name -like``, ``cmd /c for /d``, a .NET
  search); a name from a variable or an
  expression (``find -name "$n"``, ``"Zexion$(echo AP)"``, a ``$'…'`` string with escapes, text
  after a backtick); a regular expression's quantifier (``-regex 'Zex(ion)?AP'``) or an
  alternation inside a group that splits a name (``(Z|z)exion``); a brace expansion past five
  levels or 32 alternatives; a PowerShell ``@(…)`` list split by newlines or ``;``;
- a program variable that is not on the list (``_PROGRAM_VAR``), set with ``env`` before a command
  that is otherwise allowed, or set in PowerShell (``$env:GIT_PAGER = …``);
- a backslash is a separator on every platform (``protected.normalize``), so on Linux and macOS a
  folder named with them is read as a separator, and ``../..`` after it goes one level too high:
  ``cd ~/.local/share/t && echo 1 > '<two backslashes>'/../../ZexionAP/s.json`` passes;
- a ``tools/`` script is trusted in the session's own ``cwd`` without checking that it is a
  checkout of this repo.
"""

from __future__ import annotations

import contextlib
import fnmatch
import json
import os
import re
import sys
import time
import warnings
from dataclasses import dataclass
from pathlib import Path

_TOOLS_DIR = Path(__file__).resolve().parent.parent.parent / "tools"


class _OutOfTime(Exception):
    """Reading a command took longer than ``DEADLINE``."""


_due = float("inf")  # when reading the command in hand must stop (``check`` sets it)


def _tick() -> None:
    """Raise ``_OutOfTime`` when the time for the command in hand is up."""
    if time.monotonic() > _due:
        raise _OutOfTime


# What the guard reads of one command, so that it answers in about a second whatever the input
# (``settings.json`` gives the hook 120 s, but a hook that times out lets the call through: the
# deadline below is the protection, that a backstop). Past a budget (``MAX_SEGMENTS``,
# ``MAX_WORK``, ``MAX_FOLDERS``) the words are not read one by one: the command counts as
# reaching a protected folder, so only a read-only one passes (a redirect, a substitution, and the
# ``tools/`` scripts and ``git`` and ``gh`` commands that are trusted otherwise are blocked: "Too
# long or too complex to check"). Normal commands are far below them: a commit message or a PR
# body is one word, and a heredoc that is only data is cut out before the words are counted.
# Two limits make a heredoc count as code instead: ``MAX_HEREDOCS`` and ``MAX_SAVED_NAMES``.
MAX_COMMAND = 100_000  # characters; a longer command is blocked unread (use the Write tool)
MAX_SEGMENTS = 8000  # simple commands
MAX_WORK = 6000  # over the different words (``_work``) and redirect targets (8 each)
MAX_FOLDERS = 16  # different folders the command's ``cd`` goes to
MAX_HEREDOCS = 32  # Bash heredocs; with more, none is cut out as data
MAX_SAVED_NAMES = 64  # files a heredoc is saved to and copied to; with more it counts as code
MAX_LINK_WORDS = 64  # different words whose links are followed (a known gap); other reads: all
MAX_NAME_TEXT = 1000  # what is read of a name or a list of them; a longer one counts as a name
# Past ``DEADLINE`` of reading, whatever the budget says, the command is blocked too: the lexer
# (per word), the heredoc pass (per heredoc and per saved file's later commands), the final pass
# (per command) and each read of a word or name pattern that ``protected`` has not already
# answered (``_Memo``) look at the clock (``_tick``). Not interrupted: one read (a word of 96 000
# characters with a wildcard is read in full, 0.8 s; a command with several such passes takes up
# to about two seconds), the lexing of one word, and ``_heredocs``, which is linear in the length.
DEADLINE = 1.0  # seconds; a command whose reading takes longer is blocked


BLOCK_HINT = (
    "Blocked by .claude/hooks/guard_protected.py: this would write into the Zexion install, the "
    "real save folder, Steam's cloud cache for the game, or the mod's offline files "
    "(%LOCALAPPDATA%\\ZexionAP, or on Linux $XDG_DATA_HOME/ZexionAP). Read-only commands are "
    "fine. Save experiments go through "
    "tools/save_lab.py (see /save-lab), which backs up first."
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
    "unzip", "tar", "zipinfo",  # unzip and tar only when they list: ``_bash_args_write``
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
    r"|PATH|BROWSER"
    r"|TAR_OPTIONS|UNZIP\w*|ZIPINFO\w*",  # tar -t runs --use-compress-program, --checkpoint-action
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
# Words that only introduce ``NAME=value`` (``_sets_program`` adds ``typeset``).
DECLARE_WORDS = ("export", "local", "declare", "readonly")
FIND_WRITE_FLAGS = {"-delete", "-exec", "-execdir", "-ok", "-okdir", "-fprint", "-fprint0",
                    "-fprintf", "-fls"}  # fmt: skip
# ``find``'s predicates that select by name, with the kind of pattern they take (``-name``, the
# path, a regular expression over it) and whether they ignore case; ``-lname`` selects by what a
# link leads to. PowerShell's ``-Filter`` and ``-Include`` (``Get-ChildItem``'s, any unique
# abbreviation) take a name pattern too, or a list of them, and ignore case.
FIND_PREDICATES = {
    "-name": ("name", False), "-iname": ("name", True),
    "-lname": ("name", False), "-ilname": ("name", True),
    "-path": ("path", False), "-ipath": ("path", True),
    "-wholename": ("path", False), "-iwholename": ("path", True),
    "-regex": ("regex", False), "-iregex": ("regex", True),
}  # fmt: skip
# ``-Filter`` and ``-Include``, as PowerShell takes them: any unique abbreviation.
_PS_FILTER = r"fi(?:l(?:t(?:e(?:r)?)?)?)?"
_PS_INCLUDE = r"inc(?:l(?:u(?:d(?:e)?)?)?)?"
_PS_NAME_PARAM = re.compile(rf"-(?:{_PS_FILTER}|{_PS_INCLUDE})")
# PowerShell takes an en or em dash for a parameter's hyphen, and ``-Param:value`` for its value.
_DASH = "[-–—―]"
# A word that holds a command with such a predicate, or ``Get-ChildItem`` itself (its second
# operand is a name filter): it is read again as a command (any ``-inc…`` counts here).
_NAME_PREDICATE_IN_TEXT = re.compile(
    rf"(?:^|\s){_DASH}(?:i?(?:name|path|wholename|regex)|i?lname|{_PS_FILTER}|inc\w*)"
    r"(?:\s|:|$)"
    r"|(?:^|[\s;|&(])(?:get-childitem|gci|dir|ls)\s",
    re.I,
)
# The commands that list a folder: their operands that are not a path are name filters.
PS_LISTERS = {"get-childitem", "gci", "ls", "dir"}
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
# What a ``<<`` sits in when it is a shift or text, not a heredoc (``_heredocs``).
_NO_HEREDOC = ("$((", "((", "$[", "${", "[")
# A Bash ANSI-C string, $'…', whose escapes (\x24, \044, $) can spell ``$(``.
_ANSI_C = re.compile(r"\$'((?:[^'\\]|\\.)*)'")
_CAT_HEREDOC = re.compile(rf"\$\(\s*cat\s*<<-?[ \t]*{_DELIM_WORD}[ \t]*\n\s*\)")
# A ``>&`` that duplicates or closes a descriptor (2>&1, >&-) rather than naming a file.
_FD_DUP = re.compile(r"\d*-?(?=$|[\s;&|()<>])")
# A web or Steam URL, up to a space or a quote: at the start of a word or a quoted piece of one
# (not ``$d/x://``), with no ``..`` (Windows reads ``x://../y`` as ``y``) and no ``%`` (``wget
# -x`` saves ``%75serdata`` as ``userdata``).
_URL = re.compile(r"(?<![^\s\"'(=,])(?:https?|ftp|steam)://(?![^\s\"']*(?:\.\.|%))[^\s\"']*", re.I)
# A PowerShell ``$( $name.Member )``: it reads a member and runs no command.
_PS_MEMBER_SUBEXPR = re.compile(r"\$\(\s*\$(?:env:)?\w+(?:\.\w+)*\s*\)", re.I)
_ENV_REF = re.compile(r"\$\{env:(\w+)\}|\$env:(\w+)|\$\{(\w+)\}|\$(\w+)|%(\w+)%", re.I)


class Word(str):
    """A lexed word: its text, ``bare`` (the characters that were not quoted), and its span."""

    bare: str = ""
    start: int = 0
    end: int = 0
    raw: str = ""  # the source text of the word: quotes and backslashes as written


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
        _tick()
        w = _word(text, bare, start, end)
        w.raw = command[start:end]
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
        elif c == escape and command[i + 1 : i + 2] == "\n":
            i += 2  # a line continuation: the backslash and the newline are gone
            continue
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
            if c == "(" and expect_target and text is None:
                # > (expr): the target is what the parenthesis works out (PowerShell), unseen
                targets.append(_word("(", "(", i, i + 1))
                expect_target = False
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

    A ``<<`` inside quotes or a comment is text, and ``<<`` inside ``$((…))``, ``((…))``,
    ``$[…]`` or ``${…}`` (an offset or a subscript, ``${s:0:1<<2}``, ``${a[1<<2]}``) is a shift
    or text, never a heredoc. The delimiter is the whole shell word after ``<<`` (``EOF.x``),
    quotes removed. A body runs from the line after its operator to the delimiter line, or to
    the end of the command.
    """
    found: list[Heredoc] = []
    pending: list[tuple[re.Match, tuple | None]] = []
    stack: list[tuple[str, int, int]] = []  # quotes, parentheses and backticks we are inside
    arith = 0  # how many of them are arithmetic or a subscript (``_NO_HEREDOC``)

    def push(entry: tuple[str, int, int]) -> None:
        nonlocal arith
        stack.append(entry)
        arith += entry[0] in _NO_HEREDOC

    def pop() -> None:
        nonlocal arith
        arith -= stack.pop()[0] in _NO_HEREDOC

    i, n = 0, len(command)
    while i < n:
        c = command[i]
        top = stack[-1][0] if stack else ""
        if top == "'":
            if c == "'":
                pop()
            i += 1
            continue
        if c == "\\":
            i += 2
            continue
        if top == '"':
            if c == '"':
                pop()
            elif command.startswith("$(", i):
                kind = "$((" if command.startswith("$((", i) else "$("
                push((kind, i, i + len(kind)))
                i += len(kind)
                continue
            elif c == "`":
                push(("`", i, i + 1))
            i += 1
            continue
        arithmetic = arith > 0
        if c in "'\"":
            push((c, i, i + 1))
        elif c == "`":
            if top == "`":
                pop()
            else:
                push(("`", i, i + 1))
        elif command.startswith("((", i) or command.startswith("$((", i):
            kind = "$((" if c == "$" else "(("
            push((kind, i, i + len(kind)))
            i += len(kind)
            continue
        elif c == ")" and top in ("$((", "((") and command.startswith("))", i):
            pop()
            i += 2
            continue
        elif command.startswith("$[", i) or command.startswith("${", i):
            push((command[i : i + 2], i, i + 2))
            i += 2
            continue
        elif c == "[" and top in ("$[", "${", "["):  # a subscript inside one: a[1<<2]
            push(("[", i, i + 1))
        elif (c == "]" and top in ("$[", "[")) or (c == "}" and top == "${"):
            pop()
        elif c == "(":
            kind = command[i - 1] + "(" if i and command[i - 1] in "$<>" else "("
            push((kind, i - len(kind) + 1, i + 1))
        elif c == ")":
            if top.endswith("("):
                pop()
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


def _gh_runs_script(words: list[str]) -> bool:
    """Whether a gh command (its words, ``gh`` first) runs a script: ``gh alias``, ``extension``."""
    return next((w.lower() for w in words[1:] if not w.startswith("-")), "") in GH_RUN_SUBCOMMANDS


def _handles_without_running(words: list[str]) -> bool:
    """Whether a Bash command only reads, moves or posts the files it names, never runs them:
    read-only, a file verb such as ``rm``, ``git`` (not ``apply`` or ``am``), or ``gh`` (not
    ``alias`` or ``extension``)."""
    words = _strip_prefix(words, False)
    if not words:
        return True
    verb = _verb(words[0])
    if verb == "git":  # not git bisect run, rebase -x, or an alias given with -c
        return _git_subcommand(words) in GIT_FILE_SUBCOMMANDS and not any(
            w.startswith(("-c", "--config-env")) for w in words[1:]
        )
    if verb == "gh":
        return not _gh_runs_script(words)
    return verb in FILE_VERBS or _segment_is_read_only(words, False)


def _base_name(path: str) -> str:
    return re.split(r"[\\/]", path.strip("\"'").rstrip("\\/"))[-1]


def _saved_and_used(command: str, doc: Heredoc, piece: str, reader: str) -> bool:
    """Whether a heredoc's reader saves it to a file that a later command may run.

    A later command may run it when it names the file (any case) and does not only handle it
    (``_handles_without_running``: ``wc``, ``git commit -F``, ``gh pr create --body-file``,
    ``rm``), names it inside a substitution, or pipes it into a command that does not:
    ``git commit -F x`` and ``wc x`` are fine, ``sh x``, ``. x``, ``cat x | sh`` and ``cp x y &&
    sh y`` are not.
    """
    rest = command[doc.op.end() : command.find("\n", doc.op.end())]
    files = re.findall(r"\d?>>?\|?[ \t]*(\"[^\"]*\"|'[^']*'|[^\s;&|<>()]+)", piece + " " + rest)
    if reader == "tee":
        files += [w for w in piece.split()[1:] if not w.startswith("-")]
    names = {_base_name(f) for f in files} - {""}
    names = {n for n in names if n.lower() not in HARMLESS_TARGETS}
    if not names:
        return False
    if len(names) > MAX_SAVED_NAMES:  # too many to follow: the body may be run
        return True

    def pattern(names: set[str]) -> re.Pattern:
        alts = "|".join(rf"(?<![\w.-]){re.escape(n)}(?![\w.-])" for n in sorted(names))
        return re.compile(alts, re.IGNORECASE)

    named = pattern(names)
    later = command[doc.after :]
    if not named.search(later):
        return False
    segments, _ = lex(later, False)
    for k, words in enumerate(segments):
        _tick()
        mentions = [w for w in words if named.search(w)]
        if not mentions:
            continue
        stripped = _strip_prefix(words, False)
        if stripped and _verb(stripped[0]) in COPY_VERBS:  # cp x.sh y.sh: follow y.sh too
            names |= {_base_name(w) for w in stripped[1:] if not w.startswith("-")} - {""}
            if len(names) > MAX_SAVED_NAMES:  # a chain of copies: each is a new pattern to build
                return True
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
        ok = not _gh_runs_script(words)
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
    docs = _heredocs(command)
    if len(docs) > MAX_HEREDOCS:  # each one is read against the rest of the command
        return command
    for doc in docs:
        _tick()
        if _heredoc_is_data(command, doc):
            out.append(command[pos : doc.body_start])
            pos = doc.after
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


def _operands(args: list[Word], valued: set[str]) -> list[Word]:
    """The words of ``args`` that are not options or option values."""
    out: list[Word] = []
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


def _sed_scripts(args: list[Word]) -> list[tuple[str, Word]] | None:
    """The scripts of a ``sed`` command, each with the word that holds it, or None when sed may
    write because of how it is called: in place, or a script from a file."""
    scripts: list[tuple[str, Word]] = []
    take_next = skip_next = False
    for a in args:
        if skip_next:
            skip_next = False
        elif take_next:
            scripts.append((a, a))
            take_next = False
        elif a.startswith("--"):
            # GNU getopt takes any unique prefix of a long option (``--in``, ``--expr``); the
            # ambiguous ``--f`` counts as ``--file`` here.
            name, eq, value = a.partition("=")
            if len(name) >= 3 and ("--in-place".startswith(name) or "--file".startswith(name)):
                return None
            if len(name) >= 3 and "--expression".startswith(name):
                if eq:
                    scripts.append((value, a))
                take_next = not eq
            elif len(name) >= 3 and "--line-length".startswith(name):
                skip_next = not eq  # its value is no script
        elif a.startswith("-") and len(a) > 1:
            flags = a[1:]
            if "i" in flags or "f" in flags:
                return None
            if "e" in flags:
                rest = flags[flags.index("e") + 1 :]
                if rest:
                    scripts.append((rest, a))
                take_next = not rest
    if not scripts:
        long_valued = {a for a in args if len(a) >= 3 and "--line-length".startswith(a)}
        scripts = [(s, s) for s in _operands(args, {"-l", *long_valued})[:1]]
    return scripts


def _sed_script_writes(script: str) -> bool:
    """Whether a sed script has a ``w``, ``W`` or ``e`` command (or an ``s`` flag of them)."""
    s = re.sub(r"\\.", "", script)
    s = re.sub(r"([sy])(.)(?:(?!\2).)*\2(?:(?!\2).)*\2", r"\1", s)  # s/re/new/ keeps flags
    s = re.sub(r"/[^/]*/", "", s)  # addresses
    return re.search(r"[wWe]", s) is not None


def _sed_writes(args: list[Word]) -> bool:
    """Whether sed may write: in place, a script from a file, or a w, W or e command."""
    scripts = _sed_scripts(args)
    return scripts is None or any(_sed_script_writes(s) for s, _ in scripts)


def _sed_data_words(segments: list[list[Word]], powershell: bool) -> set[int]:
    """The ids of the words that are a ``sed`` command's script, which is no path: a script like
    ``s/^(.{12})[^ ]* \\\\*?/\\\\1  /`` read as one is three wildcard names in a row, which may be
    ``Gallant Leaf/Zexion``, and a chained command with any other verb then blocks. Only Bash's,
    of a sed that does not write (``_sed_writes``: a script with a ``w`` command names a file),
    and only a word quoted whole (an unquoted wildcard may be expanded by the shell into names)
    with no substitution or ``$'…'`` string in it. ``_check`` still reads the text for a
    protected folder's name, as it reads every command's."""
    out: set[int] = set()
    if powershell:
        return out
    for words in segments:
        words = _strip_prefix(words, False)
        if not words or _verb(words[0]) != "sed":
            continue
        scripts = _sed_scripts(words[1:])
        if scripts is None or any(_sed_script_writes(s) for s, _ in scripts):
            continue
        for _, word in scripts:
            if not re.search(r"[*?\[{~]", word.bare) and not re.search(r"\$[(']|`", word.raw):
                out.add(id(word))
    return out


def _unzip_lists(args: list[Word]) -> bool:
    """Whether ``unzip`` only lists or tests an archive: ``-l``, ``-v``, ``-t`` or ``-Z``
    (zipinfo) among its options, every option before the archive a cluster of those and quiet
    and short-form letters, and after the archive only names and ``-x``: ``-d`` and the rest
    that change what it does (or where it extracts) are not read."""
    seen = archive = False
    for a in args:
        if archive and not a.startswith("-"):
            continue  # a member name
        if not a.startswith("-") or a in ("-", "--"):
            archive = True
        elif archive:
            if a != "-x":
                return False
        elif re.fullmatch(r"-[lvtqZ1shmz]+", a):  # not -T: it changes the archive's own time
            seen = seen or re.search(r"[lvtZ]", a) is not None
        else:
            return False
    return seen


# tar's list mode: ``-t`` with only letters and long options that read.
_TAR_LONG_READS = {
    "--list",
    "--verbose",
    "--gzip",
    "--gunzip",
    "--bzip2",
    "--xz",
    "--zstd",
    "--file",
}


def _tar_lists(args: list[Word]) -> bool:
    """Whether ``tar`` only lists an archive (``tar -t``, ``-tvzf x.tgz``, ``tf x.tar``,
    ``--list``): ``t`` as the mode, the other letters only ``v``, ``f``, ``z``, ``j``, ``J``,
    ``a``, and no option that runs a program or writes a file (``--use-compress-program``,
    ``-I``, ``--checkpoint-action``, ``--index-file``, ``--to-command``, ``-F``, ``-C``, …).
    The archive's name is a word like any other, but not a ``host:file`` one (tar runs ``rsh``)."""
    listing = take_next = False
    for i, a in enumerate(args):
        if take_next:
            take_next, value = False, a
        elif a.startswith("--"):
            name, eq, value = a.partition("=")
            if name not in _TAR_LONG_READS:
                return False
            listing = listing or name == "--list"
            take_next = name == "--file" and not eq
            if take_next:
                continue
        elif (a.startswith("-") and len(a) > 1) or i == 0:  # options; or the old style's first
            letters = a.lstrip("-")
            if not re.fullmatch(r"[tvfzjJa]+", letters):
                return False
            listing = listing or "t" in letters
            take_next = "f" in letters
            continue
        else:
            continue  # a member name
        if ":" in value and not re.match(r"[A-Za-z]:[\\/]", value):
            return False
    return listing


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
    if verb == "unzip":
        return not _unzip_lists(args)
    if verb == "tar":
        return not _tar_lists(args)
    return False


def _git_reads(words: list[Word]) -> bool:
    """``git diff``, ``log`` or ``show`` with nothing that makes it write a file or run one."""
    return (
        _verb(words[0]) == "git"
        and _git_subcommand(words) in GIT_READERS
        and not any(w.startswith(GIT_READER_WRITE_FLAGS) for w in words[1:])
    )


def _segment_is_read_only(words: list[Word], powershell: bool) -> bool:
    """*Read-only* means the verb is on a short list (``READ_ONLY_SH``, ``READ_ONLY_PS``) and
    nothing in its arguments makes it write (``_bash_args_write``: ``find -delete``, ``sort -o``,
    ``sed -i`` and the like); ``git diff``, ``log`` and ``show`` count unless an argument starts
    with ``--output``, ``--ext-diff``, ``--exec-path``, ``--config-env`` or ``-c``. PowerShell's
    rules for script blocks, ``ForEach-Object``, expressions and ``&``:
    ``_ps_segment_is_read_only``."""
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
    if words and words[0] in (*DECLARE_WORDS, "typeset"):
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
    """Absolute as Windows, Git Bash and Linux read it: a drive, or a leading ``/`` (``/c/Users``,
    which Python 3.13 on Windows does not take for absolute and would join to the cwd's drive),
    or on Windows a leading backslash. A leading backslash on Linux is a folder's name."""
    return (
        os.path.isabs(path)
        or re.match(r"[A-Za-z]:", path) is not None
        or path.startswith("/")
        or (os.path.sep == "\\" and path.startswith("\\"))
    )


# ``cd``'s kin that leave the folder unknown: the previous one, or the one a ``pushd`` left.
POP_VERBS = {"popd", "pop-location"}


def _assignments(words: list[Word]) -> list[tuple[str, str]]:
    """The variables a simple command sets: ``NAME=value`` words at its start (also after
    ``export``) and PowerShell's ``$name = value`` and ``$env:name = value``."""
    items = [w for w in words if w not in DECLARE_WORDS]
    out = []
    for w in items:
        m = re.fullmatch(r"\$?(?:env:)?([A-Za-z_]\w*)\+?=(.*)", w, re.DOTALL)
        if not m:
            break
        out.append((m.group(1), m.group(2)))
    if not out and len(items) >= 3 and items[1] == "=":
        m = re.fullmatch(r"\$(?:env:)?(\w+)", items[0])
        if m:
            out.append((m.group(1), str(items[2])))
    return out


def _walk_cds(
    segments: list[list[Word]],
    cwd: str,
    code: str,
    protected,
    powershell: bool = False,
    assigned: set[str] | None = None,
) -> tuple[list[str | None], list[str | None]]:
    """The folder each simple command runs in, and the folders the command's ``cd``s go to.

    Each ``cd`` is resolved against the one before it, not the session's ``cwd``, once variables
    the hook's environment knows are expanded; a ``cd`` with no folder (or ``--``) goes home.
    None: a folder that is unknown, because the target still holds a variable or a substitution
    (``cd $d``, ``` cd `echo ~` ```; unless it is one that holds a protected one by name:
    ``cd $APPDATA`` where the variable is not set here), or is ``-``, ``$OLDPWD`` or ``popd``.

    ``assigned`` (``_assigned_names``, worked out when not given) are the variables the line sets
    itself: they are not read from the hook's environment. A ``cd`` to a variable is unknown
    whatever it names (``$TMPDIR``, ``$(mktemp -d)``): it may be empty, and ``cd`` with none goes
    home. ``cd /c/Users/x`` is on drive C, not under the cwd's; a bare ``cd`` and ``cd --`` go
    home.
    """
    current: str | None = cwd or os.getcwd()
    if assigned is None:
        assigned = _assigned_names(code, segments)
    before: list[str | None] = []
    targets: list[str | None] = []
    for words in segments:
        before.append(current)
        words = _strip_prefix(words, powershell)
        if not words:
            continue
        verb = _verb(words[0])
        if verb in POP_VERBS:
            current = None
            targets.append(None)
            continue
        if verb not in CD_VERBS:
            continue
        args = [w for w in words[1:] if (w == "-" or not w.startswith("-")) and w.lower() != "/d"]
        raw = args[0] if args else "~"
        names = {next(g for g in m.groups() if g).lower() for m in _ENV_REF.finditer(raw)}
        # A variable set on this line is not the hook's environment's: left as written.
        target = raw if names & assigned else _expand_env(protected._expand(raw), assigned)
        if re.fullmatch(r"[A-Za-z]:", target):
            target += "/"  # ``cd C:``: the drive's root here (and its own folder in cmd: close)
        if raw == "-" or re.search(r"\$\{?(?:OLDPWD|PWD)\b|%CD%", raw, re.IGNORECASE):
            new = None  # the shell's own variables: the environment here says nothing of them
        elif re.search(r"[$%`]", target) or target.startswith("~"):
            new = target if protected.holds_protected(target) else None
        elif _is_absolute(target):
            new = target
        else:
            new = os.path.join(current, target) if current is not None else None
        targets.append(new)
        current = new
    return before, targets


def _folder_is_suspect(folder: str | None, protected) -> bool:
    """A folder in a protected one, or one whose last names could hold one (``Gallant Leaf``,
    ``steamapps``, …: ``protected.may_be_above_protected``). Not judged by other names in it,
    so a worktree named ``steam-achievements`` or a user named Steam is fine. The folders above
    them that hold one at any depth (``~``, Roaming, ``/``) are judged by what runs in them:
    ``_ancestor_violation``."""
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
    word = _OPTION_PREFIX.sub("", word)
    bare = word.lower() in ("steamapps", "userdata")
    return any(
        protected.may_be_above_protected(p)
        and (not bare or (not protected.is_network_path(p) and os.path.isdir(p)))
        for p in _as_paths(word, folders, protected)
    )


# Folders above a protected one (``~``, Roaming, ``/``, a drive's root: ``protected.
# is_ancestor_folder``) hold the saves at some depth, but most commands that name them do nothing
# to what is in them: ``cd ~ && git clone``, ``cp x ~``, ``ls ~``, ``tar -C ~``. So only a command
# that deletes, moves out, changes or searches-and-acts on the folder reaches what is in it
# (``_ancestor_violation``). By what the verb does with the folders it is given:
RM_VERBS = {"rm", "rmdir", "unlink", "shred", "del", "erase", "rd", "ri", "remove-item"}
MV_VERBS = {"mv", "mi", "move-item", "move"}  # its sources: moving into the folder is no harm
CHANGE_VERBS = {"chmod", "chown", "chgrp", "truncate", "rni", "rename-item", "ren"}
FIND_VERBS = {"find", "gfind"}
FD_VERBS = {"fd", "fdfind"}
SHELL_VERBS = {"sh", "bash", "zsh", "dash", "ksh"}  # ``-c`` and a script: read again
PS_SHELLS = {"powershell", "pwsh"}
# Commands that list or search where they run when given no folder: what they print can feed a
# ``Remove-Item`` or an ``xargs rm``.
LISTERS = {"ls", "dir", "find", "fd", "fdfind", "tree", "du", "rg", "grep", "gci", "get-childitem"}
# cmd's switches (``/s``, ``/q``) are options, not a drive's root.
CMD_VERBS = {"rd", "rmdir", "del", "erase", "move"}
# Wrappers that run the command after them: the options that take a value, and how many plain
# words (a ``timeout``'s duration) come before the command. ``cmd /c`` is read in ``_unwrap``.
WRAPPERS: dict[str, tuple[set[str], int]] = {
    "sudo": ({"-u", "-g", "-h", "-p", "-C", "-D", "-R", "-T", "-U"}, 0),
    "doas": ({"-u", "-C"}, 0),
    "env": ({"-u", "-C", "-S", "-P"}, 0),
    "nice": ({"-n"}, 0),
    "ionice": ({"-c", "-n", "-p"}, 0),
    "timeout": ({"-s", "-k", "--signal", "--kill-after"}, 1),
    "nohup": (set(), 0),
    "command": (set(), 0),
    "builtin": (set(), 0),
    "exec": ({"-a"}, 0),
    "stdbuf": ({"-i", "-o", "-e"}, 0),
    "setsid": (set(), 0),
    "busybox": (set(), 0),
    "call": (set(), 0),  # cmd's
    "start": (set(), 0),
}
# What ``find -exec`` can run without it being a write.
EXEC_READERS = {
    "cat", "head", "tail", "grep", "egrep", "fgrep", "rg", "wc", "ls", "stat", "file", "md5sum",
    "sha1sum", "sha256sum", "sha512sum", "cksum", "du", "echo", "printf", "strings", "xxd", "od",
    "hexdump", "cmp", "diff", "basename", "dirname", "realpath", "readlink", "test", "true",
}  # fmt: skip
XARGS_VALUED = {"-I", "-n", "-P", "-L", "-s", "-d", "-E", "-a", "-l", "--max-args", "--max-procs",
                "--delimiter", "--arg-file", "--max-lines", "--replace", "--max-chars"}  # fmt: skip
_CMD_SWITCH = re.compile(r"//?[A-Za-z?]")  # Git Bash passes cmd ``//c`` for ``/c``
_SUBSTITUTED = re.compile(r"\$\(([^()]*)\)|`([^`]*)`")
_VARIABLE = re.compile(r"\$\{?(?:env:)?([\w@*]+)\}?(?:/\*)?|%(\w+)%", re.IGNORECASE)
_VALUE_START = re.compile(r"[$~%@(\"'/\\]")  # a word that is a value, not a command
_PIPELINE_VALUE = re.compile(r"\$(?:_|psitem)\b", re.IGNORECASE)
_HAS_NAME = re.compile(r"[A-Za-z0-9_]")
_OPTION_PREFIX = re.compile(r"^--?[\w-]+=")  # ``--target-directory=…``, which a path follows


def _unwrap(words: list[Word], powershell: bool) -> list[Word]:
    """The command a simple command runs: the wrappers of ``WRAPPERS`` (``sudo``, ``env``,
    ``nice``, ``timeout 5``, ``nohup``, ``command``, ``exec``, ``stdbuf``, ``busybox``, cmd's
    ``call`` and ``start /b``, …), cmd's ``if [not] exist X`` and ``cmd /c`` (also ``/v:on /c``
    and Git Bash's ``//c``) taken off, with their options and durations."""
    words = _strip_prefix(words, powershell)
    for _ in range(8):
        if not words:
            break
        verb = _verb(words[0])
        if verb == "cmd":  # ``cmd /v:on /c …``: the switches up to ``/c`` or ``/k``
            i = 1
            while i < len(words) and re.fullmatch(r"//?[A-Za-z][\w:]*", words[i]):
                i += 1
                if words[i - 1].lstrip("/").lower() in ("c", "k"):
                    break
            else:
                break
            rest = words[i:]
            if len(rest) == 1 and re.search(r"\s", rest[0]):
                # ``cmd /c "rd /s /q C:\Users\x"``: a script, read from the source text, where
                # the backslashes still are
                words = [Word("cmd-script"), rest[0]]
                break
            words = _strip_prefix(rest, powershell)
            continue
        if verb in ("not", "exist", "defined"):  # cmd's ``if [not] exist X command``
            words = _strip_prefix(words[2 if verb != "not" else 1 :], powershell)
            continue
        spec = WRAPPERS.get(verb)
        if spec is None:
            break
        valued, plain = spec
        i = 1
        while i < len(words):
            w = words[i]
            if w == "--":
                i += 1
                break
            if w.startswith("-") and len(w) > 1:
                i += 2 if w in valued else 1
            elif verb in ("start", "call") and re.fullmatch(r"//?[A-Za-z]\w*", w):
                i += 1  # cmd's ``start /b``, ``start /wait``
            elif verb == "env" and re.fullmatch(r"[A-Za-z_]\w*=.*", w, re.DOTALL):
                i += 1
            else:
                break
        words = _strip_prefix(words[i + plain :], powershell)
    return words


def _path_args(args: list[Word], cmd_switches: bool = False) -> list[str]:
    """The words of ``args`` that are not options (after ``--``, all are). PowerShell's
    ``-Path:value`` gives its value, and a list (``a,b``) its elements as well."""
    out, ended = [], False
    for a in args:
        if not ended and a == "--":
            ended = True
        elif not ended and a.startswith("-") and a != "-":
            named = re.fullmatch(r"-[A-Za-z]+:(.+)", a, re.DOTALL)
            if named:
                out.extend(_list_elements(named.group(1)))
        elif cmd_switches and _CMD_SWITCH.fullmatch(a):
            continue
        else:
            out.extend(_list_elements(str(a)))
    return out


def _list_elements(word: str) -> list[str]:
    """``word``, and its elements too when it is a comma list (PowerShell: ``ri -r x,$HOME``)."""
    return [word, *word.split(",")] if "," in word.strip(",") else [word]


def _operand_ancestor(
    word: str,
    folder: str | None,
    protected,
    anc_vars: set[str],
    join_names: bool = True,
    depth: int = 0,
) -> bool:
    """Whether a word, as an operand run in ``folder``, is a folder that may hold a protected one
    at any depth (``protected.is_ancestor_folder``: ``~``, ``"$APPDATA"``, ``/``, ``C:\\``,
    ``../..``, ``*`` in one), or a variable the command set to one (``anc_vars``), or a
    substitution that names one (``` `dirname $APPDATA` ```). A relative word is joined to
    ``folder`` (with names in it only if ``join_names``). With the folder unknown (None), a word
    counts when it is only dots and wildcards (``rm *``, ``find .``), has a ``..`` in it, or its
    last name may be a protected folder's or file's. ``$PWD``, ``%CD%`` and ``$(pwd)`` are ``.``.
    A lone ``/`` or ``/c`` counts here: the caller is a verb that deletes, moves or lists for
    one. ``depth`` (0 or 1) stops a substitution from being read inside another. Nothing asks the
    file system."""
    word = _OPTION_PREFIX.sub("", word).strip(",;")
    if word.endswith(")") and word.count(")") > word.count("("):
        word = word[:-1]  # ``(Get-Item ~)`` and ``@(~)``; not ``C:/Program Files (x86)``
    if word.startswith("(") and word.count("(") > word.count(")"):
        word = word[1:]
    if not word or "://" in word or "\n" in word or len(word) > 400:
        return False
    if re.fullmatch(r"\$\{?PWD\}?|\$\(pwd\)|`pwd`|%CD%|\$\(realpath \.\)", word, re.IGNORECASE):
        word = "."  # where the command runs
    var = _VARIABLE.fullmatch(word)
    if var and (var.group(1) or var.group(2)).lower() in anc_vars:
        return True
    if "`" in word or "$(" in word:
        if depth:
            return False
        return any(
            _operand_ancestor(inner, folder, protected, anc_vars, False, 1)
            for m in _SUBSTITUTED.finditer(word)
            for inner_words in lex(m.group(1) if m.group(1) is not None else m.group(2), False)[0]
            for inner in inner_words
        )
    # Bash drops the backslash of ``%USERPROFILE%\AppData``: cmd's variable, then a name.
    word = re.sub(r"^(%\w+%)(?=[A-Za-z_.])", r"\1/", word)
    path = protected._expand(word)
    if protected.is_ancestor_folder(path) or (
        re.search(r"[\\/]", path) and protected.may_be_above_protected(path)
    ):
        return True
    if _is_absolute(path) or path.startswith(("~", "$", "%")):
        return False
    named = _HAS_NAME.search(re.sub(r"\[[^\]]*\]", "", path)) is not None
    parts = [p for p in re.split(r"[\\/]", path) if p]
    if folder is None:
        # Unknown: dots and wildcards stand for one, so does a ``..`` (from an unknown folder it
        # may reach any), and a name that is a protected folder's or file's.
        return (
            not named
            or ".." in parts
            or bool(parts and protected.pattern_may_name_protected(parts[-1], "name", False))
        )
    if named and not join_names:
        return False
    return protected.is_ancestor_folder(os.path.join(folder, path))


def _join_backticks(args: list[Word]) -> list[Word]:
    """``args`` with the words of a backtick substitution (the lexer splits ``` `echo ~` ``` at the
    space) put back into one word."""
    out: list[Word] = []
    open_at = None
    for a in args:
        if open_at is None:
            out.append(a)
            if a.count("`") % 2:
                open_at = len(out) - 1
        else:
            out[open_at] = Word(f"{out[open_at]} {a}")
            if a.count("`") % 2:
                open_at = None
    return out


def _shell_script_at(args: list[Word]) -> int | None:
    """The index of the script word after a shell's ``-c`` (also ``-lc``, ``-ic``), if any."""
    for i, a in enumerate(args):
        if re.fullmatch(r"-[A-Za-z]*c[A-Za-z]*", a) and i + 1 < len(args):
            return i + 1
        if not a.startswith("-"):
            return None
    return None


def _find_writes(exprs: list[Word]) -> bool:
    """Whether the expression of a ``find`` writes: ``-delete``, ``-fprint``, ``-ok``, and an
    ``-exec`` of anything but a reader (``EXEC_READERS``)."""
    for i, a in enumerate(exprs):
        low = a.lower()
        if low in ("-exec", "-execdir"):
            if _verb(exprs[i + 1] if i + 1 < len(exprs) else "") not in EXEC_READERS:
                return True
        elif low in FIND_WRITE_FLAGS:
            return True
    return False


def _script_violation(script: str, folder, protected, powershell: bool, depth: int):
    """``_ancestor_violation`` of a script given as one word (``sh -c``, ``eval``)."""
    if depth >= 2 or len(script) > 2000:
        return None
    segments = lex(script, powershell)[0]
    return _ancestor_violation(segments, folder, set(), protected, powershell, script, depth + 1)


def _goes_up_to_protected(word: str, protected) -> bool:
    """Whether a relative word goes up with ``..`` and then names a protected folder or file
    (``a/../../ZexionAP/x``): from an unknown folder it may reach it."""
    if len(word) > 400 or ".." not in word or _is_absolute(word):
        return False
    parts = [p for p in re.split(r"[\\/]", word) if p]
    first = next((i for i, p in enumerate(parts) if p == ".."), None)
    return first is not None and any(
        protected.pattern_may_name_protected(p, "name", False) for p in parts[first + 1 :]
    )


def _option_value(args: list[Word], pattern: str) -> list[str]:
    """The values of the options of ``args`` whose name matches ``pattern`` (whole, any case): the
    next word, or what follows a ``:`` or ``=`` in the word (``-Destination D:\\x``,
    ``-Path:~``, ``--target-directory=~``)."""
    out = []
    for i, a in enumerate(args):
        m = re.fullmatch(rf"({pattern})(?:[:=](.*))?", a, re.IGNORECASE | re.DOTALL)
        if m:
            if m.group(2):
                out.append(m.group(2))
            elif i + 1 < len(args):
                out.append(str(args[i + 1]))
    return out


def _pipeline_groups(segments: list[list[Word]], code: str) -> list[int]:
    """Which pipeline each simple command is in: a number that goes up at every separator but a
    pipe, a script block's brace (``% { … }``) and what is inside a parenthesis (``@('x';'y')``);
    ``;``, ``&&``, ``||`` and a newline outside one start a new one. Read from the text between
    the words."""
    groups, gid, last, depth = [], 0, None, 0
    for s in segments:
        if s:
            if last is not None:
                sep = code[last.end : s[0].start]
                inside = depth > 0 or re.match(r"[^;&\n|(]*\(", sep) is not None
                if not inside and not re.fullmatch(r"[\s})\\`]*\|&?[\s\\`]*|\s*\{\s*", sep):
                    gid += 1
                depth = max(0, depth + sep.count("(") - sep.count(")"))
            last = s[-1]
        groups.append(gid)
    # A ``{ …; }`` and a ``while … done`` that a pipe goes on from (``{ ls ~; } | xargs rm``,
    # ``ls ~ | while read f; do …; done | xargs rm``) are one pipeline with it, whatever is inside.
    opened: list[int] = []
    parent: dict[int, int] = {}

    def find(g: int) -> int:
        while g in parent:
            g = parent[g]
        return g

    for i, s in enumerate(segments):
        first = s[0] if s else ""
        if first in ("{", "while", "until", "for"):
            opened.append(i)
        elif first in ("}", "done") and opened:
            start = opened.pop()
            if i + 1 < len(segments) and groups[i + 1] == groups[i]:  # a pipe goes on
                for g in {groups[k] for k in range(start, i + 1)}:
                    if find(g) != find(groups[i]):
                        parent[find(g)] = find(groups[i])
    return [find(g) for g in groups] if parent else groups


def _body_after(segments: list[list[Word]], i: int, code: str) -> list[Word]:
    """The words of the commands the lexer split off after command ``i`` at a parenthesis: the
    body of a ``$(…)`` or ``(…)`` or ``@(…)`` that is one of its words (``rm -rf $(echo ~)``,
    ``ri -r (Get-Item ~)``), up to the ``)``."""
    out: list[Word] = []
    last = segments[i][-1] if segments[i] else None
    for j in range(i + 1, len(segments)):
        s = segments[j]
        if not s:
            continue
        if last is None or "(" not in code[last.end : s[0].start]:
            break
        out.extend(s)
        last = s[-1]
        nxt = next((t for t in segments[j + 1 :] if t), None)
        if nxt is None or ")" in code[last.end : nxt[0].start]:
            break
    return out


class _Pipelines:
    """What the simple commands of a line were given, pipeline by pipeline, and the variables the
    line has set to a folder above a protected one (``anc_vars``). ``at`` is the command being
    read. ``listed()`` says whether an earlier command of its pipeline named such a folder (what a
    ``Remove-Item`` or ``xargs rm`` with nothing of its own to remove is fed), ``body()`` whether
    the commands the lexer split off at a parenthesis after it did (``rm -rf $(echo ~)``)."""

    def __init__(self, segments: list[list[Word]], before: list, code: str, protected) -> None:
        self.segments, self.before, self.code, self.protected = segments, before, code, protected
        self.groups = _pipeline_groups(segments, code)
        self.anc_vars: set[str] = set()
        self.at = 0
        self._given: dict[int, list[tuple[str, str | None]]] = {}  # per pipeline
        self._scanned: dict[int, int] = {}
        self._found: set[int] = set()

    def is_ancestor(self, word: str, folder: str | None, join_names: bool = True) -> bool:
        return _operand_ancestor(word, folder, self.protected, self.anc_vars, join_names)

    def listed(self) -> bool:
        gid = self.groups[self.at]
        if gid in self._found:
            return True
        given = self._given.get(gid, [])
        for a, f in given[self._scanned.get(gid, 0) :]:
            if self.is_ancestor(a, f, False):
                self._found.add(gid)
                return True
        self._scanned[gid] = len(given)
        return False

    def body(self) -> bool:
        folder = self.before[self.at]
        return any(
            self.is_ancestor(str(w), folder, False)
            for w in _body_after(self.segments, self.at, self.code)
        )

    def give(self, given: list[str], folder: str | None) -> None:
        """Record what the command being read was given, for the ones after it in its pipeline."""
        self._given.setdefault(self.groups[self.at], []).extend((a, folder) for a in given)


@dataclass
class _Cmd:
    """One simple command as ``_segment_violation`` reads it."""

    words: list[Word]  # with its wrappers (``sudo``, ``env``, …) taken off
    original: list[Word]  # as written: ``GIT_WORK_TREE=…`` is a word of it
    verb: str
    args: list[Word]
    folder: str | None  # where it runs
    pipes: _Pipelines
    protected: object
    powershell: bool
    depth: int
    fed: bool  # it is ``xargs``'s command

    def hit(self, word: str, names: bool = True) -> bool:
        return _operand_ancestor(word, self.folder, self.protected, self.pipes.anc_vars, names)

    def hits(self, words) -> bool:
        return any(self.hit(w) for w in words)

    @property
    def reason(self) -> str:
        shown = " ".join(map(str, self.words))[:200]
        return f"Reaches a folder above a protected one ({self.verb}): {shown}"


def _removes_or_changes(c: _Cmd) -> bool:
    """``rm``, ``mv`` (its sources: not a destination, named or last), ``chmod`` and the like."""
    ops = _path_args(c.args, c.verb in CMD_VERBS)
    if c.verb in MV_VERBS:
        named = _option_value(c.args, r"-dest\w*|-target\w*|-t|--target-directory")
        if named:
            ops = [o for o in ops if o not in named]
        elif len(ops) > 1:
            ops = ops[:-1]
    # The lexer ends the word at a ``$(`` or ``@(`` (``rm -rf $(echo ~)``): what it holds is the
    # commands that follow.
    openers = [o for o in ops if o in ("$", "@") or o.endswith("$(")]
    own = [o for o in ops if o not in openers and not _PIPELINE_VALUE.match(o)]
    return (
        c.hits(ops)
        or ((c.fed or not own) and c.pipes.listed())  # nothing of its own to remove: the pipeline's
        or ((not own or bool(openers)) and c.pipes.body())
    )


def _finds_and_writes(c: _Cmd) -> bool:
    args = c.args
    i = 0
    while i < len(args):
        if args[i] in ("-H", "-L", "-P") or re.fullmatch(r"-O\d?", args[i]):
            i += 1
        elif args[i] == "-D":
            i += 2
        else:
            break
    if args[i : i + 1] == ["--"]:
        i += 1
    starts = []
    while i < len(args) and not (args[i].startswith("-") or args[i] in ("(", "!", ",")):
        starts.append(args[i])
        i += 1
    return _find_writes(args[i:]) and any(c.hit(s) for s in starts or ["."])


def _fd_executes(c: _Cmd) -> bool:
    for i, a in enumerate(c.args):
        if a in ("-x", "-X") or a.startswith("--exec"):
            return c.hits(_path_args(c.args[:i])[1:] or ["."])
    return False


def _git_empties_tree(c: _Cmd) -> bool:
    """What empties the working tree: ``clean``, ``stash -u``/``-a``, ``reset --hard``, and
    ``checkout``/``restore`` of ``.``, in ``-C``, ``--work-tree``, ``GIT_WORK_TREE``, an operand
    or where it runs."""
    args = c.args
    dirs = [m.group(1) for w in c.original if (m := re.fullmatch(r"GIT_WORK_TREE=(.*)", w, re.S))]
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("-C", "--work-tree") and i + 1 < len(args):
            dirs.append(args[i + 1])
            i += 2
        elif a.startswith("--work-tree="):
            dirs.append(a.partition("=")[2])
            i += 1
        elif a in ("-c", "--git-dir", "--namespace", "--config-env"):
            i += 2
        elif a.startswith("-"):
            i += 1
        else:
            break
    sub, rest = (args[i] if i < len(args) else ""), args[i + 1 :]
    stash_all = re.compile(r"-[A-Za-z]*[ua][A-Za-z]*|--(?:include-untracked|all)")
    empties = (
        sub == "clean"
        or (sub == "stash" and any(stash_all.fullmatch(a) for a in rest))
        or (sub == "reset" and "--hard" in rest)
        or (sub in ("checkout", "restore") and {".", "-f", "--force"} & set(rest))
    )
    return bool(empties) and c.hits([*(dirs or ["."]), *_path_args(rest)])


def _rsync_deletes(c: _Cmd) -> bool:
    ops = _path_args(c.args)
    deletes = ops and any(a.startswith(("--delete", "--del", "--prune")) for a in c.args)
    return bool(
        (deletes and c.hit(ops[-1]))  # what is deleted is in the destination
        or (any(a.startswith("--remove") for a in c.args) and c.hits(ops[:-1]))  # the sources
    )


def _tar_removes_or_overwrites(c: _Cmd) -> bool:
    return (any(a.startswith("--remove-files") for a in c.args) and c.hits(_path_args(c.args))) or (
        "--overwrite" in c.args  # extracted over what is there
        and c.hits(_option_value(c.args, r"-C|--directory"))
    )


def _unzip_overwrites(c: _Cmd) -> bool:
    return any(re.fullmatch(r"-[A-Za-z]*o[A-Za-z]*", a) for a in c.args) and c.hits(
        _option_value(c.args, r"-d")
    )


def _expand_archive_forces(c: _Cmd) -> bool:
    return any(re.fullmatch(r"-f(?:orce)?", a, re.I) for a in c.args) and c.hits(_path_args(c.args))


def _robocopy_empties(c: _Cmd) -> bool:
    """``robocopy empty $HOME /MIR`` empties the destination."""
    return any(a.upper() in ("/MIR", "/PURGE", "/MOV", "/MOVE") for a in c.args) and c.hits(
        _path_args(c.args, cmd_switches=True)
    )


# The verbs whose operands are judged: whether the command deletes, moves out, changes or
# searches-and-acts on a folder above a protected one.
_OPERAND_RULES = {
    **dict.fromkeys(RM_VERBS | MV_VERBS | CHANGE_VERBS, _removes_or_changes),
    **dict.fromkeys(FIND_VERBS, _finds_and_writes),
    **dict.fromkeys(FD_VERBS, _fd_executes),
    "git": _git_empties_tree,
    "rsync": _rsync_deletes,
    "tar": _tar_removes_or_overwrites,
    "unzip": _unzip_overwrites,
    "expand-archive": _expand_archive_forces,
    "robocopy": _robocopy_empties,
}


def _xargs_runs(c: _Cmd) -> str | None:
    i = 0
    while i < len(c.args) and c.args[i].startswith("-"):
        i += 2 if c.args[i] in XARGS_VALUED else 1
    if not c.args[i:]:
        return None
    return _segment_violation(
        c.args[i:], c.folder, c.pipes, c.protected, c.powershell, c.depth, fed=True
    )


def _shell_runs(c: _Cmd) -> str | None:
    """``sh -c 'script' $0 $1``: the script is read again, with ``$0``, ``$@`` filled in."""
    at = _shell_script_at(c.args)
    if at is None:
        return None
    script, positional = c.args[at], [str(a) for a in c.args[at + 1 :]]

    def argument(m: re.Match) -> str:  # ``rm -rf "$0"``, ``"$@"``
        n = m.group(1)
        if n in "@*":
            return " ".join(positional[1:])
        return positional[int(n)] if int(n) < len(positional) else ""

    script = re.sub(r"\"?\$\{?([0-9@*])\}?\"?", argument, script)
    return _script_violation(script, c.folder, c.protected, False, c.depth)


def _cmd_script_runs(c: _Cmd) -> str | None:
    """``cmd /c "…"``: lexed as PowerShell, where a backslash is no escape."""
    text = c.words[1].raw or str(c.words[1])
    if len(text) >= 2 and text[0] == text[-1] and text[0] in "\"'":
        text = text[1:-1]
    return _script_violation(text, c.folder, c.protected, True, c.depth)


def _eval_runs(c: _Cmd) -> str | None:
    if not c.args:
        return None
    return _script_violation(" ".join(map(str, c.args)), c.folder, c.protected, False, c.depth)


def _ps_shell_runs(c: _Cmd) -> str | None:
    """``powershell -Command …``."""
    k = next((i for i, a in enumerate(c.args) if re.fullmatch(r"-c(?:ommand)?", a, re.I)), None)
    if k is None or k + 1 >= len(c.args):
        return None
    script = " ".join(map(str, c.args[k + 1 :]))
    return _script_violation(script, c.folder, c.protected, True, c.depth)


def _iex_runs(c: _Cmd) -> str | None:
    if not c.args:
        return None
    return _script_violation(" ".join(map(str, c.args)), c.folder, c.protected, True, c.depth)


# The verbs that run another command: its words are read again (``_script_violation``).
_COMMAND_RUNNERS = {
    "xargs": _xargs_runs,
    **dict.fromkeys(SHELL_VERBS, _shell_runs),
    "cmd-script": _cmd_script_runs,
    "eval": _eval_runs,
    **dict.fromkeys(PS_SHELLS, _ps_shell_runs),
    "iex": _iex_runs,
    "invoke-expression": _iex_runs,
}


def _segment_violation(
    words: list[Word],
    folder,
    pipes: _Pipelines,
    protected,
    powershell: bool,
    depth: int,
    fed: bool = False,
    unwrapped: list[Word] | None = None,
) -> str | None:
    """A reason when one simple command, run in ``folder``, deletes, moves out, changes or
    searches-and-acts on a folder that may hold a protected one (``_operand_ancestor``): the
    verbs of ``_OPERAND_RULES``, by what each does with the folders it is given, and what the
    verbs of ``_COMMAND_RUNNERS`` run. ``fed``: it is ``xargs``'s command. ``pipes`` tells what a
    ``Remove-Item`` or ``xargs rm`` with nothing of its own to remove is fed.

    By what the verb does with the folder it is given: ``rm``, ``rmdir``, ``shred``,
    ``Remove-Item``, cmd's ``rd`` and ``del`` (any operand); ``mv``, ``Move-Item`` and cmd's
    ``move`` (a source, so not a ``-Destination`` or ``-t``: moving into one is no harm);
    ``chmod``, ``chown``, ``truncate``, ``Rename-Item``; ``find`` (a start folder, with
    ``-delete``, ``-fprint``, ``-ok`` or an ``-exec`` of anything but a reader such as ``cat``
    and ``grep``) and ``fd -x``; ``git clean``, ``stash -u``, ``reset --hard`` and ``checkout .``
    (``-C``, ``--work-tree``, ``GIT_WORK_TREE``, an operand, or where it runs); ``rsync --delete``
    (its destination) and ``--remove-source-files`` (its sources), ``tar --remove-files``,
    ``robocopy /MIR /PURGE /MOVE``; extraction over what is there into one (``unzip -o -d ~``,
    ``tar --overwrite -C ~``, ``Expand-Archive … -Force``); and ``xargs`` or a ``Remove-Item``
    given nothing of its own to remove, fed by a command of its *pipeline* that named such a
    folder (``ls ~ | xargs rm``, ``gci $env:APPDATA -r | ri``, ``$HOME | ri -r``, a lister with
    no folder after ``cd ~``, a lone ``/`` piped in; not a command before a ``;`` or ``&&``:
    ``ls ~ ; find lab | xargs rm`` passes; a ``{ …; }`` or ``while … done`` that a pipe goes on
    from is part of it, a line continuation after a pipe too: ``_pipeline_groups``)."""
    original = words
    words = unwrapped if unwrapped is not None else _unwrap(words, powershell)
    if not words:
        return None
    verb = _verb(words[0])
    cmd = _Cmd(
        words, original, verb, _join_backticks(words[1:]), folder,
        pipes, protected, powershell, depth, fed,
    )  # fmt: skip
    if (rule := _OPERAND_RULES.get(verb)) and rule(cmd):
        return cmd.reason
    runner = _COMMAND_RUNNERS.get(verb)
    return runner(cmd) if runner else None


def _ancestor_violation(
    segments: list[list[Word]],
    start: str,
    text_ids: set[int],
    protected,
    powershell: bool,
    code: str,
    depth: int = 0,
    before: list[str | None] | None = None,
) -> str | None:
    """A reason when a simple command of the line deletes, moves out, changes or searches-and-acts
    on a folder above a protected one at any depth (``~``, Roaming, ``/``, a drive's root: the
    saves are inside), in the folder it runs in: where each ``cd`` before it left it, or from
    ``start``. See ``_segment_violation`` for what each verb does to count; a command that
    creates files in the folder, copies into it, lists or reads it does not (``cd ~ && git
    clone``, ``cp a ~``, ``ls ~ && make clean``, ``tar -C ~``, ``unzip -d ~`` pass).

    The folders above a protected one are Roaming, the home folder (``~``, ``$HOME``,
    ``%USERPROFILE%``, ``C:/Users/name``, ``/home/name``) and what is above it, ``/``, a drive's
    root (``/c`` and ``/mnt/c`` as Git Bash and WSL write it), the folders of the variables
    ``protected._ANCESTOR_ENV`` lists (``$APPDATA``, ``%LOCALAPPDATA%``, ``$XDG_DATA_HOME``, …,
    also written as the variable, with ``${APPDATA:-x}`` and ``${APPDATA%/}``, when this machine
    doesn't set it), ``.var``, ``~/snap``, ``.steam`` and Steam's folders in Program Files (x86);
    ``..`` or a last name that is only a wildcard (``~/*``) up to one. A lone ``/`` or ``/c`` is a
    root only as an operand of these verbs (``cmd /c``, ``findstr /s`` and ``print(1 / 2)`` name
    none). A relative operand is joined to the folder the command runs in (``rm -rf ../../..``
    from a folder under the home folder). Words are read again as a command's in ``sh -c`` (with
    ``$0``, ``$1`` and ``"$@"`` filled from the words after it), ``eval``, ``cmd /c "…"``,
    ``powershell -Command`` and ``iex``, after the wrappers of ``_unwrap`` are taken off. A
    backtick or ``$(…)`` operand that names one (``rm -rf `echo ~```, ``rm -rf $(echo ~)``), a
    parenthesis (``ri -r ($HOME)``, ``@(~)``), PowerShell's ``-Path:~`` and a comma list
    (``x,$HOME``) count, and so does a variable the line sets to one (``H=~; rm -rf "$H"``,
    ``for d in ~ /; do …``, PowerShell's ``$d =``).

    What feeds a command is the pipeline it is in: ``ls ~ | xargs rm``, ``gci ~ | ri``,
    ``ls ~ | while read f; do rm "$f"; done`` (``read`` and ``set --`` make their variables the
    folder, as ``H=~`` does), not a command before a ``;`` or ``&&``. A variable set on the line
    to such a folder (``H=~; rm -rf "$H"``, ``for d in ~ /; do …``) is read as the folder.
    Each command's arguments are read once, so the time is linear in the line."""
    if before is None:
        before, _ = _walk_cds(segments, start, code, protected, powershell)
    pipes = _Pipelines(segments, before, code, protected)
    for si, (words, folder) in enumerate(zip(segments, before, strict=True)):
        _tick()
        pipes.at = si
        words = [w for w in words if id(w) not in text_ids]
        _note_assignment(words, folder, pipes)
        stripped = _unwrap(words, powershell)
        if not stripped:
            continue
        verb = _verb(stripped[0])
        if verb in CD_VERBS or verb in POP_VERBS:
            continue
        given = [str(a) for a in stripped[1:] if not a.startswith("-")]
        if verb == "read" and pipes.listed():  # ``ls ~ | while read f``
            pipes.anc_vars.update(a.lower() for a in given if re.fullmatch(r"\w+", a))
        if verb == "set" and any(pipes.is_ancestor(a, folder, False) for a in given):
            pipes.anc_vars.update([*"123456789", "@", "*"])  # ``set -- ~``: the positionals
        found = _segment_violation(
            words, folder, pipes, protected, powershell, depth, unwrapped=stripped
        )
        if found:
            return found
        if verb in LISTERS and not any(not _CMD_SWITCH.fullmatch(a) for a in given):
            given = ["."]  # ``ls | xargs rm`` after ``cd ~``
        if any(a[:3].lower() in ("-ov", "-ou") for a in stripped[1:]) and any(
            pipes.is_ancestor(a, folder, False) for a in given
        ):  # ``gci ~ -OutVariable o``: what ``$o`` holds is what was listed
            pipes.anc_vars.update(
                v.lstrip("+").lower() for v in _option_value(stripped[1:], r"-ov|-outv\w*")
            )
        if _VALUE_START.match(stripped[0]):  # a value piped in: ``$HOME | ri -r``
            given.append(str(stripped[0]))
        pipes.give(given, folder)
    return None


def _note_assignment(words: list[Word], folder, pipes: _Pipelines) -> None:
    """Remember a variable the line sets to a folder above a protected one: Bash's ``NAME=value``
    (also after ``export``), PowerShell's ``$name = value`` and ``for d in ~ /`` (also over a
    ``$(…)`` that names one: ``for f in $(ls -d ~/*)``)."""
    if not any("=" in w for w in words) and words[:1] != ["for"]:
        return
    items = [w for w in words if w not in DECLARE_WORDS]
    if len(items) >= 4 and items[0] == "for" and items[2] == "in":  # ``for d in ~ /; do``
        values = [str(v) for v in items[3:] if v not in ("do", ";")]
        if any(pipes.is_ancestor(v, folder) for v in values) or (
            any(v == "$" or v.endswith("$(") for v in values) and pipes.body()
        ):
            pipes.anc_vars.add(str(items[1]).lower())
        return
    for name, value in _assignments(words):
        if value and (
            pipes.is_ancestor(value, folder)
            or (value.endswith("$") and pipes.body())  # ``x=$(ls ~)``
        ):
            pipes.anc_vars.add(name.lower())
    # PowerShell's ``$x = gci ~``: what the variable holds is what the lister listed.
    lister = len(items) >= 4 and items[1] == "=" and _verb(items[2]) in LISTERS | {"gi", "get-item"}
    if lister and any(
        pipes.is_ancestor(str(a), folder, False) for a in items[3:] if not a.startswith("-")
    ):
        pipes.anc_vars.add(str(items[0]).lstrip("$").removeprefix("env:").lower())


def _repo_checkout(folder: str | None, protected) -> bool:
    """Whether a folder is a checkout of this repo, so its ``tools/`` are the repo's tools. Not
    asked of a UNC path (``protected.is_network_path``): that asks the network."""
    return (
        folder is not None
        and not protected.is_network_path(folder)
        and os.path.isfile(os.path.join(folder, "tools", "protected.py"))
    )


def _names_app_id(text: str, words: list[Word], protected) -> bool:
    """Whether a word holds the app id as a path component (the cloud cache is
    ``userdata/<account>/3392510``), split as ``_builds_from_pieces`` splits words.

    Web and Steam URLs are left out (``steam://rungameid/3392510``, ``https://…/app/3392510/``;
    ``_URL``). So is the id multiplied by a number inside one word, as code builds a location
    id (``python -c "print(3392510 * 1000 + 154)"``), with no slash or quote right next to it,
    unless the command, URLs left out, names ``steam``, ``userdata`` or ``compatdata``. Any
    other use counts: ``str(3392510)``, ``d + 3392510``, ``rm -r 3392510 && …`` in ``sh -c``.
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
    (``_names_app_id``); a ``ZexionAP`` piece in a command that mentions ``LOCALAPPDATA`` (or
    ``AppData/Local``, or ``GetFolderPath(28)``; on Linux ``XDG_DATA_HOME``, ``.local/share`` or
    Flatpak Steam's ``…Steam/data``), however far apart, or that names ``TEMP`` or
    ``TMP`` and takes its parent (a ``..`` piece, ``dirname``, ``Split-Path``, ``.Parent``); or,
    in a command that mentions ``gallant`` or ``steamapps``, a ``Zexion`` piece that starts a word
    or a quoted or comma-separated piece of one, or follows a variable or a ``…Leaf`` or
    ``…common`` piece (``base = join(…, 'Gallant Leaf'); join(base, 'Zexion')``), or a ``common``
    component after a variable."""
    low = protected.normalize(text)
    if _names_app_id(text, words, protected):
        return True
    # Each word's pieces, split at quotes, commas, plus signs, parentheses and spaces as well
    # (code: join(base, 'Zexion', …)), then each piece's path components.
    pieces = dict.fromkeys(
        s for w in dict.fromkeys(words) for s in re.split(r"[\"'\s,+()]+", protected.normalize(w))
    )
    parts = [[c for c in s.split("/") if c] for s in pieces if s]
    # The offline folder built from a variable: ``d = $LOCALAPPDATA; join(d, 'ZexionAP')``.
    # ``GetFolderPath(28)`` (or ``0x1c``) is LocalApplicationData by number. The folder counts
    # only where it ends (or a ``..`` follows): ``AppData/Local/Temp/…``, every scratch, TEMP and
    # test-folder path, is not the folder itself, and the word ``ZexionAP`` beside one is no
    # build of the protected path. On Linux the same for ``XDG_DATA_HOME``, the
    # ``.local/share`` it defaults to, and Flatpak Steam's ``…Steam/data``.
    end = r"(?=/\.\.|(?![/\w]))"
    local = re.search(
        rf"local(?:app(?:lication)?)?data{end}|appdata/local{end}"
        r"|getfolderpath\([^)]*?\b(?:28|0x1c)\b"
        rf"|xdg_data_home{end}|\.local/share{end}|com\.valvesoftware\.steam/data{end}",
        low,
    )
    names_folder = any(c == "zexionap" for p in parts for c in p)
    if local and names_folder:
        return True
    # The TEMP and TMP folders are ``AppData/Local/Temp``: their parent is the folder. A command
    # that names one and takes its parent, with a ``..`` piece (``'..'``, ``TMP=..``) or a
    # parent function (``dirname``, ``Split-Path``, ``.Parent``, ``GetParent``), builds it.
    # Only the folder itself: ``/Temp`` at a path's end, or before ``/..`` or a variable, not
    # ``/Temp/scratch/…``, which every scratch path holds.
    temp = re.search(
        r"(?<!/)\b(?:temp|tmp)\b|/(?:temp|tmp)(?=/\.\.|/[$%]|(?![/\w]))|gettempdir|tempfile", low
    )
    dotdot = any(re.fullmatch(r"(?:[^=]*=)?\.\.[;&|]*", c) for p in parts for c in p)
    parent = re.search(r"dirname|split-path|\.parent\b|getparent|\bparent\b", low)
    if temp and (dotdot or parent) and names_folder:
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


# Another process's view through /proc: its folder (cwd), its root (a sandbox's whole view), its
# open files and its map's files lead into wherever it runs, the game's install and saves for the
# game's. Any pid's counts, a literal one too (a pid the command works out, by a variable, a
# substitution or a wildcard, can't be told from the game's): a command that isn't read-only is
# then blocked, while a read and a redirect's target are resolved (realpath) to where they lead.
# The shell's own (/proc/self) is left alone.
_PROC_VIEW = re.compile(
    r"/proc/(?!(?:self|thread-self)/)[^/\s]*/(?:cwd|root|fd|fdinfo|map_files|exe|task)\b"
)
# Looser, for a path built from pieces (a variable, a cd into /proc, find's {}, os.path.join): a
# proc component and a view's name anywhere in the command.
_PROC_PIECE = re.compile(r"(?<![\w.-])proc(?![\w-])")
_VIEW_PIECE = re.compile(r"(?<![\w.-])(?:cwd|root|fd|fdinfo|map_files|exe|task)(?![\w.-])")
_SHELLS_OWN = re.compile(r"/proc/(?:thread-)?self(?:/\S*)?")
# A pid worked out by a substitution, as one word for the view's test.
_WORKED_OUT = re.compile(r"\$\([^()]*\)|`[^`]*`")


def _through_proc(text: str) -> bool:
    """Whether the command may go through another process's view in /proc (its ``cwd``, ``root``,
    ``fd``, ``map_files``, ``exe`` or ``task``; quotes taken off, substitutions as one word, ``//``
    and ``/./`` collapsed; the shell's own, ``/proc/self``, left alone), whatever its pid: the
    game's install is the game's ``cwd``, and a pid the command works out can't be told from the
    game's. A ``proc`` piece with a view's name anywhere in the command counts too, for a path
    built from pieces. A command that isn't read-only is blocked wherever it leads, while reads
    and a redirect's target resolve to where they lead."""
    plain = _WORKED_OUT.sub("PID", text.replace("'", "").replace('"', ""))
    plain = re.sub(r"/(?:\.?/)+", "/", plain)
    if _PROC_VIEW.search(plain) is not None:
        return True
    plain = _SHELLS_OWN.sub(" ", plain)
    return _PROC_PIECE.search(plain) is not None and _VIEW_PIECE.search(plain) is not None


def _predicate_value(word: str) -> str:
    """A predicate's value with what a substitution leaves on a word taken off: the backticks
    around ``find ~ -name ZexionAP`` in ``rm -rf `find ~ -name ZexionAP```, and what follows
    one (``…ZexionAP`/x``); ``$(`` and a ``)`` that closes it (``…ZexionAP)/x``); and the ``$``
    of ``$'…'``, which the lexer leaves."""
    word = re.sub(r"^\$\(?|^`+", "", word)
    depth, i = 0, 0
    while i < len(word):
        c = word[i]
        if c == "\\":
            i += 1
        elif c == "`" or (c == ")" and depth == 0):
            return word[:i]
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        i += 1
    return word


def _elements(value: str) -> list[str]:
    """The names in a PowerShell value: a list (``a,b``, ``@('a','b')``) is read element by
    element."""
    return [e for e in (re.sub(r"^[@(]+|[()]+$", "", x) for x in value.split(",")) if e]


def _lister_operands(words: list[Word]) -> list[str]:
    """The operands of ``Get-ChildItem`` (``gci``, ``ls``, ``dir``) that name no path: a name
    filter is among them, whichever parameters come before it (``gci ~ -ea 0 ZexionAP``)."""
    words = list(words)
    while words and words[0] in (".", "&"):
        words.pop(0)  # dot-sourced, or called with &
    words = _strip_prefix(words, True)
    if not words or _verb(words[0]) not in PS_LISTERS:
        return []
    return [a for a in words[1:] if not re.match(_DASH, a) and not re.search(r"[\\/]", a)]


def _value_after(segments: list[list[Word]], si: int, wi: int, inline: str) -> str:
    """The value of the option at ``segments[si][wi]``: after a colon in the word, else the next
    word, which is the next command's first when a parenthesis came between (``-Include @(…)``)."""
    words = segments[si]
    if inline:
        return inline
    if wi + 1 < len(words) and words[wi + 1] != "@":
        return words[wi + 1]
    return segments[si + 1][0] if si + 1 < len(segments) and segments[si + 1] else ""


# PowerShell's ``-Filter`` and ``-Include`` take a list, written with spaces or in parentheses
# too, and ``Get-ChildItem`` takes its filter by position: read from the text, up to the end of
# the command or, for a parameter, the next one.
_PS_LIST_PARAM = re.compile(rf"(?:^|\s){_DASH}(?:{_PS_FILTER}|{_PS_INCLUDE})[:\s]\s*", re.I)
_PS_LISTER_CALL = re.compile(r"(?:^|[\s;|&({])(?:get-childitem|gci|dir|ls)\s+", re.I)
_NEXT_PARAM = re.compile(rf"[ \t]+{_DASH}[A-Za-z]")


def _ps_span(text: str, start: int, params_end: bool) -> str:
    """The arguments from ``start`` to the end of a PowerShell command: a pipe, a semicolon, an
    ampersand, a brace, a newline that no comma or backtick continues, and with ``params_end``
    the next parameter. At most ``MAX_NAME_TEXT`` + 1 characters."""
    end = start
    while end < len(text) and end - start <= MAX_NAME_TEXT:
        c = text[end]
        if c in "|;&{}" or (c == "\n" and not re.search(r"[,`]\s*$", text[start:end])):
            break
        if params_end and c in " \t" and _NEXT_PARAM.match(text, end):
            break
        end += 1
    return text[start:end]


def _ps_text_names(text: str, lister: bool, protected) -> bool:
    """Whether the names after a PowerShell ``-Filter`` or ``-Include`` (and, with ``lister``,
    after ``Get-ChildItem`` and its aliases) in ``text`` may name a protected folder: each
    element of a list, however it is written (``x, ZexionAP``, ``@('x', 'ZexionAP')``)."""
    found = [(m.end(), True) for m in _PS_LIST_PARAM.finditer(text)]
    if lister:
        found += [(m.end(), False) for m in _PS_LISTER_CALL.finditer(text)]
    for start, params_end in found:
        span = _ps_span(text, start, params_end)
        if len(span) > MAX_NAME_TEXT:
            return True
        names = [t for t in re.split(r"[\s,@()'\"`]+", span) if t and not re.match(_DASH, t)]
        if any(
            not re.search(r"[\\/]", t) and protected.pattern_may_name_protected(t, "ps", True)
            for t in names
        ):
            return True
    return False


def _names_protected(value: str, kind: str, ignore_case: bool, protected) -> bool:
    if len(value) > MAX_NAME_TEXT:
        return True
    value = _predicate_value(value)
    items = _elements(value) if kind == "ps" else [value]
    return any(protected.pattern_may_name_protected(v, kind, ignore_case) for v in items if v)


def _selects_protected_name(
    segments: list[list[Word]], protected, powershell: bool, depth: int = 0, text: str = ""
) -> bool:
    """Whether a name predicate of ``find`` (``-name``, ``-iname``, ``-path``, ``-regex``, …) or
    PowerShell's ``-Filter`` or ``-Include`` (also as ``-Filter:value``, as a list, and
    ``Get-ChildItem``'s positional filter) in the command takes a pattern that may match the
    name of a protected folder or one above it (``protected.pattern_may_name_protected``): a
    command that names it only by that name, from any folder, does not mention the folder's
    path (``find ~ -name ZexionAP -delete``). Whatever the command does with what it finds is
    judged after: ``-print`` and the reads pass, ``-delete``, ``-exec`` and an ``xargs rm`` after
    it do not. Words with a predicate in them (``sh -c 'find …'``, ``eval``) are read again.

    ``find``'s regular expression syntaxes, groups, braces, escapes and classes are read. A name
    pattern with fewer than three fixed characters (``*``, ``Z*``) is no folder's name, but it may
    be a file's: a pattern that may match the name of a file in these folders counts too,
    wherever the search starts (``protected._FILE_NAMES``: ``Save Game N.sav``, ``GameUnlocks.sav``,
    ``PreferencesSynced.sav``, ``ZexionAP-seed-<NAME>.sav``, ``steam_autocloud.vdf``, ``token``,
    ``seeds``, ``config-<build>.txt``, …): ``*.sav``, ``Save*``, ``*seed*``, ``Pref*``, ``*.txt``,
    ``?????`` and a bare ``*`` do, while ``*.py``, ``*.pyc`` and ``*foo*`` pass. A ``-path`` or
    ``-regex`` is read at its last name, which needs a fixed character (``./build/*`` names no
    file); a name over 200 characters is read as a file's, and a name or list of names over
    ``MAX_NAME_TEXT`` characters as a name. ``Zexion`` and ``ZexionAP`` are also this repo's
    names in lower case (``worlds/zexion``, ``zexion*.py``), so the predicates that compare case
    match them only as written, and ``-iname`` and PowerShell, which don't, in any case. A
    redirect's target is read as an argument is (``echo x > "$(find ~ -name ZexionAP)/seeds.json"``
    blocks, whatever the operator: ``>``, ``>>``, ``2>``, ``&>``, PowerShell's ``*>``)."""
    if text and _ps_text_names(text, powershell, protected):
        return True
    for si, words in enumerate(segments):
        operands = _lister_operands(words) if powershell else []
        if sum(map(len, operands)) > MAX_NAME_TEXT or any(
            _names_protected(op, "ps", True, protected) for op in operands
        ):
            return True
        for wi, word in enumerate(words):
            flag, colon, inline = re.sub(rf"^{_DASH}", "-", word.lower()).partition(":")
            if colon and not _PS_NAME_PARAM.fullmatch(flag):
                flag, inline = word.lower(), ""
            spec = ("ps", True) if _PS_NAME_PARAM.fullmatch(flag) else FIND_PREDICATES.get(flag)
            if spec:
                value = _value_after(segments, si, wi, inline)
                if value and _names_protected(value, *spec, protected):
                    return True
            elif depth < 3 and _NAME_PREDICATE_IN_TEXT.search(word):
                for ps in (False, True):
                    if _selects_protected_name(lex(word, ps)[0], protected, ps, depth + 1, word):
                        return True
    return False


# A command substitution with no other inside it: ``$(…)`` or, in Bash, backticks.
_SUBSTITUTION = re.compile(r"\$\(([^()`]*)\)|`([^`$()]*)`")


def _substitutions_only_read(code: str, powershell: bool) -> bool:
    r"""Whether every command substitution in ``code`` is a plain one (none inside another) whose
    commands are all read-only: ``ls -ld $(find ~ -name ZexionAP)`` reads, ``ls $(find ~ -name
    ZexionAP -delete)`` does not. In Bash an escaped backtick or ``$(`` (``"a[\`find …\`]"``,
    which a later arithmetic reads as a command) is no plain one."""
    if not powershell and ("\\`" in code or "\\$(" in code):
        return False
    for m in _SUBSTITUTION.finditer(code):
        inner = m.group(1) if m.group(1) is not None else m.group(2)
        if powershell and m.group(2) is not None:
            return False
        inner_segments, inner_targets = lex(inner, powershell)
        if inner_targets or not all(_segment_is_read_only(w, powershell) for w in inner_segments):
            return False
    rest = _SUBSTITUTION.sub(" ", code)
    return "$(" not in rest and (powershell or "`" not in rest)


def _may_reach_protected(text, words, targets, folders, protected, unknown_cd: bool) -> bool:
    """Whether the command may reach a protected folder by a path. ``folders``: where the command
    runs (the ``cwd``) and its ``cd`` targets. It may when:

    - its text names one, also with quotes removed and pieces joined
      (``protected.mentions_protected``), or it goes through another process's view in ``/proc``
      (``_through_proc``);
    - it runs in, or changes directory to, a folder in a protected one or a parent of one
      (``_folder_is_suspect``; other names in the folder's path don't count, so a worktree named
      ``steam-achievements`` works as any other);
    - a word could name one as a path (``..``, wildcards, 8.3 short names:
      ``protected.word_may_name_protected``), leads into one from where the command runs, also
      through an existing link (only for the first ``MAX_LINK_WORDS`` different words: a known
      gap), or names a parent of one (``"$APPDATA/Gallant Leaf"``, ``…/steamapps/common``,
      ``…/userdata/123``: ``protected.may_be_above_protected``, ``_names_parent``, for every
      word), so copying into, moving or deleting the parent blocks while reading it does not;
    - it may build one from pieces (``_builds_from_pieces``, the app id as a path component:
      ``_names_app_id``), or makes a link (``LINK_WORDS``) next to a folder named like ``gallant``
      or ``steam``;
    - after a ``cd`` to a folder that is unknown (``unknown_cd``), a ``..`` may lead anywhere: a
      word that goes up and then names a protected folder (``a/../../ZexionAP/x``) may reach it.

    A name predicate that may match a protected folder's name is ``_selects_protected_name``'s."""
    every = list(dict.fromkeys(words + targets))  # a word again is not read again
    links = any(w.lower() in LINK_WORDS for w in words)
    return (
        protected.mentions_protected(text)
        or _through_proc(text)
        or any(_folder_is_suspect(f, protected) for f in folders)
        or any(protected.word_may_name_protected(w) for w in every)
        or _builds_from_pieces(text, every, protected)
        or (links and any(_dir_is_suspect(w, protected) for w in words))
        or any(_names_parent(w, folders, protected) for w in every)
        # Each word is read from every folder, with the file system asked about links: only the
        # first ``MAX_LINK_WORDS`` are (a known gap: the docstring).
        or any(_reaches_protected(w, folders, protected) for w in every[:MAX_LINK_WORDS])
        or (unknown_cd and any(_goes_up_to_protected(str(w), protected) for w in every))
    )


def _command_assigns(words: list[Word]) -> set[str]:
    """The variables (lower case) a simple command sets other than as ``NAME=value``: ``read``,
    ``for``, ``printf -v``, PowerShell's ``Set-Variable``, ``sv``, ``New-Variable``, ``nv`` and
    ``Set-Item env:NAME``."""
    words = _strip_prefix(words, False)
    if not words:
        return set()
    verb, args = _verb(words[0]), [str(w) for w in words[1:]]
    plain = [a for a in args if not a.startswith("-")]
    if verb == "read":
        return {a.lower() for a in plain}
    if verb == "for":
        return {args[0].lower()} if args else set()
    if verb == "printf" and "-v" in args and args.index("-v") + 1 < len(args):
        return {args[args.index("-v") + 1].lower()}
    if verb in ("set-variable", "sv", "new-variable", "nv"):
        named = _option_value([Word(a) for a in args], r"-name")
        return {n.lower() for n in (named or plain[:1])}
    if verb in ("set-item", "si"):
        paths = [*_option_value([Word(a) for a in args], r"-(?:literal)?path"), *plain[:1]]
        return {p[4:].lower() for p in paths if p.lower().startswith("env:")}
    return set()


def _assigned_names(code: str, segments: list[list[Word]]) -> set[str]:
    """Variables the command sets itself, which the hook's environment says nothing about."""
    names = re.findall(r"(?:^|[\s;&|(])([A-Za-z_]\w*)\+?=", code)
    names += re.findall(r"\bfor\s+([A-Za-z_]\w*)\s+in\b", code)
    names += re.findall(
        r"\$(?:(?:global|script|local|private|env):)?(\w+)\s*(?:[-+*/%]|\?\?)?=(?!=)", code, re.I
    )
    names += re.findall(r"\$\{(?:env:)?(\w+)\}\s*(?:[-+*/%]|\?\?)?=(?!=)", code, re.I)
    names += re.findall(r"SetEnvironmentVariable\(\s*['\"](\w+)['\"]", code, re.I)
    names += re.findall(r"foreach\s*\(\s*\$(\w+)", code, re.I)
    found = {n.lower() for n in names}
    for words in segments:
        found |= _command_assigns(words)
    return found


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
    or a method outside quotes other than a few string and date ones (``.Delete()``,
    ``.'Delete'()``, ``.$name()``)."""
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


def _quotes_taken_off(code: str) -> str:
    """``code`` with Bash's quoting taken off, as Bash reads it: '…' and "…" lose their quotes,
    and $'…' (only outside other quotes) is decoded. A ``$(`` that quotes split
    (``'a[$''(…)]'``, ``$'a[\\x24(…)]'``) shows in it: Bash runs one inside a value it later
    reads as arithmetic (``$[x]``, ``${s:x}``, ``${a[x]}``, ``${!x}``), as it does in
    ``$((x))``. A backslash and the character after it are kept as they are."""
    out, i, n = [], 0, len(code)
    while i < n:
        c = code[i]
        ansi_c = _ANSI_C.match(code, i)
        if c == "\\":
            out.append(code[i : i + 2])
            i += 2
        elif ansi_c:
            try:
                text = ansi_c.group(1).encode("latin-1", "backslashreplace")
                with warnings.catch_warnings():  # \e and \cX are Bash's, not Python's
                    warnings.simplefilter("ignore")
                    out.append(text.decode("unicode_escape"))
            except (UnicodeError, ValueError):
                out.append(ansi_c.group(1))
            i = ansi_c.end()
        elif c == "'":
            end = code.find("'", i + 1)
            end = n if end < 0 else end
            out.append(code[i + 1 : end])
            i = end + 1
        elif c == '"':
            j = i + 1
            while j < n and code[j] != '"':
                j += 2 if code[j] == "\\" else 1
            out.append(code[i + 1 : j])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def _blanked(code: str, words: list[Word]) -> str:
    """``code`` with the given words (from it) replaced by spaces."""
    out, pos = [], 0
    for w in sorted(words, key=lambda w: w.start):
        out.append(code[pos : w.start])
        out.append(" " * (w.end - w.start))
        pos = w.end
    out.append(code[pos:])
    return "".join(out)


# A redirect's target that is worked out when the command runs: a variable still unexpanded, a
# substitution (``$(…)``, a backtick), or PowerShell's ``> (expr)`` (the lexer's ``(``).
_DYNAMIC_TARGET = re.compile(r"[$%`]")
_WILDCARD = re.compile(r"[*?\[{~]")  # what makes a word cost much more to read as a path


def _work(word: str) -> int:
    """What reading a word costs the budget: 1; a word with a wildcard is read as a pattern, 8
    and an eighth of its length, at most that of ``MAX_NAME_TEXT``, if that is more (a name pattern
    is read in every reading)."""
    return max(8, min(len(word), MAX_NAME_TEXT) // 8) if _WILDCARD.search(word) else 1


class _Memo:
    """``protected`` with its functions of one string remembered for one command, so a word
    that comes up again (``cd a; cd a``) is not read again. Each one that is not remembered
    first looks at the clock (``_tick``). ``__getattr__`` hands out the others as they are."""

    PURE = frozenset({
        "normalize", "_expand", "mentions_protected", "word_may_name_protected",
        "may_be_above_protected", "holds_protected", "is_ancestor_folder",
        "pattern_may_name_protected",
        "is_protected_path",
    })  # fmt: skip

    def __init__(self, module) -> None:
        self._module = module
        self._seen: dict[tuple, object] = {}

    def __getattr__(self, name: str):
        attr = getattr(self._module, name)
        if name not in self.PURE:
            return attr
        seen = self._seen

        def remembered(*args):
            key = (name, *args)
            if key not in seen:
                _tick()
                seen[key] = attr(*args)
            return seen[key]

        setattr(self, name, remembered)
        return remembered


def check(tool_name: str, tool_input: dict, protected, session_cwd: str = "") -> str | None:
    """Return a reason to block, or None to allow. ``protected`` is the tools/protected module.

    ``session_cwd`` is the payload's top-level ``cwd``: where a shell tool without its own
    ``cwd`` runs. A command that takes longer than ``DEADLINE`` to read is blocked.
    """
    global _due
    _due = time.monotonic() + DEADLINE
    try:
        return _check(tool_name, tool_input, protected, session_cwd)
    except _OutOfTime:
        return f"{BLOCK_HINT}\nToo long or too complex to check: reading it took over {DEADLINE} s."
    finally:
        _due = float("inf")


@dataclass
class _Parsed:
    """A shell command as ``_check`` reads it."""

    powershell: bool
    cwd: str  # where it runs, as far as the payload says
    code: str  # the command, with the heredoc bodies that are only data cut out
    segments: list[list[Word]]  # its simple commands
    targets: list[Word]  # its redirects' targets
    text_ids: set[int]  # the words that are message text (ids), read as no path
    words: list[Word]  # the others
    text: str  # ``code`` with the message text blanked
    over: bool  # past the budget: not read word by word
    assigned: set[str]  # the variables it sets itself
    before: list[str | None]  # the folder each simple command runs in
    cds: list[str | None]  # the folders its ``cd``s go to (None: unknown)
    folders: list[str | None]  # where it runs, and its ``cd`` targets
    unknown_cd: bool


def _parse(command: str, powershell: bool, cwd: str, protected) -> _Parsed:
    """Split a shell command into what the rules below read, and see whether it is past the
    budget (``MAX_SEGMENTS``, ``MAX_WORK``, ``MAX_FOLDERS``)."""
    code = _here_strings_to_quotes(command) if powershell else _cut_heredocs(command)
    segments, targets = lex(code, powershell)
    text_ids = _text_words(segments, powershell)
    data_ids = _sed_data_words(segments, powershell)  # no path, but its text is still read
    words = [w for s in segments for w in s if id(w) not in text_ids and id(w) not in data_ids]
    text = _blanked(code, [w for s in segments for w in s if id(w) in text_ids])
    unique = {str(w) for w in words}  # message text is read as no path
    work = sum(_work(w) for w in unique) + 8 * len({str(t) for t in targets})
    over = len(segments) > MAX_SEGMENTS or work > MAX_WORK
    assigned: set[str] = set() if over else _assigned_names(code, segments)  # unread when over
    before, cds = (
        ([], []) if over else _walk_cds(segments, cwd, code, protected, powershell, assigned)
    )
    distinct = list(dict.fromkeys(cds))  # the same folder again is not read again
    over = over or len(distinct) > MAX_FOLDERS
    return _Parsed(
        powershell, cwd, code, segments, targets, text_ids, words, text, over, assigned, before,
        cds, ([cwd] if cwd else []) + distinct, None in cds,
    )  # fmt: skip


def _reach(p: _Parsed, protected) -> tuple[bool, bool, str | None]:
    """How the command may reach a protected folder: by a path, by a name alone, and, as a reason,
    by acting on a folder above one. Past the budget it is not read word by word and is taken to
    reach one by a path."""
    if p.over:
        return True, False, None
    by_path = _may_reach_protected(p.text, p.words, p.targets, p.folders, protected, p.unknown_cd)
    kept = [[w for w in s if id(w) not in p.text_ids] for s in p.segments]
    # A redirect's target is read as an argument is: ``> "$(find ~ -name ZexionAP)/x"``.
    by_name = _selects_protected_name(
        kept + [[t] for t in p.targets], protected, p.powershell, text=p.text
    )
    # A command that deletes, moves out, changes or searches-and-acts on a folder above a
    # protected one (``~``, ``"$APPDATA"``, ``/``), where it runs or where its operand is.
    above = _ancestor_violation(
        p.segments, p.cwd or os.getcwd(), p.text_ids, protected, p.powershell, p.code,
        before=p.before,
    )  # fmt: skip
    return by_path, by_name, above


def _substitution_violation(p: _Parsed, by_name: bool, by_path: bool) -> str | None:
    """A reason when a command that may reach a protected folder holds a command substitution or,
    in PowerShell, a method or .NET call that may write. A substitution is ``$(`` (Bash
    arithmetic ``$((`` included; ``xxd -s 0x9360`` takes the offset itself) or, in Bash, a
    backtick, and in Bash also once its quoting is taken off (``_quotes_taken_off``: a ``$(``
    split by quotes in a value Bash later reads as arithmetic, through ``$[x]``, ``${s:x}`` or
    ``${a[x]}``). Not blocked: the ``"$(cat <<'EOF' … EOF)"`` message form, PowerShell's
    ``"$($_.Name)"``, which only reads a member, and, in a command reached only by a name whose
    every command reads, plain substitutions that read (``ls -ld $(find ~ -name ZexionAP)``;
    ``cd $(find …) && git clean -fdx`` is blocked). A PowerShell ``::`` call other than a known
    reader or a method call other than a few string and date ones is blocked
    (``_ps_calls_block``)."""
    substitution = _CAT_HEREDOC.sub("heredoc", p.code)
    if p.powershell:  # "$($_.Name)" only reads a member
        substitution = _PS_MEMBER_SUBEXPR.sub("member", substitution)
    else:  # also with Bash's quoting taken off: a value Bash later reads as arithmetic runs it
        substitution += "\n" + _quotes_taken_off(substitution)
    # Reached only by a name, every command a read and every substitution a plain one of
    # reads (also as quoting is taken off): ``ls -ld $(find ~ -name X)``, not ``cd $(…) &&
    # git clean``, which runs in the folder the substitution found.
    reads = (
        by_name
        and not by_path
        and all(_segment_is_read_only(w, p.powershell) for w in p.segments)
        and _substitutions_only_read(substitution, p.powershell)
    )
    if not reads and ("$(" in substitution or (not p.powershell and "`" in substitution)):
        return "Command substitution next to a protected path."
    if p.powershell and (call := _ps_calls_block(p.code)):
        return f"A method or .NET call next to a protected path: {call}"
    return None


def _redirect_violation(p: _Parsed, protected) -> str | None:
    """A reason when a redirect's target may be a protected path, or is worked out when the command
    runs (``_DYNAMIC_TARGET``: a variable, a substitution or a backtick still in it after the
    hook's own environment is applied, or PowerShell's ``> (expression)``): the guard can't see
    where it leads. That also blocks a target that is no file (Bash's ``> >(cat)``, PowerShell's
    ``> (Join-Path $env:TEMP x)``) in a command that already names a protected folder; a
    parenthesis inside quotes is part of a name."""
    for target in p.targets:
        if target.startswith("&") or target.lower() in HARMLESS_TARGETS:
            continue
        reason = f"Redirect target: {target}"
        if p.over:
            return reason
        # A variable the command sets itself (``TEMP="<folder>"; … > $TEMP/x``) is not the one
        # in the hook's environment, which ``protected.normalize`` would put in its place.
        refs = {next(g for g in m.groups() if g).lower() for m in _ENV_REF.finditer(target)}
        if refs & p.assigned:
            return reason
        expanded = protected.normalize(_expand_env(target, p.assigned))
        if (
            protected.is_protected_path(expanded)  # also by its words: Gallant*, GALLAN~1
            or _reaches_protected(expanded, p.folders, protected)
            or (p.unknown_cd and _goes_up_to_protected(str(target), protected))
            or _DYNAMIC_TARGET.search(expanded)
            or "(" in target.bare  # PowerShell's ``> (expr)``; one in quotes is a name
        ):
            return reason
    return None


def _command_violation(p: _Parsed, protected) -> str | None:
    """A reason when a simple command is not recognizably read-only, a tool of this repo or a
    ``git`` or ``gh`` command that names no protected folder, or sets a variable that names a
    program (``_PROGRAM_VAR``: ``GIT_PAGER``, ``LESSOPEN``, ``PATH``, …: in Bash an environment
    prefix or ``export`` of one blocks, even before a read-only command)."""
    suspect_dir = p.over or any(_folder_is_suspect(f, protected) for f in p.folders)
    # After a cd, tools/ is the repo's own only in a checkout of the repo.
    repo_tools = all(_repo_checkout(f, protected) for f in p.cds)
    for words in p.segments:
        _tick()
        if not p.powershell and (program := _sets_program(words)):
            return f"An environment prefix that names a program: {program}"
        if _segment_is_read_only(words, p.powershell):
            continue
        if not suspect_dir and (
            (repo_tools and _is_venv_tool(words, p.powershell))
            or _is_repo_vcs(words, p.text_ids, p.folders, protected, p.powershell)
        ):
            continue
        shown = " ".join(w for w in words if id(w) not in p.text_ids)
        return f"Not a read-only command: {shown}"
    return None


def _check(tool_name: str, tool_input: dict, protected, session_cwd: str):
    """``check`` without the deadline. A shell command goes through these phases, each of which
    gives a reason to block or None: ``_parse``, ``_reach`` (a command that cannot reach a
    protected folder is allowed here), then ``_substitution_violation``, ``_redirect_violation``
    and ``_command_violation``."""
    if tool_name in {"Write", "Edit", "MultiEdit", "NotebookEdit"}:
        path = tool_input.get("file_path") or tool_input.get("notebook_path") or ""
        if path and protected.is_protected_path(path):
            return f"{BLOCK_HINT}\nTarget: {path}"
        return None

    if tool_name not in SHELL_TOOLS:
        return None
    powershell = SHELL_TOOLS[tool_name]
    command = str(tool_input.get("command") or "")
    if len(command) > MAX_COMMAND:
        return f"{BLOCK_HINT}\nA command of {len(command):,} characters is too long to check."
    protected = _Memo(protected)
    cwd = str(tool_input.get("cwd") or session_cwd or "")
    p = _parse(command, powershell, cwd, protected)
    by_path, by_name, above = _reach(p, protected)
    if not (by_path or by_name or above):
        return None
    detail = (
        above
        or _substitution_violation(p, by_name, by_path)
        or _redirect_violation(p, protected)
        or _command_violation(p, protected)
    )
    if not detail:
        return None
    if p.over:
        detail = f"Too long or too complex to check, so read as reaching one. {detail}"
    return f"{BLOCK_HINT}\n{detail}"


def _mentions_sensitive(text: str) -> bool:
    low = text.lower()
    return any(s in low for s in ("gallant", "steamapps", "3392510", "zexionap"))


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
