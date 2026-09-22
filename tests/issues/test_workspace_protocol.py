"""Issues 4, 5, 21 and 22: the collaboration protocol answers what it was asked.

A project named by the path the caller already used, a lint that works on a plain
directory, a preview that does not read as a completed action, and one decision
that settles the hundred conflicts it applies to.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.paths import project_label
from ue_node_nexus_mcp.transcode.sync import run_sync
from ue_node_nexus_mcp.transcode.sync_project import SyncError, resolve_context

from tests.collaboration.test_recovery import MATERIAL, call, edit, project  # noqa: F401


@pytest.mark.parametrize("spelling", [
    "Demo",
    "Demo.uproject",
    "D:/Projects/Demo/Demo.uproject",
    r"D:\Projects\Demo\Demo.uproject",
    "/home/user/Demo/Demo.uproject",
    '"D:\\Projects\\Demo\\Demo.uproject"',
])
def test_a_project_is_named_by_its_uproject_stem_however_it_is_spelled(spelling):
    assert project_label(spelling) == "Demo"


def test_an_empty_hint_stays_empty_rather_than_becoming_a_project(spelling=""):
    assert project_label(spelling) == ""
    assert project_label(None) == ""


def test_a_uproject_path_hint_binds_the_project_it_names(project):  # noqa: F811
    ue, env, workspace, store = project
    # The editor reports this project as a bare name; the caller has its path.
    bound = resolve_context(ue, env=env, project_hint=r"D:\UE\Shadetest\Shadetest.uproject")
    assert bound.project_name == "Shadetest"
    assert resolve_context(ue, env=env, project_hint="Shadetest").project == bound.project

    with pytest.raises(SyncError) as failure:
        resolve_context(ue, env=env, project_hint=r"C:\Games\Other\Other.uproject")
    assert failure.value.code == "project_mismatch"
    # The error shows what the path was read as, not just the path.
    assert failure.value.details["resolved"] == "Other"
    assert failure.value.details["actual"] == "Shadetest"


def test_lint_accepts_a_plain_directory_of_mirror_files(project, tmp_path):  # noqa: F811
    ue, env, workspace, store = project
    offline = tmp_path / "handoff"
    (offline / "Materials").mkdir(parents=True)
    source = Path(workspace["file_paths"][MATERIAL])
    target = offline / "Materials" / source.name
    target.write_text(source.read_text(encoding="utf-8"), encoding="utf-8")
    (offline / "notes.txt").write_text("not a mirror file", encoding="utf-8")

    result = run_sync(ue, "lint", options=dict(files_root=str(offline)), env=env)
    assert result["action"] == "lint" and result["files_root"] == str(offline)
    assert [row["file"] for row in result["rows"]] == ["Materials/" + source.name]
    assert result["rows"][0]["errors"] == 0
    assert result["skipped"] == []

    broken = target.read_text(encoding="utf-8").replace("[graph]", "[graph")
    target.write_text(broken, encoding="utf-8")
    failed = run_sync(ue, "lint", options=dict(files_root=str(offline)), env=env)
    assert failed["error_count"] > 0 and failed["rows"][0]["errors"] > 0


def test_lint_refuses_a_files_root_that_is_not_a_directory(project, tmp_path):  # noqa: F811
    ue, env, workspace, store = project
    with pytest.raises(SyncError) as failure:
        run_sync(ue, "lint", options=dict(files_root=str(tmp_path / "nowhere")), env=env)
    assert failure.value.code == "invalid_option"


def test_a_simulated_write_does_not_report_itself_as_the_completed_action(project):  # noqa: F811
    ue, env, workspace, store = project
    edit(project, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    head = store.ref("refs/heads/agents/" + workspace["id"])

    preview = call(project, "commit", all=True, message="A", dry_run=True)
    result = preview["result"]
    assert preview["dry_run"] is True
    assert result["action"] == "preview" and result["preview_of"] == "committed"
    assert result["status"] == "preview" and result["dry_run"] is True
    # Nothing moved: the completed-form verb described a throwaway copy.
    assert store.ref("refs/heads/agents/" + workspace["id"]) == head


def test_a_clean_push_preview_still_reads_as_a_preview(project):  # noqa: F811
    ue, env, workspace, store = project
    edit(project, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    call(project, "commit", all=True, message="A")

    preview = call(project, "push", [MATERIAL], dry_run=True)
    assert preview["status"] == "preview" and preview["dry_run"] is True and preview["applied"] == 0
    assert preview["execute"]["options"]["dry_run"] is False
    assert preview["execute"]["paths"] == [MATERIAL]
    assert store.ref("refs/ue/published") is None
