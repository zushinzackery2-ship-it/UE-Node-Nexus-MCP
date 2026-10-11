"""Terminal task bookkeeping isolates independent cleanup obligations."""

import logging
import time

LOG = logging.getLogger(__name__)


def complete(record, owner, cancelled=False):
    callbacks = [("scope", record.work_scope.release)] if record.work_scope else []
    if cancelled and record.on_cancel:
        callbacks.append(("cancel", record.on_cancel))
    if record.on_finish:
        callbacks.append(("finish", lambda: record.on_finish(record)))
    failures = []
    for phase, callback in callbacks:
        try:
            callback()
        except Exception as exc:
            failure = dict(phase=phase, type=type(exc).__name__, message=str(exc))
            failures.append(failure)
            LOG.exception("task_cleanup_failed task_id=%s operation=%s phase=%s", record.task_id, record.operation, phase)
            with owner._lock:
                record.status = "failed"
                record.error = dict(code="task_completion_failed", message="task cleanup could not complete",
                                    details=dict(failures=list(failures), operation_error=record.error))
    with owner._lock:
        record.finished_at = time.time()
    record.done.set()
    owner._prune_finished()
