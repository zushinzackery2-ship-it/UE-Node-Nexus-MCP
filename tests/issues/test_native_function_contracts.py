"""Blueprint call validation and native construction graph planning."""

from ue_node_nexus_mcp.transcode.diff import build_plan
from ue_node_nexus_mcp.transcode.lint import lint_document
from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock


def test_native_non_reflected_call_is_rejected(tmp_path):
    directory = tmp_path / "schema"
    publish(directory, "key", dict(k2node=dict(K2Node_CallFunction=dict(dynamic_pins=True)),
        callable_functions=dict([("/Script/Engine.KismetStringLibrary.Conv_DoubleToString", dict(params=[]))])))
    schema = SchemaLock(directory, "key")
    source = "nexus: 1\nasset: /Game/BP_Test\nclass: Blueprint\nschema: key\n[graph EventGraph]\nn : CallFunction(KismetStringLibrary.Conv_FloatToString)\n"
    document, _ = parse(source)
    assert any(item.code == "unknown_function" for item in lint_document(document, "blueprint", schema).errors())
    document, _ = parse(source.replace("Conv_Float", "Conv_Double"))
    assert not lint_document(document, "blueprint", schema).has_errors


def test_new_actor_blueprint_reuses_construction_graph():
    document, _ = parse("nexus: 1\nasset: /Game/BP_Test\nclass: Blueprint\nschema: key\n"
                        "[asset]\nParentClass = /Script/Engine.Actor\n[function UserConstructionScript()]\n")
    plan = build_plan(document, None, "blueprint", dict())
    assert not plan.has_errors
    creation = [verb for verb in plan.verbs if verb.op == "bp_function_add"]
    assert len(creation) == 1
    assert creation[0].args["reuse_builtin"] is True
