"""Regression tests for behaviours pinned down during live acceptance against UE 5.5."""

from __future__ import annotations

from ue_node_nexus_mcp.transcode.bp_types import parse_type_text, type_text
from ue_node_nexus_mcp.transcode.codec import document_from_raw
from ue_node_nexus_mcp.transcode.diff import build_plan
from ue_node_nexus_mcp.transcode.emitter import emit
from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.raw_blueprint import variable_decl
from ue_node_nexus_mcp.transcode.raw_material import output_pin_name
from ue_node_nexus_mcp.transcode.values import needs_quotes

from .fixtures import blueprint_raw, niagara_raw


def test_vector_outputs_use_channel_aliases_and_implicit_default() -> None:
    node = {"outputs": ["", "", "", ""]}  # Constant3Vector: RGB, R, G, B
    assert output_pin_name(node, 0) is None
    assert [output_pin_name(node, index) for index in (1, 2, 3)] == ["R", "G", "B"]
    texture = {"outputs": ["", "", "", "", ""]}
    assert output_pin_name(texture, 0) is None
    assert output_pin_name(texture, 4) == "A"
    named = {"outputs": ["Result", "Mask"]}
    assert output_pin_name(named, 1) == "Mask"


def test_type_keywords_are_case_insensitive() -> None:
    parsed = parse_type_text("Array<Name>")
    assert (parsed["container"], parsed["category"]) == ("array", "name")
    assert parse_type_text("Float")["subcategory"] == "float"
    assert parse_type_text("Integer")["category"] == "int"
    assert type_text(parse_type_text("Array<Name>")) == "Array<name>"


def test_negative_numbers_are_not_quoted() -> None:
    assert needs_quotes("-0.529999") is False
    assert needs_quotes("-1") is False


def test_variable_default_read_from_cdo_drops_zero_values() -> None:
    zero = {"name": "Names", "type": {"category": "array", "subcategory": "name"}, "default": "()"}
    assert variable_decl(zero).default is None
    zero_float = {"name": "Speed", "type": {"category": "real", "subcategory": "float"}, "default": "0.000000"}
    assert variable_decl(zero_float).default is None
    value = {"name": "Health", "type": {"category": "real", "subcategory": "float"}, "default": "100.000000"}
    assert variable_decl(value).default == "100.000000"
    default_category = {"name": "X", "type": {"category": "int"}, "default": "", "category": ""}
    assert variable_decl(default_category).props == []


def test_component_prop_change_names_component_and_prop() -> None:
    document, ids, _ = document_from_raw(blueprint_raw())
    base_text = emit(document)
    local_text = base_text.replace("RelativeLocation=(X=0,Y=0,Z=50)", "RelativeLocation=(X=0,Y=0,Z=80), RelativeScale3D=(X=2,Y=2,Z=2)")
    local, sink = parse(local_text)
    assert not sink.has_errors
    plan = build_plan(local, parse(base_text)[0], "blueprint", {identifier: guid for guid, identifier in ids.items()})
    verbs = [verb.args for verb in plan.verbs if verb.op == "bp_component_set_prop"]
    assert {(verb["name"], verb["prop"]) for verb in verbs} == {("Mesh", "RelativeLocation"), ("Mesh", "RelativeScale3D")}
    assert not plan.has_errors


def test_set_variables_modules_cannot_be_created_from_text() -> None:
    raw = niagara_raw()
    document, ids, _ = document_from_raw(raw)
    base_text = emit(document)
    local_text = base_text.replace("[stack Sparks/ParticleUpdate]\n", "[stack Sparks/ParticleUpdate]\nassign : SetVariables(Particles.Scale=2)\n")
    local, sink = parse(local_text)
    assert not sink.has_errors
    plan = build_plan(local, parse(base_text)[0], "niagara_system", {identifier: guid for guid, identifier in ids.items()})
    assert plan.has_errors
    assert any(item.code == "unsupported_edit" for item in plan.diagnostics)
    assert not [verb for verb in plan.verbs if verb.op == "ns_module_add"]


def test_ghost_events_are_hidden_from_text() -> None:
    raw = blueprint_raw()
    graph = raw["blueprint"]["graphs"][0]
    graph["nodes"].append(
        {
            "guid": "G-GHOST",
            "class": "/Script/BlueprintGraph.K2Node_Event",
            "class_short": "Event",
            "x": 0,
            "y": 400,
            "enabled": False,
            "comment": "This node is disabled",
            "config": {"function_owner": "/Script/Engine.Actor", "function_name": "ReceiveTick"},
            "props": [],
            "pins": [{"guid": "P-GHOST-THEN", "name": "then", "dir": "out", "type": {"category": "exec"}, "linked": []}],
            "supported": True,
        }
    )
    document, ids, _ = document_from_raw(raw)
    text = emit(document)
    assert "ReceiveTick" not in text
    assert "G-GHOST" not in ids
