"""client/procfs.py against a stand-in ``/proc`` (and the real one, on Linux)."""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

from client import procfs


def _proc(tmp_path: Path, processes: dict[str, bytes], nspid: str = "100") -> Path:
    """A ``/proc`` of ``processes`` (id to comm), with ``self`` in a namespace of ``nspid``."""
    proc = tmp_path / "proc"
    proc.mkdir()
    for pid, comm in processes.items():
        _process(proc, pid, comm)
    (proc / "self").mkdir()  # not a process id
    (proc / "self" / "comm").write_bytes(b"Zexion\n")
    (proc / "self" / "status").write_text(f"Name:\tpython\nNSpid:\t{nspid}\n")
    return proc


def _process(proc: Path, pid: str, comm: bytes, cmdline: bytes = b"", exe: str = "") -> None:
    (proc / pid).mkdir()
    (proc / pid / "comm").write_bytes(comm)
    (proc / pid / "cmdline").write_bytes(cmdline)
    if exe:
        (proc / pid / "exe").symlink_to(exe)


def _root(tmp_path: Path) -> Path:
    root = tmp_path / "root"
    root.mkdir(exist_ok=True)
    return root


def test_a_process_is_found_by_its_kernel_name(tmp_path: Path) -> None:
    proc = _proc(tmp_path, {"1": b"systemd\n", "4242": b"Zexion\n", "77": b"Zexion.exe\n"})
    root = _root(tmp_path)
    assert procfs.find_pid("Zexion", proc, root) == 4242
    assert procfs.find_pid(("Zexion.exe",), proc, root) == 77
    assert procfs.find_pid(("Zexion", "Zexion.exe"), proc, root) == 77  # the lower id first
    assert procfs.find_pid("zexion", proc, root) is None  # the name is exact


def test_a_name_longer_than_the_kernel_keeps_is_cut_as_it_cuts(tmp_path: Path) -> None:
    proc = _proc(tmp_path, {"9": b"ArchipelagoLaun\n"})
    assert procfs.find_pid("ArchipelagoLauncher", proc, _root(tmp_path)) == 9


@pytest.mark.skipif(sys.platform == "win32", reason="links need a privilege on Windows")
def test_a_renamed_main_thread_is_still_found_by_its_exe(tmp_path: Path) -> None:
    proc = _proc(tmp_path, {})
    _process(proc, "31", b"MainThread\n", exe="/games/Zexion/Zexion (deleted)")
    assert procfs.find_pid("Zexion", proc, _root(tmp_path)) == 31


def test_under_proton_the_exe_is_found_in_the_command_line(tmp_path: Path) -> None:
    proc = _proc(tmp_path, {})
    cmdline = b"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Zexion\\Zexion.exe\0"
    _process(proc, "52", b"wine-preloader\n", cmdline)
    assert procfs.find_pid("Zexion.exe", proc, _root(tmp_path)) == 52


def test_a_process_that_ends_while_listed_is_skipped(tmp_path: Path) -> None:
    proc = _proc(tmp_path, {"5": b"Zexion\n"})
    (proc / "3").mkdir()  # no comm: gone between the listing and the read
    assert procfs.find_pid("Zexion", proc, _root(tmp_path)) == 5


@pytest.mark.skipif(
    sys.platform == "win32" or os.geteuid() == 0, reason="needs POSIX modes, not as root"
)
def test_a_process_of_this_user_that_cant_be_read_is_an_error(tmp_path: Path) -> None:
    proc = _proc(tmp_path, {"8": b"Zexion\n"})
    (proc / "8" / "comm").chmod(0)
    try:
        with pytest.raises(procfs.ProcessError, match="process 8 can't be read"):
            procfs.find_pid("Zexion", proc, _root(tmp_path))
    finally:
        (proc / "8" / "comm").chmod(0o644)


@pytest.mark.parametrize(
    ("place", "said"),
    [("nspid", "process-id namespace"), ("flatpak", "Flatpak"), ("wsl", "WSL")],
)
def test_a_sandbox_that_hides_processes_is_an_error_not_an_absence(
    tmp_path: Path, place: str, said: str
) -> None:
    proc = _proc(tmp_path, {}, nspid="8123\t2" if place == "nspid" else "100")
    root = _root(tmp_path)
    if place == "flatpak":
        (root / ".flatpak-info").write_text("[Application]\n")
    if place == "wsl":
        (proc / "sys" / "fs" / "binfmt_misc").mkdir(parents=True)
        (proc / "sys" / "fs" / "binfmt_misc" / "WSLInterop").write_text("enabled\n")
    with pytest.raises(procfs.ProcessError, match=said):
        procfs.find_pid("Zexion", proc, root)


