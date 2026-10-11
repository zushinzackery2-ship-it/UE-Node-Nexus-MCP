"""Prepare a worker and durable metadata before publishing callable work."""

import time

from ..instances.errors import InstanceError


def submit(owner, operation, executor, work_scope, on_cancel, on_finish, on_submit):
    with owner._lock:
        if owner._closing:
            raise InstanceError("session_closed", "the MCP task queue is closing")
        active = sum(record.status in ("reserving", "queued", "running") for record in owner._tasks.values())
        if active >= owner.MAX_ACTIVE_TASKS:
            raise InstanceError("task_queue_full", "the MCP task queue is full", dict(limit=owner.MAX_ACTIVE_TASKS))
        identifier = f"task-{owner._next_task_number}"
        owner._next_task_number += 1
        record = owner._TaskRecord(identifier, operation, dict(), status="reserving", submitted_at=time.time(),
                                   executor=executor, on_cancel=on_cancel, on_finish=on_finish)
        owner._tasks[identifier] = record
    try:
        owner._ensure_worker()
        with owner._lock:
            if owner._closing or record.status == "cancelled":
                raise InstanceError("session_closed", "the MCP task queue closed during submission")
            if on_submit:
                on_submit(identifier)
            record.work_scope = work_scope
            record.status = "queued"
            owner._queue.put(identifier)
    except Exception:
        with owner._lock:
            owner._tasks.pop(identifier, None)
        raise
    return identifier
