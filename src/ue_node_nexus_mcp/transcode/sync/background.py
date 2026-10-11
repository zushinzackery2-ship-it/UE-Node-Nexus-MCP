"""Bounded facade waits hand long workflows back as persistent job handles."""

from pathlib import Path
import os

from ...task_queue import wait_for_task
from ..collaboration.jobs.service import submit, observe
from ..collaboration.store import Store
from ..collaboration.report.options import JOB_ACTIONS
from ..storage.paths import project_dir
from .options import validate


def run(action, paths, options, runner, root: Path, scope=None):
    store, transferred = None, False

    def transfer(_task_id):
        nonlocal transferred
        transferred = True

    try:
        project = project_dir(root, options["project"])
        validate(action, options, project)
        store = Store(project / ".nexus/collaboration")
        if action in JOB_ACTIONS:
            return observe(store, action, options)
        request = dict(options)
        background = request.pop("background", False)
        wait = request.pop("wait_seconds", 30)
        job = submit(store, action, paths, request, runner, scope, on_submit=transfer)
        record = store.record("sync_job", job["job_id"])
        if not background and record.get("task_id") and record["owner"]["pid"] == os.getpid():
            wait_for_task(record["task_id"], wait)
            record = store.record("sync_job", job["job_id"])
        if record.get("result_id") and not background:
            return observe(store, "job_result", dict(job_id=record["id"]))
        return observe(store, "job_status", dict(job_id=record["id"]))
    finally:
        try:
            if store is not None:
                store.db.discard()
        finally:
            if scope and not transferred:
                scope.release()
