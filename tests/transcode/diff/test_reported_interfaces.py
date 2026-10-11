"""Declaration identity and reflected reset defaults remain in the plan."""

from ue_node_nexus_mcp.transcode.diff.service import build_plan
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock
from ue_node_nexus_mcp.transcode.text.parser import parse


def document(body, cls="Material"):
    result, sink = parse(f"nexus: 1\nasset: /Game/Test\nclass: {cls}\nschema: key\n{body}")
    assert not sink.has_errors
    return result


def test_removed_asset_field_uses_known_schema_default(tmp_path):
    publish(tmp_path, "key", dict(asset=dict(Material=dict(path="/Script/Engine.Material", props=dict(
        FloatPrecisionMode=dict(type="enum", default="MFPM_Default", default_source="class_default_object"))))))
    schema = SchemaLock(tmp_path, "key")
    base = document("[asset]\nFloatPrecisionMode = MFPM_Full_MaterialExpressionOnly\n")
    plan = build_plan(document("[asset]\n"), base, "material", schema=schema)
    assert not plan.has_errors
    assert plan.verbs[0].args["value"] == "MFPM_Default"
    assert plan.verbs[0].args["default_source"]["class_path"] == "/Script/Engine.Material"
    assert base.section("asset").props()[0].default is None


def test_unknown_asset_reset_remains_rejected():
    plan = build_plan(document("[asset]\n"), document("[asset]\nUnknown = 3\n"), "material")
    assert plan.has_errors and plan.diagnostics[0].code == "cannot_reset"


def test_function_input_alias_rebuild_is_an_interface_identity_change():
    base = document("[graph]\nold : FunctionInput(InputName=Amount,InputType=FunctionInput_Scalar)\n", "MaterialFunction")
    local = document("[graph]\nnew : FunctionInput(InputName=Amount,InputType=FunctionInput_Scalar)\n", "MaterialFunction")
    plan = build_plan(local, base, "material_function")
    assert plan.interface_changed and plan.interface_identity_changed
    assert [verb.op for verb in plan.verbs] == ["delete_node", "create_node"]


def test_collection_parameter_declaration_changes_require_dependency_order():
    base = document("[asset]\nScalarParameters = ()\n", "MaterialParameterCollection")
    local = document("[asset]\nScalarParameters = ((ParameterName=Amount,DefaultValue=1))\n", "MaterialParameterCollection")
    assert build_plan(local, base, "asset").interface_changed
