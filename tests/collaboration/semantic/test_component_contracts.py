"""Native reflected defaults survive document and semantic normalization."""

from copy import deepcopy

from ue_node_nexus_mcp.transcode.collaboration.apply.readback import entity_fields
from ue_node_nexus_mcp.transcode.collaboration.semantic.decode import to_document
from ue_node_nexus_mcp.transcode.collaboration.semantic.encode import encode
from ue_node_nexus_mcp.transcode.collaboration.semantic.normalization import normalize_snapshot
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import from_raw
from ue_node_nexus_mcp.transcode.text.semantic import equivalent


def native_state():
    contract = dict(kind="struct", type="FLightingChannels", path="/Script/Engine.LightingChannels",
                    fields=dict(bChannel0=dict(type="bool", default="True"),
                                bChannel1=dict(type="bool", default="False"),
                                bChannel2=dict(type="bool", default="False")))
    channels = "(bChannel0=True,bChannel1=False,bChannel2=False)"
    component = dict(name="Instances", guid="BF779FA9-46C5-7728-3041-74A87B63CD0F", parent="",
                     class_short="HierarchicalInstancedStaticMeshComponent",
                     props=[dict(name="LightingChannels", type="FLightingChannels", value=channels,
                                 default=channels, value_schema=contract)])
    component["class"] = "/Script/Engine.HierarchicalInstancedStaticMeshComponent"
    raw = dict(asset_path="/Game/Test/BP_Volume.BP_Volume", kind="blueprint", class_short="Blueprint",
               raw_version=1, props=[], blueprint=dict(blueprint_type="normal", parent_class="/Script/Engine.Actor",
               components=[component], variables=[], graphs=[], defaults=[]))
    raw["class"] = "/Script/Engine.Blueprint"
    return from_raw(raw)


def component(snapshot):
    return next(iter(snapshot["semantic"]["sections"]["components:"]["entities"].values()))


def test_default_normalization_is_idempotent():
    native = native_state()
    normalized = normalize_snapshot(native, native)
    assert component(normalized)["props"]["LightingChannels"]["state"] == "default"
    assert equivalent(native["semantic"], normalized["semantic"])
    assert equivalent(normalized["semantic"], normalize_snapshot(normalized, native)["semantic"])


def test_decode_keeps_reflected_member_contracts():
    document = to_document(native_state(), include_defaults=True)
    decl = document.section("components").decls()[0]
    assert isinstance(decl.meta["input_types"]["LightingChannels"], dict)
    assert decl.meta["prop_types"]["LightingChannels"] == decl.meta["input_types"]["LightingChannels"]


def test_explicit_override_still_requires_matching_readback():
    native = native_state()
    document = to_document(native)
    document.section("components").decls()[0].props = [
        ("LightingChannels", "(bChannel0=False,bChannel1=False,bChannel2=False)")]
    semantic, bindings, locations = encode(document, "blueprint", native, "test")
    authored = dict(native, semantic=semantic, bindings=bindings, locations=locations)
    desired = normalize_snapshot(authored, native)
    operation = dict(op="bp_component_set_prop", prop="LightingChannels")
    assert component(desired)["props"]["LightingChannels"]["state"] == "explicit"
    assert entity_fields(operation, component(desired), component(native))
    correct = deepcopy(native)
    component(correct)["props"]["LightingChannels"] = deepcopy(component(desired)["props"]["LightingChannels"])
    assert entity_fields(operation, component(desired), component(correct)) == []
