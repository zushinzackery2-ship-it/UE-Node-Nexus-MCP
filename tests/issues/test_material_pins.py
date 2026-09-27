"""Issues2 #2/#4/#9: lint accepts the pins UE exports and checks required previews."""

import pytest

from ue_node_nexus_mcp.transcode.lint import lint_document
from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock


def expression(name, inputs=(), outputs=("",), **props):
    path = f"/Script/Engine.MaterialExpression{name}"
    return path, dict(path=path, props=props, inputs=list(inputs), outputs=list(outputs))


@pytest.fixture
def schema(tmp_path):
    mode = dict(kind="enum", default="TMVM_None", enum_values=[
        "TMVM_None", "TMVM_MipLevel", "TMVM_MipBias", "TMVM_Derivative"])
    classes = dict([
        expression("Constant"),
        expression("TextureObject"),
        expression("TextureSample", ("Coordinates", "TextureObject", "Apply View MipBias"), MipValueMode=mode),
        expression("TextureSampleParameter2D", ("Coordinates", "Apply View MipBias"), MipValueMode=mode),
        expression("Custom", ("None",), Inputs=dict(kind="array", default="(())"),
                   AdditionalOutputs=dict(kind="array", default="")),
        expression("FunctionInput", ("Preview",), InputType=dict(kind="enum", default="FunctionInput_Vector3")),
        expression("NamedRerouteUsage"),
    ])
    directory = tmp_path / "schema"
    publish(directory, "key", dict(material_expression=classes))
    return SchemaLock(directory, "key")


def lint(schema, graph):
    text = "nexus: 1\nasset: /Game/Test/MF_Pins\nclass: MaterialFunction\nschema: key\n\n[graph]\n" + graph
    document, sink = parse(text)
    assert sink.errors() == []
    return lint_document(document, "material_function", schema).errors()


@pytest.mark.parametrize("mode,pin", [
    ("TMVM_MipLevel", "MipLevel"), ("TMVM_MipBias", "MipBias"),
    ("TMVM_Derivative", "DDX(UVs)"), ("TMVM_Derivative", "DDY(UVs)"),
    ("ETextureMipValueMode::TMVM_MipLevel", "MipLevel"),
])
@pytest.mark.parametrize("cls", ["TextureSample", "TextureSampleParameter2D"])
def test_sample_inputs_follow_mode_including_subclasses(schema, mode, pin, cls):
    assert lint(schema, f"level : Constant\nsample : {cls}(MipValueMode={mode})\nlevel -> sample.{pin}\n") == []


@pytest.mark.parametrize("mode,pin", [("TMVM_None", "MipLevel"), ("TMVM_MipBias", "MipLevel"),
                                      ("TMVM_MipLevel", "DDX(UVs)")])
def test_inactive_mip_inputs_are_rejected(schema, mode, pin):
    errors = lint(schema, f"level : Constant\nsample : TextureSample(MipValueMode={mode})\nlevel -> sample.{pin}\n")
    assert [item.code for item in errors] == ["unknown_pin"]


def test_mip_inputs_move_numeric_pin_indices(schema):
    assert lint(schema, "level : Constant\nsample : TextureSample(MipValueMode=TMVM_Derivative)\nlevel -> sample.4\n") == []
    assert [item.code for item in lint(schema, "level : Constant\nsample : TextureSample\nlevel -> sample.4\n")] == ["unknown_pin"]


def custom(links):
    return ('value : Constant\nnode : Custom(Inputs=((InputName="UV"),(InputName="Gain")), '
            'AdditionalOutputs=((OutputName="Mask")))\n' + links)


def test_format_two_custom_uses_declared_inputs_and_outputs(schema):
    assert lint(schema, custom("value -> node.UV\nvalue -> node.Gain\nnode.Mask -> node.UV\n")) == []


def test_custom_pin_errors_name_the_declared_pins(schema):
    errors = lint(schema, custom("value -> node.Nope\nnode.Nope -> node.UV\n"))
    assert [item.message for item in errors] == [
        "node (Custom) has no input 'Nope'; inputs: UV, Gain",
        "node (Custom) has no output 'Nope'; outputs: return, Mask",
    ]


@pytest.mark.parametrize("kind", ["Texture2D", "TextureCube", "Texture2DArray", "TextureExternal", "VolumeTexture"])
def test_texture_function_inputs_require_a_preview(schema, kind):
    errors = lint(schema, f"tex : FunctionInput(InputType=FunctionInput_{kind})\n")
    assert [item.code for item in errors] == ["missing_preview_connection"]
    assert errors[0].line > 0


@pytest.mark.parametrize("pin", ["Preview", "0", ""])
def test_a_texture_preview_connection_satisfies_the_rule(schema, pin):
    target = "tex" + ("." + pin if pin else "")
    assert lint(schema, f"source : TextureObject\ntex : FunctionInput(InputType=FunctionInput_Texture2D)\nsource -> {target}\n") == []


def test_numeric_function_inputs_may_omit_preview(schema):
    assert lint(schema, "value : FunctionInput(InputType=FunctionInput_Scalar)\n") == []


def test_synthetic_parameters_work_with_catalog_paths(schema):
    assert lint(schema, 'use : NamedRerouteUsage(DeclarationName="Shared")\n') == []
