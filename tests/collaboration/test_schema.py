"""Schema identity, invalidation, targeted collection and pinned bindings."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.paths import schema_dir
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock, find_any_schema_lock
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.sync import run_sync
from ue_node_nexus_mcp.transcode.sync_project import SyncError, ensure_schema, resolve_context
from tests.transcode.fake_ue import SCHEMA_KEY
from tests.transcode.fixtures import material_instance_raw

from .fake_bridge import ProtocolUe

MATERIAL = "/Game/Materials/M_Glass.M_Glass"
TEXTURE = "/Game/T/T_Rock.T_Rock"
INSTANCE = "/Game/Materials/MI_Glass_Soft.MI_Glass_Soft"
NORMAL = "/Game/T/T_N.T_N"
RELOADED = "5.5.4-ffff9999"


@pytest.fixture
def project(tmp_path):
    ue = ProtocolUe()
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(tmp_path / "decoded"))
    workspace = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="A"), env=env)
    root = Path(env["UE_NEXUS_TRANSCODE_DIR"])
    return ue, env, workspace, Store(Path(workspace["files_root"]).parents[2]), root


def call(project, action, paths=None, **options):
    ue, env, workspace, _, _ = project
    return run_sync(ue, action, paths, dict(dict(dry_run=False, workspace_id=workspace["id"]), **options), env=env)


def manifest(root, key):
    return json.loads((schema_dir(root, key) / "key.json").read_text(encoding="utf-8"))


def with_instance(ue):
    """A material instance plus the parent chain and textures it points at."""
    raw = material_instance_raw()
    raw["asset_path"] = INSTANCE
    ue.assets[INSTANCE] = raw
    stub = dict(raw_version=1, asset_path=NORMAL, kind="stub", class_short="Texture2D", schema_key=SCHEMA_KEY, props=[], tags=[])
    stub["class"] = "/Script/Engine.Texture2D"
    ue.assets[NORMAL] = stub
    return raw


def instance_project(tmp_path):
    ue = ProtocolUe()
    with_instance(ue)
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(tmp_path / "decoded"))
    workspace = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="A"), env=env)
    path = Path(workspace["file_paths"][INSTANCE])
    path.write_text(path.read_text(encoding="utf-8").replace("玻璃缩放 = 500", "玻璃缩放 = 250"), encoding="utf-8")
    run_sync(ue, "commit", options=dict(workspace_id=workspace["id"], dry_run=False, all=True, message="tune"), env=env)
    return ue, env, workspace


def test_a_rebuilt_plugin_publishes_a_new_catalog_beside_the_old_one(project):
    ue, env, workspace, store, root = project
    before = manifest(root, SCHEMA_KEY)
    assert before["environment"]["schema_key"] == SCHEMA_KEY

    ue.schema_key = RELOADED
    result = run_sync(ue, "schema", options=dict(refresh=True), env=env)
    assert result["schema_key"] == RELOADED and result["freshness"] == "observed"
    current = manifest(root, RELOADED)
    assert current["generation"] == 1 and current["key"] == RELOADED
    # The old catalog stays readable: snapshots recorded under it still resolve.
    assert manifest(root, SCHEMA_KEY) == before
    assert find_any_schema_lock(root).key == RELOADED
    assert current["content_hash"] != before["content_hash"]


def test_an_export_stamped_by_a_reloaded_editor_is_refused(project):
    ue, env, workspace, store, root = project

    def reload_after_handshake(operation, payload, **kwargs):
        response = ue.call(operation, payload, **kwargs)
        if operation == "bridge_capabilities_get":
            ue.schema_key = RELOADED
        return response

    with pytest.raises(SyncError) as failure:
        run_sync(reload_after_handshake, "fetch", options=dict(workspace_id=workspace["id"], dry_run=False), env=env)
    assert failure.value.code == "schema_stale"
    assert failure.value.details["asset"] in (MATERIAL, TEXTURE)
    assert store.ref("refs/ue/observed")


def test_a_catalog_and_a_repository_both_refuse_another_project(project):
    ue, env, workspace, store, root = project
    context = resolve_context(ue, env=env)
    assert ensure_schema(ue, context).key == SCHEMA_KEY
    context.project_file = "D:/UE/Other/Shadetest.uproject"
    with pytest.raises(SyncError) as failure:
        ensure_schema(ue, context)
    assert failure.value.code == "schema_project_mismatch"

    with pytest.raises(SyncError) as repository_failure:
        Store(store.root, "D:/UE/Other/Shadetest.uproject")
    assert repository_failure.value.code == "project_mismatch"


def test_historical_schema_answers_from_the_recorded_snapshots(project):
    ue, env, workspace, store, root = project
    result = run_sync(ue, "schema", options=dict(revision="HEAD", workspace_id=workspace["id"]), env=env)
    assert result["freshness"] == "historical"
    names = set(row["definition"]["name"] for row in result["rows"])
    assert "MaterialExpressionAdd" in names and "Material" in names
    assert all(row["definition"]["schema_key"] == SCHEMA_KEY for row in result["rows"])

    filtered = run_sync(ue, "schema", options=dict(revision="HEAD", workspace_id=workspace["id"], category="asset"), env=env)
    assert set(row["definition"]["category"] for row in filtered["rows"]) == set(["asset"])
    queried = run_sync(ue, "schema", options=dict(revision="HEAD", workspace_id=workspace["id"], query="Constant"), env=env)
    assert [row["definition"]["name"] for row in queried["rows"]] == ["MaterialExpressionConstant"]

    # A rebuilt plugin does not rewrite what the recorded commits were validated against.
    ue.schema_key = RELOADED
    run_sync(ue, "schema", options=dict(refresh=True), env=env)
    again = run_sync(ue, "schema", options=dict(revision="HEAD", workspace_id=workspace["id"]), env=env)
    assert all(row["definition"]["schema_key"] == SCHEMA_KEY for row in again["rows"])
    assert again["revision"] == result["revision"]


def test_dynamic_pin_classes_are_marked_as_needing_a_target_context(project):
    ue, env, workspace, store, root = project
    catalog = run_sync(ue, "schema", options=dict(category="blueprint", query="CallFunction", details=True), env=env)
    assert catalog["freshness"] == "unknown"
    row = next(item for item in catalog["rows"] if item["name"] == "K2Node_CallFunction")
    assert row["coverage"] == "context_required"
    assert row["definition"]["dynamic_pins"] is True
    assert "schema(target=" in row["definition"]["syntax"]["context"]
    # The gap is answered with the call that closes it, not just named.
    pins = catalog["coverage"]["blueprint_pins"]
    assert pins["state"] == "context_required"
    assert pins["resolve_with"]["action"] == "schema"
    assert "target" in pins["resolve_with"]["options"]
    assert "pins" in pins["resolve_with"]["returns"]


def test_a_target_context_is_collected_once_and_then_served_from_the_catalog(project):
    ue, env, workspace, store, root = project
    conditions = dict(function="PrintString", target_class="Actor")
    observed = run_sync(ue, "schema", options=dict(target=MATERIAL, context=conditions), env=env)["context"]
    assert observed["freshness"] == "observed" and observed["conditions_evaluated"] is False
    assert observed["coverage"] == "context_required"
    assert Path(observed["file"]).is_file()

    cached = run_sync(ue, "schema", options=dict(target=MATERIAL, context=conditions, refresh=False), env=env)
    assert cached["freshness"] == "unknown"
    assert cached["context"]["interface_hash"] == observed["interface_hash"]
    assert cached["context"]["context_hash"] == observed["context_hash"]

    with pytest.raises(SyncError) as failure:
        run_sync(ue, "schema", options=dict(target=MATERIAL, context=dict(function="Other"), refresh=False), env=env)
    assert failure.value.code == "context_required"
    with pytest.raises(SyncError) as unknown:
        run_sync(ue, "schema", options=dict(target="/Game/Nope.Nope"), env=env)
    assert unknown.value.code == "context_unavailable"


def test_a_material_instance_carries_its_parent_in_the_publication_read_set(tmp_path):
    ue, env, workspace = instance_project(tmp_path)
    result = run_sync(ue, "push", [INSTANCE], dict(workspace_id=workspace["id"], dry_run=False), env=env)
    assert result["status"] == "published", result
    request = next(item for item in ue.applied if item["asset_path"] == INSTANCE)
    read_set = dict((row["asset_path"], row) for row in request["read_set"])
    assert sorted(read_set) == [MATERIAL, NORMAL]
    assert read_set[MATERIAL]["expected_revision"] == ue.stamped(ue.assets[MATERIAL])["live_revision"]
    assert read_set[MATERIAL]["kind"] == "material" and read_set[NORMAL]["kind"] == "stub"


def test_a_parent_edited_during_publication_rejects_the_instance(tmp_path):
    ue, env, workspace = instance_project(tmp_path)
    ue.before_apply = lambda: ue.assets[MATERIAL]["props"][0].update(value="BLEND_Opaque")
    result = run_sync(ue, "push", [INSTANCE], dict(workspace_id=workspace["id"], dry_run=False), env=env)
    assert result["errors"][INSTANCE]["code"] == "stale_target", result
    # The parent moved under it: nothing was written, so no receipt exists to publish.
    assert ue.applied == [] and ue.receipts == dict()


def test_targeted_function_details_extend_the_catalog_in_place(project):
    ue, env, workspace, store, root = project
    before = manifest(root, SCHEMA_KEY)
    result = run_sync(ue, "schema", options=dict(function="KismetSystemLibrary.PrintString"), env=env)
    row = next(item for item in result["rows"] if item["name"] == "KismetSystemLibrary.PrintString")
    assert row["category"] == "blueprint"
    assert [param["name"] for param in row["definition"]["params"]] == ["InString", "Duration"]
    after = manifest(root, SCHEMA_KEY)
    assert after["generation"] == before["generation"] + 1
    assert after["tables"]["material_expression"] == before["tables"]["material_expression"]
    assert "functions" not in before["tables"] or "KismetSystemLibrary.PrintString" not in before["tables"]["functions"]

    with pytest.raises(SyncError) as failure:
        run_sync(ue, "schema", options=dict(function="Nope.Missing"), env=env)
    assert failure.value.code == "schema_unknown"
    assert manifest(root, SCHEMA_KEY)["generation"] == after["generation"]


def test_an_interrupted_collection_leaves_the_catalog_and_its_directory_clean(project):
    ue, env, workspace, store, root = project
    before = manifest(root, SCHEMA_KEY)

    def aborted(payload):
        out = Path(payload["out_dir"])
        out.mkdir(parents=True, exist_ok=True)
        (out / "key.json").write_text(json.dumps(dict(key="half-written")), encoding="utf-8")
        return dict(__error__=dict(code="schema_provider_failed", message="reflection aborted"))

    ue.op_schema_export = aborted
    with pytest.raises(SyncError) as failure:
        run_sync(ue, "schema", options=dict(refresh=True), env=env)
    assert failure.value.code == "schema_provider_failed"
    assert manifest(root, SCHEMA_KEY) == before
    assert [path.name for path in (root / ".nexus" / "schema").iterdir()] == [SCHEMA_KEY]
    assert find_any_schema_lock(root).key == SCHEMA_KEY
    del ue.op_schema_export
    assert run_sync(ue, "schema", options=dict(refresh=True), env=env)["schema_key"] == SCHEMA_KEY


def test_a_preview_pins_every_schema_record_it_used(project):
    ue, env, workspace, store, root = project
    preview = run_sync(ue, "push", options=dict(workspace_id=workspace["id"]), env=env)
    record = store.record("proposal", preview["proposal_id"])
    pinned = set((item["family"], item["name"]) for item in record["schema"]["entries"])
    assert ("asset", "Material") in pinned
    assert ("material_expression", "MaterialExpressionAdd") in pinned
    assert record["schema"]["schema_key"] == SCHEMA_KEY

    lock = SchemaLock(schema_dir(root, SCHEMA_KEY), SCHEMA_KEY)
    changed = dict(lock.info()["tables"]["material_expression"])
    record_file = lock.directory / changed["MaterialExpressionAdd"]["file"]
    definition = json.loads(record_file.read_text(encoding="utf-8"))
    definition["props"]["ConstA"]["default"] = "7.0"
    publish(lock.directory, SCHEMA_KEY, dict(material_expression=dict(MaterialExpressionAdd=definition)), incremental=True)
    with pytest.raises(SyncError) as failure:
        run_sync(ue, "push", options=dict(workspace_id=workspace["id"], dry_run=False, proposal_id=preview["proposal_id"]), env=env)
    assert failure.value.code == "schema_stale"
    assert failure.value.details["name"] == "MaterialExpressionAdd"


def test_module_records_stay_explicit_about_missing_metadata(tmp_path):
    directory = tmp_path / ".nexus" / "schema" / "key"
    publish(directory, "key", dict(niagara_modules={
        "/Game/VFX/Modules/Gravity.Gravity": dict(short="Gravity", inputs=[dict(name="Strength", type="float", default="980")]),
        "/Game/Other/Gravity.Gravity": dict(short="Gravity", inputs=[]),
        "/Game/VFX/Modules/Drag.Drag": dict(short="Drag", inputs=[], conditions=dict(static_switch="bUseDrag")),
    }))
    lock = SchemaLock(directory, "key")
    path, record, ambiguous = lock.niagara_module("/Game/VFX/Modules/Drag.Drag")
    assert record["conditions"] == dict(static_switch="bUseDrag")
    assert record["coverage"] == "reflected"
    assert record["plugin"] == "metadata_not_provided"
    assert record["module"] == "project"
    assert ambiguous == [] and path == "/Game/VFX/Modules/Drag.Drag"

    _, without, _ = lock.niagara_module("/Game/VFX/Modules/Gravity.Gravity")
    assert without["conditions"] == "metadata_not_provided"
    assert without["inputs"][0]["name"] == "Strength"
    with pytest.raises(SyncError) as failure:
        lock.niagara_module("Gravity")
    assert failure.value.code == "schema_ambiguous"
    assert sorted(failure.value.details["candidates"]) == ["/Game/Other/Gravity.Gravity", "/Game/VFX/Modules/Gravity.Gravity"]
