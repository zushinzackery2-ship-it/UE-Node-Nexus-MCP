"""Creation-only Blueprint fields are rejected before worker admission."""

import pytest

from ue_node_nexus_mcp.transcode.diff.service import build_plan
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock
from ue_node_nexus_mcp.transcode.text.parser import parse


def document(parent="/Script/Engine.Actor", blueprint_type="normal", components=""):
    parsed, sink = parse("nexus: 1\nasset: /Game/BP_Test\nclass: Blueprint\nschema: key\n"
                         "[asset]\nParentClass = " + parent + "\nBlueprintType = " + blueprint_type + "\n" + components)
    assert not sink.has_errors
    return parsed


@pytest.mark.parametrize("field,value,line", [("ParentClass", "/Script/Engine.HUD", 6),
                                               ("BlueprintType", "function_library", 7)])
def test_changed_creation_property_is_an_early_plan_error(field, value, line):
    desired = document(parent=value) if field == "ParentClass" else document(blueprint_type=value)
    plan = build_plan(desired, document(), "blueprint")
    assert plan.has_errors
    assert any(item.code == "unsupported_edit" and field in item.message and item.line == line for item in plan.diagnostics)
    assert not any(verb.op == "set_asset_prop" and verb.args["name"] == field for verb in plan.verbs)


def test_creation_properties_are_writable_on_a_new_blueprint():
    plan = build_plan(document(parent="/Script/Engine.HUD"), None, "blueprint")
    assert not plan.has_errors and plan.creates_asset
    assert any(verb.args.get("name") == "ParentClass" for verb in plan.verbs)


def test_same_parent_alias_does_not_schedule_a_reparent(tmp_path):
    directory = tmp_path / "schema"
    publish(directory, "key", dict(asset={"/Script/Engine.Actor": dict(path="/Script/Engine.Actor", name="Actor")}))
    schema = SchemaLock(directory, "key")
    plan = build_plan(document(parent="Actor"), document(), "blueprint", schema=schema)
    assert not plan.has_errors
    assert not any(verb.args.get("name") == "ParentClass" for verb in plan.verbs)


@pytest.mark.parametrize("declaration", ["DefaultSceneRoot : StaticMeshComponent", "DefaultSceneRoot : SceneComponent(parent=Root)"])
def test_invalid_default_root_declaration_is_rejected_before_isolation(declaration):
    plan = build_plan(document(components="[components]\n" + declaration + "\n"), None, "blueprint")
    assert any(item.code == "unsupported_edit" and "DefaultSceneRoot" in item.message for item in plan.diagnostics)


def test_explicit_scene_root_is_planned_without_rewriting_its_name():
    plan = build_plan(document(parent="/Script/Engine.HUD", components="[components]\nDefaultSceneRoot : SceneComponent\n"), None, "blueprint")
    assert not plan.has_errors
    assert [verb.args["name"] for verb in plan.verbs if verb.op == "bp_component_add"] == ["DefaultSceneRoot"]
