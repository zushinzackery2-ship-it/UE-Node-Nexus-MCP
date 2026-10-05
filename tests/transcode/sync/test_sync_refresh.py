import json

from ue_node_nexus_mcp.transcode.storage.paths import base_path
from ue_node_nexus_mcp.transcode.sync.service import run_sync

from ..fake_ue import SCHEMA_KEY
from ..fixtures import prop

MF = "/Game/WaterStains/Functions/MF_WS_S.MF_WS_S"
MAT = "/Game/Materials/M_Glass.M_Glass"


def _edit_interface(project):
    file = project / "WaterStains/Functions/MF_WS_S.mf.nexus"
    file.write_text(file.read_text(encoding="utf-8").replace("InputName=B", "InputName=Bee"), encoding="utf-8")


def test_a_refresh_records_the_catalog_offline_queries_read(sync_workspace):
    """A published catalog is keyed by its collection identity; lint reads the recorded key."""
    ue, env, project = sync_workspace
    info = json.loads((project / "project.json").read_text(encoding="utf-8"))
    info["schema_key"] = "5.5.4-stale0000"
    (project / "project.json").write_text(json.dumps(info), encoding="utf-8")

    report = run_sync(ue, "schema", None, dict(refresh=True), env)

    recorded = json.loads((project / "project.json").read_text(encoding="utf-8"))["schema_key"]
    assert recorded == report["schema_key"] == SCHEMA_KEY


def test_an_online_action_restores_a_stale_recorded_catalog_key(sync_workspace):
    """The editor's identity is the current catalog, so a stale record must follow it."""
    ue, env, project = sync_workspace
    info = json.loads((project / "project.json").read_text(encoding="utf-8"))
    info["schema_key"] = "5.5.4-stale0000"
    (project / "project.json").write_text(json.dumps(info), encoding="utf-8")

    run_sync(ue, "pull", [MAT], env=env)

    assert json.loads((project / "project.json").read_text(encoding="utf-8"))["schema_key"] == SCHEMA_KEY


def test_legacy_rejection_happens_before_function_and_caller_apply(sync_workspace):
    """A legacy interface edit must enter the guarded workspace protocol first."""
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
    rows = dict((row["asset"], row) for row in report["rows"])
    assert rows[MF]["action"] == "failed", report["rows"]
    assert any("safety_workspace_required" in item for item in report["diagnostics"])
    assert not ue.applied and MAT not in rows
    assert base_path(project, MAT).read_bytes() == base
    assert not list((project / ".nexus/pending").rglob("M_Glass.push.json"))


def test_legacy_interface_rejection_preserves_selected_caller_intent(sync_workspace):
    """Dependency ordering rejects the unguarded interface before either asset applies."""
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

    assert report["error_count"] == 1, report["diagnostics"]
    assert any("safety_workspace_required" in item for item in report["diagnostics"])
    assert not ue.applied
    assert "BLEND_Translucent" not in file.read_text(encoding="utf-8")
    assert run_sync(ue, "status", [MF, MAT], env=env)["counts"] == dict(**{"local-modified": 2})
