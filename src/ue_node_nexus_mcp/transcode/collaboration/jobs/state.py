"""Small job records, cooperative deadlines, and publication progress."""

from contextlib import contextmanager
from contextvars import ContextVar
import time

from ...errors import SyncError
from ...progress import cancellation_deferred, listen
from ....diagnostics.contracts.observation import fields as observation_fields

TERMINAL = ("succeeded", "failed", "cancelled", "interrupted")
_job = ContextVar("nexus_sync_job", default=None)
STOP_CODES = frozenset(("sync_job_cancelled", "sync_job_deadline"))


def current_id():
    context = _job.get()
    return context[1] if context else None


def progress(stage, details):
    if stage.startswith("schema_"):
        checkpoint(stage=stage, schema_progress=details)
    else:
        checkpoint(stage=stage, validation_progress=details)


def update(store, identifier, **changes):
    with store.db.connection(write=True) as connection:
        record = store.record("sync_job", identifier)
        if record is None:
            raise SyncError("job_not_found", identifier)
        record.update(changes)
        record["updated_at"] = time.time()
        roots = [record[key] for key in ("source_commit", "result_id") if record.get(key)]
        record["generation"] = store.put_record("sync_job", identifier, record, roots, record["generation"], connection)
    return record


@contextmanager
def active(store, identifier):
    token = _job.set((store, identifier))
    try:
        with listen(progress):
            checkpoint(stage="running")
            yield
    finally:
        _job.reset(token)


def checkpoint(stage=None, asset=None, completed=False, schema_progress=None, validation_progress=None,
               planned_assets=None, refresh_assets=None):
    context = _job.get()
    if context is None:
        return
    store, identifier = context
    record = store.record("sync_job", identifier)
    expired = record.get("deadline_at") is not None and time.time() >= record["deadline_at"]
    changes = dict(updated_at=time.time())
    changes.update(observation_fields())
    if stage:
        changes["stage"] = stage
    if planned_assets is not None:
        changes["planned_assets"] = list(dict.fromkeys(planned_assets))
    refresh = dict(record.get("pending_refresh", {}))
    for target in refresh_assets or ([asset] if stage == "caller_refresh" and asset else []):
        refresh.setdefault(target, time.time())
    if completed and stage == "caller_refreshed":
        refresh.pop(asset, None)
    if refresh != record.get("pending_refresh", {}):
        changes["pending_refresh"] = refresh
    if stage and not stage.startswith("schema_") and schema_progress is None:
        changes["schema_progress"] = None
    if stage and not stage.startswith("isolation_") and validation_progress is None:
        changes["validation_progress"] = None
    if schema_progress is not None:
        changes["schema_progress"] = schema_progress
        changes["current_asset"] = None
    if validation_progress is not None:
        changes.update(validation_progress=validation_progress, schema_progress=None)
        changes["current_asset"] = validation_progress.get("asset")
    if record.get("cancel_requested") or expired:
        changes["cancel_pending"] = True
    if asset:
        changes["current_asset"] = asset
    if completed:
        changes["completed_assets"] = list(dict.fromkeys([*record.get("completed_assets", []), asset]))
    current = update(store, identifier, **changes)
    store.event("sync_job_progress", job_id=identifier, stage=current.get("stage"), asset=current.get("current_asset"),
                completed=completed, revision=current["generation"], cancel_pending=current.get("cancel_pending", False),
                validation_progress=current.get("validation_progress"))
    if not completed and not cancellation_deferred() and (current.get("cancel_requested") or expired):
        raise SyncError("sync_job_deadline" if expired else "sync_job_cancelled",
                        "workflow stopped between atomic operations", dict(job_id=identifier, completed_assets=current.get("completed_assets", [])))
