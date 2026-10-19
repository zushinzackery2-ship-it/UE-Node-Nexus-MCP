"""One worker binds all candidates and refuses incomplete batch evidence."""

from copy import deepcopy

import pytest

from ue_node_nexus_mcp.safety.publication import gate
from ue_node_nexus_mcp.transcode.collaboration.apply import units
from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.storage.io import digest
from .test_publication_gate import fixture
from .test_publication_evidence import builds, environment


def batch_fixture(monkeypatch):
    bridge, context, workspace, item, observation, capabilities, calls, events = fixture(monkeypatch)
    second = deepcopy(item)
    second["asset"] = second["payload"]["asset_path"] = "/Game/Second.Second"
    second["dependencies"] = [item["asset"]]
    entries = [(item["asset"], item), (second["asset"], second)]
    invocations = []

    def validate(project, operations, engine, rhi, timeout, dry_run, **options):
        invocations.append(deepcopy(operations))
        assert options["baseline"]["project"] == project
        results = [dict(ok=True, data=dict(isolated_publication=dict(
            compiled=True, saved=True, readback_verified=True, apply_id="worker-" + str(index),
            payload=operation["payload"], receipt=dict(after=dict())))) for index, operation in enumerate(operations)]
        return dict(ok=True, data=dict(state="validated", validation_id="worker", receipt_path="receipt.json",
            snapshot_digest="snapshot", source_unchanged=True, operations_digest=digest(operations),
            worker=dict(build=builds(), rhi=rhi, environment=environment()),
            cleanup=dict(exit_confirmed=True, exit_code=0), results=results))

    monkeypatch.setattr(gate, "validate", validate)
    return bridge, context, workspace, entries, observation, capabilities, calls, events, invocations


def test_batch_uses_one_baseline_and_one_worker(monkeypatch):
    bridge, context, workspace, entries, observation, _, calls, events, invocations = batch_fixture(monkeypatch)
    receipts = gate.admit_batch(bridge, context, workspace, entries, "candidate", observation, {})
    assert len(invocations) == 1 and len(invocations[0]) == 2
    assert len([name for name, _ in calls if name == "safety_baseline_export"]) == 1
    assert set(receipts) == {asset for asset, _ in entries}
    assert len(events) == 2
    for asset, item in entries:
        assert receipts[asset]["batch_size"] == 2
        assert gate.receipt_valid(receipts[asset], item, "candidate")
        assert not gate.receipt_valid(receipts[asset], item, "other-candidate")
        changed = dict(item, payload=dict(item["payload"], changed=True))
        assert not gate.receipt_valid(receipts[asset], changed, "candidate")


@pytest.mark.parametrize("defect", ["short", "extra", "bad_exit", "payload", "readback", "failed"])
def test_batch_defects_prevent_any_admission(monkeypatch, defect):
    bridge, context, workspace, entries, observation, _, _, events, _ = batch_fixture(monkeypatch)
    original = gate.validate

    def validate(*args, **options):
        report = original(*args, **options)
        data = report["data"]
        if defect == "short":
            data["results"].pop()
        elif defect == "extra":
            data["results"].append(data["results"][0])
        elif defect == "bad_exit":
            data["cleanup"]["exit_code"] = 3
        elif defect == "payload":
            data["operations_digest"] = "different"
        elif defect == "failed":
            data["results"][1]["ok"] = False
        else:
            data["results"][1]["data"]["isolated_publication"]["readback_verified"] = False
        return report

    monkeypatch.setattr(gate, "validate", validate)
    with pytest.raises(SyncError):
        gate.admit_batch(bridge, context, workspace, entries, "candidate", observation, {})
    assert not events


def test_batch_failure_is_attributed_without_restarting_workers(monkeypatch):
    bridge, context, workspace, entries, observation, _, _, _, _ = batch_fixture(monkeypatch)
    calls = []

    def reject(*args):
        calls.append("batch")
        raise SyncError("safety_worker_exit_failed", "worker crashed")

    monkeypatch.setattr(gate, "admit_batch", reject)
    monkeypatch.setattr(gate, "admit", lambda *args: pytest.fail("batch failure must not start another worker"))
    batch = dict(order=[asset for asset, _ in entries], units=dict(entries), errors={})
    result = units.run_units(bridge, context, workspace, "source", "candidate", {}, observation, batch, dict(isolation="auto"))
    assert calls == ["batch"]
    assert len(result) == 2 and all(row["action"] == "failed" for row in result)
    assert all(row["code"] == "safety_worker_exit_failed" for row in result)


def test_cached_binding_distinguishes_internal_and_external_revision_changes(monkeypatch):
    bridge, context, workspace, entries, observation, capabilities, _, _, _ = batch_fixture(monkeypatch)
    first, second = [asset for asset, _ in entries]
    observation["revisions"] = {first: "before", second: "before"}
    receipts = gate.admit_batch(bridge, context, workspace, entries, "candidate", observation, {})
    item = entries[1][1]
    receipt = receipts[second]
    observation["revisions"][first] = "verified-main-apply"
    assert not gate.receipt_valid(receipt, item, "candidate", bridge=bridge, observation=observation)
    assert gate.receipt_valid(receipt, item, "candidate", bridge=bridge, observation=observation, completed={first})
    observation["revisions"][second] = "external-change"
    assert not gate.receipt_valid(receipt, item, "candidate", bridge=bridge, observation=observation, completed={first})
    observation["revisions"][second] = "before"
    capabilities["safety_environment"]["render_cvars"]["r.ScreenPercentage"] = "80"
    assert not gate.receipt_valid(receipt, item, "candidate", bridge=bridge, observation=observation, completed={first})
