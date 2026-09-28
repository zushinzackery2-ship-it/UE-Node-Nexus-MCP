"""New function calls keep their asset signature through workspace capture."""

import pytest

from ue_node_nexus_mcp.transcode.collaboration.semantic.decode import to_document
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture, text_of
from ue_node_nexus_mcp.transcode.lint import lint_document
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock


FUNCTION = "/Game/Face/MF_Surface.MF_Surface"
SOURCE = (
    "nexus: 1\nasset: /Game/M_Face\nclass: Material\nschema: key\n"
    "[graph]\n"
    "uv : Constant\n"
    "face : MaterialFunctionCall(MaterialFunction=" + FUNCTION + ")\n"
    "uv -> face.0\n"
    "face.Color -> out.EmissiveColor\n"
    "face.Opacity -> out.OpacityMask\n"
)


def schema_lock(tmp_path, cdo_outputs, signature=True):
    directory = tmp_path / "schema"
    call = dict(path="/Script/Engine.MaterialExpressionMaterialFunctionCall",
                props=dict(MaterialFunction=dict(type="object", kind="object", default="None")),
                inputs=[], outputs=list(cdo_outputs))
    constant = dict(path="/Script/Engine.MaterialExpressionConstant", props=dict(), outputs=[""])
    functions = dict()
    if signature:
        functions[FUNCTION] = dict(path=FUNCTION,
            inputs=[dict(name="UV"), dict(name="Mask")],
            outputs=[dict(name="Color"), dict(name="Opacity")])
    publish(directory, "key", dict(
        material_expression=dict(MaterialExpressionMaterialFunctionCall=call,
                                 MaterialExpressionConstant=constant),
        material_functions=functions))
    return SchemaLock(directory, "key")


@pytest.mark.parametrize("cdo_outputs", [(), ("",)])
@pytest.mark.parametrize("color_pin", ["Color", "0"])
def test_new_multi_output_call_round_trip_uses_referenced_signature(tmp_path, cdo_outputs, color_pin):
    schema = schema_lock(tmp_path, cdo_outputs)
    source = SOURCE.replace("face.Color", "face." + color_pin)
    snapshot = capture(source, None, "face", "material", schema)
    document = to_document(snapshot)
    assert not lint_document(document, "material", schema).has_errors
    links = list(document.section("graph").links())
    assert [(link.src_pin, link.dst_pin) for link in links] == [
        (None, "UV"), ("Color", "EmissiveColor"), ("Opacity", "OpacityMask")]
    repeated = capture(text_of(snapshot), snapshot, "face", "material", schema)
    assert repeated["semantic"] == snapshot["semantic"]


@pytest.mark.parametrize("cdo_outputs", [(), ("",)])
def test_unknown_function_signature_preserves_explicit_output(tmp_path, cdo_outputs):
    schema = schema_lock(tmp_path, cdo_outputs, signature=False)
    snapshot = capture(SOURCE, None, "face", "material", schema)
    outputs = [link.src_pin for link in to_document(snapshot).section("graph").links()
               if link.src == "face"]
    assert outputs == ["Color", "Opacity"]
