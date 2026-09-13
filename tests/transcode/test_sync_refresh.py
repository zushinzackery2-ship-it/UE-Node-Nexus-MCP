import json

from ue_node_nexus_mcp.transcode.paths import base_path
from ue_node_nexus_mcp.transcode.sync import run_sync

from .fake_ue import SCHEMA_KEY
from .fixtures import prop

MF = "/Game/WaterStains/Functions/MF_WS_S.MF_WS_S"
MAT = "/Game/Materials/M_Glass.M_Glass"


def _edit_interface(project):
    file = project / "WaterStains/Functions/MF_WS_S.mf.nexus"
    file.write_text(file.read_text(encoding="utf-8").replace("InputName=B", "InputName=Bee"), encoding="utf-8")


def test_refresh_failure_returns_one_outcome_per_selected_asset(sync_workspace):
    ue, env, project = sync_workspace
    ue.referencers[MF] = [MAT]
    _edit_interface(project)
    base = base_path(project, MAT).read_bytes()

    def bridge(operation, payload):
        response = ue(operation, payload)
        if operation == "transcode_apply" and payload["asset_path"] == MAT:
            response["data"]["compile"] = dict(ran=True, ok=False, error_count=1)
        return response

    report = run_sync(bridge, "push", [MF], dict(dry_run=False), env)

    assert report["error_count"] > 0
    assert len(report["rows"]) == 1, report["rows"]
    assert report["rows"][0]["action"] == "pushed-with-errors"
    assert base_path(project, MAT).read_bytes() == base
    assert list((project / ".nexus/pending").rglob("M_Glass.push.json"))


def test_selected_locally_edited_caller_is_replanned_after_refresh(sync_workspace):
    ue, env, project = sync_workspace
    ue.assets[MAT]["graph"]["nodes"].append(dict(
        guid="G-CALL", name="Call", class_short="MaterialFunctionCall",
        **dict((("class", "/Script/Engine.MaterialExpressionMaterialFunctionCall"),)),
        x=0, y=0, inputs=["A", "B"], outputs=["Result"],
        props=[prop("MaterialFunction", "UMaterialFunctionInterface*", MF, "None")],
    ))
    ue.assets[MAT]["saved_hash"] = "caller-added"
    run_sync(ue, "pull", [MAT], env=env)
    from ue_node_nexus_mcp.transcode.schema.catalog import publish

    schema = project.parent / ".nexus/schema" / SCHEMA_KEY
    record = dict(
        path="/Script/Engine.MaterialExpressionMaterialFunctionCall",
        props=dict(MaterialFunction=dict(type="object", kind="object", default="None")),
        inputs=["A", "B"], outputs=["Result"],
    )
    publish(schema, SCHEMA_KEY, dict(material_expression=dict(MaterialExpressionMaterialFunctionCall=record)), incremental=True)
    ue.referencers[MF] = [MAT]
    _edit_interface(project)
    file = project / "Materials/M_Glass.mat.nexus"
    file.write_text(file.read_text(encoding="utf-8").replace("BLEND_Translucent", "BLEND_Opaque"), encoding="utf-8")

    report = run_sync(ue, "push", [MF, MAT], dict(dry_run=False), env)

    assert report["error_count"] == 0, report["diagnostics"]
    assert [payload["asset_path"] for payload in ue.applied] == [MF, MAT, MAT]
    assert ue.applied[1]["plan"][0]["op"] == "refresh_function_calls"
    assert ue.applied[2]["plan"][0]["name"] == "BlendMode"
    assert "BLEND_Translucent" not in file.read_text(encoding="utf-8")
    assert run_sync(ue, "status", [MF, MAT], env=env)["counts"] == dict(clean=2)
