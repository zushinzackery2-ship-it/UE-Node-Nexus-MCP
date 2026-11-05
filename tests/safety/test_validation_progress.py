"""Real snapshot stages publish progress and stop without abandoning a worker."""

import json
from pathlib import Path
from types import SimpleNamespace
import itertools
import time

import pytest

from ue_node_nexus_mcp.safety.isolation import progress, service
from ue_node_nexus_mcp.transcode.collaboration.jobs import state
from ue_node_nexus_mcp.transcode.collaboration.jobs.service import create, report
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.progress import cancellation_deferred, listen
from .test_isolation import project


@pytest.mark.parametrize("stop_phase", [None, "copy", "copy_chunk", "startup", "execute", "worker_between", "source_check"])
def test_validation_stage_progress_and_safe_cancellation(all_features, tmp_path, monkeypatch, stop_phase):
    source = project(tmp_path)
    store = Store(tmp_path / "repository")
    job = create(store, "push", None, {})
    phases, writes, closed = [], [], []
    if stop_phase == "copy_chunk":
        (source.parent / "Content/Large.uasset").write_bytes(b"source" * (512 * 1024))
        ticks = itertools.count()
        monkeypatch.setattr(progress, "time", SimpleNamespace(monotonic=lambda: next(ticks), time=time.time))

    class Worker:
        def __init__(self, *args):
            pass

        def start(self):
            return dict(instance=dict(instance_id="isolated"))

        def execute(self, item):
            writes.append(item)
            if stop_phase == "worker_between" and len(writes) == 1:
                state.update(store, job["id"], cancel_requested=True)
                progress.advance("execute", force=True, operation_index=0)
            return dict(ok=True)

        def close(self):
            closed.append(True)
            return dict(exit_confirmed=True, exit_code=0)

    def observe(stage, details):
        phases.append((stage, dict(details)))
        if details["phase"] == stop_phase or (stop_phase == "copy_chunk" and details.get("copied_bytes", 0) > 0):
            state.update(store, job["id"], cancel_requested=True)
        state.progress(stage, details)

    monkeypatch.setenv("UE_NEXUS_VALIDATION_COPY", "full")
    monkeypatch.setattr(service, "runtime_root", lambda: tmp_path / "runtime")
    monkeypatch.setattr("ue_node_nexus_mcp.instances.lifecycle.engine.resolve_engine", lambda *args: tmp_path)
    monkeypatch.setattr("ue_node_nexus_mcp.safety.isolation.session.ValidationSession", Worker)
    operations = [dict(operation="asset_create", payload=dict(asset_path="/Game/Safety/Probe", asset_kind="material"))] * 2
    try:
        with state.active(store, job["id"]), listen(observe):
            if stop_phase:
                with pytest.raises(SyncError) as failure:
                    service.validate(str(source), operations, None, "d3d12", 30, False)
                assert failure.value.code == "sync_job_cancelled"
                receipt = Path(failure.value.details["receipt_path"])
                data = json.loads(receipt.read_text())
                assert data["state"] == "cancelled"
            else:
                result = service.validate(str(source), operations, None, "d3d12", 30, False)
                assert result["ok"]
                data = result["data"]
        assert data["cleanup"]["exit_confirmed"]
        expected_writes = 2 if stop_phase in (None, "source_check") else int(stop_phase == "worker_between")
        assert len(writes) == expected_writes
        assert bool(closed) == (stop_phase not in ("copy", "copy_chunk"))
        observed = [details["phase"] for _, details in phases]
        if stop_phase is None:
            assert set(("preflight", "copy", "startup", "execute", "exit", "source_check", "complete")) <= set(observed)
        status = report(store.record("sync_job", job["id"]))
        assert status["revision"] > 1
        assert "validation_id" in status["validation_progress"]
        assert not cancellation_deferred()
    finally:
        store.db.discard()
