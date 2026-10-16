"""Unpublished component defaults remain inherited throughout candidate normalization."""

from copy import deepcopy
from types import SimpleNamespace

from ue_node_nexus_mcp.transcode.collaboration.apply.readback import entity_fields
from ue_node_nexus_mcp.transcode.collaboration.semantic.decode import to_document
from ue_node_nexus_mcp.transcode.collaboration.semantic.normalization import normalize_snapshot
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture, from_raw
from ue_node_nexus_mcp.transcode.diff.blueprint import diff_blueprint
from ue_node_nexus_mcp.transcode.diff.plan import AssetPlan
from ue_node_nexus_mcp.transcode.schema.lock import ClassInfo


CHANNELS = "(bChannel0=True,bChannel1=False,bChannel2=False)"
CONTRACT = dict(kind="struct", type="FLightingChannels", path="/Script/Engine.LightingChannels",
                fields=dict(bChannel0=dict(type="bool", default="True"),
                            bChannel1=dict(type="bool", default="False"),
                            bChannel2=dict(type="bool", default="False")))
TEXT = """nexus: 1
asset: /Game/Test/BP_Water
class: Blueprint

[components]
Water : BoxComponent { BoxExtent=(X=500,Y=500,Z=250) }
"""


def schema():
    properties = dict(LightingChannels=dict(type="FLightingChannels", default=CHANNELS, value_schema=CONTRACT),
                      BoxExtent=dict(type="FVector", default="(X=32,Y=32,Z=32)"))
    info = ClassInfo(name="BoxComponent", path="/Script/Engine.BoxComponent", props=properties)
    return SimpleNamespace(key="components", info=lambda: dict(),
                           resolve_class=lambda family, name, **kwargs: info if family == "component" else None)


def component(snapshot):
    return next(iter(snapshot["semantic"]["sections"]["components:"]["entities"].values()))


def native_state(previous=None, catalog=None):
    raw = dict(asset_path="/Game/Test/BP_Water.BP_Water", kind="blueprint", class_short="Blueprint",
               raw_version=1, props=[], blueprint=dict(blueprint_type="normal", parent_class="/Script/Engine.Actor",
               components=[dict(name="Water", guid="GUID", parent="", class_short="BoxComponent",
               props=[dict(name="LightingChannels", type="FLightingChannels", value=CHANNELS, default=CHANNELS,
                           value_schema=CONTRACT),
                      dict(name="BoxExtent", type="FVector", value="(X=500,Y=500,Z=250)",
                           default="(X=32,Y=32,Z=32)")], **dict([("class", "/Script/Engine.BoxComponent")]))],
               variables=[], graphs=[], defaults=[]))
    raw["class"] = "/Script/Engine.Blueprint"
    return from_raw(raw, previous, schema=catalog)


def test_unpublished_default_does_not_become_an_explicit_write():
    candidate = capture(TEXT, None, "author", "blueprint", schema())
    normalized = normalize_snapshot(candidate, schema=schema())
    normalized = normalize_snapshot(normalized, schema=schema())
    assert component(normalized)["props"]["LightingChannels"]["state"] == "default"
    plan = AssetPlan(asset_path="/Game/Test/BP_Water", kind="blueprint")
    diff_blueprint(to_document(normalized), None, plan, schema())
    writes = [row.args["prop"] for row in plan.verbs if row.op == "bp_component_set_prop"]
    assert writes == ["BoxExtent"]


def test_live_member_contracts_survive_generated_string_types():
    candidate = capture(TEXT, None, "author", "blueprint", schema())
    candidate = normalize_snapshot(candidate, schema=schema())
    actual = native_state(candidate, schema())
    desired = normalize_snapshot(candidate, actual, schema())
    assert entity_fields(dict(op="bp_component_add"), component(desired), component(actual)) == []


def test_explicit_struct_edit_still_requires_exact_effective_readback():
    candidate = capture(TEXT.replace("BoxExtent=", "LightingChannels=(bChannel2=True,bChannel0=False,bChannel1=False), BoxExtent="),
                        None, "author", "blueprint", schema())
    desired = normalize_snapshot(candidate, native_state(), schema())
    operation = dict(op="bp_component_set_prop", prop="LightingChannels")
    assert entity_fields(operation, component(desired), component(native_state()))
    correct = deepcopy(native_state())
    component(correct)["props"]["LightingChannels"] = deepcopy(component(desired)["props"]["LightingChannels"])
    assert entity_fields(operation, component(desired), component(correct)) == []
