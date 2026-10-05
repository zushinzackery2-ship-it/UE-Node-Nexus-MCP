"""Exercise actual admission, including the boundary before a main apply."""

from copy import deepcopy
from types import SimpleNamespace

import pytest

from ue_node_nexus_mcp.safety.publication import gate
from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.storage.io import digest
from .test_publication_evidence import builds, environment


def fixture(monkeypatch):
    item = dict(asset="/Game/Probe.Probe", kind="niagara_system", empty=False, dependencies=[],
                payload=dict(asset_path="/Game/Probe.Probe", kind="niagara_system", plan=[dict(op="ns_module_add")]))
    context = SimpleNamespace(project_file="private/Probe.uproject")
    capabilities = dict(safety_contract=1, rhi="D3D12", build=builds(), safety_environment=environment())
    calls, events = [], []
    workspace = SimpleNamespace(store=SimpleNamespace(event=lambda *args, **kwargs: events.append((args, kwargs))))
    observation = dict(revisions=dict())

    def bridge(operation, payload):
        calls.append((operation, payload))
        if operation == "bridge_capabilities_get":
            return dict(ok=True, data=deepcopy(capabilities))
        assert operation == "safety_baseline_export"
        return dict(ok=True, data=dict(project=context.project_file, files=[], root="private", dry_run=False, environment=environment()))

    def validate(project, operations, engine, rhi, timeout, dry_run, *, baseline, allow_low_memory):
        assert project == context.project_file and baseline["project"] == project and not dry_run
        publication = dict(compiled=True, saved=True, readback_verified=True, apply_id="worker-apply",
                           payload=operations[0]["payload"], receipt=dict(after=dict()))
        return dict(ok=True, data=dict(state="validated", validation_id="worker", receipt_path="private/receipt.json",
            snapshot_digest="snapshot", baseline_digest="baseline", source_unchanged=True,
            operations_digest=digest(operations), worker=dict(build=builds(), rhi=rhi, environment=environment()),
            cleanup=dict(exit_confirmed=True, forced=False, exit_code=0),
            results=[dict(ok=True, data=dict(isolated_publication=publication))]))

    monkeypatch.setattr(gate, "validate", validate)
    monkeypatch.setattr(gate, "actual_snapshot", lambda *args: dict())
    return bridge, context, workspace, item, observation, capabilities, calls, events


def test_admission_records_bound_proof_before_main_mutation(monkeypatch):
    bridge, context, workspace, item, observation, _, calls, events = fixture(monkeypatch)
    receipt = gate.admit(bridge, context, workspace, item, "candidate", observation, dict())
    assert receipt["binding"]["candidate"] == "candidate" and receipt["baseline_digest"] == "baseline"
    assert events[0][0] == ("safety_validated",)
    assert all(name != "vfx_transcode_apply" for name, _ in calls)


def test_failed_worker_prevents_main_mutation(monkeypatch):
    bridge, context, workspace, item, observation, _, calls, events = fixture(monkeypatch)
    monkeypatch.setattr(gate, "validate", lambda *args, **kwargs: dict(ok=False, data=dict(state="failed")))
    with pytest.raises(SyncError, match="failed complete"):
        gate.admit(bridge, context, workspace, item, "candidate", observation, dict())
    assert not events and all(name != "vfx_transcode_apply" for name, _ in calls)


def test_readback_mismatch_prevents_admission(monkeypatch):
    bridge, context, workspace, item, observation, _, _, events = fixture(monkeypatch)

    def mismatch(*args):
        raise SyncError("apply_result_mismatch", "candidate value differs")

    monkeypatch.setattr(gate, "actual_snapshot", mismatch)
    with pytest.raises(SyncError, match="candidate value"):
        gate.admit(bridge, context, workspace, item, "candidate", observation, dict())
    assert not events


def test_source_build_change_during_validation_prevents_admission(monkeypatch):
    bridge, context, workspace, item, observation, capabilities, _, events = fixture(monkeypatch)
    original = gate.validate

    def validate(*args, **kwargs):
        result = original(*args, **kwargs)
        capabilities["build"]["UeNodeNexusBridge"]["source_fingerprint"] = "changed"
        return result

    monkeypatch.setattr(gate, "validate", validate)
    with pytest.raises(SyncError, match="environment changed"):
        gate.admit(bridge, context, workspace, item, "candidate", observation, dict())
    assert not events


def test_plain_material_parameter_does_not_start_worker(monkeypatch):
    bridge, context, workspace, item, observation, _, calls, _ = fixture(monkeypatch)
    item.update(kind="material", payload=dict(plan=[dict(op="set_property", name="Roughness", value="0.5")]))
    assert gate.admit(bridge, context, workspace, item, "candidate", observation, dict()) is None
    assert not calls


def test_compilation_cannot_be_disabled_for_high_risk(monkeypatch):
    bridge, context, workspace, item, observation, _, calls, _ = fixture(monkeypatch)
    with pytest.raises(SyncError, match="requires compilation"):
        gate.admit(bridge, context, workspace, item, "candidate", observation, dict(compile=False))
    assert not calls
