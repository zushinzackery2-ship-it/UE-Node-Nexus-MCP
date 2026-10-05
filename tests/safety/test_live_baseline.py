"""Unsaved memory is private, byte-verified and confined to the copied project."""

import hashlib

import pytest

from ue_node_nexus_mcp.safety.isolation.baseline import overlay
from ue_node_nexus_mcp.instances.errors import InstanceError


def setup(tmp_path):
    source = tmp_path / "Source/Probe.uproject"
    source.parent.mkdir()
    source.write_text("{}")
    target = tmp_path / "Copy/Probe.uproject"
    target.parent.mkdir()
    target.write_text("{}")
    root = source.parent / "Saved/Nexus/SafetyBaseline/id"
    file = root / "Content/Probe.uasset"
    file.parent.mkdir(parents=True)
    file.write_bytes(b"unsaved memory")
    baseline = dict(project=str(source), root=str(root), dry_run=False,
                    files=[dict(relative="Content/Probe.uasset", source=str(file), hash=hashlib.md5(file.read_bytes()).hexdigest())])
    return dict(project=str(target), source=str(source)), baseline, file


def test_memory_overlay_copies_without_changing_source(tmp_path):
    snapshot, baseline, file = setup(tmp_path)
    overlay(snapshot, baseline)
    assert snapshot["baseline"] == "live_memory" and snapshot["baseline_digest"]
    from pathlib import Path
    assert (Path(snapshot["project"]).parent / "Content/Probe.uasset").read_bytes() == file.read_bytes()
    assert not (Path(snapshot["source"]).parent / "Content/Probe.uasset").exists()


def test_changed_baseline_bytes_are_refused(tmp_path):
    snapshot, baseline, file = setup(tmp_path)
    file.write_bytes(b"other state")
    with pytest.raises(InstanceError, match="bytes changed"):
        overlay(snapshot, baseline)


def test_baseline_cannot_escape_the_copy(tmp_path):
    snapshot, baseline, _ = setup(tmp_path)
    baseline["files"][0]["relative"] = "../User.uasset"
    with pytest.raises(InstanceError, match="inside copied Content"):
        overlay(snapshot, baseline)
