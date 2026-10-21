"""client/prepare.py on a temporary save folder (never the real one)."""

from __future__ import annotations

from pathlib import Path

import pytest
from test_save_patch import fresh, plan

import prepare as pr
import save_patch as sp


@pytest.fixture
def folders(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> tuple[Path, Path]:
    saves = tmp_path / "saves"
    saves.mkdir()
    monkeypatch.setenv("ZEXION_SAVE_DIR", str(saves))
    return saves, tmp_path / "backups"


def not_running() -> bool:
    return False


def test_prepare_backs_up_then_writes_the_seed(folders) -> None:
    saves, backups = folders
    original = bytes(fresh())
    (saves / "Save Game 7.sav").write_bytes(original)
    done = pr.prepare(7, plan(), backups, not_running)
    assert done.changed
    assert done.backup.read_bytes() == original
    assert (saves / "Save Game 7.sav").read_bytes() == sp.patch(original, plan())
    assert not list(saves.glob("*" + pr.TMP_SUFFIX))


def test_a_slot_that_already_holds_the_seed_is_left_alone(folders) -> None:
    saves, backups = folders
    (saves / "Save Game 7.sav").write_bytes(sp.patch(bytes(fresh()), plan()))
    done = pr.prepare(7, plan(), backups, not_running)
    assert not done.changed and done.backup is None
    assert not backups.exists()


def test_nothing_is_written_while_the_game_runs(folders) -> None:
    saves, backups = folders
    original = bytes(fresh())
    (saves / "Save Game 7.sav").write_bytes(original)
    with pytest.raises(pr.PrepareError, match="close Zexion"):
        pr.prepare(7, plan(), backups, lambda: True)
    assert (saves / "Save Game 7.sav").read_bytes() == original


def test_a_save_that_does_not_fit_is_refused_untouched(folders) -> None:
    saves, backups = folders
    save = fresh()
    save[sp.PICKUP_COUNTER] = 1
    (saves / "Save Game 7.sav").write_bytes(bytes(save))
    with pytest.raises(pr.PrepareError, match="slot 7: something has been picked up"):
        pr.prepare(7, plan(), backups, not_running)
    assert (saves / "Save Game 7.sav").read_bytes() == bytes(save)
    assert not backups.exists()


def test_missing_and_impossible_slots_are_refused(folders) -> None:
    _, backups = folders
    with pytest.raises(pr.PrepareError, match="not found"):
        pr.prepare(7, plan(), backups, not_running)
    with pytest.raises(pr.PrepareError, match="1 to 30"):
        pr.prepare(31, plan(), backups, not_running)
