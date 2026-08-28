"""MCP-local background task queue for long-running internal operations.

``task_submit`` validates one registry operation upfront, queues it, and
returns immediately with a task id; a single daemon worker thread executes
queued tasks strictly in submission order (so writes never interleave over the
bridge). ``task_status`` / ``task_result`` / ``task_cancel`` observe and manage
the queue. State is in-memory and per MCP session: task ids do not survive a
server restart.
"""

from __future__ import annotations

import queue
import threading
import time
from dataclasses import dataclass, field
from typing import Any

from .errors import BridgeError
from .facade_response import compact_data_summary
from .operation_validation import validate_operation_call
from .runtime import default_tool

MAX_ACTIVE_TASKS = 20
MAX_FINISHED_TASKS = 50
_FINISHED_STATUSES = ("succeeded", "failed", "cancelled")

# task_* operations may not wrap each other (a task wrapping task_status would
# only observe this same in-memory queue); batch_execute is allowed so one task
# can run a whole sequential batch in the background.
_FORBIDDEN_IN_TASK = {
    name: "nested_task"
    for name in ("task_submit", "task_status", "task_result", "task_cancel")
}


@dataclass
class _TaskRecord:
    task_id: str
    operation: str
    payload: dict[str, Any]
    status: str = "queued"
    submitted_at: float = 0.0
    started_at: float | None = None
    finished_at: float | None = None
    result: dict[str, Any] | None = None
    error: dict[str, Any] | None = None
    done: threading.Event = field(default_factory=threading.Event)


_lock = threading.Lock()
_tasks: dict[str, _TaskRecord] = {}
_queue: queue.Queue[str] = queue.Queue()
_worker: threading.Thread | None = None
_next_task_number = 1


def _envelope(operation: str, ok: bool, data: dict[str, Any], error: dict[str, Any] | None = None) -> dict[str, Any]:
    envelope: dict[str, Any] = {"ok": ok, "operation": operation, "data": data}
    if error is not None:
        envelope["error"] = error
    envelope["diagnostics"] = []
    envelope["warnings"] = []
    return envelope


def _task_error(operation: str, code: str, message: str, details: dict[str, Any] | None = None) -> dict[str, Any]:
    return _envelope(operation, False, {}, {"code": code, "message": message, "details": details or {}})


def _run_one(record: _TaskRecord) -> None:
    from .facade_execute import execute_operation

    try:
        response = execute_operation(record.operation, record.payload)
    except (BridgeError, ValueError) as exc:
        code = "bridge_error" if isinstance(exc, BridgeError) else "invalid_operation"
        with _lock:
            record.status = "failed"
            record.error = {"code": code, "message": str(exc)}
        return
    with _lock:
        record.result = response
        if response.get("ok") is True:
            record.status = "succeeded"
        else:
            record.status = "failed"
            error = response.get("error")
            record.error = error if isinstance(error, dict) else {"code": "operation_failed", "message": "operation failed"}


def _worker_loop() -> None:
    while True:
        task_id = _queue.get()
        with _lock:
            record = _tasks.get(task_id)
            if record is None or record.status != "queued":
                continue
            record.status = "running"
            record.started_at = time.time()
        _run_one(record)
        with _lock:
            record.finished_at = time.time()
        record.done.set()
        _prune_finished()


def _ensure_worker() -> None:
    global _worker
    if _worker is None or not _worker.is_alive():
        _worker = threading.Thread(target=_worker_loop, name="ue-nexus-task-worker", daemon=True)
        _worker.start()


def _prune_finished() -> None:
    with _lock:
        finished = [task_id for task_id, record in _tasks.items() if record.status in _FINISHED_STATUSES]
        for task_id in finished[: max(0, len(finished) - MAX_FINISHED_TASKS)]:
            del _tasks[task_id]


