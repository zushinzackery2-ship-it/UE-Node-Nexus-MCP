"""Cancellation reports the durable write set, including interrupted bookkeeping."""

import pytest

from ue_node_nexus_mcp.safety.publication import gate
from ue_node_nexus_mcp.transcode.collaboration.apply import transactions
from ue_node_nexus_mcp.transcode.collaboration.jobs import service, state
from ue_node_nexus_mcp.transcode.collaboration.jobs.results import reconcile_result
from ue_node_nexus_mcp.transcode.progress import emit
from .test_jobs import MATERIAL, SECOND, commit, finish, project as project, submit


@pytest.mark.parametrize("phase", ["copy", "execute", "readback", "main_apply"])
@pytest.mark.parametrize("reason", ["cancel", "deadline"])
def test_stop_before_main_write_reports_every_unapplied_asset(project, monkeypatch, phase, reason):
    commit(project, (MATERIAL, SECOND))
    store = project[4]

    def stop(*args):
        state.update(store, state.current_id(), **({"cancel_requested": True} if reason == "cancel" else {"deadline_at": 0}))
        if phase != "main_apply":
            emit("isolation_" + phase, dict(phase=phase, asset=MATERIAL, validation_id="validation"))
        return {}

    monkeypatch.setattr(gate, "admit_batch", stop)
    result = finish(project, submit(project, isolation="always"))
    assert project[0].applied == []
    assert result["job_status"] == result["status"] == "cancelled"
    assert result["applied"] == 0 and result["completed_assets"] == []
    assert result["unapplied_assets"] == [MATERIAL, SECOND]
    assert result["pending_transactions"] == []
    assert result["error"]["code"] == ("sync_job_cancelled" if reason == "cancel" else "sync_job_deadline")


@pytest.mark.parametrize("reason", ["cancel", "deadline"])
def test_stop_after_native_commit_finishes_readback_and_history_first(project, monkeypatch, reason):
    commit(project, (MATERIAL, SECOND))
    original = transactions.actual_snapshot
    checks = []

    def verify(workspace, record):
        state.update(project[4], state.current_id(), **({"cancel_requested": True} if reason == "cancel" else {"deadline_at": 0}))
        emit("schema_readback", dict(asset=record["asset"]))
        result = original(workspace, record)
        checks.append(record["asset"])
        return result

    monkeypatch.setattr(transactions, "actual_snapshot", verify)
    result = finish(project, submit(project))
    assert checks == [MATERIAL]
    assert result["status"] == "partial" and result["job_status"] == "cancelled"
    assert result["applied"] == 1 and result["completed_assets"] == [MATERIAL]
    assert result["unapplied_assets"] == [SECOND]
    assert [row["asset"] for row in result["rows"] if row["action"] == "pushed"] == [MATERIAL]
    assert result["pending_transactions"] == []
    observed = service.observe(project[4], "job_status", dict(job_id=result["job_id"]))
    assert observed["completed_assets"] == [MATERIAL]


def test_committed_result_survives_post_commit_persistence_exception(project, monkeypatch):
    commit(project, (MATERIAL, SECOND))
    original = transactions.adopt

    def reject_after_history_commit(*args):
        original(*args)
        raise OSError("post-commit observation write failed")

    monkeypatch.setattr(transactions, "adopt", reject_after_history_commit)
    result = finish(project, submit(project))
    assert result["status"] == "partial" and result["job_status"] == "failed"
    assert result["applied"] == 1 and result["completed_assets"] == [MATERIAL]
    assert result["unapplied_assets"] == [SECOND]
    assert result["pending_transactions"] == []
    assert service.observe(project[4], "job_status", dict(job_id=result["job_id"]))["completed_assets"] == [MATERIAL]


def test_pending_native_transaction_cannot_be_reported_as_success(project):
    store = project[4]
    job = service.create(store, "push", None, dict(project[2]))
    state.update(store, job["id"], planned_assets=[MATERIAL, SECOND])
    store.put_record("apply", "pending", dict(id="pending", job_id=job["id"], asset=MATERIAL, phase="ue_committed"))
    result = reconcile_result(store, job["id"], dict(status="published", error_count=0))
    assert result["status"] == "failed" and result["error_count"] == 1
    assert result["error"]["code"] == "recovery_required"
    assert result["applied"] == 0
    assert result["pending_transactions"] == [dict(apply_id="pending", asset=MATERIAL, phase="ue_committed")]
    assert result["unapplied_assets"] == [SECOND]


def test_reconstructed_row_retains_safety_and_deletion_evidence(project):
    store = project[4]
    job = service.create(store, "push", None, dict(project[2]))
    state.update(store, job["id"], planned_assets=[MATERIAL, SECOND])
    safety = dict(validation_id="validation", binding_digest="exact-candidate")
    store.put_record("apply", "done", dict(id="done", job_id=job["id"], asset=MATERIAL,
        phase="completed", published="published", safety_validation=safety, request=dict(delete_asset=True)))
    result = reconcile_result(store, job["id"], dict(status="cancelled", error_count=1))
    assert result["rows"][0]["deleted"] is True
    assert result["rows"][0]["safety_validation"] == safety
    assert result["unapplied_assets"] == [SECOND]


def test_interrupted_job_result_uses_durable_transactions_instead_of_claiming_zero(project):
    store = project[4]
    job = service.create(store, "push", None, dict(project[2]))
    store.put_record("apply", "done", dict(id="done", job_id=job["id"], asset=MATERIAL,
        phase="completed", published="published", request={}))
    state.update(store, job["id"], status="interrupted", planned_assets=[MATERIAL, SECOND])
    result = service.observe(store, "job_result", dict(job_id=job["id"]))
    assert result["applied"] == 1 and result["completed_assets"] == [MATERIAL]
    assert result["unapplied_assets"] == [SECOND]
    assert result["job_status"] == "interrupted"
