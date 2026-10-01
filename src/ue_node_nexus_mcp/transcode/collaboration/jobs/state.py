"""Small job records, cooperative deadlines, and publication progress."""

from contextlib import contextmanager
from contextvars import ContextVar
import time

from ...errors import SyncError

TERMINAL = ("succeeded", "failed", "cancelled", "interrupted")
_job = ContextVar("nexus_sync_job", default=None)


def update(store, identifier, **changes):
    with store.db.connection(write=True) as connection:
        record = store.record("sync_job", identifier)
        if record is None:
            raise SyncError("job_not_found", identifier)
        record.update(changes)
        roots = [record[key] for key in ("source_commit", "result_id") if record.get(key)]
        record["generation"] = store.put_record("sync_job", identifier, record, roots, record["generation"], connection)
    return record


@contextmanager
def active(store, identifier):
    token = _job.set((store, identifier))
    try:
        checkpoint(stage="running")
        yield
    finally:
        _job.reset(token)


def checkpoint(stage=None, asset=None, completed=False):
    context = _job.get()
    if context is None:
        return
    store, identifier = context
    record = store.record("sync_job", identifier)
    expired = record.get("deadline_at") is not None and time.time() >= record["deadline_at"]
    if not completed and (record.get("cancel_requested") or expired):
        raise SyncError("sync_job_deadline" if expired else "sync_job_cancelled",
                        "workflow stopped between atomic operations", dict(job_id=identifier, completed_assets=record.get("completed_assets", [])))
    changes = dict(updated_at=time.time())
    if stage:
        changes["stage"] = stage
    if asset:
        changes["current_asset"] = asset
    if completed:
        changes["completed_assets"] = [*record.get("completed_assets", []), asset]
    current = update(store, identifier, **changes)
    store.event("sync_job_progress", job_id=identifier, stage=current.get("stage"), asset=asset, completed=completed)
