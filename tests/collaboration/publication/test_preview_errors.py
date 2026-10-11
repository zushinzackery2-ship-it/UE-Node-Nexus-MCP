"""A completed preview cannot claim a rejected candidate is executable."""

from pathlib import Path

from ue_node_nexus_mcp.transcode.sync.service import run_sync
from tests.collaboration.support.publication import ASSET, call, project as project


def test_deletion_rejected_by_preview_is_visible_at_top_level(project):  # noqa: F811
    ue, env, workspace, _ = project
    Path(workspace["file_paths"][ASSET]).unlink()
    call(project, "commit", workspace, all=True, delete=True, allow_delete=True, message="remove asset")
    preview = run_sync(ue, "push", options=dict(workspace_id=workspace["id"], dry_run=True), env=env)
    assert preview["status"] == "preview"
    assert preview["error_count"] == 1
    assert preview["executable"] is False
    assert preview["plan_complete"] is False
    assert preview["blocked_assets"][0]["asset"] == ASSET
    assert preview["blocked_assets"][0]["code"] == "delete_not_allowed"
    valid = run_sync(ue, "push", options=dict(workspace_id=workspace["id"], dry_run=True, allow_delete=True), env=env)
    assert valid["executable"] is True
    assert valid["plan_complete"] is True
    assert valid["error_count"] == 0
