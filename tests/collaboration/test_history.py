from __future__ import annotations

import pytest

from ue_node_nexus_mcp.transcode.collaboration.history.actions import reset, restore, tag
from ue_node_nexus_mcp.transcode.collaboration.history.integrate import integrate
from ue_node_nexus_mcp.transcode.collaboration.history.rebase import start as rebase
from ue_node_nexus_mcp.transcode.collaboration.merge.sessions import Sessions
from ue_node_nexus_mcp.transcode.collaboration.workspace import Workspace, checkout
from ue_node_nexus_mcp.transcode.collaboration.workspace import stash
from ue_node_nexus_mcp.transcode.sync_project import SyncError
from .helpers import ASSET, commit, edit, repository, values


def test_merge_preserves_both_agents_changes_and_merge_parents(tmp_path):
    store, root = repository(tmp_path)
    a, b = checkout(store, root), checkout(store, root)
    ca, cb = commit(a, "A", "1"), commit(b, "B", "2")
    report = integrate(b, "merge", ca)
    assert report["status"] == "completed"
    assert values(b) == dict(A="1", B="2")
    assert b.history.commit(b.state["head"])["parents"] == [cb, ca]


def test_conflict_resolve_after_restart_and_idempotent_continue(tmp_path):
    store, root = repository(tmp_path)
    a, b = checkout(store, root), checkout(store, root)
    source = commit(a, "A", "1")
    commit(b, "A", "2")
    report = integrate(b, "merge", source)
    assert report["status"] == "conflict"
    assert values(b)["A"] == "2"
    sessions = Sessions(Workspace(store, b.state["id"]))
    item = report["conflicts"][0]
    ready = sessions.resolve(report["merge_id"], item["conflict_id"], dict(choice="theirs"))
    assert ready["status"] == "ready"
    result = sessions.finish(report["merge_id"])
    assert sessions.finish(report["merge_id"])["commit_id"] == result["commit_id"]
    assert values(sessions.workspace)["A"] == "1"


def test_revert_undoes_only_selected_commit_and_reports_overlap(tmp_path):
    store, root = repository(tmp_path)
    workspace = checkout(store, root)
    first = commit(workspace, "A", "1")
    commit(workspace, "B", "2")
    integrate(workspace, "revert", first)
    assert values(workspace) == dict(A="0", B="2")
    later = commit(workspace, "A", "3")
    commit(workspace, "A", "4")
    assert integrate(workspace, "revert", later)["status"] == "conflict"


def test_restore_tag_and_mainline_requirement(tmp_path):
    store, root = repository(tmp_path)
    a, b = checkout(store, root), checkout(store, root)
    tag(a, "before")
    ca = commit(a, "A", "1")
    commit(b, "B", "2")
    merge = integrate(b, "merge", ca)["commit_id"]
    with pytest.raises(SyncError) as failure:
        integrate(b, "cherry-pick", merge)
    assert failure.value.code == "mainline_required"
    restore(b, "before", [ASSET])
    assert values(b) == dict(A="0", B="0")
    assert b.state["head"] == merge


@pytest.mark.parametrize("mode,staged,unstaged", [("soft", True, False), ("mixed", False, True), ("hard", False, False)])
def test_reset_modes_and_safety_ref(tmp_path, mode, staged, unstaged):
    store, root = repository(tmp_path)
    workspace = checkout(store, root)
    previous = commit(workspace, "A", "1")
    result = reset(workspace, root, mode)
    status = workspace.status()
    assert bool(status["staged"]) == staged
    assert bool(status["unstaged"]) == unstaged
    assert store.ref(result["safety_ref"]) == previous


def test_stash_preserves_index_and_unstaged_layers(tmp_path):
    store, root = repository(tmp_path)
    workspace = checkout(store, root)
    edit(workspace, "A", "1")
    workspace.stage()
    edit(workspace, "B", "2")
    saved = stash.push(workspace, "two layers")
    assert not workspace.status()["dirty"]
    report = stash.apply(workspace, saved["stash_id"], pop=True)
    assert report["status"] == "completed"
    assert values(workspace) == dict(A="1", B="2")
    status = workspace.status()
    assert status["staged"] == [ASSET] and status["unstaged"] == [ASSET]
    assert store.record("stash", saved["stash_id"]) is None


def test_pull_replays_staged_and_unstaged_changes(tmp_path):
    store, root = repository(tmp_path)
    a, b = checkout(store, root), checkout(store, root)
    source = commit(a, "A", "1")
    edit(b, "B", "2")
    b.stage()
    edit(b, "B", "3")
    result = integrate(b, "pull", source)
    assert result["status"] == "completed"
    assert values(b) == dict(A="1", B="3")
    b.commit("staged only")
    snapshot_id = b.history.entries(b.state["head"])[ASSET]
    snapshot = store.objects.data(snapshot_id, "snapshot")
    assert snapshot["semantic"]["sections"]["asset:"]["props"]["B"]["value"] == "2"


def test_rebase_pick_squash_drop_and_published_guard(tmp_path):
    store, root = repository(tmp_path)
    base, feature = checkout(store, root), checkout(store, root)
    onto = commit(base, "A", "1")
    one = commit(feature, "B", "2")
    two = commit(feature, "B", "3")
    report = rebase(feature, onto, [dict(commit=one, action="pick"), dict(commit=two, action="squash")])
    assert report["status"] == "completed"
    assert values(feature) == dict(A="1", B="3")
    assert feature.history.commit(feature.state["head"])["parents"] == [onto]
    store.move("refs/ue/published", feature.state["head"], None)
    with pytest.raises(SyncError) as failure:
        reset(feature, root, "hard")
    assert failure.value.code == "published_history"
