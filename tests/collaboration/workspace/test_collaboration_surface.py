"""Issues2: truthful publication previews, history paths and schema context."""


from ue_node_nexus_mcp.transcode.collaboration.history import History
from ue_node_nexus_mcp.transcode.collaboration.workspace.service import checkout
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock
from ue_node_nexus_mcp.transcode.sync.schema import query
from ue_node_nexus_mcp.transcode.sync.service import run_sync
from tests.collaboration.helpers import repository, edit as edit_field
from tests.collaboration.support.publication import ASSET, call, edit, project as project


def test_history_show_and_diff_resolve_workspace_file_paths(project):
    ue, env, a, _ = project
    original = call(project, "status", a)["head"]
    edit(a, "BLEND_Translucent", "BLEND_Opaque")
    committed = call(project, "commit", a, all=True, message="opaque")["commit_id"]
    path = a["file_paths"][ASSET]
    shown = run_sync(ue, "show", paths=[path], options=dict(workspace_id=a["id"], revision=original), env=env)
    assert len(shown["rows"]) == 1
    assert "BLEND_Translucent" in shown["rows"][0]["text"]
    diff = run_sync(ue, "diff", paths=[path], options=dict(workspace_id=a["id"], left=original, right=committed), env=env)
    assert [row["asset"] for row in diff["rows"]] == [ASSET]


def test_push_describes_ue_only_drift_and_force_local_applies_the_source(project):
    ue, env, a, b = project
    edit(a, "BLEND_Translucent", "BLEND_Opaque")
    call(project, "commit", a, all=True, message="opaque")
    call(project, "push", a)
    preview = run_sync(ue, "push", options=dict(workspace_id=b["id"]), env=env)
    assert any(row.get("origin") == "ue" for row in preview["changes"])
    assert any(item["code"] == "ue_drift_adopted" for item in preview["warnings"])
    forced = call(project, "push", b, force="local")
    assert forced["status"] == "published", forced
    assert next(item["value"] for item in ue.assets[ASSET]["props"] if item["name"] == "BlendMode") == "BLEND_Translucent"


def test_push_preview_can_be_executed_through_continue(project):
    ue, env, a, b = project
    edit(a, "Constant(R=0.000001)", "Constant(R=0.04)")
    edit(b, "Constant(R=0.000001)", "Constant(R=0.07)")
    call(project, "commit", a, all=True, message="a")
    call(project, "commit", b, all=True, message="b")
    call(project, "push", a)
    conflict = call(project, "push", b)
    call(project, "resolve", b, merge_id=conflict["merge_id"], all=True, choice="ours")
    preview = run_sync(ue, "push", options=dict(workspace_id=b["id"], merge_id=conflict["merge_id"]), env=env)
    result = call(project, "continue", b, merge_id=conflict["merge_id"], proposal_id=preview["proposal_id"])
    assert result["status"] == "published", result


def test_offline_status_reports_that_it_did_not_contact_the_bridge(project):
    ue, env, a, _ = project
    result = call(project, "status", a)
    assert result["bridge_contacted"] is False
    assert "bridge_available" not in result


def test_workspace_schema_and_commit_schema_have_distinct_meanings(tmp_path):
    store, revision = repository(tmp_path / "repo")
    directory = tmp_path / "schema"
    publish(directory, "current", dict())
    workspace = checkout(store, revision)
    workspace.schema = SchemaLock(directory, "current")
    status = workspace.status()
    assert "workspace_schema_key" in status
    assert status["schema_key"] == "current"
    edit_field(workspace, "A", "1")
    identifier = workspace.commit("current environment", all_files=True)["commit_id"]
    assert History(store).commit(identifier)["metadata"]["schema_key"] == "current"


def test_schema_category_accepts_its_reflection_family_name(tmp_path):
    directory = tmp_path / "schema"
    publish(directory, "key", dict(material_expression=dict(Constant=dict(path="/Script/Engine.MaterialExpressionConstant"))))
    result = query(SchemaLock(directory, "key"), "material_expression")
    assert result["total"] == 1
    assert result["rows"][0]["family"] == "material_expression"
