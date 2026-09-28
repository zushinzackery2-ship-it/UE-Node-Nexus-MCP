"""Issues3 #6: Blueprint node IDs share the native asset-wide namespace."""

import pytest

from ue_node_nexus_mcp.transcode.collaboration.apply import transactions
from ue_node_nexus_mcp.transcode.collaboration.apply.transactions import actual_snapshot
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture
from ue_node_nexus_mcp.transcode.diff import build_plan
from ue_node_nexus_mcp.transcode.lint import lint_document
from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.sync_project import SyncError
from tests.issues.test_published_semantics import receipt_workspace, self_graph_raw


@pytest.mark.parametrize("other", ["distance", "Distance"])
def test_duplicate_ids_across_function_graphs_are_rejected_before_apply(other):
    document, parsed = parse(
        "nexus: 1\nasset: /Game/BP_Test\nclass: Blueprint\n"
        "[function First()]\ndistance : Self @ 0,0\n"
        f"[function Second()]\n{other} : Self @ 0,0\n")
    assert not parsed.errors()
    errors = lint_document(document, "blueprint", None).errors()
    assert [item.code for item in errors] == ["duplicate_node_id"]
    assert errors[0].line == 7
    assert "First" in errors[0].message


@pytest.mark.parametrize("member", ["Actor", "actor"])
def test_apply_identity_map_excludes_members_and_retains_the_node_guid(member):
    source = (
        "nexus: 1\nasset: /Game/BP_Test\nclass: Blueprint\n"
        f"[variables]\n{member} : Object(/Script/Engine.Actor)\n"
        "[graph EventGraph]\nactor : Self @ 0,0\n")
    base, parsed = parse(source)
    assert not parsed.errors()
    base.section("variables").decl_map()[member].meta["guid"] = "VARIABLE"
    base.section("graph").decl_map()["actor"].meta["guid"] = "NODE"
    local, parsed = parse(source.replace("@ 0,0", "@ 100,0"))
    assert not parsed.errors()
    known = dict(actor="NODE")
    known[member] = "VARIABLE"
    plan = build_plan(local, base, "blueprint", known)
    assert not plan.has_errors
    assert plan.to_payload()["ids"] == dict(actor="NODE")
    assert len(plan.to_payload()["plan"]) == 1


def test_local_variable_names_remain_function_scoped():
    document, parsed = parse(
        "nexus: 1\nasset: /Game/BP_Test\nclass: Blueprint\n"
        "[function First()]\nlocal cache : float\na : VariableGet(cache) @ 0,0\n"
        "[function Second()]\nlocal cache : float\nb : VariableGet(cache) @ 0,0\n")
    assert not parsed.errors()
    assert not lint_document(document, "blueprint", None).errors()


def test_created_node_binding_does_not_overwrite_same_named_variable(tmp_path):
    raw = self_graph_raw()
    raw["blueprint"]["variables"] = [dict(
        name="actor", guid="VARIABLE", type=dict(category="object", subobject="/Script/Engine.Actor"),
        default="None", flags=[])]
    raw["blueprint"]["graphs"][0]["nodes"] = [dict(
        guid="NODE", class_short="Self", supported=True, x=0, y=0,
        config=dict(), props=[], pins=[])]
    candidate = capture(
        "nexus: 1\nasset: /Game/BP_Test\nclass: Blueprint\nschema: key\n"
        "[variables]\nactor : Object(/Script/Engine.Actor)\n"
        "[graph EventGraph]\nactor : Self @ 0,0\n", None, "author", "blueprint")
    workspace, commit, asset = receipt_workspace(tmp_path, candidate)
    record = dict(id="apply", candidate=commit, asset=asset,
                  request=dict(plan=[dict(op="create_node", id="actor", graph="EventGraph")]),
                  receipt=dict(after=raw, response_data=dict(id_map=dict(actor="NODE"))))
    actual = actual_snapshot(workspace, record)
    variable_id = next(iter(candidate["semantic"]["sections"]["variables:"]["entities"]))
    node_id = next(iter(candidate["semantic"]["sections"]["graph:EventGraph"]["entities"]))
    assert actual["bindings"][variable_id]["physical"] == "VARIABLE"
    assert actual["bindings"][node_id]["physical"] == "NODE"


def test_identity_readback_failure_is_recorded_for_guarded_restoration(tmp_path, monkeypatch):
    candidate = capture(
        "nexus: 1\nasset: /Game/BP_Test\nclass: Blueprint\n"
        "[graph EventGraph]\nactor : Self @ 0,0\n", None, "author", "blueprint")
    workspace, commit, asset = receipt_workspace(tmp_path, candidate)
    workspace.state["id"] = "test-workspace"
    record = dict(id="apply", workspace_id="test-workspace", phase="ue_committed",
                  candidate=commit, target=commit, asset=asset)

    def ambiguous(*args):
        raise SyncError("ambiguous_identity", "duplicate entity identity distance")

    monkeypatch.setattr(transactions, "actual_snapshot", ambiguous)
    with pytest.raises(SyncError, match="duplicate entity identity"):
        transactions.publish(workspace, record)
    saved = workspace.store.record("apply", "apply")
    assert saved["phase"] == "result_rejected"
    assert saved["verification_error"]["code"] == "ambiguous_identity"
    assert not saved.get("published")
