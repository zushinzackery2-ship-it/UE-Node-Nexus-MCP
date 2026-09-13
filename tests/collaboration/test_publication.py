from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.sync import run_sync
from ue_node_nexus_mcp.transcode.sync_project import SyncError

from .fake_bridge import ProtocolUe

ASSET = "/Game/Materials/M_Glass.M_Glass"


@pytest.fixture
def project(tmp_path):
    ue = ProtocolUe()
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(tmp_path / "decoded"))
    first = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="A"), env=env)
    second = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="B"), env=env)
    return ue, env, first, second


def call(project, action, workspace, **options):
    ue, env, _, _ = project
    return run_sync(ue, action, options=dict(workspace_id=workspace["id"], dry_run=False, **options), env=env)


def edit(workspace, old, new):
    file = Path(workspace["file_paths"][ASSET])
    text = file.read_text(encoding="utf-8")
    assert old in text
    file.write_text(text.replace(old, new), encoding="utf-8")


def value(ue, name):
    return next(row["value"] for row in ue.assets[ASSET]["props"] if row["name"] == name)


def test_two_stale_workspaces_merge_distinct_fields(project):
    ue, env, a, b = project
    edit(a, "BLEND_Translucent", "BLEND_Opaque")
    call(project, "commit", a, all=True, message="opaque")
    first = call(project, "push", a)
    assert first["status"] == "published", first
    edit(b, "TwoSided = true", "TwoSided = false")
    call(project, "commit", b, all=True, message="single sided")
    second = call(project, "push", b)
    assert second["status"] == "published", second
    assert value(ue, "BlendMode") == "BLEND_Opaque"
    assert value(ue, "TwoSided").lower() == "false"
    assert len(ue.applied) == 2


def test_conflict_resolve_restart_continue(project):
    ue, env, a, b = project
    edit(a, "Constant(R=0.000001)", "Constant(R=0.04)")
    edit(b, "Constant(R=0.000001)", "Constant(R=0.07)")
    call(project, "commit", a, all=True, message="A")
    call(project, "commit", b, all=True, message="B")
    call(project, "push", a)
    conflict = call(project, "push", b)
    assert conflict["status"] == "conflict", conflict
    assert len(ue.applied) == 1
    for item in conflict["conflicts"]:
        call(project, "resolve", b, merge_id=conflict["merge_id"], conflict_id=item["conflict_id"], choice="ours")
    result = call(project, "continue", b, merge_id=conflict["merge_id"])
    assert result["status"] == "published", result
    assert len(ue.applied) == 2


def test_response_loss_uses_saved_receipt_once(project):
    ue, _, a, _ = project
    edit(a, "BLEND_Translucent", "BLEND_Opaque")
    call(project, "commit", a, all=True, message="A")
    ue.lose_response = True
    result = call(project, "push", a)
    assert result["status"] == "published", result
    again = call(project, "push", a)
    assert again["applied"] == 0, again
    assert len(ue.applied) == 1


def test_dirty_files_during_save_are_preserved(project):
    ue, _, a, _ = project
    edit(a, "BLEND_Translucent", "BLEND_Opaque")
    call(project, "commit", a, all=True, message="A")
    ue.after_apply = lambda: edit(a, "TwoSided = true", "TwoSided = false")
    result = call(project, "push", a)
    assert result["workspace_rebase_required"] is True
    assert "TwoSided = false" in Path(a["file_paths"][ASSET]).read_text(encoding="utf-8")
    assert value(ue, "TwoSided").lower() == "true"
    pulled = call(project, "pull", a)
    assert pulled["status"] == "completed", pulled
    status = call(project, "status", a)
    assert status["unstaged"] == [ASSET]


def test_ue_memory_cas_rejects_edit_after_preflight(project):
    ue, _, a, _ = project
    edit(a, "BLEND_Translucent", "BLEND_Opaque")
    call(project, "commit", a, all=True, message="A")
    ue.before_apply = lambda: ue.assets[ASSET]["props"][0].update(value="different")
    result = call(project, "push", a)
    assert result["errors"][ASSET]["code"] == "stale_target", result
    assert ue.applied == []


def test_save_failure_restores_and_retry_creates_new_execution(project):
    ue, _, a, _ = project
    edit(a, "BLEND_Translucent", "BLEND_Opaque")
    call(project, "commit", a, all=True, message="A")
    ue.fail_save = True
    failed = call(project, "push", a)
    assert failed["errors"][ASSET]["code"] == "apply_rolled_back"
    assert value(ue, "BlendMode") == "BLEND_Translucent"
    ue.fail_save = False
    succeeded = call(project, "push", a)
    assert succeeded["status"] == "published", succeeded
    assert len(ue.receipts) == 2


def test_preview_rejects_changed_file_without_staging(project):
    ue, env, a, _ = project
    preview = run_sync(ue, "stage", options=dict(workspace_id=a["id"]), env=env)
    assert preview["proposal_id"]
    edit(a, "BLEND_Translucent", "BLEND_Opaque")
    with pytest.raises(SyncError) as error:
        call(project, "stage", a, proposal_id=preview["proposal_id"])
    assert error.value.code == "stale_proposal"
    assert call(project, "status", a)["staged"] == []
