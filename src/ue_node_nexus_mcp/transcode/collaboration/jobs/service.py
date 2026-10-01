"""Persistent sync jobs executed by the existing bounded MCP task queue."""

from __future__ import annotations

from copy import deepcopy
import os
import time
from uuid import uuid4

from ....instances.identity.processes import inspect_process, is_alive
from ....instances.errors import InstanceError
from ...errors import SyncError
from ...storage.io import digest
from ..history import History
from .state import TERMINAL, active, update


def submit(store, action, paths, options, runner, scope=None):
    from ....task_queue import submit_callable

    key = options.pop("idempotency_key", None)
    request = dict(action=action, paths=paths, options=deepcopy(options))
    request_id = digest(dict(workspace_id=options.get("workspace_id"), key=key)) if key else None
    with store.lock("sync-submit", timeout=5.0):
        previous = store.record("sync_job_request", request_id) if request_id else None
        if previous:
            if previous["digest"] != digest(request):
                raise SyncError("idempotency_conflict", "sync job key parameters changed")
            if scope:
                scope.release()
            return observe(store, "job_status", dict(job_id=previous["job_id"]))
        record = create(store, action, paths, options)
        if request_id:
            store.put_record("sync_job_request", request_id, dict(job_id=record["id"], digest=digest(request)))
        try:
            task_id = submit_callable("ue_sync_" + action, lambda: execute(store, record["id"], runner), scope,
                                      lambda: cancel_queued(store, record["id"]),
                                      lambda task: finish_failed_task(store, record["id"], task))
        except InstanceError as exc:
            update(store, record["id"], status="failed", error=dict(code=exc.code, message=str(exc)), finished_at=time.time())
            if scope:
                scope.release()
            raise
        update(store, record["id"], task_id=task_id)
    return observe(store, "job_status", dict(job_id=record["id"]))


def create(store, action, paths, options):
    identifier = uuid4().hex
    owner = inspect_process(os.getpid())
    if not owner:
        raise SyncError("instance_unverified", "cannot verify the sync worker process")
    source = None
    workspace_id = options.get("workspace_id")
    if action == "push" and workspace_id:
        workspace = store.record("workspace", workspace_id)
        if workspace is None or workspace.get("closed"):
            raise SyncError("workspace_not_found", workspace_id)
        source = History(store).resolve(options.get("source") or options.get("revision", "HEAD"), workspace["head"])
        options["source"] = source
    seconds = options.pop("deadline_seconds", None)
    record = dict(id=identifier, workspace_id=workspace_id, operation=action, paths=paths, options=deepcopy(options),
                  status="queued", submitted_at=time.time(), owner=owner, source_commit=source,
                  deadline_at=time.time() + seconds if seconds else None, completed_assets=[])
    store.put_record("sync_job", identifier, record, [source] if source else [])
    store.event("sync_job_submitted", job_id=identifier, workspace_id=workspace_id, operation=action, source_commit=source)
    return record


def execute(store, identifier, runner):
    record = update(store, identifier, status="running", started_at=time.time())
    try:
        with active(store, identifier):
            result = runner(record["operation"], record["paths"], record["options"])
        status = "failed" if result.get("error_count") or result.get("status") in ("blocked", "partial", "conflict", "stale") else "succeeded"
    except (SyncError, InstanceError) as exc:
        status = "cancelled" if exc.code in ("sync_job_cancelled", "sync_job_deadline") else "failed"
        result = dict(status=status, error_count=1, error=dict(code=exc.code, message=str(exc), details=exc.details))
    except Exception as exc:
        status = "failed"
        result = dict(status=status, error_count=1, error=dict(code="sync_job_failed", message=str(exc)))
    result_id = store.objects.put("sync_result", result)
    update(store, identifier, status=status, result_id=result_id, finished_at=time.time())
    store.event("sync_job_finished", job_id=identifier, status=status, result_id=result_id)
    return dict(ok=status == "succeeded", data=result, error=result.get("error"))


def finish_failed_task(store, identifier, task):
    record = store.record("sync_job", identifier)
    if record["status"] not in TERMINAL:
        update(store, identifier, status="failed", error=task.error, finished_at=time.time())
    store.db.discard()


def cancel_queued(store, identifier):
    record = store.record("sync_job", identifier)
    if record["status"] not in TERMINAL:
        update(store, identifier, status="cancelled", finished_at=time.time())


def observe(store, action, options):
    identifier = options["job_id"]
    record = store.record("sync_job", identifier)
    if record is None:
        raise SyncError("job_not_found", identifier)
    if record["status"] not in TERMINAL and not is_alive(record["owner"]):
        record = update(store, identifier, status="interrupted", stage="owner_exited", finished_at=time.time())
    if action == "job_cancel" and record["status"] not in TERMINAL:
        record = update(store, identifier, cancel_requested=True)
        if record.get("task_id") and record["owner"]["pid"] == os.getpid():
            from ....task_queue import task_cancel

            task_cancel(record["task_id"])
        record = store.record("sync_job", identifier)
    if action == "job_result":
        if not record.get("result_id"):
            if record["status"] in TERMINAL:
                return dict(report(record), error_count=1)
            raise SyncError("job_not_done", "job has no completed result", report(record))
        return dict(store.objects.data(record["result_id"], "sync_result"), job_id=identifier, job_status=record["status"])
    return report(record)


def report(record):
    keys = ("workspace_id", "operation", "status", "submitted_at", "started_at", "finished_at", "source_commit",
            "stage", "current_asset", "completed_assets", "deadline_at", "cancel_requested", "error")
    result = dict((key, record[key]) for key in keys if key in record)
    result.update(action="job_status", job_id=record["id"], durable=True,
                  next=dict(action="job_result" if record.get("result_id") else "job_status", options=dict(job_id=record["id"])))
    if record["status"] == "interrupted":
        result["recover"] = dict(action="recover", options=dict(workspace_id=record.get("workspace_id"), dry_run=False))
        result["error_count"] = 1
    return result
