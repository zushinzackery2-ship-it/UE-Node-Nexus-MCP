"""Keep authored event types structured across the Python/native apply boundary."""

import pytest

from ue_node_nexus_mcp.transcode.diff.service import build_plan
from ue_node_nexus_mcp.transcode.text.parser import parse


@pytest.mark.parametrize("type_text,expected", [
    ("Object(/Script/Engine.Actor)", dict(category="object", subobject="/Script/Engine.Actor", container="none")),
    ("Array<Object(/Script/Engine.Actor)>", dict(category="object", subobject="/Script/Engine.Actor", container="array")),
    ("Struct(/Script/CoreUObject.Vector)", dict(category="struct", subobject="/Script/CoreUObject.Vector", container="none")),
    ("double", dict(category="real", subcategory="double", container="none")),
    ("Enum(/Script/Engine.EMovementMode)", dict(category="byte", subobject="/Script/Engine.EMovementMode", container="none")),
])
def test_custom_event_create_carries_native_pin_contract(type_text, expected):
    document, diagnostics = parse("nexus: 1\nasset: /Game/EventTypes\nclass: Blueprint\nschema: test\n"
        "[graph EventGraph]\nevent : CustomEvent(Receive, Value: " + type_text + ")\n")
    assert not diagnostics.has_errors
    plan = build_plan(document, None, "blueprint", {}, None)
    assert not plan.has_errors
    operation = next(verb for verb in plan.verbs if verb.op == "create_node")
    parameter = operation.args["custom_event_signature"]["inputs"][0]
    assert parameter["name"] == "Value"
    assert all(parameter["type"][key] == value for key, value in expected.items())


def test_custom_event_default_and_empty_signature_survive_creation():
    document, diagnostics = parse("nexus: 1\nasset: /Game/EventTypes\nclass: Blueprint\nschema: test\n"
        "[graph EventGraph]\na : CustomEvent(ValueChanged, Value: double = 1.25)\n"
        "b : CustomEvent(Reset)\n")
    assert not diagnostics.has_errors
    creates = [verb for verb in build_plan(document, None, "blueprint", {}, None).verbs if verb.op == "create_node"]
    assert creates[0].args["custom_event_signature"]["inputs"][0]["default"] == "1.25"
    assert creates[1].args["custom_event_signature"]["inputs"] == []
