"""One sequential worker; cleanup runs on every completion and cancellation path."""

from contextlib import nullcontext
import threading
import time

from ..instances.errors import InstanceError
from ..diagnostics.contracts.observation import capture
from ..diagnostics.contracts.response import destination, finalize
from ..diagnostics.contracts.workflows import stored_fields
from .completion import complete


def run_one(record) -> None:
    from ..facade_execute import execute_operation
    from .. import task_queue as owner
    with capture() as observed:
        run_observed(record, owner, execute_operation, observed)


def run_observed(record, owner, execute_operation, observed) -> None:
    try:
        with record.work_scope.activate() if record.work_scope else nullcontext():
            response = record.executor() if record.executor else execute_operation(record.operation, record.payload)
        response = finalize(response, record.operation, observed.last)
        with owner._lock:
            record.result = response
            record.diagnostic_snapshot = stored_fields(destination(response))
            record.status = "succeeded" if response.get("ok") else "failed"
            if record.status == "failed":
                record.error = response.get("error") or dict(code="operation_failed", message="operation failed")
    except Exception as exc:
        with owner._lock:
            record.status = "failed"
            record.error = exc.envelope()["error"] if isinstance(exc, InstanceError) else dict(code="operation_failed", message=str(exc))
            result = finalize(dict(ok=False, error=record.error), record.operation, observed.last)
            record.diagnostic_snapshot = stored_fields(result)


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
            try:
                run_one(record)
            except Exception as exc:
                with owner._lock:
                    record.status = "failed"
                    record.error = dict(code="task_worker_failed", message=str(exc))
            finally:
                complete(record, owner)
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
