#!/bin/sh
# Run a hook script from this folder with Python 3, wherever Python is on this machine:
#
#   sh "${CLAUDE_PROJECT_DIR}/.claude/hooks/run_python.sh" guard_protected.py
#
# Claude Code runs hook commands in sh (Linux, macOS) or Git Bash (Windows). A bare `python`
# is missing on many machines (Debian, Ubuntu and macOS have only python3), is the Microsoft
# Store stub on some Windows ones, and was Python 2 on old ones; Claude Code lets a tool call
# through when its hook exits with anything but 0 or 2, so such a hook would be off without a
# word. This tries, in order, the repo's .venv (Windows, then Linux and macOS layout),
# python3, python and the Windows launcher `py -3`, and keeps the first whose run of the
# script ends in 0 or 2 (the hook scripts here exit with nothing else). One that cannot run
# it (not found, a stub, a SyntaxError) is skipped, its stderr dropped.
#
# With none, it fails as guard_protected.py does when it breaks: a call whose payload mentions
# the saves or a Steam folder is blocked (exit 2); anything else goes ahead with a hook error
# notice (exit 1) naming the fix.

hooks=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$(dirname "$hooks")")
script=$1
# The payload, read once and given to each try. The x keeps a trailing newline.
input=$(cat; echo x)
input=${input%x}

try() {
    command -v "$1" >/dev/null 2>&1 || return 1
    { err=$(printf '%s' "$input" | "$@" "$hooks/$script" 2>&1 1>&3); status=$?; } 3>&1
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

reason="No Python 3 could run .claude/hooks/$script (tried .venv, python3, python, py -3)."
fix="Create .venv as CLAUDE.md's Setup shows, or put Python 3.11 or later on PATH."
# The words guard_protected.py fails closed on, any case. A case pattern, not grep: Git for
# Windows' grep -i with several -e patterns aborts (exit 134, seen 2026-10-26).
case $input in
    *[Gg][Aa][Ll][Ll][Aa][Nn][Tt]* | *[Ss][Tt][Ee][Aa][Mm][Aa][Pp][Pp][Ss]* | *3392510*)
        printf '%s\n%s\nThis call mentions the saves or a Steam folder, so it is blocked.\n' \
            "$reason" "$fix" >&2
        exit 2
        ;;
esac
printf '%s\n%s\n' "$reason" "$fix" >&2
exit 1
