"""Runtime evidence survives real publication, recovery and durable job stores."""

from pathlib import Path

import pytest

from tests.collaboration.support.recovery import MATERIAL, call, edit, lose_publication, project as project
from tests.diagnostics.support.evidence import notice
from ue_node_nexus_mcp import task_queue
from ue_node_nexus_mcp.transcode.collaboration.jobs import service
from ue_node_nexus_mcp.transcode.collaboration.store import Store


def with_notice(ue):
    def bridge(operation, payload):
        return dict(ue(operation, payload), runtime_diagnostics=notice())
    return bridge


def publish(project, monkeypatch=None):
    edit(project, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    call(project, "commit", all=True, message="runtime evidence")
    return call(project, "push", [MATERIAL], bridge=with_notice(project[0]))


def assert_notice(value):
    assert value["runtime_diagnostics"]["error_count"] == 303
    assert value["runtime_diagnostics"]["status"] == "failed"
    assert value["runtime_verification"] == "not_run"


def test_publication_and_durable_apply_keep_evidence(project):
    result = publish(project)
    assert result["status"] == "published"
    assert_notice(result)
    record = project[3].record("apply", result["rows"][0]["apply_id"])
    assert_notice(record)
    assert record["runtime_diagnostics"]["live_state"] is False


def test_recovery_keeps_original_apply_session(project, monkeypatch):
    lost = lose_publication(monkeypatch)
    result = publish(project)
    assert result["errors"][MATERIAL]["code"] == "publication_io_failed"
    monkeypatch.undo()
    record = project[3].record("apply", lost[0])
    assert_notice(record)
    recovered = call(project, "recover", apply_id=lost[0], bridge=with_notice(project[0]))
    assert recovered["phase"] == "completed"
    assert_notice(recovered)
    assert recovered["runtime_diagnostics"]["session_id"] == record["runtime_diagnostics"]["session_id"]


@pytest.mark.parametrize("fail", (False, True))
def test_job_status_and_result_preserve_evidence_after_reopen(project, fail):
    task_queue.reset_for_tests()
    ue, env, workspace, store = project
    edit(project, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    call(project, "commit", all=True, message="queued runtime evidence")
    if fail:
        ue.fail_save = set([MATERIAL])
    job = call(project, "push", [MATERIAL], bridge=with_notice(ue), background=True)
    record = store.record("sync_job", job["job_id"])
    try:
        assert task_queue.wait_for_task(record["task_id"], 10)
        reopened = Store(Path(store.root))
        try:
            for action in ("job_status", "job_result"):
                result = service.observe(reopened, action, dict(job_id=job["job_id"]))
                assert_notice(result)
                assert result["runtime_diagnostics"]["live_state"] is False
            assert result["job_status"] == ("failed" if fail else "succeeded")
        finally:
            reopened.db.discard()
    finally:
        task_queue.reset_for_tests()
