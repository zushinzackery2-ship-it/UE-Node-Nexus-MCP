"""Metadata cannot poison status, commit(all=True), previews or publication."""

from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.sync.service import run_sync
from ue_node_nexus_mcp.transcode.text.emitter import emit
from ue_node_nexus_mcp.transcode.text.parser import parse
from tests.collaboration.support.divergence import MATERIAL, call, change, project as project


@pytest.mark.parametrize("directory", [".auto-index-mcp", ".git", ".nexus/undo", "Materials/.cache", "Backups"])
def test_metadata_is_ignored_through_the_complete_publish_path(project, directory):
    ue, env, workspace, _, _ = project
    original_assets = set(ue.assets)
    root = Path(workspace["files_root"])
    cache = root / directory
    cache.mkdir(parents=True)
    (cache / "M_Bad.mat.nexus").write_text("invalid tool data", encoding="utf-8")
    (cache / "Bad.scene.nexus").write_text("invalid scene backup", encoding="utf-8")
    if directory == "Backups":
        (root / ".nexusignore").write_text("Backups/\n", encoding="utf-8")
    change(workspace, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    status = call(project, "status", workspace)
    assert status["errors"] == []
    assert status["unstaged"] == [MATERIAL]
    assert call(project, "lint", workspace)["error_count"] == 0
    call(project, "commit", workspace, all=True, message="authored edit with metadata present")
    preview = run_sync(ue, "push", options=dict(workspace_id=workspace["id"], dry_run=True), env=env)
    assert preview["errors"] == dict() and preview["conflict_count"] == 0
    pushed = call(project, "push", workspace)
    assert pushed["status"] == "published", pushed
    assert pushed["applied"] == 1
    assert set(ue.assets) == original_assets
    assert (cache / "M_Bad.mat.nexus").read_text(encoding="utf-8") == "invalid tool data"


def test_ignore_rules_keep_tracked_edits_and_history(project):
    _, _, workspace, _, store = project
    original_files = store.record("workspace", workspace["id"])["files"]
    root = Path(workspace["files_root"])
    (root / ".nexusignore").write_text("Materials/\n", encoding="utf-8")
    change(workspace, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    assert call(project, "status", workspace)["unstaged"] == [MATERIAL]
    call(project, "commit", workspace, all=True, message="tracked edit")
    assert call(project, "push", workspace)["status"] == "published"
    assert store.record("workspace", workspace["id"])["files"] == original_files


def test_explicit_selection_cannot_bypass_ignore_for_untracked_files(project):
    _, _, workspace, _, _ = project
    root = Path(workspace["files_root"])
    cache = root / "Backups"
    cache.mkdir()
    (cache / "M_New.mat.nexus").write_text("tool data", encoding="utf-8")
    (root / ".nexusignore").write_text("Backups/\n", encoding="utf-8")
    with pytest.raises(SyncError) as failure:
        call(project, "stage", workspace, paths=["Backups/M_New.mat.nexus"])
    assert failure.value.code == "path_not_found"


def test_ignore_keeps_newly_staged_assets_tracked(project):
    _, _, workspace, _, store = project
    root = Path(workspace["files_root"])
    file = root / "Materials/M_New.mat.nexus"
    text = Path(workspace["file_paths"][MATERIAL]).read_text(encoding="utf-8")
    asset = "/Game/Materials/M_New.M_New"
    document, sink = parse(text)
    assert not sink.has_errors
    document.header.asset = asset
    file.write_text(emit(document), encoding="utf-8")
    call(project, "stage", workspace, paths=[str(file)])
    (root / ".nexusignore").write_text("M_New.mat.nexus\n", encoding="utf-8")
    call(project, "commit", workspace, message="newly staged asset")
    pushed = call(project, "push", workspace)
    assert pushed["status"] == "published" and pushed["applied"] == 1
    assert asset in store.record("workspace", workspace["id"])["files"]


def test_ignore_preserves_explicit_deletions_in_committed_history(project):
    _, _, workspace, _, _ = project
    root = Path(workspace["files_root"])
    file = Path(workspace["file_paths"][MATERIAL])
    (root / ".nexusignore").write_text("Materials/\n", encoding="utf-8")
    file.unlink()
    assert call(project, "status", workspace)["local_deleted"] == [MATERIAL]
    call(project, "commit", workspace, all=True, delete=True, allow_delete=True, message="explicit tracked deletion")
    pushed = call(project, "push", workspace, allow_delete=True)
    assert pushed["status"] == "published"
    assert MATERIAL not in project[0].assets
