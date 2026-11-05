"""Unsaved memory is private, byte-verified and confined to the copied project."""

import hashlib
from pathlib import Path

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


def test_project_plugin_relative_baseline_file_is_overlaid(tmp_path):
    snapshot, baseline, _ = setup(tmp_path)
    source = Path(baseline["project"]).parent
    held = Path(baseline["root"]) / "Plugins/Tests/Content/Probe.uasset"
    held.parent.mkdir(parents=True)
    held.write_bytes(b"mounted memory")
    baseline["files"] = [dict(relative="Plugins/Tests/Content/Probe.uasset", source=str(held),
                              hash=hashlib.md5(held.read_bytes()).hexdigest())]
    overlay(snapshot, baseline)
    assert (Path(snapshot["project"]).parent / "Plugins/Tests/Content/Probe.uasset").read_bytes() == b"mounted memory"
    assert not (source / "Plugins/Tests/Content/Probe.uasset").exists()


def test_baseline_cannot_escape_the_copy(tmp_path):
    snapshot, baseline, _ = setup(tmp_path)
    baseline["files"][0]["relative"] = "../User.uasset"
    with pytest.raises(InstanceError, match="inside the copied project"):
        overlay(snapshot, baseline)
