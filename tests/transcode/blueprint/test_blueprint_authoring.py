"""Issues2: authoring declarations and graph topology before UE assigns GUIDs."""

from __future__ import annotations

import pytest

from ue_node_nexus_mcp.transcode.collaboration.semantic.encode import encode
from ue_node_nexus_mcp.transcode.diff.service import build_plan
from ue_node_nexus_mcp.transcode.lint.service import lint_document
from ue_node_nexus_mcp.transcode.text.parser import parse
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock
from ue_node_nexus_mcp.transcode.errors import SyncError


@pytest.fixture
def schema(tmp_path):
    classes = ("IfThenElse", "ExecutionSequence", "Knot", "CallFunction", "VariableGet", "VariableSet", "MacroInstance")
    nodes = dict((f"/Script/BlueprintGraph.K2Node_{name}", dict(dynamic_pins=True)) for name in classes)
    nodes["/Script/UnrealEd.EdGraphNode_Comment"] = dict()
    functions = dict()
    functions["/Script/Engine.Actor.GetOwner"] = dict(pure=True, params=[dict(name="ReturnValue", dir="out", type=dict(category="object"))])
    functions["/Script/Engine.KismetSystemLibrary.PrintString"] = dict(pure=False, params=[dict(name="InString", dir="in", type=dict(category="string"))])
    assets = dict()
    assets["/Script/Engine.Pawn"] = dict(inheritance="/Script/Engine.Actor")
    directory = tmp_path / "schema"
    publish(directory, "key", dict(k2node=nodes, functions=functions, asset=assets))
    return SchemaLock(directory, "key")


def document(body):
    doc, sink = parse("nexus: 1\nasset: /Game/BP_Issues\nclass: Blueprint\nschema: key\n\n" + body)
    assert not sink.has_errors, sink.items
    return doc


def test_local_variable_is_visible_only_inside_its_function(schema):
    doc = document("[function Count()]\nlocal total : float = 0\nget : VariableGet(total)\nset : VariableSet(total)\n"
                   "[function Other()]\nwrong : VariableGet(total)\n")
    errors = lint_document(doc, "blueprint", schema).errors()
    assert [(item.code, item.line) for item in errors] == [("unknown_variable", 11)]


def test_self_call_resolves_inherited_native_function_and_retains_unknown_warning(schema):
    doc = document("[asset]\nParentClass = /Script/Engine.Pawn\n[graph EventGraph]\n"
                   "owner : CallFunction(self.GetOwner)\nmissing : CallFunction(self.DoesNotExist)\n")
    items = lint_document(doc, "blueprint", schema).items
    warnings = [item for item in items if item.code == "unknown_self_function"]
    assert len(warnings) == 1
    assert "DoesNotExist" in warnings[0].message
    assert warnings[0].line == 10


@pytest.mark.parametrize("alias,expected", [
    ("Branch", "/Script/BlueprintGraph.K2Node_IfThenElse"),
    ("Sequence", "/Script/BlueprintGraph.K2Node_ExecutionSequence"),
    ("Reroute", "/Script/BlueprintGraph.K2Node_Knot"),
    ("Comment", "/Script/UnrealEd.EdGraphNode_Comment"),
])
def test_aliases_create_the_class_that_schema_resolves(schema, alias, expected):
    plan = build_plan(document(f"[graph EventGraph]\nnode : {alias}\n"), None, "blueprint", dict(), schema)
    assert not plan.has_errors
    create = next(verb for verb in plan.verbs if verb.op == "create_node")
    assert create.args["class"] == expected


def test_every_function_and_local_is_declared_before_graph_nodes(schema):
    doc = document("[graph EventGraph]\ncall : CallFunction(self.Later)\n"
                   "[function First()]\ncall2 : CallFunction(self.Later)\n"
                   "[function Later()]\nlocal total : float = 0\nget : VariableGet(total)\n")
    plan = build_plan(doc, None, "blueprint", dict(), schema)
    first_node = next(index for index, verb in enumerate(plan.verbs) if verb.op == "create_node")
    declarations = [index for index, verb in enumerate(plan.verbs) if verb.op in ("bp_function_add", "bp_local_variable_add")]
    assert len(declarations) == 3
    assert max(declarations) < first_node


@pytest.mark.parametrize("source", ["Branch", "MacroInstance(StandardMacros.IsValid)"])
def test_new_execution_branches_can_rejoin_at_a_new_call(schema, source):
    doc = document(f"[graph EventGraph]\nsplit : {source}\n"
                   "call : CallFunction(KismetSystemLibrary.PrintString)\n"
                   "split.then -> call.execute\nsplit.else -> call.execute\n")
    state, _, _ = encode(doc, "blueprint", None, "author", schema)
    links = state["sections"]["graph:EventGraph"]["links"]
    assert len(links) == 2
    assert all(key.startswith("out:") for key in links)


def test_new_execution_output_cannot_drive_two_inputs(schema):
    doc = document("[graph EventGraph]\nsplit : Branch\na : Branch\nb : Branch\n"
                   "split.then -> a.execute\nsplit.then -> b.execute\n")
    with pytest.raises(SyncError, match="multiple connections"):
        encode(doc, "blueprint", None, "author", schema)


def test_new_data_input_still_rejects_multiple_sources(schema):
    doc = document("[graph EventGraph]\na : VariableGet(A)\nb : VariableGet(B)\nc : Branch\n"
                   "a -> c.Condition\nb -> c.Condition\n")
    with pytest.raises(SyncError, match="multiple connections"):
        encode(doc, "blueprint", None, "author", schema)


def test_interfaces_and_dispatchers_have_reversible_member_plans(schema):
    doc = document("[interfaces]\n/Game/BPI_Test.BPI_Test_C\n[dispatchers]\nChanged(Value: float)\n")
    added = build_plan(doc, None, "blueprint", dict(), schema)
    assert not added.has_errors
    assert [verb.op for verb in added.verbs] == ["bp_interface_add", "bp_dispatcher_add"]
    removed = build_plan(document(""), doc, "blueprint", dict(), schema)
    assert not removed.has_errors
    assert [verb.op for verb in removed.verbs] == ["bp_interface_remove", "bp_dispatcher_remove"]


def test_dispatcher_signature_changes_are_typed_and_invalid_returns_are_linted(schema):
    before = document("[dispatchers]\nChanged(Value: float)\n")
    after = document("[dispatchers]\nChanged(Value: int, Label: string)\n")
    plan = build_plan(after, before, "blueprint", dict(), schema)
    assert not plan.has_errors
    verb, = plan.verbs
    assert verb.op == "bp_dispatcher_signature_set"
    assert verb.args["signature"]["inputs"][0]["type"]["category"] == "int"
    invalid = document("[dispatchers]\nChanged() -> (Result: bool)\n")
    assert "invalid_dispatcher" in [item.code for item in lint_document(invalid, "blueprint", schema).errors()]
