"""Bounded facade waits hand long workflows back as persistent job handles."""

from pathlib import Path
import os

from ...task_queue import wait_for_task
from ..collaboration.jobs.service import submit, observe
from ..collaboration.store import Store
from ..collaboration.report.options import validate, JOB_ACTIONS
from ..storage.paths import project_dir


def run(action, paths, options, runner, root: Path, scope=None):
    validate(action, options)
    project = project_dir(root, options["project"])
    store = Store(project / ".nexus/collaboration")
    try:
        if action in JOB_ACTIONS:
            return observe(store, action, options)
        request = dict(options)
        background = request.pop("background", False)
        wait = request.pop("wait_seconds", 30)
        job = submit(store, action, paths, request, runner, scope)
        record = store.record("sync_job", job["job_id"])
        if not background and record.get("task_id") and record["owner"]["pid"] == os.getpid():
            wait_for_task(record["task_id"], wait)
            record = store.record("sync_job", job["job_id"])
        if record.get("result_id") and not background:
            return observe(store, "job_result", dict(job_id=record["id"]))
        return observe(store, "job_status", dict(job_id=record["id"]))
    except BaseException:
        if scope:
            scope.release()
        raise
    finally:
        store.db.discard()
