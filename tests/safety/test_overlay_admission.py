"""Disk admission uses the same mandatory copies and links as the snapshot."""

from types import SimpleNamespace

import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.safety.isolation import capacity, snapshot
from tests.safety.test_isolation_overlay import project_tree


def test_overlay_budget_includes_plugins_binaries_source_config_and_descriptor(tmp_path, monkeypatch):
    root = project_tree(tmp_path)
    for directory in ("Plugins", "Binaries", "Source"):
        (root / directory).mkdir()
        (root / directory / "mandatory.bin").write_bytes(b"x" * 8193)
    monkeypatch.setattr(capacity, "allocation_unit", lambda path: 4096)
    plan = snapshot.inventory(root / "Game.uproject")
    budget = capacity.estimate(plan, tmp_path / "copy", 1, 0, subset=set(("Content/Mat/MF_Base.uasset",)))
    assert budget["copied_files"] == 6
    assert budget["copy_bytes"] == sum(path.stat().st_size for path in plan["files"]
                                      if path.name != "MF_Other.uasset")
    assert budget["linked_files"] == 1


def test_insufficient_target_space_is_rejected_before_creating_copy(tmp_path, monkeypatch):
    root = project_tree(tmp_path)
    (root / "Plugins").mkdir()
    (root / "Plugins" / "large.bin").write_bytes(b"x" * (2 * 1024 * 1024))
    monkeypatch.setattr(snapshot, "WORKING_BYTES", 0)
    monkeypatch.setattr(capacity.shutil, "disk_usage", lambda path: SimpleNamespace(free=1024 * 1024))
    target = tmp_path / "copy" / "Project"
    with pytest.raises(InstanceError) as caught:
        snapshot.copy_project(snapshot.inventory(root / "Game.uproject"), target,
                              candidates=set(("Content/Mat/MF_Base.uasset",)))
    assert caught.value.code == "isolation_disk_budget"
    assert caught.value.details["free_bytes"] == 1024 * 1024
    assert caught.value.details["target_path"] == str(target.absolute())
    assert caught.value.details["measured_at"] > 0
    assert not target.exists()


def test_success_receipt_records_volume_measurement(tmp_path):
    root = project_tree(tmp_path)
    result = snapshot.copy_project(snapshot.inventory(root / "Game.uproject"), tmp_path / "copy",
                                   candidates=set(("Content/Mat/MF_Base.uasset",)))
    budget = result["disk_budget"]
    assert budget["free_bytes"] >= budget["required_bytes"]
    assert budget["copied_files"] == result["copied_files"]
    assert budget["linked_files"] == result["linked_files"]


def test_link_failure_does_not_start_an_unbudgeted_full_copy(tmp_path, monkeypatch):
    root = project_tree(tmp_path)
    def reject(*args, **kwargs):
        raise OSError("symlink privilege unavailable")
    monkeypatch.setattr(snapshot.os, "symlink", reject)
    target = tmp_path / "copy"
    with pytest.raises(InstanceError) as caught:
        snapshot.copy_project(snapshot.inventory(root / "Game.uproject"), target,
                              candidates=set(("Content/Mat/MF_Base.uasset",)))
    assert caught.value.code == "isolation_link_failed"
    assert "symlink privilege" in caught.value.details["reason"]
    assert not (target / "Content/Mat/MF_Other.uasset").exists()
