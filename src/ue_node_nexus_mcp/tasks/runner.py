"""One sequential worker; cleanup runs on every completion and cancellation path."""

from contextlib import nullcontext
import threading
import time

from ..instances.errors import InstanceError


def run_one(record) -> None:
    from ..facade_execute import execute_operation
    from .. import task_queue as owner
    try:
        with record.work_scope.activate() if record.work_scope else nullcontext():
            response = execute_operation(record.operation, record.payload)
        with owner._lock:
            record.result = response
            record.status = "succeeded" if response.get("ok") else "failed"
            if record.status == "failed":
                record.error = response.get("error") or dict(code="operation_failed", message="operation failed")
    except Exception as exc:
        with owner._lock:
            record.status = "failed"
            record.error = exc.envelope()["error"] if isinstance(exc, InstanceError) else dict(code="operation_failed", message=str(exc))
    finally:
        if record.work_scope:
            record.work_scope.release()


def worker_loop() -> None:
    from .. import task_queue as owner
    while True:
        task_id = owner._queue.get()
        try:
            if task_id is None:
                return
            with owner._lock:
                record = owner._tasks.get(task_id)
                if record is None or record.status != "queued":
                    continue
                record.status = "running"
                record.started_at = time.time()
            run_one(record)
            with owner._lock:
                record.finished_at = time.time()
            record.done.set()
            owner._prune_finished()
        finally:
            owner._queue.task_done()


def ensure_worker() -> None:
    from .. import task_queue as owner
    with owner._lock:
        if owner._closing:
            return
        if owner._worker is None or not owner._worker.is_alive():
            owner._worker = threading.Thread(target=worker_loop, name="ue-nexus-task-worker", daemon=True)
            owner._worker.start()
