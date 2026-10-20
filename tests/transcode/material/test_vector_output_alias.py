"""A vector parameter has RGB, not RGBA: the missing output must be an error, not alpha."""

from ue_node_nexus_mcp.transcode.lint.service import lint_document
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock
from ue_node_nexus_mcp.transcode.text.parser import parse

DOCUMENT = """nexus: 1
asset: /Game/M_AlphaProbe
class: Material
schema: key
[graph]
st : VectorParameter(ParameterName="BaseMap")
st.RGB -> out.BaseColor
st.A -> out.Opacity
"""


def schema_at(tmp_path):
    publish(tmp_path / "schema", "key", dict(material_expression=dict(
        MaterialExpressionVectorParameter=dict(path="/Script/Engine.MaterialExpressionVectorParameter",
            outputs=["RGB", "R", "G", "B", "A"], props=dict(ParameterName=dict(type="FName"))))))
    return SchemaLock(tmp_path / "schema", "key")


def lint(tmp_path, document):
    parsed, _ = parse(document)
    return lint_document(parsed, "material", schema_at(tmp_path))


def test_declared_outputs_are_accepted(tmp_path):
    result = lint(tmp_path, DOCUMENT)
    assert not result.has_errors, result.errors()


def test_rgba_is_rejected_on_a_vector_parameter(tmp_path):
    result = lint(tmp_path, DOCUMENT.replace("st.A -> out.Opacity", "st.RGBA -> out.Opacity"))
    assert result.has_errors
    assert any("RGBA" in item.message and item.code == "unknown_pin" for item in result.errors())


CALL = """nexus: 1
asset: /Game/M_Hair
class: Material
schema: key
[graph]
call : MaterialFunctionCall(MaterialFunction=/Game/MF_Hair)
call.World -> out.EmissiveColor
"""


def call_schema(tmp_path):
    publish(tmp_path / "calls", "key", dict(material_expression=dict(
        MaterialExpressionMaterialFunctionCall=dict(path="/Script/Engine.MaterialExpressionMaterialFunctionCall",
            props=dict(MaterialFunction=dict(type="object")))),
        material_functions=dict([("/Game/MF_Hair.MF_Hair", dict(outputs=[dict(name="Tangent"), dict(name="World")], inputs=[]))])))
    return SchemaLock(tmp_path / "calls", "key")


def call_lint(tmp_path, document):
    parsed, _ = parse(document)
    return lint_document(parsed, "material", call_schema(tmp_path))


def test_a_function_call_output_is_named_not_numbered(tmp_path):
    assert not call_lint(tmp_path, CALL).has_errors
    numbered = call_lint(tmp_path, CALL.replace("call.World", "call.1"))
    assert numbered.has_errors
    assert any(item.code == "pin_requires_name" for item in numbered.errors())
