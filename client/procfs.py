"""Processes on Linux, through ``/proc``.

A process matches a name by any of: ``/proc/<pid>/comm`` (the kernel's name for it, its
executable's file name cut to 15 bytes, which a thread may rename), the file name of
``/proc/<pid>/exe``, or the file name of an entry of ``/proc/<pid>/cmdline`` (split at ``/``
and ``\\``: under Wine or Proton the command line holds the Windows path). Zexion's Linux build
is assumed to run as ``Zexion`` and the Windows exe under Proton as ``Zexion.exe``; neither name
has been seen with the running game yet.

What is not seen is not known to be absent: ``find_pid`` raises ``ProcessError`` rather than
answer None when this process is in a sandbox whose ``/proc`` shows only its own processes (a
nested process-id namespace, a Flatpak, WSL), or when a process of this user can't be read.
"""

from __future__ import annotations

import os
import re
from pathlib import Path

PROC = Path("/proc")
ROOT = Path("/")
COMM_MAX = 15  # the kernel's TASK_COMM_LEN, less its NUL


class ProcessError(OSError):
    """``/proc`` could not be read, or does not show every process, so whether a process is
    there is not known."""


def _blind_spot(proc: Path, root: Path) -> str | None:
    """Why ``proc`` may not show every process of this user, or None."""
    try:
        status = (proc / "self" / "status").read_text(errors="replace")
    except OSError as exc:
        return f"{proc / 'self' / 'status'} can't be read ({exc})"
    nspid = re.search(r"^NSpid:\s*(.*)$", status, re.MULTILINE)
    if nspid and len(nspid.group(1).split()) > 1:
        return "this runs in a process-id namespace of its own, which hides the game"
    if (root / ".flatpak-info").exists():
        return "this runs inside a Flatpak sandbox, which hides the game"
    if (proc / "sys" / "fs" / "binfmt_misc" / "WSLInterop").exists():
        return "this runs in WSL, whose processes do not include Windows'"
    return None


def _names(proc: Path, entry: str, cmdline: bool = True) -> set[bytes]:
    """The names process ``entry`` goes by. FileNotFoundError or ProcessLookupError when it has
    ended; PermissionError when it isn't ours to read."""
    pid_dir = proc / entry
    names = {(pid_dir / "comm").read_bytes().rstrip(b"\n")}
    try:
        target = os.readlink(pid_dir / "exe")
    except OSError:
        target = ""  # a kernel thread (no link), or not ours: comm and cmdline still say
    if target:
        names.add(os.path.basename(target.removesuffix(" (deleted)")).encode())
    if not cmdline:
        return names
    for arg in (pid_dir / "cmdline").read_bytes().split(b"\0"):
        if arg:
            names.add(re.split(rb"[/\\]", arg)[-1])
    return names


def find_pids(
    names: tuple[str, ...] | str, proc: Path = PROC, root: Path = ROOT, cmdline: bool = True
) -> list[int]:
    """The ids of the running processes going by one of ``names`` (exact; ``comm`` compared
    after the kernel's cut to 15 bytes), lowest first. ``cmdline=False`` leaves the command
    line's names out: a launcher whose command line ends in the game's path (Steam's wrappers)
    then does not match, nor does a Windows exe under Wine. ``ProcessError`` when ``proc`` can't
    be listed, may not show every process, or holds a process of this user that can't be read.
    A process that ends while it is read is skipped."""
    if isinstance(names, str):
        names = (names,)
    full = {name.encode() for name in names}
    cut = {name[:COMM_MAX] for name in full}
    blind = _blind_spot(proc, root)
    if blind:
        raise ProcessError(f"not every process can be seen: {blind}")
    try:
        entries = os.listdir(proc)
    except OSError as exc:
        raise ProcessError(f"listing processes in {proc}: {exc}") from exc
    found_pids = []
    for entry in sorted((e for e in entries if e.isdigit()), key=int):
        try:
            found = _names(proc, entry, cmdline)
        except (FileNotFoundError, ProcessLookupError):
            continue  # gone between the listing and the read
        except PermissionError as exc:
            try:
                ours = (proc / entry).stat().st_uid == os.getuid()
            except OSError:
                continue  # gone
            if ours:
                raise ProcessError(f"process {entry} can't be read: {exc}") from exc
            continue  # another user's: not the game
        if found & full or found & cut:
            found_pids.append(int(entry))
    return found_pids


def find_pid(names: tuple[str, ...] | str, proc: Path = PROC, root: Path = ROOT) -> int | None:
    """The lowest id of ``find_pids`` (command lines included), or None."""
    pids = find_pids(names, proc, root)
    return pids[0] if pids else None
