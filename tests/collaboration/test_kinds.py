"""Per-kind publication planning, dependency order and function refresh."""

from __future__ import annotations

from copy import deepcopy
from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.collaboration.apply import planning
from ue_node_nexus_mcp.transcode.collaboration.history import History
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import from_raw
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.collaboration.workspace import checkout
from ue_node_nexus_mcp.transcode.collaboration.workspace.files import capture_files
from ue_node_nexus_mcp.transcode.sync import run_sync
from ue_node_nexus_mcp.transcode.sync_project import SyncError
from tests.transcode.fake_ue import SCHEMA_KEY
from tests.transcode.fixtures import (blueprint_raw, material_function_raw, material_instance_raw,
                                      material_raw, niagara_raw)

from .fake_bridge import ProtocolUe

FUNCTION = "/Game/WaterStains/Functions/MF_WS_S.MF_WS_S"
MATERIAL = "/Game/Materials/M_Glass.M_Glass"
TEXTURE = "/Game/T/T_Rock.T_Rock"
SECOND = "/Game/Materials/M_Rim.M_Rim"
EMITTER = "/Game/VFX/Emitters/E_Base.E_Base"


def texture_raw():
    raw = dict(raw_version=1, asset_path=TEXTURE, kind="stub", class_short="Texture2D",
               schema_key=SCHEMA_KEY, props=[], tags=[])
    raw["class"] = "/Script/Engine.Texture2D"
    return raw


def emitter_raw():
    raw = niagara_raw()
    raw.update(asset_path=EMITTER, kind="niagara_emitter", class_short="NiagaraEmitter")
    raw["class"] = "/Script/Niagara.NiagaraEmitter"
    return raw

WRITABLE = [
    (material_raw, "material", "BlendMode = BLEND_Translucent", "BlendMode = BLEND_Opaque", ["set_asset_prop"], False),
    (material_function_raw, "material_function", "SortPriority=0", "SortPriority=5", ["set_node_param"], True),
    (material_instance_raw, "material_instance", "玻璃缩放 = 500", "玻璃缩放 = 250", ["mi_set_param"], False),
    (blueprint_raw, "blueprint", "Health  : float = 100", "Health  : float = 55", ["bp_variable_set"], False),
    (niagara_raw, "niagara_system", "Lifetime=2", "Lifetime=4", ["ns_module_input_set"], False),
]


def repository(tmp_path, *factories):
    store = Store(tmp_path)
    history = History(store)
    snapshots = dict()
    for factory in factories:
        snapshot = from_raw(factory())
        snapshots[snapshot["raw"]["asset_path"]] = snapshot
    entries = dict((asset, store.objects.put("snapshot", value)) for asset, value in snapshots.items())
    root = history.create(history.tree(entries), [], "import")
    store.move("refs/heads/main", root, None)
    return store, root, checkout(store, root), snapshots


def retext(workspace, asset, old, new):
    path = workspace.root / workspace.state["files"][asset]
    text = path.read_text(encoding="utf-8")
    assert old in text, (asset, text)
    path.write_text(text.replace(old, new), encoding="utf-8")


def candidate_of(workspace, asset):
    entries, _, _ = capture_files(workspace)
    return workspace.store.objects.data(entries[asset], "snapshot")


@pytest.mark.parametrize("factory,kind,old,new,verbs,interface", WRITABLE, ids=[item[1] for item in WRITABLE])
def test_every_writable_kind_plans_its_own_verbs(tmp_path, factory, kind, old, new, verbs, interface):
    store, root, workspace, snapshots = repository(tmp_path, factory)
    asset, current = next(iter(snapshots.items()))
    retext(workspace, asset, old, new)
    item = planning.unit(workspace, asset, candidate_of(workspace, asset), current, current["raw"], dict())
    assert item["kind"] == kind
    assert item["empty"] is False
    assert sorted(set(verb["op"] for verb in item["payload"]["plan"])) == verbs
    assert bool(item.get("interface_changed")) is interface
    assert item["payload"]["asset_path"] == asset


def test_an_unchanged_asset_produces_no_execution(tmp_path):
    store, root, workspace, snapshots = repository(tmp_path, material_raw)
    asset, current = next(iter(snapshots.items()))
    item = planning.unit(workspace, asset, candidate_of(workspace, asset), current, current["raw"], dict())
    assert item["empty"] is True and item["payload"]["plan"] == []


def test_a_dirty_editor_asset_is_published_even_without_a_semantic_change(tmp_path):
    store, root, workspace, snapshots = repository(tmp_path, material_raw)
    asset, current = next(iter(snapshots.items()))
    item = planning.unit(workspace, asset, candidate_of(workspace, asset), current, dict(current["raw"], dirty=True), dict())
    assert item["empty"] is False


