"""Validate real copy independence, mount restrictions and candidate admission."""

import json
from pathlib import Path

import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.operation_registry import get_operation_spec, operation_schema
from ue_node_nexus_mcp.safety.isolation.inputs import SUPPORTED, validate_operations
from ue_node_nexus_mcp.safety.isolation.snapshot import copy_project, inventory, unchanged
from ue_node_nexus_mcp.tools_facade import ue_execute


def project(tmp_path):
    source = tmp_path / "source"
    (source / "Content").mkdir(parents=True)
    descriptor = source / "Sample.uproject"
    descriptor.write_text(json.dumps(dict(FileVersion=3, EngineAssociation="5.5")), encoding="utf-8")
    (source / "Content/Asset.uasset").write_bytes(b"saved source asset")
    (source / "Saved/Build/Jobs").mkdir(parents=True)
    (source / "Saved/Build/Jobs/unsafe.request.json").write_text("{}")
    return descriptor


def test_copy_has_no_shared_writable_inputs_or_live_jobs(tmp_path):
    source = project(tmp_path)
    plan = inventory(source)
    snapshot = copy_project(plan, tmp_path / "validation/Project")
    clone = Path(snapshot["project"]).parent
    assert not (clone / "Saved").exists()
    assert unchanged(snapshot)
    (clone / "Content/Asset.uasset").write_bytes(b"validated candidate")
    assert (source.parent / "Content/Asset.uasset").read_bytes() == b"saved source asset"
    assert unchanged(snapshot)
    (source.parent / "Content/Asset.uasset").write_bytes(b"user changed the original")
    assert not unchanged(snapshot)


def test_source_overlap_is_rejected(tmp_path):
    source = project(tmp_path)
    with pytest.raises(InstanceError, match="disjoint"):
        copy_project(inventory(source), source.parent / "Validation")


def test_external_plugin_roots_rejected(tmp_path):
    source = project(tmp_path)
    source.write_text(json.dumps(dict(AdditionalPluginDirectories=["D:/OtherProject/Plugins"])))
    with pytest.raises(InstanceError, match="external"):
        inventory(source)


def test_scene_plan_is_copied_without_copying_live_queue(tmp_path):
    source = project(tmp_path)
    plan_file = source.parent / "Saved/Authoring/candidate.json"
    plan_file.parent.mkdir(parents=True)
    plan_file.write_text(json.dumps(dict(operations=[])))
    snapshot = copy_project(inventory(source, ["Saved/Authoring/candidate.json"]), tmp_path / "validation")
    clone = Path(snapshot["project"]).parent
    assert (clone / "Saved/Authoring/candidate.json").read_bytes() == plan_file.read_bytes()
    assert not (clone / "Saved/Build/Jobs").exists()


@pytest.mark.parametrize("item", [dict(operation="bridge_instance_close", payload=dict()),
                                  dict(operation="asset_create", payload=dict(asset_path="/Engine/Unsafe", asset_kind="material")),
                                  dict(operation="level_open", payload=dict(map_path="/Engine/Maps/Templates/Template_Default")),
                                  dict(operation="transcode_apply", payload=dict(asset_path="/Game/Safe", repository="D:/Original"))])
def test_unsafe_operation_is_rejected_before_copy(all_features, item):
    with pytest.raises(InstanceError):
        validate_operations([item])


def test_supported_operations_are_real_registry_entries():
    for name in SUPPORTED:
        assert get_operation_spec(name).bridge_operation == name
    schema = operation_schema(get_operation_spec("safety_validate"))
    assert set(schema["payload_schema"]["required"]) == set(("project_path", "operations"))


def test_facade_preview_does_not_create_a_copy(all_features, tmp_path):
    source = project(tmp_path)
    result = ue_execute("safety_validate", dict(project_path=str(source), dry_run=True,
                        operations=[dict(operation="asset_create", payload=dict(asset_path="/Game/Safety/Probe", asset_kind="material"))]))
    assert result["ok"], result
    assert result["data"]["baseline"] == "saved_disk" and result["data"]["state"] == "preview"
    assert not Path(result["data"]["validation_root"]).exists()


def test_interrupted_worker_is_journaled_without_replaying_writes(all_features, tmp_path, monkeypatch):
    from ue_node_nexus_mcp.safety.isolation import service
    source = project(tmp_path)
    calls = []
    class Worker:
        def __init__(self, *args):
            pass

        def start(self):
            return dict(instance=dict(instance_id="isolated"))

        def execute(self, item):
            calls.append(item)
            raise InstanceError("operation_outcome_unknown", "worker exited during the write")

        def close(self):
            return dict(exit_confirmed=True, state="EXITED")

    monkeypatch.setattr(service, "runtime_root", lambda: tmp_path / "runtime")
    monkeypatch.setattr("ue_node_nexus_mcp.instances.lifecycle.engine.resolve_engine", lambda *args: tmp_path)
    monkeypatch.setattr("ue_node_nexus_mcp.safety.isolation.session.ValidationSession", Worker)
    operation = dict(operation="asset_create", payload=dict(asset_path="/Game/Safety/Probe", asset_kind="material"))
    result = service.validate(str(source), [operation, operation], None, "d3d12", 30, False)
    assert not result["ok"] and len(calls) == 1
    record = json.loads(Path(result["data"]["receipt_path"]).read_text())
    assert record["state"] == "failed" and record["error"]["code"] == "operation_outcome_unknown"
    assert record["source_unchanged"] and record["cleanup"]["exit_confirmed"]


def test_cancellation_refuses_a_different_project_before_opening_a_process(tmp_path, monkeypatch):
    from ue_node_nexus_mcp.safety.isolation.process import stop_owned
    def forbidden():
        raise AssertionError("no process handle may be opened")
    monkeypatch.setattr("ue_node_nexus_mcp.safety.isolation.process.kernel", forbidden)
    with pytest.raises(InstanceError, match="validation project"):
        stop_owned(dict(project_path=str(tmp_path / "UserProject.uproject")), tmp_path / "Validation.uproject")
