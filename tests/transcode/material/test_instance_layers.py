"""Material instance parameter identities survive raw, text, plans and readback."""

from copy import deepcopy

from ue_node_nexus_mcp.transcode.collaboration.apply.readback import property_result
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import from_raw
from ue_node_nexus_mcp.transcode.diff.service import build_plan
from ue_node_nexus_mcp.transcode.raw.simple import material_instance_document
from ue_node_nexus_mcp.transcode.text.emitter import emit
from ue_node_nexus_mcp.transcode.text.parser import parse


def raw():
    return dict(raw_version=1, kind="material_instance", asset_path="/Game/MI.MI",
                class_short="MaterialInstanceConstant", props=[], instance=dict(scalar=[
                    dict(name="Amount", type="float", value="0.25", association="global", index=-1),
                    dict(name="Amount", type="float", value="0.5", association="layer", index=0),
                    dict(name="Amount", type="float", value="0.75", association="layer", index=1)]))


def test_layer_identity_roundtrips_and_targets_one_parameter():
    base = material_instance_document(raw())
    assert {section.args for section in base.sections if section.name == "scalar"} == {"", "layer 0", "layer 1"}
    local, errors = parse(emit(base).replace("[scalar layer 1]\nAmount = 0.75", "[scalar layer 1]\nAmount = 0.9"))
    assert not errors.has_errors
    plan = build_plan(local, base, "material_instance")
    assert len(plan.verbs) == 1
    assert plan.verbs[0].args == dict(kind="scalar", name="Amount", value="0.9", association="layer", index=1)


def test_layer_parameter_readback_uses_association_and_index():
    actual = from_raw(raw())
    operation = dict(op="mi_set_param", kind="scalar", name="Amount", value="0.75", association="layer", index=1)
    assert property_result(operation, actual) == []
    assert property_result(dict(operation, index=0), actual)
    changed = deepcopy(raw())
    changed["instance"]["scalar"].pop(1)
    plan = build_plan(material_instance_document(changed), material_instance_document(raw()), "material_instance")
    assert len(plan.verbs) == 1
    assert plan.verbs[0].op == "mi_clear_param"
    assert plan.verbs[0].args == dict(kind="scalar", name="Amount", association="layer", index=0)
