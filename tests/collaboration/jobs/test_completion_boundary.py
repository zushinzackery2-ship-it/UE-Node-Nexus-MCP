"""Durable job success is published after scope cleanup, with matching diagnostics."""

from threading import Event

import pytest

from ue_node_nexus_mcp.facade.sync import _finish
from ue_node_nexus_mcp.tasks import queue
from ue_node_nexus_mcp.transcode.collaboration.jobs import service
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.sync import background
from tests.support.scopes import Scope


@pytest.fixture(autouse=True)
def clean_queue():
    queue.shutdown()
    queue.reset_for_tests()
    yield
    queue.shutdown()
    queue.reset_for_tests()


def test_durable_job_waits_for_scope_and_reports_completion_failure(tmp_path):
    entered, release = Event(), Event()

    class FailingScope(Scope):
        def release(self):
            entered.set()
            assert release.wait(5)
            raise OSError("scope release probe")

    store = Store(tmp_path / "repository")
    job = service.submit(store, "pull", None, {}, lambda *_: dict(status="ok"), FailingScope())
    identifier = job["job_id"]
    task = store.record("sync_job", identifier)["task_id"]
    try:
        assert entered.wait(5)
        status = service.observe(store, "job_status", dict(job_id=identifier))
        assert status["status"] not in service.TERMINAL
        with pytest.raises(SyncError, match="no completed result"):
            service.observe(store, "job_result", dict(job_id=identifier))
    finally:
        release.set()
        assert queue.wait_for_task(task, 5)
    status = service.observe(store, "job_status", dict(job_id=identifier))
    result = service.observe(store, "job_result", dict(job_id=identifier))
    assert status["status"] == result["job_status"] == "failed"
    assert result["error"]["code"] == "task_completion_failed"
    assert result["operation_diagnostics"]["status"] == "failed"
    assert result["error_count"] == 1
    store.db.discard()


def test_bounded_wait_returns_handle_while_scope_is_completing(tmp_path, monkeypatch):
    entered, release = Event(), Event()

    class WaitingScope(Scope):
        def release(self):
            entered.set()
            assert release.wait(5)

    wait_for_task = background.wait_for_task

    def wait_during_completion(identifier, seconds):
        assert entered.wait(5)
        return wait_for_task(identifier, seconds)

    monkeypatch.setattr(background, "wait_for_task", wait_during_completion)
    try:
        result = background.run("pull", None, dict(project="Demo", wait_seconds=0),
                                lambda *_: dict(status="ok"), tmp_path, WaitingScope())
        assert result["action"] == "job_status"
        assert result["status"] == "completing"
        assert result["next"]["action"] == "job_status"
    finally:
        release.set()


@pytest.mark.parametrize("status", ["base-conflict", "blocked", "partial", "recovery_required"])
def test_unsuccessful_sync_status_agrees_across_facade_job_and_diagnostics(tmp_path, status):
    report = dict(status=status)
    facade = _finish(dict(report))
    assert facade["ok"] is False
    assert facade["data"]["operation_diagnostics"]["status"] == "failed"
    store = Store(tmp_path / "repository")
    job = service.submit(store, "push", None, {}, lambda *_: dict(report))
    task = store.record("sync_job", job["job_id"])["task_id"]
    assert queue.wait_for_task(task, 5)
    result = service.observe(store, "job_result", dict(job_id=job["job_id"]))
    assert result["job_status"] == "failed"
    assert result["operation_diagnostics"]["status"] == "failed"
    store.db.discard()
