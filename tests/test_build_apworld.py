"""The release builder, with stand-in DLLs and a stand-in Linux helper (no native build
needed)."""

from __future__ import annotations

import json
import os
import time
import zipfile
from pathlib import Path

import pytest
from fakes import verneed_image

import build_apworld as bap
from protected import ProtectedPathError

# What a Steam Runtime build of the Linux helper needs: glibc's own versions, none past 2.31,
# with dl and pthread still their own libraries there (merged into libc from 2.34).
RUNTIME_NEEDS = [
    ("libm.so.6", ["GLIBC_2.2.5"]),
    ("libdl.so.2", ["GLIBC_2.2.5"]),
    ("libpthread.so.0", ["GLIBC_2.2.5", "GLIBC_2.3.2"]),
    ("libc.so.6", ["GLIBC_2.2.5", "GLIBC_2.4", "GLIBC_2.31"]),
    ("ld-linux-x86-64.so.2", ["GLIBC_2.3"]),
]


def stub_linux_helper(path: Path, version: str, needs=RUNTIME_NEEDS) -> Path:
    """An ELF file needing ``needs``' symbol versions, naming ``version`` as a C string after it,
    newer than any source."""
    path.write_bytes(verneed_image(needs) + b"\x00" + version.encode() + b"\x00")
    later = time.time() + 3600
    os.utime(path, (later, later))
    return path


def stub_dlls(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, version: str) -> tuple[Path, Path]:
    """A helper naming ``version`` as its C string, a proxy, and a Linux helper of ``version``
    that loads in the Steam Runtime, all newer than any source."""
    helper, proxy = tmp_path / "zexion_ap.dll", tmp_path / "version.dll"
    helper.write_bytes(b"MZ stand-in\x00" + version.encode() + b"\x00tail")
    proxy.write_bytes(b"MZ proxy stand-in")
    later = time.time() + 3600
    for dll in (helper, proxy):
        os.utime(dll, (later, later))
    monkeypatch.setattr(bap, "HELPER", helper)
    monkeypatch.setattr(bap, "PROXY", proxy)
    linux = stub_linux_helper(tmp_path / "libzexion_ap.so", version)
    monkeypatch.setattr(bap, "LINUX_HELPER", linux)
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
    assert (out / "libzexion_ap.so").read_bytes() == bap.LINUX_HELPER.read_bytes()
    # The player guides go beside the apworld.
    docs = bap.WORLD / "docs"
    for source, name in (
        ("setup_en.md", "zexion-setup-guide.md"),
        ("en_Zexion.md", "zexion-game-page.md"),
    ):
        assert (out / name).read_bytes() == (docs / source).read_bytes()
    # The setup guide sends the player to the game page by its shipped name.
    assert "zexion-game-page.md" in (docs / "setup_en.md").read_text(encoding="utf-8")
    # And the sample YAMLs, one per mode, as they are.
    modes = ("easy", "standard", "ohko", "custom")
    assert sorted(p.name for p in out.glob("*.yaml")) == sorted(f"zexion-{m}.yaml" for m in modes)
    for sample in bap.SAMPLES.glob("*.yaml"):
        assert (out / sample.name).read_bytes() == sample.read_bytes()


def test_a_sample_for_another_version_is_refused(tmp_path: Path, monkeypatch, dlls) -> None:
    samples = tmp_path / "samples"
    samples.mkdir()
    sample = samples / "zexion-easy.yaml"
    sample.write_text("requires:\n  game:\n    Zexion: 0.9.0 # made before the raise\n")
    monkeypatch.setattr(bap, "SAMPLES", samples)
    real = bap.tracked  # outside the repo, the stand-in sample is taken as tracked
    stub = lambda *folders: {sample.resolve()} if folders == (samples,) else real(*folders)  # noqa: E731
    monkeypatch.setattr(bap, "tracked", stub)
    refused(tmp_path, r"zexion-easy\.yaml does not require Zexion")


def test_an_untracked_sample_stops_the_build(tmp_path: Path, monkeypatch, dlls) -> None:
    real = bap.tracked
    left_out = (bap.SAMPLES / "zexion-ohko.yaml").resolve()
    monkeypatch.setattr(bap, "tracked", lambda *folders: real(*folders) - {left_out})
    refused(tmp_path, r"not tracked by git, so not released: samples.zexion-ohko\.yaml")


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


def test_a_missing_linux_helper_is_refused(tmp_path: Path, dlls) -> None:
    bap.LINUX_HELPER.unlink()
    refused(tmp_path, "libzexion_ap.so is missing")


