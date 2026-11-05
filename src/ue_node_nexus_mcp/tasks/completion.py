"""Terminal task bookkeeping isolates independent cleanup obligations."""

import logging
import time
from copy import deepcopy
from dataclasses import replace

from ..diagnostics.contracts.response import destination, finalize
from ..diagnostics.contracts.workflows import stored_fields

LOG = logging.getLogger(__name__)


def complete(record, owner, cancelled=False):
    outcome = replace(record, status="cancelled" if cancelled else record.completion_status,
                      result=deepcopy(record.result), error=deepcopy(record.error),
                      diagnostic_snapshot=deepcopy(record.diagnostic_snapshot))
    callbacks = [("scope", record.work_scope.release)] if record.work_scope else []
    if cancelled and record.on_cancel:
        callbacks.append(("cancel", record.on_cancel))
    if record.on_finish:
        callbacks.append(("finish", lambda: record.on_finish(outcome)))
    failures = []
    for phase, callback in callbacks:
        try:
            callback()
        except Exception as exc:
            failure = dict(phase=phase, type=type(exc).__name__, message=str(exc))
            failures.append(failure)
            LOG.exception("task_cleanup_failed task_id=%s operation=%s phase=%s", record.task_id, record.operation, phase)
            outcome.status = "failed"
            outcome.error = dict(code="task_completion_failed", message="task cleanup could not complete",
                                 details=dict(failures=list(failures), operation_error=record.error))
            outcome.result = finalize(dict(ok=False, error=outcome.error, operation_result=record.result),
                                      record.operation, record.diagnostic_snapshot.get("runtime_diagnostics"))
            outcome.diagnostic_snapshot = stored_fields(destination(outcome.result))
    with owner._lock:
        record.status = outcome.status
        record.error, record.result = outcome.error, outcome.result
        record.diagnostic_snapshot = outcome.diagnostic_snapshot
        record.finished_at = time.time()
        record.done.set()
    owner._prune_finished()
