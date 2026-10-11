"""Preflight contracts for the reported showcase authoring failures."""

import pytest

from ue_node_nexus_mcp.transcode.blueprint.pins import node_pins
from ue_node_nexus_mcp.transcode.diff.service import build_plan
from ue_node_nexus_mcp.transcode.lint.service import lint_document
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock
from ue_node_nexus_mcp.transcode.text.parser import parse


@pytest.fixture
def schema(tmp_path):
    nodes = dict(("/Script/BlueprintGraph.K2Node_" + name, dict(dynamic_pins=True))
                 for name in ("CustomEvent", "CallFunction", "FunctionEntry", "FunctionResult", "VariableGet"))
    functions = dict()
    functions["/Script/Engine.KismetMathLibrary.Conv_IntToDouble"] = dict(pure=True, params=[
        dict(name="InInt", dir="in", type=dict(category="int")),
        dict(name="ReturnValue", dir="out", type=dict(category="real", subcategory="double"))])
    directory = tmp_path / "schema"
    publish(directory, "key", dict(k2node=nodes, functions=functions))
    return SchemaLock(directory, "key")


def document(body):
    result, sink = parse("nexus: 1\nasset: /Game/BP_Reported\nclass: Blueprint\nschema: key\n" + body)
    assert not sink.has_errors, sink.items
    return result


def test_custom_event_and_same_batch_self_call_share_parameter_contract(schema):
    doc = document("[graph EventGraph]\ncall : CallFunction(self.LoadExhibit)\n"
                   "event : CustomEvent(LoadExhibit, Index: int)\nevent.Index -> call.Index\n")
    assert lint_document(doc, "blueprint", schema).items == []
    section = doc.section("graph", "EventGraph")
    call, event = section.decl_map()["call"], section.decl_map()["event"]
    assert next(item for item in node_pins(call, doc, section, schema) if item["name"] == "Index")["type"]["category"] == "int"
    assert next(item for item in node_pins(event, doc, section, schema) if item["name"] == "Index")["dir"] == "out"


def test_multiple_explicit_function_results_are_rejected_before_publication(schema):
    doc = document("[function Value() -> (Result: int)]\na : FunctionResult\nb : FunctionResult\n")
    assert [item.code for item in lint_document(doc, "blueprint", schema).errors()] == [
        "implicit_function_node", "implicit_function_node"]


@pytest.mark.parametrize("conversion", [False, True])
def test_numeric_input_requires_explicit_conversion(schema, conversion):
    body = "[variables]\nWidth : int = 640\n[function Resize(Size: double)]\n[graph EventGraph]\n"
    body += "width : VariableGet(Width)\nresize : CallFunction(self.Resize)\n"
    body += ("convert : CallFunction(KismetMathLibrary.Conv_IntToDouble)\nwidth.Width -> convert.InInt\n"
             "convert.ReturnValue -> resize.Size\n") if conversion else "width.Width -> resize.Size\n"
    errors = lint_document(document(body), "blueprint", schema).errors()
    assert [item.code for item in errors] == ([] if conversion else ["explicit_conversion_required"])


@pytest.mark.parametrize("source,target", [("double", "float"), ("float", "double")])
def test_real_precision_links_follow_native_k2_compatibility(schema, source, target):
    body = f"[variables]\nValue : {source}\n[function Store(Value: {target})]\n[graph EventGraph]\n"
    body += "value : VariableGet(Value)\nstore : CallFunction(self.Store)\nvalue.Value -> store.Value\n"
    assert lint_document(document(body), "blueprint", schema).errors() == []


def test_new_parent_component_is_bound_before_child_attachment():
    doc = document("[asset]\nParentClass = /Script/Engine.SpectatorPawn\n[components]\n"
                   "View : CameraComponent(parent=CollisionComponent)\nCollisionComponent : @inherited(SphereComponent)\n")
    plan = build_plan(doc, None, "blueprint")
    assert not plan.has_errors
    binding = next(index for index, verb in enumerate(plan.verbs) if verb.op == "bp_component_bind")
    child = next(index for index, verb in enumerate(plan.verbs) if verb.op == "bp_component_add")
    assert binding < child
    assert plan.verbs[binding].args == dict(name="CollisionComponent", **{"class": "SphereComponent"})
