"""Finding the install and the saves (tools/protected.py) and fingerprinting the build
(tools/build_id.py) for Linux, in a stand-in home folder."""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path

import pytest
from fakes import ELF_BUILD_ID, elf_image, pe_image

import build_id
import protected

FLATPAK = Path(".var/app/com.valvesoftware.Steam")


@pytest.fixture
def home(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    """A Linux home folder with nothing in it, and no overrides."""
    home = tmp_path / "home"
    home.mkdir()
    monkeypatch.setattr(protected.sys, "platform", "linux")
    monkeypatch.setattr(protected.Path, "home", classmethod(lambda cls: home))
    for name in ("ZEXION_SAVE_DIR", "ZEXION_GAME_DIR", "XDG_DATA_HOME"):
        monkeypatch.delenv(name, raising=False)
    return home


def _install(library: Path, name: str = "Zexion", data: bytes | None = None) -> Path:
    install = library / "steamapps" / "common" / "Zexion"
    (install / "Files").mkdir(parents=True)
    (install / name).write_bytes(elf_image() if data is None else data)
    return install


def test_flatpak_steams_install_is_found(home: Path) -> None:
    install = _install(home / FLATPAK / ".local/share/Steam")
    assert protected.game_dir() == install


def test_an_install_in_another_library_is_found_through_libraryfolders(home: Path) -> None:
    root = home / ".local/share/Steam"
    (root / "steamapps").mkdir(parents=True)
    other = home / "games" / "SteamLibrary"
    (root / "steamapps" / "libraryfolders.vdf").write_text(
        f'"libraryfolders"\n{{\n\t"0"\n\t{{\n\t\t"path"\t\t"{root}"\n\t}}\n'
        f'\t"1"\n\t{{\n\t\t"path"\t\t"{other}"\n\t}}\n}}\n'
    )
    install = _install(other)
    assert protected.game_dir() == install


@pytest.mark.skipif(sys.platform == "win32", reason="links need a privilege on Windows")
def test_the_steam_link_and_its_target_count_once(home: Path) -> None:
    root = home / ".local/share/Steam"
    root.mkdir(parents=True)
    (home / ".steam").mkdir()
    (home / ".steam" / "steam").symlink_to(root)
    assert protected._steam_roots() == [home / ".steam" / "steam"]


def test_an_install_under_proton_has_the_exe(home: Path) -> None:
    install = _install(home / ".local/share/Steam", "Zexion.exe", pe_image())
    assert protected.game_dir() == install
    assert protected.game_file(install) == install / "Zexion.exe"


def test_no_install_says_what_to_set(home: Path) -> None:
    with pytest.raises(RuntimeError, match="set ZEXION_GAME_DIR"):
        protected.game_dir()


def test_two_installs_are_an_error_not_a_guess(home: Path) -> None:
    _install(home / ".local/share/Steam")
    _install(home / FLATPAK / ".local/share/Steam")
    with pytest.raises(RuntimeError, match="in 2 places.*set ZEXION_GAME_DIR"):
        protected.game_dir()


def test_the_windows_builds_saves_under_proton_are_a_candidate(home: Path) -> None:
    library = home / ".local/share/Steam"
    _install(library, "Zexion.exe", pe_image())
    saves = library.joinpath(*protected.PROTON_APPDATA, "Gallant Leaf", "Zexion")
    saves.mkdir(parents=True)
    assert protected.save_dir() == saves
    (home / ".local/share/Gallant Leaf/Zexion").mkdir(parents=True)  # Steam Cloud's, say
    with pytest.raises(RuntimeError, match="in 2 places"):
        protected.save_dir()


def test_the_save_folder_is_sdls_preference_folder(home: Path) -> None:
    saves = home / ".local/share/Gallant Leaf/Zexion"
    saves.mkdir(parents=True)
    assert protected.save_dir() == saves


def test_xdg_data_home_moves_the_save_folder(home: Path, monkeypatch) -> None:
    saves = home / "data" / "Gallant Leaf" / "Zexion"
    saves.mkdir(parents=True)
    monkeypatch.setenv("XDG_DATA_HOME", str(home / "data"))
    assert protected.save_dir() == saves


def test_flatpak_steams_save_folder_is_found(home: Path) -> None:
    saves = home / FLATPAK / ".local/share/Gallant Leaf/Zexion"
    saves.mkdir(parents=True)
    assert protected.save_dir() == saves


def test_no_save_folder_or_two_is_an_error(home: Path) -> None:
    with pytest.raises(RuntimeError, match="not found; set ZEXION_SAVE_DIR"):
        protected.save_dir()
    (home / ".local/share/Gallant Leaf/Zexion").mkdir(parents=True)
    (home / FLATPAK / ".local/share/Gallant Leaf/Zexion").mkdir(parents=True)
    with pytest.raises(RuntimeError, match="in 2 places"):
        protected.save_dir()


def test_the_save_override_still_wins(home: Path, monkeypatch) -> None:
    monkeypatch.setenv("ZEXION_SAVE_DIR", str(home / "elsewhere"))
    assert protected.save_dir() == home / "elsewhere"


def test_the_linux_folders_are_protected(home: Path) -> None:
    assert protected.is_protected_path(home / FLATPAK / ".local/share/Gallant Leaf/Zexion/x.sav")
    assert protected.is_protected_path(home / ".local/share/Steam/steamapps/common/Zexion/Zexion")


def test_a_linux_build_is_fingerprinted_from_its_elf_header(home: Path) -> None:
    install = _install(home / ".local/share/Steam")
    (install / "libSDL2-2.0.so.0").write_bytes(b"")
    (install.parent.parent / "appmanifest_3392510.acf").write_text('"buildid"\t\t"23943567"\n')
    text = build_id.fingerprint(install)
    digest = hashlib.sha256(elf_image()).hexdigest()
    assert f"- Zexion (Linux) SHA-256: `{digest}`" in text
    assert f"- GNU build id: `{ELF_BUILD_ID.hex()}`" in text
    assert "type: EXEC (fixed addresses); symbol table: kept" in text
    assert "- Libraries beside the binary: libSDL2-2.0.so.0" in text
    assert "- Steam build id: 23943567" in text
    assert "  - `Files/RandoCells`: missing" in text