@pytest.mark.parametrize("factory,kind", [(texture_raw, "stub"), (emitter_raw, "niagara_emitter")], ids=["stub", "emitter"])
def test_inspectable_kinds_refuse_every_write(tmp_path, factory, kind):
    store, root, workspace, snapshots = repository(tmp_path, factory)
    asset, current = next(iter(snapshots.items()))
    assert current["semantic"]["kind"] == kind
    changed = deepcopy(current)
    changed["semantic_hash"] = "a different semantic state"
    with pytest.raises(SyncError) as failure:
        planning.unit(workspace, asset, changed, current, current["raw"], dict())
    assert failure.value.code == "read_only"
    assert failure.value.details == dict(asset=asset, kind=kind)
    assert planning.unit(workspace, asset, current, current, current["raw"], dict())["empty"] is True


def test_deletion_needs_permission_and_cannot_orphan_a_reference(tmp_path):
    store, root, workspace, snapshots = repository(tmp_path, material_raw, material_function_raw, texture_raw)
    current = snapshots[FUNCTION]
    with pytest.raises(SyncError) as failure:
        planning.unit(workspace, FUNCTION, None, current, current["raw"], dict())
    assert failure.value.code == "delete_not_allowed"
    removal = planning.unit(workspace, FUNCTION, None, current, current["raw"], dict(allow_delete=True))
    assert removal["payload"]["delete_asset"] is True and removal["risky"] is True

    history = workspace.history
    entries = history.entries(root)
    observation = dict(commit=root, raw=dict((asset, snapshots[asset]["raw"]) for asset in entries), revisions=dict())
    kept = dict(entries)
    kept.pop(TEXTURE, None)
    merged = dict(candidate=history.tree(kept), conflicts=[])
    batch = planning.preflight(workspace, merged, observation, sorted(entries), dict(allow_delete=True))
    assert batch["errors"][TEXTURE]["code"] == "referenced_asset"
    assert batch["errors"][TEXTURE]["consumer"] in (MATERIAL, FUNCTION)


def unobserved(workspace) -> dict:
    """UE holds none of these assets yet, so every one of them is a creation."""
    return dict(commit=workspace.history.create(workspace.history.tree(dict()), [], "empty"), raw=dict(), revisions=dict())


def test_missing_and_unselected_dependencies_are_reported_apart(tmp_path):
    store, root, workspace, snapshots = repository(tmp_path, material_raw)
    history = workspace.history
    observation = unobserved(workspace)
    tree = history.commit(root)["tree"]
    batch = planning.preflight(workspace, dict(candidate=tree, ours=tree, conflicts=[]), observation, [MATERIAL], dict())
    assert batch["errors"][MATERIAL]["code"] == "dependency_missing"
    assert batch["errors"][MATERIAL]["details"]["assets"] == [TEXTURE]

    texture = deepcopy(snapshots[MATERIAL])
    texture["semantic"] = dict(texture["semantic"], kind="stub", sections=dict(), header=dict(texture["semantic"]["header"], asset=TEXTURE))
    candidate = history.entries(root)
    candidate[TEXTURE] = store.objects.put("snapshot", texture)
    tree = history.tree(candidate)
    batch = planning.preflight(workspace, dict(candidate=tree, ours=tree, conflicts=[]), observation, [MATERIAL], dict())
    assert batch["errors"][MATERIAL]["code"] == "dependency_not_selected"
    assert batch["errors"][MATERIAL]["details"]["assets"] == [TEXTURE]


def test_blueprint_class_method_dependency_survives_collaboration_preflight(tmp_path):
    def self_call_blueprint_raw():
        raw = blueprint_raw()
        node = raw["blueprint"]["graphs"][0]["nodes"][2]
        node["config"] = {
            "function_owner": "/Game/Blueprints/BP_Door.BP_Door_C",
            "function_name": "TakeDamage",
            "self_context": "true",
        }
        mesh = raw["blueprint"]["components"][1]
        mesh["props"] = [prop for prop in mesh["props"] if prop["name"] != "StaticMesh"]
        return raw

    store, root, workspace, snapshots = repository(tmp_path, self_call_blueprint_raw)
    asset = next(iter(snapshots))
    target = "/Game/Blueprints/BP_Door.BP_Door_C.TakeDamage"
    retext(workspace, asset, "self.TakeDamage", target)

    candidate = candidate_of(workspace, asset)
    entries = workspace.history.entries(root)
    entries[asset] = store.objects.put("snapshot", candidate)
    tree = workspace.history.tree(entries)
    observation = dict(commit=root, raw={asset: snapshots[asset]["raw"]}, revisions=dict())
    batch = planning.preflight(
        workspace, dict(candidate=tree, ours=tree, conflicts=[]), observation, [asset], dict(allow_delete=True))

    assert batch["errors"] == {}
    assert batch["units"][asset]["dependencies"] == []


