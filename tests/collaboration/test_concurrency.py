"""Lock ownership, fixed publication inputs and single-writer workspace state."""

from __future__ import annotations

import multiprocessing
import os
import time
from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.collaboration.apply import publish as publication
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.collaboration.workspace import Workspace, checkout
from ue_node_nexus_mcp.transcode.sync import run_sync
from ue_node_nexus_mcp.transcode.sync_project import SyncError

from .fake_bridge import ProtocolUe
from .helpers import edit, repository

MATERIAL = "/Game/Materials/M_Glass.M_Glass"


def _hold(path, seconds, ready, output):
    store = Store(Path(path))
    try:
        with store.lock("publication"):
            ready.put(os.getpid())
            time.sleep(seconds)
        output.put("released")
    except SyncError as exc:
        output.put(exc.code)


@pytest.fixture
def project(tmp_path):
    ue = ProtocolUe()
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(tmp_path / "decoded"))
    workspace = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="A"), env=env)
    root = Path(workspace["files_root"]).parents[2]
    return ue, env, workspace, Store(root)


def call(project, action, workspace=None, **options):
    ue, env, _, _ = project
    if workspace:
        options["workspace_id"] = workspace["id"]
    return run_sync(ue, action, options=dict(dry_run=False, **options), env=env)


def change(workspace, old, new):
    path = Path(workspace["file_paths"][MATERIAL])
    text = path.read_text(encoding="utf-8")
    assert old in text, text
    path.write_text(text.replace(old, new), encoding="utf-8")


def spawn_holder(tmp_path, seconds):
    context = multiprocessing.get_context("spawn")
    ready, output = context.Queue(), context.Queue()
    process = context.Process(target=_hold, args=(str(tmp_path), seconds, ready, output))
    process.start()
    return process, ready.get(timeout=30), output


def test_publication_lock_names_the_holding_process(tmp_path):
    store = Store(tmp_path)
    process, holder_pid, output = spawn_holder(tmp_path, 30)
    try:
        with pytest.raises(SyncError) as failure:
            with store.lock("publication"):
                pass
        assert failure.value.code == "sync_busy"
        assert failure.value.details["holder"]["pid"] == holder_pid
        assert failure.value.details["holder"]["action"] == "publication"
    finally:
        process.terminate()
        process.join(timeout=30)


def test_lock_survives_the_holder_being_killed(tmp_path):
    store = Store(tmp_path)
    process, holder_pid, _ = spawn_holder(tmp_path, 30)
    process.terminate()
    process.join(timeout=30)
    inspector = store.lock("publication")
    # The killed owner stays the recorded holder, so a stale lock stays diagnosable.
    assert inspector.describe()["holder"]["pid"] == holder_pid
    with inspector as lock:
        assert lock.describe()["holder"]["pid"] == os.getpid()


def test_bounded_wait_acquires_after_the_holder_releases(tmp_path):
    store = Store(tmp_path)
    process, _, output = spawn_holder(tmp_path, 1.0)
    try:
        with store.lock("publication", timeout=30) as lock:
            assert lock.waited >= 0.5
            assert lock.describe()["holder"]["pid"] == os.getpid()
        assert lock.held >= 0
    finally:
        process.join(timeout=30)
    assert output.get(timeout=10) == "released"


def test_clean_release_clears_the_recorded_holder(tmp_path):
    store = Store(tmp_path)
    lock = store.lock("gc")
    with lock:
        assert lock.describe()["holder"]["action"] == "gc"
    assert lock.describe()["holder"] == dict()


def test_publication_reports_the_blocking_workspace(tmp_path, project, monkeypatch):
    ue, env, workspace, store = project
    monkeypatch.setattr(publication, "PUBLICATION_WAIT_SECONDS", 0.5)
    change(workspace, "BLEND_Translucent", "BLEND_Opaque")
    call(project, "commit", workspace, all=True, message="A")
    process, holder_pid, _ = spawn_holder(store.root, 30)
    try:
        with pytest.raises(SyncError) as failure:
            call(project, "push", workspace)
        assert failure.value.code == "sync_busy"
        assert failure.value.details["holder"]["pid"] == holder_pid
        assert ue.applied == []
    finally:
        process.terminate()
        process.join(timeout=30)
    assert call(project, "push", workspace)["status"] == "published"


def test_a_commit_during_execution_does_not_join_the_published_source(project):
    ue, env, workspace, store = project
    change(workspace, "BLEND_Translucent", "BLEND_Opaque")
    published_source = call(project, "commit", workspace, all=True, message="A")["commit_id"]

    def later_commit():
        change(workspace, "TwoSided = true", "TwoSided = false")
        call(project, "commit", workspace, all=True, message="A2")

    ue.before_apply = later_commit
    result = call(project, "push", workspace)
    assert result["status"] == "published", result
    assert result["source_commit"] == published_source
    assert result["workspace_rebase_required"] is True
    assert next(row["value"] for row in ue.assets[MATERIAL]["props"] if row["name"] == "TwoSided").lower() == "true"
    assert store.record("workspace", workspace["id"])["head"] != published_source


