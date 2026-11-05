"""Function publication stays recorded when cancellation reaches caller refresh."""

from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.collaboration.apply import refresh, transactions
from ue_node_nexus_mcp.transcode.collaboration.jobs import service, state
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.progress import emit
from ue_node_nexus_mcp.transcode.sync.service import run_sync
from tests.collaboration.publication.test_function_interface_batch import (
    FUNCTION, SECOND, call, package, project as project, rename_input,
)


@pytest.mark.parametrize("boundary", ["before_refresh", "refresh_committed"])
def test_cancellation_during_caller_refresh_reports_all_committed_transactions(project, monkeypatch, boundary):
    ue, env, workspace = project
    ue.referencers[FUNCTION] = [package(SECOND)]
    rename_input(project)
    call(project, "commit", all=True, message="interface refresh cancellation")
    store = Store(Path(workspace["files_root"]).parents[2])
    options = dict(workspace_id=workspace["id"], dry_run=False)
    job = service.create(store, "push", [FUNCTION, SECOND], options)
    original_consumer, original_readback = refresh.consumer, transactions.actual_snapshot

    def consumer(*args):
        if boundary == "before_refresh":
            state.update(store, job["id"], cancel_requested=True)
        return original_consumer(*args)

    def readback(owner, record):
        if boundary == "refresh_committed" and not record.get("consume", True):
            state.update(store, job["id"], cancel_requested=True)
            emit("schema_readback", dict(asset=record["asset"]))
        return original_readback(owner, record)

    monkeypatch.setattr(refresh, "consumer", consumer)
    monkeypatch.setattr(transactions, "actual_snapshot", readback)
    try:
        answer = service.execute(store, job["id"], lambda action, paths, values: run_sync(ue, action, paths, values, env=env))
        result = answer["data"]
        committed = [FUNCTION, SECOND] if boundary == "refresh_committed" else [FUNCTION]
        assert result["status"] == "partial" and result["interruption"]["code"] == "sync_job_cancelled"
        assert result["applied"] == len(committed)
        assert result["completed_assets"] == committed
        assert result["unapplied_assets"] == ([] if boundary == "refresh_committed" else [SECOND])
        assert result["pending_refresh_assets"] == ([] if boundary == "refresh_committed" else [SECOND])
        assert {row["asset"] for row in result["rows"] if row["action"] == "pushed"} == set(committed)
        assert result["pending_transactions"] == []
    finally:
        store.db.discard()