def _status_row(record: _TaskRecord) -> dict[str, Any]:
    row: dict[str, Any] = {
        "task_id": record.task_id,
        "operation": record.operation,
        "status": record.status,
        "submitted_at": record.submitted_at,
    }
    if record.started_at is not None:
        row["started_at"] = record.started_at
    if record.finished_at is not None:
        row["finished_at"] = record.finished_at
    if record.error is not None:
        row["error"] = record.error
    if record.status == "succeeded" and record.result is not None:
        row["summary"] = compact_data_summary(record.result.get("data"))
    return row


@default_tool()
def task_submit(operation: str, payload: dict[str, Any] | None = None) -> dict[str, Any]:
    """Validate one internal operation, queue it for background execution, return a task id."""
    call_payload = payload if payload is not None else {}
    error = validate_operation_call(operation, call_payload, _FORBIDDEN_IN_TASK)
    if error is not None:
        return _task_error("task_submit", error["code"], error["message"])

    with _lock:
        active = sum(1 for record in _tasks.values() if record.status in ("queued", "running"))
        if active >= MAX_ACTIVE_TASKS:
            return _task_error(
                "task_submit",
                "task_queue_full",
                f"{active} tasks are already queued or running (limit {MAX_ACTIVE_TASKS})",
            )
        global _next_task_number
        task_id = f"task-{_next_task_number}"
        _next_task_number += 1
        record = _TaskRecord(task_id=task_id, operation=operation, payload=call_payload, submitted_at=time.time())
        _tasks[task_id] = record
        queued_before = sum(1 for other in _tasks.values() if other.status == "queued") - 1

    _queue.put(task_id)
    _ensure_worker()
    return _envelope(
        "task_submit",
        True,
        {"task_id": task_id, "operation": operation, "status": "queued", "queued_before": queued_before},
    )


@default_tool()
def task_status(task_id: str | None = None) -> dict[str, Any]:
    """Report one task's status by id, or list all known tasks newest-first."""
    with _lock:
        if task_id is None:
            rows = [_status_row(record) for record in reversed(list(_tasks.values()))]
            return _envelope("task_status", True, {"tasks": rows, "count": len(rows)})
        record = _tasks.get(task_id)
        if record is None:
            return _task_error("task_status", "task_not_found", f"unknown task id: {task_id}")
        return _envelope("task_status", True, _status_row(record))


@default_tool()
def task_result(task_id: str) -> dict[str, Any]:
    """Return the stored full response envelope of a finished task."""
    with _lock:
        record = _tasks.get(task_id)
        if record is None:
            return _task_error("task_result", "task_not_found", f"unknown task id: {task_id}")
        if record.status not in _FINISHED_STATUSES:
            return _task_error(
                "task_result",
                "task_not_done",
                f"task is still {record.status}; poll task_status until it finishes",
                {"task_id": task_id, "status": record.status},
            )
        data: dict[str, Any] = {"task_id": task_id, "status": record.status, "operation": record.operation}
        if record.result is not None:
            data["result"] = record.result
        if record.error is not None:
            data["error"] = record.error
        return _envelope("task_result", True, data)


@default_tool()
def task_cancel(task_id: str) -> dict[str, Any]:
    """Cancel one still-queued task; running or finished tasks cannot be cancelled."""
    with _lock:
        record = _tasks.get(task_id)
        if record is None:
            return _task_error("task_cancel", "task_not_found", f"unknown task id: {task_id}")
        if record.status != "queued":
            return _task_error(
                "task_cancel",
                "task_not_cancellable",
                f"only queued tasks can be cancelled; task is {record.status}",
                {"task_id": task_id, "status": record.status},
            )
        record.status = "cancelled"
        record.finished_at = time.time()
    record.done.set()
    return _envelope("task_cancel", True, {"task_id": task_id, "status": "cancelled"})


def wait_for_task(task_id: str, timeout_seconds: float) -> bool:
    """Block until a task finishes (used by tests; not exposed as an operation)."""
    with _lock:
        record = _tasks.get(task_id)
    if record is None:
        return False
    return record.done.wait(timeout_seconds)


def reset_for_tests() -> None:
    """Forget all task records. Stale queued ids are skipped by the worker."""
    with _lock:
        _tasks.clear()
