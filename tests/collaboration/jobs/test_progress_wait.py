"""Wait for durable state revisions without losing cancellation or progress."""

from concurrent.futures import ThreadPoolExecutor
import threading
import time

import pytest

from ue_node_nexus_mcp.transcode.collaboration.jobs import service, state
from ue_node_nexus_mcp.transcode.collaboration.report.options import validate
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.progress import emit


def test_waiting_reader_observes_cancellation_and_the_same_copy_progress(tmp_path):
    store = Store(tmp_path / "repository")
    job = service.create(store, "push", None, {})
    entered = threading.Event()

    def wait(revision):
        reader = Store(store.root)
        try:
            entered.set()
            return service.observe(reader, "job_status", dict(job_id=job["id"], after_revision=revision, wait_seconds=2))
        finally:
            reader.db.discard()

    try:
        with state.active(store, job["id"]):
            emit("isolation_copy", dict(phase="copy", validation_id="validation", copied_bytes=4096, copied_files=2))
        before = service.observe(store, "job_status", dict(job_id=job["id"]))
        with ThreadPoolExecutor(max_workers=1) as pool:
            future = pool.submit(wait, before["revision"])
            assert entered.wait(1)
            assert not future.done()
            service.observe(store, "job_cancel", dict(job_id=job["id"]))
            after = future.result(timeout=2)
        assert after["revision"] > before["revision"] and after["updated_at"] >= before["updated_at"]
        assert after["cancel_requested"]
        assert after["validation_progress"] == before["validation_progress"]
        assert service.observe(store, "job_status", dict(job_id=job["id"]))["revision"] == after["revision"]
    finally:
        store.db.discard()


def test_wait_timeout_does_not_manufacture_progress(tmp_path):
    store = Store(tmp_path / "repository")
    try:
        job = service.create(store, "schema", None, {})
        before = service.observe(store, "job_status", dict(job_id=job["id"]))
        started = time.monotonic()
        after = service.observe(store, "job_status", dict(job_id=job["id"], after_revision=before["revision"], wait_seconds=0.03))
        assert time.monotonic() - started >= 0.025
        assert after == before
        state.update(store, job["id"], status="cancelled")
        terminal = service.observe(store, "job_status", dict(job_id=job["id"], after_revision=10000, wait_seconds=60))
        assert terminal["status"] == "cancelled"
    finally:
        store.db.discard()


@pytest.mark.parametrize("options", [dict(after_revision=True), dict(after_revision=-1), dict(after_revision=1.5),
                                       dict(wait_seconds=61), dict(wait_seconds=float("nan"))])
def test_invalid_wait_options_are_rejected(options):
    with pytest.raises(SyncError, match="must"):
        validate("job_status", dict(job_id="job", **options))
