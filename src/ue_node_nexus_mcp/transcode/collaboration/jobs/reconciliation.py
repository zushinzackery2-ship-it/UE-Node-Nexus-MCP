"""Persist scheduler cleanup evidence once the repository can accept writes."""

import os
import time

from .state import TERMINAL, update
from ....diagnostics.contracts.workflows import attach_report, stored_fields


def reconcile(store, record):
    if not record.get("task_id") or record["owner"]["pid"] != os.getpid():
        return record
    from ....tasks.queue import task_status
    task = task_status(record["task_id"])
    if not task.get("ok"):
        if record["status"] not in TERMINAL and task.get("error", {}).get("code") == "task_not_found":
            error = dict(code="task_evidence_missing", message="scheduler completion evidence expired before durable settlement")
            evidence = attach_report(dict(status="interrupted", error_count=1, error=error), record["operation"], recorded=True)
            return update(store, record["id"], status="interrupted", error=error, finished_at=time.time(),
                          stage="task_reconciled", **stored_fields(evidence))
        return record
    row = task["data"]
    if row["status"] not in TERMINAL and record["status"] in TERMINAL:
        return dict(record, status="completing", finished_at=None)
    if row["status"] not in ("failed", "cancelled"):
        return record
    error = row.get("error")
    completion_failed = (error or {}).get("code") in ("task_completion_failed", "task_worker_failed")
    if record["status"] in TERMINAL and not completion_failed:
        return record
    if record["status"] == row["status"] and record.get("error") == error:
        return record
    return update(store, record["id"], status=row["status"], error=error,
                  finished_at=row.get("finished_at") or time.time(), stage="task_reconciled", **stored_fields(row))
