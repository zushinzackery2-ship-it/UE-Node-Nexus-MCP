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
