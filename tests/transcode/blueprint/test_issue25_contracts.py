"""Issue25 Blueprint contracts: InputKey, Branch aliases and derived camera props."""

from __future__ import annotations

import pytest

from ue_node_nexus_mcp.transcode.blueprint.pins import canonical_pin, class_pin_aliases, node_pins
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture
from ue_node_nexus_mcp.transcode.lint.service import lint_document
from ue_node_nexus_mcp.transcode.raw.nodes import node_decl
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock
from ue_node_nexus_mcp.transcode.text.model import Decl
from ue_node_nexus_mcp.transcode.text.parser import parse


def schema(tmp_path):
    directory = tmp_path / "schema"
    publish(directory, "key", dict(k2node={
        "/Script/BlueprintGraph.K2Node_InputKey": dict(
            path="/Script/BlueprintGraph.K2Node_InputKey", dynamic_pins=True,
            props={"InputKey": dict(type="FKey", kind="name", value_schema=dict(type="name"), default="None")}),
    }, component={
        "CineCameraComponent": dict(
            path="/Script/CinematicCamera.CineCameraComponent",
            props={
                "FieldOfView": dict(type="float", kind="number", value_schema=dict(type="float"),
                                    derived=True, writable=False, derived_from="CurrentFocalLength, Filmback"),
                "CurrentFocalLength": dict(type="float", kind="number", value_schema=dict(type="float")),
                "Filmback": dict(type="struct", kind="struct", value_schema=dict(kind="struct")),
            }),
    }))
    return SchemaLock(directory, "key")


def document(body: str):
    value, sink = parse("nexus: 1\nasset: /Game/BP_Issue25\nclass: Blueprint\nschema: key\n\n" + body)
    assert not sink.has_errors, sink.items
    return value


def test_input_key_positional_value_is_the_reflected_property(tmp_path):
    lock = schema(tmp_path)
    semantic = capture("nexus: 1\nasset: /Game/BP_Issue25\nclass: Blueprint\nschema: key\n\n"
                       "[graph EventGraph]\nfour : InputKey(Four)\n", None, "test", "blueprint", lock)
    entity = next(iter(semantic["semantic"]["sections"]["graph:EventGraph"]["entities"].values()))
    assert entity["args"]["InputKey"]["state"] == "explicit"
    assert entity["args"]["InputKey"]["value"] == "Four"


def test_input_key_exporter_does_not_repeat_its_positional_key():
    node = dict(guid="FOUR", class_short="InputKey", config=dict(input_key="Four"), pins=[], supported=True,
                props=[dict(name="InputKey", type="FKey", value="Four", default="None")],
                **{"class": "/Script/BlueprintGraph.K2Node_InputKey"})
    decl = node_decl("four", node)
    assert ("InputKey", "Four") not in decl.args
    assert decl.positional() == ["Four"]


def test_branch_true_false_share_the_then_else_contract():
    aliases = class_pin_aliases(None, "IfThenElse")
    pins = node_pins(Decl("branch", "IfThenElse"), None, None)
    assert canonical_pin("False", "out", pins, aliases) == "else"
    assert canonical_pin("True", "out", pins, aliases) == "then"


def test_branch_aliases_lint_and_unknown_pins_are_rejected():
    good = document("[graph EventGraph]\nbranch : Branch\nerror : ExecutionSequence\n"
                    "view : ExecutionSequence\nbranch.False -> error.execute\nbranch.True -> view.execute\n")
    assert lint_document(good, "blueprint", None).errors() == []
    bad = document("[graph EventGraph]\nbranch : Branch\nerror : ExecutionSequence\n"
                   "branch.Other -> error.execute\n")
    assert [item.code for item in lint_document(bad, "blueprint", None).errors()] == ["unknown_pin"]


def test_derived_camera_property_is_rejected_by_component_lint(tmp_path):
    lock = schema(tmp_path)
    bad = document("[components]\nCameraComponent : @inherited(CineCameraComponent) { FieldOfView=52 }\n")
    errors = lint_document(bad, "blueprint", lock).errors()
    assert [item.code for item in errors] == ["derived_property"]
    good = document("[components]\nCameraComponent : @inherited(CineCameraComponent) { CurrentFocalLength=24.35761 }\n")
    assert lint_document(good, "blueprint", lock).errors() == []
