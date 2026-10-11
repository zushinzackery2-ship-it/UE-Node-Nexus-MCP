"""Stash consumes captured files and restores authored bytes atomically."""

from ue_node_nexus_mcp.transcode.collaboration.workspace import checkout, stash
from tests.collaboration.helpers import ASSET, commit, edit, repository, values


def test_new_unstaged_file_is_removed_and_restored(tmp_path):
    store, root = repository(tmp_path)
    workspace = checkout(store, root)
    original = workspace.root / workspace.state["files"][ASSET]
    fresh = workspace.root / "Test/New.asset.nexus"
    fresh.write_bytes(b"# authored\r\n" + original.read_bytes().replace(b"/Game/Test/Data", b"/Game/Test/New"))
    authored = fresh.read_bytes()
    saved = stash.push(workspace)
    assert not fresh.exists()
    assert not workspace.status()["dirty"]
    assert stash.apply(workspace, saved["stash_id"], pop=True)["status"] == "completed"
    assert fresh.read_bytes() == authored


def test_staged_and_files_layers_restore_original_bytes(tmp_path):
    store, root = repository(tmp_path)
    workspace = checkout(store, root)
    edit(workspace, "A", "1")
    workspace.stage()
    edit(workspace, "B", "2")
    file = workspace.root / workspace.state["files"][ASSET]
    file.write_bytes(b"# author note\r\n" + file.read_bytes().replace(b"\n", b"\r\n"))
    authored = file.read_bytes()
    index = workspace.state["index"]
    saved = stash.push(workspace)
    assert not workspace.status()["dirty"]
    assert stash.apply(workspace, saved["stash_id"], pop=True)["status"] == "completed"
    assert workspace.state["index"] == index
    assert file.read_bytes() == authored
    assert values(workspace) == dict(A="1", B="2")
    assert store.record("stash", saved["stash_id"]) is None


def test_merge_changes_reuse_original_comment_lines(tmp_path):
    store, root = repository(tmp_path)
    workspace = checkout(store, root)
    edit(workspace, "A", "1")
    file = workspace.root / workspace.state["files"][ASSET]
    file.write_bytes(b"# stash author\r\n" + file.read_bytes().replace(b"\n", b"\r\n"))
    saved = stash.push(workspace)
    commit(workspace, "B", "3")
    assert stash.apply(workspace, saved["stash_id"], pop=True)["status"] == "completed"
    assert values(workspace) == dict(A="1", B="3")
    assert b"# stash author\r\n" in file.read_bytes()


def test_conflict_resolution_preserves_matching_stash_bytes(tmp_path):
    from ue_node_nexus_mcp.transcode.collaboration.merge.sessions import Sessions

    store, root = repository(tmp_path)
    workspace = checkout(store, root)
    edit(workspace, "A", "1")
    file = workspace.root / workspace.state["files"][ASSET]
    file.write_bytes(b"# stash choice\r\n" + file.read_bytes().replace(b"\n", b"\r\n"))
    authored = file.read_bytes()
    saved = stash.push(workspace)
    commit(workspace, "A", "2")
    report = stash.apply(workspace, saved["stash_id"], pop=True)
    assert report["status"] == "conflict"
    sessions = Sessions(workspace)
    sessions.resolve(report["merge_id"], dict(all=True, choice="theirs"))
    assert stash.finish(workspace, report["merge_id"])["status"] == "completed"
    assert file.read_bytes() == authored