def _wsl_proc(tmp_path: Path, how: str) -> Path:
    """A stand-in ``/proc`` with the one sign of WSL ``how`` names (none for "none")."""
    proc = _proc(tmp_path, {})
    if how in ("interop", "interop-late", "other"):
        binfmt = proc / "sys" / "fs" / "binfmt_misc"
        binfmt.mkdir(parents=True)
        entry = {"interop": "WSLInterop", "interop-late": "WSLInterop-late", "other": "python3"}
        (binfmt / entry[how]).write_text("enabled\n")
    elif how in ("wsl2", "wsl1", "native"):
        (proc / "sys" / "kernel").mkdir(parents=True)
        release = {
            "wsl2": "5.15.153.1-microsoft-standard-WSL2",
            "wsl1": "4.4.0-19041-Microsoft",
            "native": "6.8.0-45-generic",
        }[how]
        (proc / "sys" / "kernel" / "osrelease").write_text(release + "\n")
    return proc


@pytest.mark.parametrize("how", ["interop", "interop-late", "wsl2", "wsl1"])
def test_wsl_is_found_by_any_of_its_signs(tmp_path: Path, how: str) -> None:
    # WSL 2 with systemd registers WSLInterop-late; with interop off no handler is there at all.
    proc = _wsl_proc(tmp_path, how)
    assert "WSL" in (procfs._blind_spot(proc, _root(tmp_path), {}) or "")
    with pytest.raises(procfs.ProcessError, match="WSL"):
        procfs.find_pid("Zexion", proc, _root(tmp_path))


@pytest.mark.parametrize("variable", ["WSL_DISTRO_NAME", "WSL_INTEROP"])
def test_wsl_is_found_by_the_variables_it_sets_with_no_other_sign(
    tmp_path: Path, variable: str
) -> None:
    proc = _wsl_proc(tmp_path, "none")
    assert procfs._blind_spot(proc, _root(tmp_path), {}) is None
    assert "WSL" in (procfs._blind_spot(proc, _root(tmp_path), {variable: "Ubuntu"}) or "")


@pytest.mark.parametrize("how", ["none", "native", "other"])
def test_no_sign_of_wsl_is_no_blind_spot(tmp_path: Path, how: str) -> None:
    proc = _wsl_proc(tmp_path, how)
    environ = {"HOME": "/home/u", "WSL_DISTRO_NAME": ""}
    assert procfs._blind_spot(proc, _root(tmp_path), environ) is None
    assert procfs.find_pid("Zexion", proc, _root(tmp_path)) is None


def test_the_variables_count_only_for_the_real_proc(tmp_path: Path, monkeypatch) -> None:
    # A stand-in /proc is no one's: a developer in WSL running these tests is not refused.
    monkeypatch.setenv("WSL_DISTRO_NAME", "Ubuntu")
    proc = _wsl_proc(tmp_path, "none")
    assert procfs._blind_spot(proc, _root(tmp_path)) is None
    monkeypatch.setattr(procfs, "PROC", proc)
    assert "WSL" in (procfs._blind_spot(proc, _root(tmp_path)) or "")


def test_an_unreadable_proc_is_an_error_not_an_absence(tmp_path: Path) -> None:
    with pytest.raises(procfs.ProcessError):
        procfs.find_pid("Zexion", tmp_path / "missing", _root(tmp_path))


@pytest.mark.skipif(not sys.platform.startswith("linux"), reason="/proc is Linux's")
def test_this_process_is_found_in_the_real_proc() -> None:
    if procfs._blind_spot(procfs.PROC, procfs.ROOT):
        pytest.skip("this runs in a sandbox: find_pid refuses there")
    comm = Path(f"/proc/{os.getpid()}/comm").read_text().strip()
    found = procfs.find_pid(comm)
    assert found is not None
    assert comm in Path(f"/proc/{found}/comm").read_text()
