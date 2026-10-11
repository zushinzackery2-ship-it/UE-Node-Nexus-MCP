"""The isolated worker must exercise the real commit and exported readback chain."""

from pathlib import Path

import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.safety.isolation.inputs import inspect_payload
from ue_node_nexus_mcp.safety.isolation.session import ValidationSession


def test_niagara_engine_plugin_reference_is_admitted():
    inspect_payload(dict(script="/Niagara/Modules/Emitter/SpawnRate.SpawnRate"), mounts=set(("Game", "Engine", "Script", "Niagara")))


def test_unknown_plugin_mount_is_refused():
    with pytest.raises(InstanceError):
        inspect_payload(dict(script="/Untrusted/Modules/Unsafe.Unsafe"), mounts=set(("Game", "Engine", "Script")))


def test_worker_apply_has_transaction_and_worker_owned_readback(tmp_path, monkeypatch):
    worker = ValidationSession.__new__(ValidationSession)
    worker.project = tmp_path / "Project/Probe.uproject"
    worker.project.parent.mkdir()
    worker.project.touch()
    calls = []

    def call(operation, payload):
        calls.append((operation, payload))
        if operation == "transcode_root_set":
            return dict(ok=True, data=dict(collaboration_version=1))
        if operation in ("transcode_export", "vfx_transcode_export"):
            return dict(ok=True, data=dict(assets=[], skipped=[dict(asset_path="/Game/Probe.Probe", reason="asset_not_found")]))
        return dict(ok=False, error=dict(code="deliberate_rejection", message="inspect accepted request"))

    worker.call = call
    result = worker.execute(dict(operation="vfx_transcode_apply", payload=dict(asset_path="/Game/Probe.Probe", kind="niagara_system", plan=[])))
    assert not result["ok"]
    request = next(payload for operation, payload in calls if operation == "vfx_transcode_apply")
    assert request["collaboration_version"] == 1 and request["apply_id"]
    assert request["expected_absent"] and request["compile"] and request["save"]
    Path(request["out_dir"]).resolve().relative_to(worker.project.parent.resolve())
    Path(request["repository"]).resolve().relative_to(worker.project.parent.resolve())


def test_multi_asset_worker_reuses_the_same_project_repository_binding(tmp_path):
    worker = ValidationSession.__new__(ValidationSession)
    worker.project = tmp_path / "Project/Probe.uproject"
    worker.project.parent.mkdir()
    worker.project.touch()
    bindings, requests = [], []

    def call(operation, payload):
        if operation == "transcode_root_set":
            if bindings:
                expected = Path(payload["root"]) / worker.project.stem / ".nexus/collaboration"
                if Path(payload["collaboration_root"]) != expected or bindings[0] != payload:
                    return dict(ok=False, error=dict(code="repository_mismatch"))
            bindings.append(payload)
            return dict(ok=True, data=dict(collaboration_version=1))
        if operation == "transcode_export":
            return dict(ok=True, data=dict(assets=[], skipped=[dict(asset_path=payload["asset_paths"][0], reason="asset_not_found")]))
        requests.append(payload)
        return dict(ok=False, error=dict(code="deliberate_rejection"))

    worker.call = call
    for asset in ("/Game/First.First", "/Game/Second.Second"):
        response = worker.execute(dict(operation="transcode_apply", payload=dict(asset_path=asset, kind="material", plan=[])))
        assert response["error"]["code"] == "deliberate_rejection"
    assert bindings[0] == bindings[1]
    assert requests[0]["apply_id"] != requests[1]["apply_id"]