def test_a_linux_helper_of_another_version_is_refused(tmp_path: Path, dlls) -> None:
    stub_linux_helper(bap.LINUX_HELPER, "0.9.0")
    refused(tmp_path, "the built Linux helper is not version")


def test_a_linux_helper_needing_a_newer_glibc_than_the_runtimes_is_refused(
    tmp_path: Path, dlls
) -> None:
    # A build on a newer distribution than the Steam Runtime's: 2.38 does not load in 2.31's.
    newer = [("libc.so.6", ["GLIBC_2.2.5", "GLIBC_2.38"]), ("libm.so.6", ["GLIBC_2.29"])]
    stub_linux_helper(bap.LINUX_HELPER, bap.client_version(), newer)
    refused(tmp_path, r"needs glibc 2\.38, newer than the Steam Runtime's 2\.31")


def test_a_linux_helper_needing_glibc_2_31_exactly_is_kept(tmp_path: Path, dlls) -> None:
    assert bap.linux_helper_problem(bap.LINUX_HELPER) is None


def test_a_linux_helper_needing_libstdc_versions_is_refused(tmp_path: Path, dlls) -> None:
    # Its C++ runtime not linked in: the game's container may hold an older libstdc++.
    dynamic = [*RUNTIME_NEEDS, ("libstdc++.so.6", ["GLIBCXX_3.4.29"])]
    stub_linux_helper(bap.LINUX_HELPER, bap.client_version(), dynamic)
    refused(tmp_path, "needs versions of libstdc\\+\\+.so.6, which are not glibc's")


def test_a_linux_helper_that_is_no_elf_file_is_refused(tmp_path: Path, dlls) -> None:
    bap.LINUX_HELPER.write_bytes(b"not an ELF\x00" + bap.client_version().encode() + b"\x00")
    later = time.time() + 3600
    os.utime(bap.LINUX_HELPER, (later, later))
    refused(tmp_path, "can't be read as an ELF file")


def test_a_linux_helper_older_than_its_sources_is_stale(tmp_path: Path, monkeypatch, dlls) -> None:
    linux_src = tmp_path / "linux"
    linux_src.mkdir()
    newer = linux_src / "helper.cpp"
    newer.write_text("// edited after the build")
    later = bap.LINUX_HELPER.stat().st_mtime + 60
    os.utime(newer, (later, later))
    monkeypatch.setattr(bap, "LINUX_SOURCES", (linux_src,))
    refused(tmp_path, "libzexion_ap.so is older than")


@pytest.mark.parametrize("version", ["GLIBC_PRIVATE", "GLIBC_ABI_DT_RELR"])
def test_a_linux_helper_needing_an_unnumbered_glibc_version_is_refused(
    tmp_path: Path, dlls, version: str
) -> None:
    needs = [*RUNTIME_NEEDS[:-1], ("libc.so.6", ["GLIBC_2.2.5", version])]
    stub_linux_helper(bap.LINUX_HELPER, bap.client_version(), needs)
    refused(tmp_path, f"needs {version}, which no numbered glibc version covers")


def test_a_version_inside_a_glibc_symbol_version_does_not_count(tmp_path: Path) -> None:
    # A Linux helper's .dynstr holds GLIBC_2.2.5: not its own version 2.2.5.
    so = stub_linux_helper(tmp_path / "libzexion_ap.so", "1.2.0", RUNTIME_NEEDS)
    assert bap.helper_has_version(so, "1.2.0")
    assert not bap.helper_has_version(so, "2.2.5")


def test_the_linux_helper_alone_is_checked_as_the_build_would(tmp_path: Path, capsys) -> None:
    good = stub_linux_helper(tmp_path / "good.so", bap.client_version())
    assert bap.main(["--check-linux-helper", str(good)]) == 0
    assert "loads in the Steam Runtime" in capsys.readouterr().out
    newer = [("libc.so.6", ["GLIBC_2.38"])]
    bad = stub_linux_helper(tmp_path / "bad.so", bap.client_version(), newer)
    with pytest.raises(SystemExit, match="bad.so: it would not load in the Steam Runtime"):
        bap.main(["--check-linux-helper", str(bad)])
    other = stub_linux_helper(tmp_path / "other.so", "0.9.0")
    with pytest.raises(SystemExit, match="other.so: it is not version"):
        bap.main(["--check-linux-helper", str(other)])
    with pytest.raises(SystemExit, match="is missing"):
        bap.main(["--check-linux-helper", str(tmp_path / "none.so")])
    assert not (tmp_path / "dist").exists()
