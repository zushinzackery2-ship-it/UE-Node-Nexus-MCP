import json

import pytest

from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.sync import run_sync
from ue_node_nexus_mcp.transcode.sync_deps import document_dependencies, order_assets
from ue_node_nexus_mcp.transcode.sync_project import SyncError

from .fake_ue import SCHEMA_KEY


def _document(asset, body, cls="Blueprint"):
    text = f"nexus: 1\nasset: {asset}\nclass: {cls}\nschema: {SCHEMA_KEY}\n\n{body}\n"
    document, sink = parse(text)
    assert not sink.has_errors, sink.items
    return document, text


def test_dependencies_include_component_properties_and_nested_arrays():
    document, _ = _document(
        "/Game/A",
        "[components]\nMesh : StaticMeshComponent { "
        "OverlayMaterial=/Game/Z.Z, "
        "OverrideMaterials=(MaterialInstanceConstant'/Game/Y.Y',/Game/X.X) }",
    )
    assert document_dependencies(document, "blueprint") == set(("/Game/Z.Z", "/Game/Y.Y", "/Game/X.X"))


def test_dependency_cycle_is_reported_with_asset_paths():
    a, _ = _document("/Game/A", "[asset]\nParent=/Game/B.B", "MaterialInstanceConstant")
    b, _ = _document("/Game/B", "[asset]\nParent=/Game/A.A", "MaterialInstanceConstant")
    documents = dict((("/Game/A.A", ("material_instance", a)), ("/Game/B.B", ("material_instance", b))))
    with pytest.raises(SyncError, match="/Game/A.A") as caught:
        order_assets(documents)
    assert caught.value.code == "dependency_cycle"


def test_material_subobject_connections_do_not_create_self_dependency():
    document, _ = _document(
        "/Game/MF_Wet",
        '[graph]\nnode : Custom(Inputs=((InputName="A",Input=('
        'Expression="/Script/Engine.MaterialExpressionFunctionInput'
        "'/Game/MF_Wet.MF_Wet:MaterialExpressionFunctionInput_0'\"))))",
        "MaterialFunction",
    )
    assert document_dependencies(document, "material_function") == set()
    assert order_assets(dict((("/Game/MF_Wet.MF_Wet", ("material_function", document)),))) == ["/Game/MF_Wet.MF_Wet"]


def test_external_subobject_retains_its_asset_dependency():
    document, _ = _document("/Game/A", "[asset]\nReference=Object(/Game/B.B:SomeSubobject)")
    assert document_dependencies(document, "asset") == set(("/Game/B.B",))


def test_actual_recursive_material_function_still_fails():
    document, _ = _document(
        "/Game/MF_Wet",
        "[graph]\ncall : MaterialFunctionCall(MaterialFunction=/Game/MF_Wet.MF_Wet)",
        "MaterialFunction",
    )
    with pytest.raises(SyncError, match="cyclic asset dependencies"):
        order_assets(dict((("/Game/MF_Wet.MF_Wet", ("material_function", document)),)))


def test_push_plans_referenced_asset_before_blueprint(sync_workspace):
    ue, env, project = sync_workspace
    schema = project.parent / ".nexus/schema" / SCHEMA_KEY / "classes.component.json"
    schema.write_text(json.dumps(dict(StaticMeshComponent=dict(
        path="/Script/Engine.StaticMeshComponent",
        props=dict(OverlayMaterial=dict(type="object", kind="object", default="None")),
    ))), encoding="utf-8")
    _, blueprint = _document(
        "/Game/A_Consumer",
        "[asset]\nParentClass=/Script/Engine.Actor\n\n[components]\n"
        "Mesh : StaticMeshComponent { OverlayMaterial=/Game/Z_Surface.Z_Surface }",
    )
    _, instance = _document(
        "/Game/Z_Surface",
        "[asset]\nParent=/Game/Materials/M_Glass.M_Glass",
        "MaterialInstanceConstant",
    )
    (project / "A_Consumer.bp.nexus").write_text(blueprint, encoding="utf-8")
    (project / "Z_Surface.mi.nexus").write_text(instance, encoding="utf-8")

    report = run_sync(ue, "push", env=env)

    assert report["error_count"] == 0, report["diagnostics"]
    assert [plan["asset"] for plan in report["plans"]] == ["/Game/Z_Surface.Z_Surface", "/Game/A_Consumer.A_Consumer"]


def _instance(project, name, parent):
    asset = f"/Game/{name}.{name}"
    _, text = _document(asset, f"[asset]\nParent={parent}", "MaterialInstanceConstant")
    (project / f"{name}.mi.nexus").write_text(text, encoding="utf-8")
    return asset


def test_failed_dependency_blocks_consumer_when_batch_continues(sync_workspace):
    ue, env, project = sync_workspace
    parent = _instance(project, "B_Parent", "/Game/Materials/M_Glass.M_Glass")
    consumer = _instance(project, "A_Consumer", parent)
    independent = _instance(project, "C_Independent", "/Game/Materials/M_Glass.M_Glass")

    def bridge(operation, payload):
        ue.fail_apply_index = 0 if payload.get("asset_path") == parent else None
        return ue(operation, payload)

    report = run_sync(bridge, "push", [consumer, parent, independent], dict(dry_run=False, stop_on_error=False), env)

    assert [entry["asset_path"] for entry in ue.applied] == [parent, independent]
    actions = dict((entry["asset"], entry["action"]) for entry in report["rows"])
    assert actions[consumer] == "blocked"
    assert actions[independent] == "pushed"
    assert report["error_count"] == 2, report


def test_unselected_new_dependency_requires_explicit_selection(sync_workspace):
    ue, env, project = sync_workspace
    parent = _instance(project, "B_Parent", "/Game/Materials/M_Glass.M_Glass")
    consumer = _instance(project, "A_Consumer", parent)

    report = run_sync(ue, "push", [consumer], dict(dry_run=False), env)

    assert report["counts"] == dict(blocked=1)
    assert "dependency_not_selected" in report["diagnostics"][0]
    assert ue.applied == []


def test_preflight_errors_stop_other_writes(sync_workspace):
    ue, env, project = sync_workspace
    invalid = _instance(project, "B_Invalid", "/Game/Materials/M_Glass.M_Glass")
    (project / "B_Invalid.mi.nexus").write_text("nexus: 88\n", encoding="utf-8")
    valid = _instance(project, "A_Valid", "/Game/Materials/M_Glass.M_Glass")

    report = run_sync(ue, "push", [invalid, valid], dict(dry_run=False), env)

    assert report["stopped"] is True
    assert report["error_count"] > 0
    assert ue.applied == []


def test_existing_mutually_referencing_blueprints_need_no_creation_order():
    a, _ = _document("/Game/A", "[asset]\nOther=/Game/B.B")
    b, _ = _document("/Game/B", "[asset]\nOther=/Game/A.A")
    documents = dict((("/Game/A.A", ("blueprint", a)), ("/Game/B.B", ("blueprint", b))))
    assert order_assets(documents, required=set()) == ["/Game/A.A", "/Game/B.B"]


def test_value_parser_ignores_prose_and_supports_wrapped_references():
    document, _ = _document(
        "/Game/A", '[asset]\nNotes="Example /Game/NotAnAsset in prose"\n'
        "ParentClass=BlueprintGeneratedClass'/Game/B.B_C'\n"
        "Items=((Value=SoftObject(/Game/C.C)),(Value=\"/Game/D.D\"))",
    )
    assert document_dependencies(document, "asset") == set(("/Game/B.B", "/Game/C.C", "/Game/D.D"))
