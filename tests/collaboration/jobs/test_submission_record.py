"""Durable job status distinguishes rejected submission from accepted work."""

from contextlib import nullcontext
from unittest.mock import Mock

import pytest

from ue_node_nexus_mcp import task_queue
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.sync import background


def test_unaccepted_generic_submission_failure_is_terminal(tmp_path, monkeypatch):
    monkeypatch.setattr(task_queue, "submit_callable", Mock(side_effect=RuntimeError("rejected enqueue")))
    scope = Mock()
    with pytest.raises(RuntimeError, match="rejected enqueue"):
        background.run("pull", None, dict(project="Demo", background=True), Mock(), tmp_path, scope)
    jobs = Store(tmp_path / "Demo" / ".nexus" / "collaboration").records("sync_job")
    assert len(jobs) == 1 and jobs[0]["status"] == "failed"
    assert jobs[0]["error"]["code"] == "sync_job_submit_failed"
    scope.release.assert_called_once_with()


def test_worker_start_failure_rejects_job_before_scope_transfer(tmp_path, monkeypatch):
    task_queue.shutdown()
    task_queue.reset_for_tests()
    scope = Mock()
    scope.activate.side_effect = nullcontext
    monkeypatch.setattr(task_queue, "_ensure_worker", Mock(side_effect=RuntimeError("worker start")))
    try:
        with pytest.raises(RuntimeError, match="worker start"):
            background.run("pull", None, dict(project="Demo", background=True),
                           lambda *_args: dict(status="ok"), tmp_path, scope)
        job = Store(tmp_path / "Demo" / ".nexus" / "collaboration").records("sync_job")[0]
        assert job["status"] == "failed" and not job.get("task_id")
        assert job["error"]["code"] == "sync_job_submit_failed"
        assert task_queue.task_status()["data"]["tasks"] == []
        scope.release.assert_called_once_with()
    finally:
        task_queue.shutdown()
        task_queue.reset_for_tests()