def test_a_moved_source_reference_invalidates_a_push_preview(project):
    ue, env, workspace, store = project
    change(workspace, "BLEND_Translucent", "BLEND_Opaque")
    call(project, "commit", workspace, all=True, message="A")
    preview = run_sync(ue, "push", options=dict(workspace_id=workspace["id"]), env=env)
    assert preview["proposal_id"]
    change(workspace, "TwoSided = true", "TwoSided = false")
    call(project, "commit", workspace, all=True, message="A2")
    with pytest.raises(SyncError) as failure:
        call(project, "push", workspace, proposal_id=preview["proposal_id"])
    assert failure.value.code == "stale_proposal"
    assert ue.applied == []


def test_a_new_editor_epoch_rejects_the_prepared_execution(project):
    ue, env, workspace, store = project
    change(workspace, "BLEND_Translucent", "BLEND_Opaque")
    call(project, "commit", workspace, all=True, message="A")
    ue.before_apply = lambda: setattr(ue, "epoch", "epoch-two")
    result = call(project, "push", workspace)
    assert result["errors"][MATERIAL]["code"] == "stale_target", result
    rejected = result["errors"][MATERIAL]["details"]["apply_id"]
    assert store.record("apply", rejected)["phase"] == "rejected"
    retried = call(project, "push", workspace)
    assert retried["status"] == "published", retried
    assert ue.assets[MATERIAL]["props"][0]["value"] == "BLEND_Opaque"


def test_an_editor_restart_invalidates_a_pending_conflict_session(project, tmp_path):
    ue, env, first, store = project
    second = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="B"), env=env)
    change(first, "Constant(R=0.000001)", "Constant(R=0.04)")
    call(project, "commit", first, all=True, message="A")
    assert call(project, "push", first)["status"] == "published"
    change(second, "Constant(R=0.000001)", "Constant(R=0.07)")
    call(project, "commit", second, all=True, message="B")
    conflict = call(project, "push", second)
    assert conflict["status"] == "conflict", conflict
    for item in conflict["conflicts"]:
        call(project, "resolve", second, merge_id=conflict["merge_id"], conflict_id=item["conflict_id"], choice="ours")
    ue.epoch = "epoch-two"
    with pytest.raises(SyncError) as failure:
        call(project, "continue", second, merge_id=conflict["merge_id"])
    assert failure.value.code == "stale_session"
    assert store.record("session", conflict["merge_id"])["status"] == "stale"


def test_unresolved_conflicts_report_a_session_while_the_rest_publishes(project):
    ue, env, first, store = project
    second = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="B"), env=env)
    change(first, "Constant(R=0.000001)", "Constant(R=0.04)")
    call(project, "commit", first, all=True, message="A")
    assert call(project, "push", first)["status"] == "published"
    change(second, "Constant(R=0.000001)", "Constant(R=0.07)")
    call(project, "commit", second, all=True, message="B")
    result = call(project, "push", second, stop_on_error=False)
    assert result["status"] == "partial", result
    assert result["merge_id"], result
    assert result["errors"][MATERIAL]["code"] == "conflict"
    assert store.record("session", result["merge_id"])["status"] == "conflict"


@pytest.mark.parametrize("first,second,code", [
    ("stage", "stage", "stale_state"),
    ("stage", "commit", "stale_state"),
    ("commit", "stage", "stale_state"),
    ("commit", "commit", "branch_moved"),
])
def test_two_handles_on_one_workspace_never_both_win(tmp_path, first, second, code):
    store, root = repository(tmp_path)
    created = checkout(store, root)
    edit(created, "A", "1")
    left, right = Workspace(store, created.state["id"]), Workspace(store, created.state["id"])
    branch = created.state["branch"]

    def act(workspace, action):
        return workspace.stage() if action == "stage" else workspace.commit("m", all_files=True)

    act(left, first)
    generation = store.record("workspace", created.state["id"])["generation"]
    head = store.ref(branch)
    with pytest.raises(SyncError) as failure:
        act(right, second)
    assert failure.value.code == code
    state = store.record("workspace", created.state["id"])
    assert state["generation"] == generation
    assert store.ref(branch) == head
    assert state["head"] == (head if first == "commit" else root)


def test_a_second_process_cannot_drive_the_same_workspace(tmp_path, project):
    ue, env, workspace, store = project
    with store.lock("workspace-" + workspace["id"]):
        with pytest.raises(SyncError) as failure:
            call(project, "stage", workspace)
    assert failure.value.code == "sync_busy"
    assert failure.value.details["holder"]["action"] == "workspace-" + workspace["id"]
    assert call(project, "stage", workspace)["workspace_id"] == workspace["id"]
