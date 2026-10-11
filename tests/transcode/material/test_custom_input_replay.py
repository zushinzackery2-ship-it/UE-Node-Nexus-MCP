"""Replay Custom inputs through the publication receipt, including old history."""

from copy import deepcopy

import pytest

from tests.support.semantics import receipt_workspace
from ue_node_nexus_mcp.transcode.collaboration.apply.transactions import actual_snapshot
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import from_raw
from ue_node_nexus_mcp.transcode.diff.service import build_plan
from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.raw.material import material_document


INPUT = dict(kind="struct", type="ExpressionInput", fields=dict(
    Expression=dict(kind="object", type="object", default="None"),
    OutputIndex=dict(type="int", default="0"), Mask=dict(type="int", default="0"),
    MaskR=dict(type="int", default="0")))
INPUTS = dict(kind="array", type="TArray<CustomInput>", element=dict(
    kind="struct", type="CustomInput", fields=dict(InputName=dict(type="FName"), Input=INPUT)))


def inputs(expression="MaterialExpressionConstant_1", **fields):
    values = dict(OutputIndex="0", Mask="0", MaskR="0")
    values.update(fields)
    content = ",".join(["Expression=" + expression, *[name + "=" + value for name, value in values.items()]])
    return '((InputName="Gain",Input=(' + content + ')))'


def raw(value):
    source = dict(guid="SOURCE", class_short="MaterialExpressionConstant", x=0, y=0, props=[],
                  inputs=[], outputs=[""])
    custom = dict(guid="CUSTOM", class_short="MaterialExpressionCustom", x=0, y=0, inputs=["Gain"], outputs=[""],
                  props=[dict(name="Inputs", type=INPUTS["type"], value=value, default="()", value_schema=INPUTS)])
    return dict(raw_version=1, asset_path="/Game/M_Test.M_Test", kind="material", schema_key="key",
                class_short="Material", props=[], graph=dict(nodes=[source, custom],
                links=[dict(**{"from": "SOURCE", "to": "CUSTOM"}, from_out=0, to_in=0)], outputs=[]))


def receipt(tmp_path, desired, native, *, legacy=False, linked=True):
    candidate = from_raw(raw(desired))
    custom = next(entity for entity in candidate["semantic"]["sections"]["graph:"]["entities"].values()
                  if entity["type"].endswith("Custom"))
    if legacy:
        custom["args"]["Inputs"]["value"] = desired
    workspace, commit, asset = receipt_workspace(tmp_path, candidate)
    after = raw(native)
    if not linked:
        after["graph"]["links"] = []
    record = dict(id="replay", asset=asset, candidate=commit,
                  request=dict(plan=[dict(op="set_node_param", id=custom["alias"], name="Inputs", value=desired)]),
                  receipt=dict(after=after))
    return workspace, record


@pytest.mark.parametrize("legacy", [False, True])
def test_expression_object_rename_is_equivalent_history(tmp_path, legacy):
    workspace, record = receipt(tmp_path, inputs(), inputs("MaterialExpressionConstant_3"), legacy=legacy)
    assert actual_snapshot(workspace, record)["raw"]["graph"]["links"]


def test_input_name_only_accepts_native_struct_expansion(tmp_path):
    workspace, record = receipt(tmp_path, '((InputName="Gain"))', inputs())
    assert actual_snapshot(workspace, record)["raw"]["graph"]["links"]


@pytest.mark.parametrize("native", [inputs(Mask="1"), inputs(OutputIndex="1"),
                                   inputs().replace('"Gain"', '"Wrong"'),
                                   "(" + inputs()[1:-1] + "," + inputs()[1:-1] + ")"])
def test_layout_fields_remain_strict(tmp_path, native):
    workspace, record = receipt(tmp_path, inputs(), native)
    with pytest.raises(SyncError, match="readback differs"):
        actual_snapshot(workspace, record)


def test_missing_graph_connection_is_rejected_even_after_inputs_write(tmp_path):
    workspace, record = receipt(tmp_path, inputs(), inputs(), linked=False)
    with pytest.raises(SyncError, match="readback differs"):
        actual_snapshot(workspace, record)


def test_rebuilding_inputs_reconnects_unchanged_graph_link():
    base, _, _ = material_document(raw(inputs()))
    local = deepcopy(base)
    custom = next(decl for _, decl in local.iter_decls() if decl.type_name.endswith("Custom"))
    source = next(decl for _, decl in local.iter_decls() if decl.type_name.endswith("Constant"))
    custom.args = [("Inputs", inputs(Mask="1"))]
    plan = build_plan(local, base, "material")
    assert not plan.has_errors
    write = next(verb for verb in plan.verbs if verb.op == "set_node_param")
    assert "Expression=" not in write.args["value"]
    links = [verb for verb in plan.verbs if verb.op == "connect_pins"]
    assert len(links) == 1 and links[0].args["from"] == source.id and links[0].args["to"] == custom.id
