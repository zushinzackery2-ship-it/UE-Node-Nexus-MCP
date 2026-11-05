"""Project-local content mounts other than /Game are valid isolation inputs and writes."""

import json
from pathlib import Path

import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.safety.isolation.inputs import validate_operations
from ue_node_nexus_mcp.safety.isolation.mounts import plugin_mounts, writable_mounts
from ue_node_nexus_mcp.safety.isolation.snapshot import content_candidates
from ue_node_nexus_mcp.safety.publication import gate
from tests.safety.test_publication_gate import fixture


def project(tmp_path):
    root = tmp_path / "Projects" / "Game"
    plugin = root / "Plugins" / "Tests"
    (plugin / "Content" / "Maps").mkdir(parents=True)
    (plugin / "Tests.uplugin").write_text(json.dumps(dict(
        FileVersion=3, VersionName="1.0", CanContainContent=True, EnabledByDefault=True,
        Modules=[dict(Name="Tests", Type="Editor", LoadingPhase="Default")])), encoding="utf-8")
    (root / "Content").mkdir()
    (root / "Game.uproject").write_text(json.dumps(dict(FileVersion=3)), encoding="utf-8")
    (plugin / "Content" / "Maps" / "M_Test.uasset").write_bytes(b"mounted asset")
    return root / "Game.uproject"


def test_project_plugin_mount_is_writable_and_candidate_is_protected(tmp_path):
    descriptor = project(tmp_path)
    mounts = plugin_mounts(descriptor, tmp_path / "Engine")
    assert mounts["Tests"]["project_local"] is True
    assert writable_mounts(mounts) == {"Game", "Tests"}
    entries = [dict(operation="transcode_apply", payload=dict(
        asset_path="/Tests/Maps/M_Test.M_Test", kind="material", plan=[]))]
    assert content_candidates(descriptor, entries, mounts) == {
        "Plugins/Tests/Content/Maps/M_Test.uasset"}
    validate_operations(entries, {"Game", "Tests"}, writable_mounts(mounts))


def test_external_mount_cannot_be_a_write_target(tmp_path):
    descriptor = project(tmp_path)
    entries = [dict(operation="transcode_apply", payload=dict(
        asset_path="/Tests/Maps/M_Test.M_Test", kind="material", plan=[]))]
    with pytest.raises(InstanceError, match="project content mount"):
        validate_operations(entries, {"Game", "Tests"}, {"Game"})


def test_gate_exports_a_mounted_asset_to_the_native_baseline(monkeypatch):
    bridge, context, workspace, item, observation, _, calls, _ = fixture(monkeypatch)
    item.update(asset="/Tests/Maps/M_Test.M_Test",
                payload=dict(asset_path="/Tests/Maps/M_Test.M_Test", kind="niagara_system",
                             plan=[dict(op="ns_module_add")]))
    item["dependencies"] = ["/Tests/Maps/M_Dep.M_Dep"]
    gate.admit(bridge, context, workspace, item, "candidate", observation, dict(isolation="auto"))
    exported = next(payload["asset_paths"] for name, payload in calls if name == "safety_baseline_export")
    assert exported == ["/Tests/Maps/M_Dep.M_Dep", "/Tests/Maps/M_Test.M_Test"]
