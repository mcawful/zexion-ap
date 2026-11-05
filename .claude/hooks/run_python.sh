#!/bin/sh
# Run a hook script from this folder with Python 3.11 or later, wherever Python is here:
#
#   sh "${CLAUDE_PROJECT_DIR}/.claude/hooks/run_python.sh" guard_protected.py
#
# Claude Code runs hook commands in sh (Linux, macOS) or Git Bash (Windows). A bare `python`
# is missing on many machines (Debian, Ubuntu and macOS have only python3), is the Microsoft
# Store stub on some Windows ones, and was Python 2 on old ones; Claude Code lets a tool call
# through when its hook exits with anything but 0 or 2, so such a hook would be off without a
# word. This tries, in order, the repo's .venv (Windows, then Linux and macOS layout),
# python3, python and the Windows launcher `py -3`, and keeps the first that runs the script.
#
# Each try runs the script through RUN below, so that 0 or 2 is the script's own verdict:
# RUN exits 3 on a Python older than 3.11 (which may parse the script yet break in it), and a
# script it cannot open or import ends in a traceback (1), never in Python's own exit 2 for a
# file it cannot open. Any other code (not found, a stub, a SyntaxError) moves on to the next
# candidate, its stderr dropped.
#
# With none, a call to guard_protected.py whose payload mentions the saves or a Steam folder is
# blocked (exit 2), as the guard does when it breaks; anything else goes ahead with a hook
# error notice (exit 1) naming the fix.

CDPATH=
hooks=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$(dirname "$hooks")")
script=$1
# The payload, read once and given to each try. The x keeps a trailing newline.
input=$(cat; echo x)
input=${input%x}

# Valid Python 2 and 3 alike, so Python 2 reaches the version check. The script's folder goes
# first on sys.path, as when Python runs a script itself.
RUN='import os, sys
if sys.version_info < (3, 11):
    sys.exit(3)
import runpy
sys.argv = sys.argv[1:]
sys.path[0] = os.path.dirname(sys.argv[0])
runpy.run_path(sys.argv[0], run_name="__main__")'

try() {
    command -v "$1" >/dev/null 2>&1 || return 1
    { err=$(printf '%s' "$input" | "$@" -c "$RUN" "$hooks/$script" 2>&1 1>&3); status=$?; } 3>&1
    case $status in
        0 | 2)
            [ -n "$err" ] && printf '%s\n' "$err" >&2
            exit "$status"
            ;;
    esac
    return 1
}

try "$root/.venv/Scripts/python.exe"
try "$root/.venv/bin/python"
try python3
try python
try py -3

printf 'No Python 3.11 or later could run .claude/hooks/%s (tried .venv, python3, python, py -3).\n' \
    "$script" >&2
printf "Create .venv as CLAUDE.md's Setup shows, or put Python 3.11 or later on PATH.\n" >&2
# The words guard_protected.py fails closed on, any case. A case pattern, not grep: Git for
# Windows' grep -i with several -e patterns aborts (exit 134, seen 2026-10-26).
if [ "$script" = guard_protected.py ]; then
    case $input in
        *[Gg][Aa][Ll][Ll][Aa][Nn][Tt]* | *[Ss][Tt][Ee][Aa][Mm][Aa][Pp][Pp][Ss]* | *3392510*)
            echo "This call mentions the saves or a Steam folder, so it is blocked." >&2
            exit 2
            ;;
    esac
fi
exit 1
