"""Queued publication has a fixed source, observable progress, and recoverable results."""

from pathlib import Path
import threading

import pytest

from ue_node_nexus_mcp import task_queue
from ue_node_nexus_mcp.transcode.collaboration.jobs import service
from ue_node_nexus_mcp.transcode.collaboration.jobs.state import update
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.sync.background import run
from ue_node_nexus_mcp.transcode.sync.service import run_sync
from tests.collaboration.fake_bridge import ProtocolUe
from tests.transcode.fixtures import material_raw

MATERIAL = "/Game/Materials/M_Glass.M_Glass"
SECOND = "/Game/Materials/M_Second.M_Second"


@pytest.fixture
def project(tmp_path):
    task_queue.reset_for_tests()
    ue = ProtocolUe()
    second = material_raw()
    second["asset_path"] = SECOND
    ue.assets[SECOND] = second
    root = tmp_path / "decoded"
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(root))
    workspace = run_sync(ue, "checkout", options=dict(dry_run=False), env=env)
    options = dict(project="Shadetest", workspace_id=workspace["id"], dry_run=False)
    runner = lambda action, paths, values: run_sync(ue, action, paths, values, env=env)
    store = Store(Path(workspace["files_root"]).parents[2])
    yield ue, root, options, runner, store, workspace
    for task in task_queue.task_status()["data"]["tasks"]:
        task_queue.wait_for_task(task["task_id"], 5)
    store.db.discard()
    task_queue.reset_for_tests()


def commit(project, assets=(MATERIAL,)):
    ue, root, options, runner, store, workspace = project
    for asset in assets:
        path = Path(workspace["file_paths"][asset])
        text = path.read_text(encoding="utf-8")
        path.write_text(text.replace("BLEND_Translucent", "BLEND_Opaque"), encoding="utf-8")
    return runner("commit", None, dict(options, all=True, message="job regression"))["commit_id"]


def submit(project, **extra):
    ue, root, options, runner, store, workspace = project
    return run("push", None, dict(options, background=True, **extra), runner, root)


def finish(project, job):
    store = project[4]
    record = store.record("sync_job", job["job_id"])
    assert task_queue.wait_for_task(record["task_id"], 5)
    return service.observe(store, "job_result", dict(job_id=job["job_id"]))


def test_progress_and_result_survive_another_store_handle(project):
    ue, root, options, runner, store, workspace = project
    commit(project)
    entered, release = threading.Event(), threading.Event()

    def pause():
        entered.set()
        assert release.wait(5)

    ue.before_apply = pause
    job = submit(project)
    try:
        assert entered.wait(3)
        reopened = Store(store.root)
        status = service.observe(reopened, "job_status", dict(job_id=job["job_id"]))
        assert status["status"] == "running" and status["current_asset"] == MATERIAL
        assert status["durable"] and "result" not in status
        reopened.db.discard()
    finally:
        release.set()
    result = finish(project, job)
    assert result["status"] == "published" and result["job_status"] == "succeeded"
    assert result["applied"] == 1
    reopened = Store(store.root)
    assert service.observe(reopened, "job_result", dict(job_id=job["job_id"])) == result
    reopened.db.discard()


def test_queued_push_freezes_the_submitted_head_and_reuses_its_request_key(project):
    entered, release = threading.Event(), threading.Event()
    hold = task_queue.submit_callable("hold", lambda: (entered.set(), release.wait(5), dict(ok=True))[-1])
    assert entered.wait(3)
    try:
        source = commit(project)
        first = submit(project, idempotency_key="one-source")
        second = submit(project, idempotency_key="one-source")
        assert first["job_id"] == second["job_id"]
        assert first["source_commit"] == source
        commit(project, (SECOND,))
    finally:
        release.set()
        task_queue.wait_for_task(hold, 5)
    result = finish(project, first)
    assert result["source_commit"] == source
    assert [item["asset_path"] for item in project[0].applied] == [MATERIAL]


def test_queued_cancel_stops_before_any_editor_write(project):
    entered, release = threading.Event(), threading.Event()
    hold = task_queue.submit_callable("hold", lambda: (entered.set(), release.wait(5), dict(ok=True))[-1])
    assert entered.wait(3)
    try:
        commit(project)
        job = submit(project)
        status = service.observe(project[4], "job_cancel", dict(job_id=job["job_id"]))
        assert status["status"] == "cancelled"
        assert project[0].applied == []
    finally:
        release.set()
        task_queue.wait_for_task(hold, 5)


@pytest.mark.parametrize("reason", ["cancel", "deadline"])
def test_running_stop_preserves_committed_receipts_and_skips_later_assets(project, reason):
    commit(project, (MATERIAL, SECOND))
    entered, release = threading.Event(), threading.Event()

    def pause():
        entered.set()
        assert release.wait(5)

    project[0].before_apply = pause
    job = submit(project)
    try:
        assert entered.wait(3)
        if reason == "cancel":
            service.observe(project[4], "job_cancel", dict(job_id=job["job_id"]))
        else:
            update(project[4], job["job_id"], deadline_at=0)
    finally:
        release.set()
    result = finish(project, job)
    assert result["job_status"] == "cancelled"
    assert result["error"]["code"] == ("sync_job_cancelled" if reason == "cancel" else "sync_job_deadline")
    assert [item["asset_path"] for item in project[0].applied] == [MATERIAL]
    assert all(row["phase"] == "completed" for row in project[4].records("apply"))


def test_an_exited_owner_is_reported_as_interrupted_without_replaying(project):
    record = service.create(project[4], "push", None, dict(project[2]))
    owner = dict(record["owner"], pid=2147483647)
    update(project[4], record["id"], owner=owner)
    status = service.observe(project[4], "job_status", dict(job_id=record["id"]))
    assert status["status"] == "interrupted" and status["recover"]["action"] == "recover"
    assert project[0].applied == []
