from __future__ import annotations

import pytest

from ue_node_nexus_mcp.transcode.collaboration.history import History
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import from_raw, text_of
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.collaboration.workspace import Workspace, checkout
from ue_node_nexus_mcp.transcode.collaboration.workspace.projection import execute, prepare
from ue_node_nexus_mcp.transcode.sync_project import SyncError
from tests.transcode.fixtures import material_raw


@pytest.fixture
def repository(tmp_path):
    store = Store(tmp_path)
    history = History(store)
    snapshot = from_raw(material_raw())
    asset = snapshot["raw"]["asset_path"]
    identifier = store.objects.put("snapshot", snapshot)
    root = history.create(history.tree(dict([(asset, identifier)])), [], "import")
    store.move("refs/heads/main", root, None)
    return store, root, asset


def test_workspaces_are_isolated_and_local_commits_are_offline(repository):
    store, root, asset = repository
    a, b = checkout(store, root), checkout(store, root)
    file = a.root / a.state["files"][asset]
    before = file.read_text(encoding="utf-8")
    file.write_text(before.replace("BaseColor", "Metallic"), encoding="utf-8")
    a.stage()
    result = a.commit("edit A")
    assert result["action"] == "committed"
    assert b.state["head"] == root
    assert (b.root / b.state["files"][asset]).read_text(encoding="utf-8") == before
    assert Workspace(store, a.state["id"]).state["head"] == result["commit_id"]


def test_commit_records_index_and_preserves_later_file_edits(repository):
    store, root, asset = repository
    workspace = checkout(store, root)
    file = workspace.root / workspace.state["files"][asset]
    original = file.read_text(encoding="utf-8")
    staged = original.replace("BaseColor", "Metallic")
    file.write_text(staged, encoding="utf-8")
    workspace.stage()
    index = workspace.state["index"]
    file.write_text(staged.replace("Metallic", "Specular"), encoding="utf-8")
    workspace.commit("only staged")
    assert workspace.history.commit(workspace.state["head"])["tree"] == index
    assert workspace.status()["staged"] == []
    assert workspace.status()["unstaged"] == [asset]


def test_missing_file_requires_explicit_deletion(repository):
    store, root, asset = repository
    workspace = checkout(store, root)
    (workspace.root / workspace.state["files"][asset]).unlink()
    with pytest.raises(SyncError) as failure:
        workspace.stage()
    assert failure.value.code == "local-deleted"
    assert workspace.status()["local_deleted"] == [asset]
    workspace.stage(delete=True)
    assert asset not in workspace.history.entries(workspace.state["index"])


def test_projection_recovery_refuses_to_erase_new_bytes(repository):
    store, root, asset = repository
    workspace = checkout(store, root)
    relative = workspace.state["files"][asset]
    file = workspace.root / relative
    journal = prepare(workspace, dict(workspace.state), dict([(relative, b"replacement")]), "test")
    file.write_bytes(b"new draft after preparation")
    with pytest.raises(SyncError) as failure:
        execute(workspace, journal)
    assert failure.value.code == "local_restore_conflict"
    assert file.read_bytes() == b"new draft after preparation"
    assert store.record("projection", journal["id"])["phase"] == "local_restore_conflict"


def test_same_branch_is_compare_and_swap(repository):
    store, root, asset = repository
    a, b = checkout(store, root, branch="shared"), checkout(store, root, branch="shared")
    for workspace in (a, b):
        file = workspace.root / workspace.state["files"][asset]
        file.write_text(file.read_text(encoding="utf-8").replace("BaseColor", "Metallic"), encoding="utf-8")
        workspace.stage()
    a.commit("A")
    with pytest.raises(SyncError) as failure:
        b.commit("B")
    assert failure.value.code == "branch_moved"


def test_cross_workspace_file_selection_is_rejected(repository):
    store, root, asset = repository
    a, b = checkout(store, root), checkout(store, root)
    with pytest.raises(SyncError) as failure:
        a.stage([str(b.root / b.state["files"][asset])])
    assert failure.value.code == "invalid_path"