def test_a_conflicted_asset_blocks_only_itself_and_its_consumers(tmp_path):
    store, root, workspace, snapshots = repository(tmp_path, material_raw, material_function_raw, texture_raw)
    history = workspace.history
    observation = unobserved(workspace)
    tree = history.commit(root)["tree"]
    merged = dict(candidate=tree, ours=tree, conflicts=[dict(asset=TEXTURE)])
    batch = planning.preflight(workspace, merged, observation, sorted(history.entries(root)), dict())
    assert batch["errors"][TEXTURE]["code"] == "conflict"
    assert batch["errors"][MATERIAL]["code"] == "dependency_failed"
    assert batch["errors"][FUNCTION]["code"] == "dependency_failed"


def test_an_untouched_asset_is_not_planned_when_the_project_is_published(tmp_path):
    store, root, workspace, snapshots = repository(tmp_path, material_raw, material_function_raw, texture_raw)
    history = workspace.history
    tree = history.commit(root)["tree"]
    observation = dict(commit=root, raw=dict((asset, value["raw"]) for asset, value in snapshots.items()), revisions=dict())
    batch = planning.preflight(workspace, dict(candidate=tree, ours=tree, conflicts=[]), observation, sorted(history.entries(root)), dict())
    assert batch == dict(units=dict(), order=[], errors=dict())


@pytest.fixture
def project(tmp_path):
    ue = ProtocolUe()
    ue.assets[FUNCTION] = material_function_raw()
    second = material_raw()
    second["asset_path"] = SECOND
    ue.assets[SECOND] = second
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(tmp_path / "decoded"))
    workspace = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="A"), env=env)
    return ue, env, workspace, Store(Path(workspace["files_root"]).parents[2])


def call(project, action, paths=None, **options):
    ue, env, workspace, _ = project
    return run_sync(ue, action, paths, dict(dry_run=False, workspace_id=workspace["id"], **options), env=env)


def edit(project, asset, old, new):
    path = Path(project[2]["file_paths"][asset])
    text = path.read_text(encoding="utf-8")
    assert old in text, (asset, text)
    path.write_text(text.replace(old, new), encoding="utf-8")


def test_a_function_interface_change_refreshes_its_callers(project):
    ue, env, workspace, store = project
    ue.referencers[FUNCTION] = [MATERIAL.split(".", 1)[0], SECOND.split(".", 1)[0]]
    edit(project, FUNCTION, "SortPriority=0", "SortPriority=5")
    call(project, "commit", all=True, message="interface")
    result = call(project, "push", [FUNCTION])
    assert result["status"] == "published", result
    assert ue.assets[MATERIAL].get("refreshed") == [FUNCTION]
    assert ue.assets[SECOND].get("refreshed") == [FUNCTION]
    applied = [item for item in ue.applied if item["plan"] and item["plan"][0]["op"] == "refresh_function_calls"]
    assert sorted(item["asset_path"] for item in applied) == sorted([MATERIAL, SECOND])
    published = store.ref("refs/ue/published")
    assert store.record("apply", applied[0]["apply_id"])["phase"] == "completed"
    assert published and store.record("integration", workspace["id"] + ":" + MATERIAL) is None


def test_an_incomplete_referencer_set_stops_the_refresh(project, monkeypatch):
    ue, env, workspace, store = project
    ue.referencers[FUNCTION] = [MATERIAL.split(".", 1)[0]]
    original = ue.op_asset_referencers_get
    monkeypatch.setattr(ue, "op_asset_referencers_get", lambda payload: dict(original(payload), truncated=True))
    edit(project, FUNCTION, "SortPriority=0", "SortPriority=5")
    call(project, "commit", all=True, message="interface")
    result = call(project, "push", [FUNCTION])
    assert result["errors"][FUNCTION]["code"] == "referencers_incomplete", result
    assert "refreshed" not in ue.assets[MATERIAL]


def test_a_deleted_asset_can_be_restored_from_history(project):
    ue, env, workspace, store = project
    before = store.ref("refs/ue/observed")
    Path(workspace["file_paths"][SECOND]).unlink()
    call(project, "commit", all=True, message="remove the second material", delete=True, allow_delete=True)
    removed = call(project, "push", [SECOND], allow_delete=True)
    assert removed["status"] == "published", removed
    assert SECOND not in ue.assets
    assert SECOND not in store.objects.data(store.objects.data(store.ref("refs/ue/observed"), "commit")["tree"], "tree")

    call(project, "restore", [SECOND], revision=before)
    call(project, "commit", all=True, message="bring it back")
    restored = call(project, "push", [SECOND])
    assert restored["status"] == "published", restored
    assert SECOND in ue.assets
    assert next(item for item in ue.applied if item["asset_path"] == SECOND and item.get("create"))
