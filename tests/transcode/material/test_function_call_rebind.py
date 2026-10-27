"""A call node pointed at another function carries its links over by output name.

Issue 21-2a: the link text ``call.RawColor -> out.BaseColor`` reads the same before
and after the rebind, so the plan kept the link and UE kept the old function's output
index, wiring BaseColor to whatever the new function exposes at that index.
"""

from ue_node_nexus_mcp.transcode.diff.service import build_plan
from ue_node_nexus_mcp.transcode.material.calls import rebound_calls
from ue_node_nexus_mcp.transcode.text.parser import parse

OLD = "/Game/Functions/MF_Old.MF_Old"
NEW = "/Game/Functions/MF_New.MF_New"
BASE = (
    "nexus: 1\nasset: /Game/M_Rebind\nclass: Material\nschema: key\n[graph]\n"
    "tint : Constant\n"
    f"call : MaterialFunctionCall(MaterialFunction={OLD}) @ 0,0\n"
    "tint -> call.Tint\n"
    "call.RawColor -> out.BaseColor\n"
    "call.B -> out.Roughness\n"
)
RAW_COLOR = dict([("from", "call"), ("from_pin", "RawColor"), ("to", "out"), ("to_pin", "BaseColor")])
TINT = dict([("from", "tint"), ("from_pin", None), ("to", "call"), ("to_pin", "Tint")])
B = dict([("from", "call"), ("from_pin", "B"), ("to", "out"), ("to_pin", "Roughness")])


def graph(text):
    document, sink = parse(text)
    assert sink.items == [], [item.format() for item in sink.items]
    return document


def plan_ops(local):
    plan = build_plan(graph(local), graph(BASE), "material", dict(tint="G-TINT", call="G-CALL"))
    assert not plan.has_errors, plan.diagnostics
    return [verb.to_payload() for verb in plan.verbs]


def position(ops, op, **fields):
    return next(index for index, item in enumerate(ops)
                if item["op"] == op and all(item.get(key) == value for key, value in fields.items()))


def test_rebound_call_cuts_and_remakes_unchanged_link_text():
    ops = plan_ops(BASE.replace(OLD, NEW).replace("call.B -> out.Roughness\n", ""))

    rebind = position(ops, "set_node_param", id="call", name="MaterialFunction", value=NEW)
    refresh = position(ops, "refresh_function_calls", function=NEW)
    assert position(ops, "disconnect_pins", **RAW_COLOR) < rebind < refresh < position(ops, "connect_pins", **RAW_COLOR)
    assert position(ops, "disconnect_pins", **TINT) < rebind < refresh < position(ops, "connect_pins", **TINT)
    assert position(ops, "disconnect_pins", **B) < rebind
    assert not [item for item in ops if item["op"] == "connect_pins" and item.get("from_pin") == "B"]


def test_call_kept_on_its_function_keeps_its_links():
    assert [item["op"] for item in plan_ops(BASE.replace("@ 0,0", "@ 40,0"))] == ["set_node_position"]


def test_only_kept_calls_whose_function_changes_are_rebound():
    base = graph(BASE).section("graph")
    assert rebound_calls(graph(BASE.replace(OLD, NEW)).section("graph"), base) == frozenset(["call"])
    assert rebound_calls(graph(BASE).section("graph"), base) == frozenset()
    assert rebound_calls(graph(BASE.replace(OLD, NEW)).section("graph"), None) == frozenset()
    recreated = BASE.replace(OLD, NEW).replace("call ", "fresh ").replace("call.", "fresh.")
    assert rebound_calls(graph(recreated).section("graph"), base) == frozenset()
