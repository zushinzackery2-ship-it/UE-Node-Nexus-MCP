"""Held copies are pruned to the configured keep count; the validation root follows the project volume."""

import os
import time

from ue_node_nexus_mcp.safety.isolation import retention, service


def held(validation_root, name, age_seconds):
    directory = validation_root / ".del" / name
    (directory / "Project").mkdir(parents=True)
    stamp = time.time() - age_seconds
    os.utime(directory, (stamp, stamp))
    return directory


def test_prune_keeps_the_newest_copies(tmp_path):
    oldest = held(tmp_path, "a" * 32, 300)
    older = held(tmp_path, "b" * 32, 120)
    newest = held(tmp_path, "c" * 32, 5)
    assert sorted(retention.prune(tmp_path, keep=1)) == sorted([oldest.name, older.name])
    assert newest.is_dir()
    assert not oldest.exists() and not older.exists()


def test_prune_zero_removes_every_held_copy(tmp_path):
    one = held(tmp_path, "d" * 32, 30)
    assert retention.prune(tmp_path, keep=0) == [one.name]
    assert not one.exists()


def test_prune_ignores_directories_it_does_not_own(tmp_path):
    foreign = tmp_path / ".del" / "notes"
    foreign.mkdir(parents=True)
    (foreign / "keep.txt").write_text("x", encoding="utf-8")
    retention.prune(tmp_path, keep=0)
    assert (foreign / "keep.txt").is_file()


def test_keep_count_reads_the_environment(monkeypatch):
    monkeypatch.setenv("UE_NEXUS_VALIDATION_KEEP", "3")
    assert retention.keep_count() == 3
    monkeypatch.setenv("UE_NEXUS_VALIDATION_KEEP", "nonsense")
    assert retention.keep_count() == 1
    monkeypatch.setenv("UE_NEXUS_VALIDATION_KEEP", "-2")
    assert retention.keep_count() == 0


def test_validation_root_sits_beside_the_project(tmp_path, monkeypatch):
    monkeypatch.delenv("UE_NEXUS_VALIDATION_DIR", raising=False)
    project = tmp_path / "Projects" / "Game" / "Game.uproject"
    assert service.validation_root(str(project)) == tmp_path / "Projects" / ".nexus-validation" / "Game"


def test_validation_root_honours_the_override(tmp_path, monkeypatch):
    configured = tmp_path / "OtherVolume"
    monkeypatch.setenv("UE_NEXUS_VALIDATION_DIR", str(configured))
    assert service.validation_root("D:/Game/Game.uproject") == configured.resolve()
