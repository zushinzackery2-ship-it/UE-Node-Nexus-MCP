"""Persist scheduler cleanup evidence once the repository can accept writes."""

import os
import time

from .state import TERMINAL, update


def reconcile(store, record):
    if not record.get("task_id") or record["owner"]["pid"] != os.getpid():
        return record
    from ....task_queue import task_status
    task = task_status(record["task_id"])
    if not task.get("ok"):
        return record
    row = task["data"]
    if row["status"] not in ("failed", "cancelled"):
        return record
    error = row.get("error")
    completion_failed = (error or {}).get("code") in ("task_completion_failed", "task_worker_failed")
    if record["status"] in TERMINAL and not completion_failed:
        return record
    if record["status"] == row["status"] and record.get("error") == error:
        return record
    return update(store, record["id"], status=row["status"], error=error,
                  finished_at=row.get("finished_at") or time.time(), stage="task_reconciled")
