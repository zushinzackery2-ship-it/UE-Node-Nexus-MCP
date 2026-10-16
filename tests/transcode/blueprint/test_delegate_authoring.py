"""Multicast declarations must retain their native member and event signature."""

import pytest

from ue_node_nexus_mcp.transcode.blueprint.pins import node_pins
from ue_node_nexus_mcp.transcode.collaboration.semantic.blueprint import positional_values
from ue_node_nexus_mcp.transcode.diff.service import build_plan
from ue_node_nexus_mcp.transcode.lint.service import lint_document
from ue_node_nexus_mcp.transcode.raw.nodes import node_decl
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock
from ue_node_nexus_mcp.transcode.text.parser import parse


NODES = ("CallDelegate", "AddDelegate", "RemoveDelegate", "ClearDelegate")
OWNER = "/Script/Test.DelegateSource"


@pytest.fixture
def schema(tmp_path):
    nodes = dict((f"/Script/BlueprintGraph.K2Node_{name}", dict(dynamic_pins=True))
                 for name in (*NODES, "CustomEvent", "Self"))
    parameters = [dict(name="Value", dir="in", type=dict(category="int"))]
    delegates = {OWNER + ".Changed": dict(params=parameters, callable=True, assignable=True),
                 OWNER + ".ReadOnly": dict(params=parameters, callable=False, assignable=False)}
    assets = {OWNER: dict(), "/Script/Test.DelegateChild": dict(inheritance=OWNER),
              "/Script/CoreUObject.Object": dict()}
    directory = tmp_path / "schema"
    publish(directory, "key", dict(k2node=nodes, delegates=delegates, asset=assets),
            coverage=dict(delegates="registered_multicast_delegate_index"))
    return SchemaLock(directory, "key")


def document(body):
    doc, sink = parse("nexus: 1\nasset: /Game/BP_Delegates\nclass: Blueprint\nschema: key\n" +
                      "[asset]\nParentClass = /Script/CoreUObject.Object\n" + body)
    assert not sink.has_errors, sink.items
    return doc


@pytest.mark.parametrize("node", NODES)
def test_new_local_dispatcher_is_declared_before_native_node(schema, node):
    doc = document(f"[dispatchers]\nChanged(Value: int)\n[graph EventGraph]\nnode : {node}(self.Changed)\n")
    assert not lint_document(doc, "blueprint", schema).has_errors
    plan = build_plan(doc, None, "blueprint", dict(), schema)
    assert not plan.has_errors
    declaration = next(index for index, item in enumerate(plan.verbs) if item.op == "bp_dispatcher_add")
    creation = next(index for index, item in enumerate(plan.verbs) if item.op == "create_node")
    assert declaration < creation
    assert plan.verbs[creation].args["class"] == "/Script/BlueprintGraph.K2Node_" + node
    assert plan.verbs[creation].args["positional"] == ["self.Changed"]


@pytest.mark.parametrize("node", NODES)
def test_external_native_owner_and_member_round_trip(schema, node):
    raw = dict(class_short=node, **{"class": "/Script/BlueprintGraph.K2Node_" + node},
               config=dict(delegate_owner=OWNER, delegate_name="Changed", self_context="false"))
    decl = node_decl("node", raw, schema)
    assert decl.positional() == ["DelegateSource.Changed"]
    assert positional_values(decl, "k2node", schema=schema) == ["DelegateSource.Changed"]


@pytest.mark.parametrize("node", NODES)
def test_self_reference_keeps_its_scope_when_export_uses_a_skeleton_path(schema, node):
    raw = dict(class_short=node, **{"class": "/Script/BlueprintGraph.K2Node_" + node},
               config=dict(delegate_owner="/Game/BP_Delegates.SKEL_BP_Delegates_C",
                           delegate_name="Changed", self_context="true"))
    decl = node_decl("node", raw, schema)
    assert decl.positional() == ["self.Changed"]
    assert positional_values(decl, "k2node", schema=schema) == ["self.Changed"]


@pytest.mark.parametrize("node", NODES)
def test_private_native_delegate_is_rejected(schema, node):
    doc = document(f"[graph EventGraph]\nnode : {node}(DelegateSource.ReadOnly)\n")
    assert "delegate_not_exposed" in {item.code for item in lint_document(doc, "blueprint", schema).errors()}


def test_inherited_native_delegate_retains_declaring_owner(schema):
    assert schema.delegate("/Script/Test.DelegateChild", "Changed")["path"] == OWNER + ".Changed"
    doc = document("[graph EventGraph]\nnode : CallDelegate(DelegateChild.Changed)\n")
    assert not lint_document(doc, "blueprint", schema).has_errors


@pytest.mark.parametrize("reference", ("Changed", "self.", "self.Missing", "DelegateSource.Missing"))
def test_bad_dispatcher_reference_has_a_source_diagnostic(schema, reference):
    doc = document(f"[graph EventGraph]\nnode : CallDelegate({reference})\n")
    errors = lint_document(doc, "blueprint", schema).errors()
    assert any(item.code in ("invalid_delegate_reference", "unknown_delegate") and item.line for item in errors)


def test_local_call_exposes_the_declared_value_pin(schema):
    doc = document("[dispatchers]\nChanged(Value: int)\n[graph EventGraph]\ncall : CallDelegate(self.Changed)\n")
    graph = doc.section("graph", "EventGraph")
    pins = node_pins(graph.decl_map()["call"], doc, graph, schema)
    assert [(item["name"], item["dir"]) for item in pins] == [
        ("execute", "in"), ("then", "out"), ("self", "in"), ("Value", "in")]


@pytest.mark.parametrize("node", ("AddDelegate", "RemoveDelegate"))
@pytest.mark.parametrize("parameter", ("Value: int", "DifferentName: int", "Value: bool", "Value: int, Extra: int"))
def test_binding_checks_types_and_order_instead_of_parameter_names(schema, node, parameter):
    doc = document("[dispatchers]\nChanged(Value: int)\n[graph EventGraph]\n" +
                   f"callback : CustomEvent(OnValue, {parameter})\nbind : {node}(self.Changed)\n" +
                   "callback.OutputDelegate -> bind.Delegate\n")
    codes = {item.code for item in lint_document(doc, "blueprint", schema).errors()}
    assert ("delegate_signature_mismatch" in codes) == ("bool" in parameter or "Extra" in parameter)


def test_new_project_owner_requires_live_reflection_without_inventing_a_signature(schema):
    doc = document("[graph EventGraph]\nnode : AddDelegate(/Game/New.Owner_C.Changed)\n")
    result = lint_document(doc, "blueprint", schema)
    assert not result.has_errors
    assert any(item.code == "delegate_context_required" for item in result.items)
