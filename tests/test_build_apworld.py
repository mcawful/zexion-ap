"""The release builder, with stand-in DLLs (no native build needed)."""

from __future__ import annotations

import json
import os
import time
import zipfile
from pathlib import Path

import pytest

import build_apworld as bap
from protected import ProtectedPathError


def stub_dlls(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, version: str) -> tuple[Path, Path]:
    """A helper naming ``version`` as its C string, and a proxy, both newer than any source."""
    helper, proxy = tmp_path / "zexion_ap.dll", tmp_path / "version.dll"
    helper.write_bytes(b"MZ stand-in\x00" + version.encode() + b"\x00tail")
    proxy.write_bytes(b"MZ proxy stand-in")
    later = time.time() + 3600
    for dll in (helper, proxy):
        os.utime(dll, (later, later))
    monkeypatch.setattr(bap, "HELPER", helper)
    monkeypatch.setattr(bap, "PROXY", proxy)
    return helper, proxy


@pytest.fixture
def dlls(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> tuple[Path, Path]:
    """Stand-in DLLs of the client's version."""
    return stub_dlls(tmp_path, monkeypatch, bap.client_version())


def refused(tmp_path: Path, why: str) -> None:
    """The build stops, saying ``why``, before it writes the dist folder."""
    with pytest.raises(SystemExit, match=why):
        bap.build(tmp_path / "dist")
    assert not (tmp_path / "dist").exists()


def test_the_release_holds_the_apworld_the_dlls_and_the_guides(tmp_path: Path, dlls) -> None:
    out = tmp_path / "dist"
    apworld = bap.build(out)
    world = json.loads((bap.WORLD / "archipelago.json").read_text(encoding="utf-8"))
    with zipfile.ZipFile(apworld) as zf:
        names = set(zf.namelist())
        manifest = json.loads(zf.read("zexion/archipelago.json"))
    # AP 0.6.8's APWorldContainer.read() needs these, or it ignores the versions below.
    assert manifest["version"] == 7
    assert manifest["compatible_version"] == 7
    assert manifest["game"] == "Zexion"
    for key in ("world_version", "minimum_ap_version", "authors"):
        assert manifest[key] == world[key]
    assert "zexion/__init__.py" in names
    assert "zexion/data.py" in names
    assert "zexion/docs/setup_en.md" in names
    assert "zexion/client/__init__.py" in names  # the subpackage worlds.zexion.client
    assert "zexion/client/zexion_client.py" in names
    assert not [n for n in names if "/test/" in n or n.endswith(".pyc")]
    assert sorted(names).count("zexion/archipelago.json") == 1
    assert (out / "zexion_ap.dll").is_file()
    assert (out / "version.dll").is_file()
    # The player guides go beside the apworld.
    docs = bap.WORLD / "docs"
    for source, name in (
        ("setup_en.md", "zexion-setup-guide.md"),
        ("en_Zexion.md", "zexion-game-page.md"),
    ):
        assert (out / name).read_bytes() == (docs / source).read_bytes()
    # The setup guide sends the player to the game page by its shipped name.
    assert "zexion-game-page.md" in (docs / "setup_en.md").read_text(encoding="utf-8")


def test_a_world_version_that_is_not_the_clients_is_refused(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    stub_dlls(tmp_path, monkeypatch, "9.9.9")
    monkeypatch.setattr(bap, "client_version", lambda: "9.9.9")
    refused(tmp_path, "one number")


def test_a_helper_of_another_version_is_refused(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    stub_dlls(tmp_path, monkeypatch, "0.0.1")
    refused(tmp_path, "rebuild native")


def test_a_version_inside_a_longer_one_does_not_count(tmp_path: Path) -> None:
    dll = tmp_path / "zexion_ap.dll"
    dll.write_bytes(b"\x0010.25.3\x00")
    assert not bap.helper_has_version(dll, "0.25.3")
    dll.write_bytes(b"\x000.25.3\x00")
    assert bap.helper_has_version(dll, "0.25.3")


def test_a_dll_older_than_cmakelists_is_stale(tmp_path: Path, monkeypatch, dlls) -> None:
    helper, _ = dlls
    src = tmp_path / "src"
    src.mkdir()
    (src / "a.cpp").write_text("", encoding="utf-8")
    cmake = tmp_path / "CMakeLists.txt"
    cmake.write_text("", encoding="utf-8")
    old = time.time() - 3600
    os.utime(src / "a.cpp", (old, old))
    later = helper.stat().st_mtime + 60
    os.utime(cmake, (later, later))
    monkeypatch.setattr(bap, "HELPER_SOURCES", (src, cmake))
    refused(tmp_path, "zexion_ap.dll is older than")


def test_an_out_folder_in_the_game_install_is_refused(tmp_path: Path, dlls) -> None:
    game = tmp_path / "steamapps" / "common" / "Zexion"
    with pytest.raises(ProtectedPathError):
        bap.build(game)
    assert not game.exists()


def test_an_untracked_file_stops_the_build(tmp_path: Path, monkeypatch, dlls) -> None:
    real = bap.tracked
    left_out = (bap.WORLD / "data.py").resolve()
    monkeypatch.setattr(bap, "tracked", lambda *folders: real(*folders) - {left_out})
    refused(tmp_path, r"not tracked by git.*data\.py")


def test_a_manifest_naming_another_game_is_refused(tmp_path: Path, monkeypatch, dlls) -> None:
    world = tmp_path / "world"
    world.mkdir()
    info = json.loads((bap.WORLD / "archipelago.json").read_text(encoding="utf-8"))
    (world / "archipelago.json").write_text(json.dumps({**info, "game": "Other"}), "utf-8")
    monkeypatch.setattr(bap, "WORLD", world)
    refused(tmp_path, "does not name the game Zexion")


def test_two_files_of_one_name_are_refused(tmp_path: Path, monkeypatch, dlls) -> None:
    real = bap.packed_files
    # A client file that would land on the manifest's name.
    monkeypatch.setattr(
        bap, "packed_files", lambda: [*real(), (bap.CLIENT / "ids.py", bap.MANIFEST)]
    )
    refused(tmp_path, "share a name")
