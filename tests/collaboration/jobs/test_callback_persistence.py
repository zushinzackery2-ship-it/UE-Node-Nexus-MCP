"""SQLite callback faults leave durable evidence and never strand the worker."""

from unittest.mock import Mock

import pytest

from ue_node_nexus_mcp import task_queue
from ue_node_nexus_mcp.transcode.collaboration.jobs import service
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.sync import background
from tests.support.scopes import Scope


@pytest.fixture
def queue():
    task_queue.shutdown()
    task_queue.reset_for_tests()
    yield
    task_queue.shutdown()
    task_queue.reset_for_tests()


def test_durable_task_id_fault_never_transfers_scope_or_executes(tmp_path, monkeypatch, queue):
    scope = Scope()
    execute = Mock(return_value=dict(status="ok"))
    update = service.update

    def reject(store, identifier, **changes):
        if "task_id" in changes:
            raise OSError("durable task identity")
        return update(store, identifier, **changes)

    monkeypatch.setattr(service, "update", reject)
    with pytest.raises(OSError, match="durable task identity"):
        background.run("pull", None, dict(project="Demo", background=True), execute, tmp_path, scope)
    assert scope.released
    execute.assert_not_called()
    store = Store(tmp_path / "Demo/.nexus/collaboration")
    record = store.records("sync_job")[0]
    assert record["status"] == "failed" and not record.get("task_id")
    assert task_queue.task_status()["data"]["tasks"] == []
    store.db.discard()


def test_completion_persistence_fault_is_reconciled_after_database_recovers(tmp_path, monkeypatch, queue):
    store = Store(tmp_path / "repository")
    update = service.update
    failed_job = []

    def reject(store, identifier, **changes):
        if changes.get("status") in ("succeeded", "failed") and identifier in failed_job:
            raise OSError("completion persistence")
        return update(store, identifier, **changes)

    def submitted(identifier):
        failed_job.append(next(row["id"] for row in store.records("sync_job") if row.get("task_id") == identifier))

    monkeypatch.setattr(service, "update", reject)
    first = service.submit(store, "pull", None, {}, lambda *_args: dict(status="ok"), Scope(), submitted)
    task = store.record("sync_job", first["job_id"])["task_id"]
    assert task_queue.wait_for_task(task, 3)
    second = service.submit(store, "pull", None, {}, lambda *_args: dict(status="ok"))
    assert task_queue.wait_for_task(store.record("sync_job", second["job_id"])["task_id"], 3)
    monkeypatch.setattr(service, "update", update)
    observed = service.observe(store, "job_status", dict(job_id=first["job_id"]))
    assert observed["status"] == "failed" and observed["stage"] == "task_reconciled"
    assert observed["error"]["code"] == "task_completion_failed"
    assert service.observe(store, "job_status", dict(job_id=second["job_id"]))["status"] == "succeeded"
    store.db.discard()
