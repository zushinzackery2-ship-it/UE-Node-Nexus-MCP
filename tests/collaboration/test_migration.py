"""Legacy import, sparse worktrees, repeated resolution and history queries."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.collaboration.history import query
from ue_node_nexus_mcp.transcode.collaboration.merge.sessions import Sessions
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import from_raw, text_of
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.collaboration.workspace import Workspace, checkout
from ue_node_nexus_mcp.transcode.collaboration.workspace.commands import run as command
from ue_node_nexus_mcp.transcode.paths import base_path, pending_dir, state_path, text_path
from ue_node_nexus_mcp.transcode.sync import run_sync
from ue_node_nexus_mcp.transcode.sync_project import SyncError
from tests.scene.fixtures import snapshot as scene_raw
from tests.transcode.fixtures import material_raw

from .fake_bridge import ProtocolUe
from .helpers import ASSET, commit, repository, values

MATERIAL = "/Game/Materials/M_Glass.M_Glass"
SECOND = "/Game/Materials/M_Rim.M_Rim"
SCENE = "/Game/Maps/World#Group"


def write(path: Path, data: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(data, encoding="utf-8")


@pytest.fixture
def legacy(tmp_path):
    """A single-mirror project with a recorded base, a draft and push evidence."""
    ue = ProtocolUe()
    root = tmp_path / "decoded"
    project = root / "Shadetest"
    raw = ue.stamped(ue.assets[MATERIAL])
    write(base_path(project, MATERIAL), json.dumps(raw))
    file = text_path(project, MATERIAL, "material")
    write(file, text_of(from_raw(raw)).replace("BLEND_Translucent", "BLEND_Opaque"))
    write(state_path(project), json.dumps(dict(version=1, assets={MATERIAL: dict(
        kind="material", file=file.relative_to(project).as_posix(), base_hash="h1", ue_saved_hash="h1")})))
    evidence = pending_dir(project) / "Materials" / "M_Glass.push.json"
    write(evidence, json.dumps(dict(asset_path=MATERIAL, errors=["compile failed"], local_text="draft")))
    scene = from_raw(scene_raw())
    write(project / "Scenes" / "Maps" / "World" / "Group.scene.nexus", text_of(scene))
    return ue, dict(UE_NEXUS_TRANSCODE_DIR=str(root)), project, evidence


def test_a_first_checkout_previews_the_entire_import(legacy):
    ue, env, project, evidence = legacy
    preview = run_sync(ue, "checkout", options=dict(dry_run=True), env=env)
    survey = preview["migration"]
    assert survey["assets"] == [MATERIAL]
    assert survey["unregistered"] == [SCENE]
    assert survey["missing_bases"] == []
    assert survey["evidence"] == [evidence.relative_to(project).as_posix()]
    assert survey["local_files"] == 2
    assert not (project / ".nexus" / "collaboration" / "active.json").exists()


def test_the_import_keeps_drafts_evidence_and_unregistered_scenes(legacy):
    ue, env, project, evidence = legacy
    created = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="A"), env=env)
    store = Store(Path(created["files_root"]).parents[2])
    imported = Workspace(store, "imported")
    status = imported.status()
    assert sorted(status["unstaged"]) == sorted([MATERIAL, SCENE])
    assert "BLEND_Opaque" in (imported.root / imported.state["files"][MATERIAL]).read_text(encoding="utf-8")
    record = store.record("migration", "legacy")
    assert record["phase"] == "completed"
    keys = list(record["evidence"])
    assert keys == [evidence.relative_to(project).as_posix()]
    from ue_node_nexus_mcp.transcode.collaboration.workspace.projection import content

    assert json.loads(content(store, record["evidence"][keys[0]]))["errors"] == ["compile failed"]
    assert store.ref("refs/ue/observed")
    # The recorded base, not the draft, is what the imported history contains.
    head = imported.history.entries(imported.state["head"])
    assert "BLEND_Translucent" in text_of(store.objects.data(head[MATERIAL], "snapshot"))


def test_a_missing_base_stops_the_import_with_the_exact_asset(legacy):
    ue, env, project, _ = legacy
    base_path(project, MATERIAL).unlink()
    survey = run_sync(ue, "checkout", options=dict(dry_run=True), env=env)["migration"]
    assert survey["missing_bases"] == [MATERIAL]
    with pytest.raises(SyncError) as failure:
        run_sync(ue, "checkout", options=dict(dry_run=False), env=env)
    assert failure.value.code == "base_missing"
    assert failure.value.details["asset"] == MATERIAL


@pytest.fixture
def project(tmp_path):
    ue = ProtocolUe()
    second = material_raw()
    second["asset_path"] = SECOND
    ue.assets[SECOND] = second
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(tmp_path / "decoded"))
    sparse = run_sync(ue, "checkout", [MATERIAL], dict(dry_run=False, agent_id="A"), env=env)
    full = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="B"), env=env)
    return ue, env, sparse, full, Store(Path(full["files_root"]).parents[2])


def call(project, action, workspace=None, paths=None, **options):
    ue, env = project[0], project[1]
    if workspace:
        options["workspace_id"] = workspace["id"]
    return run_sync(ue, action, paths, dict(dry_run=False, **options), env=env)


def test_a_sparse_worktree_ignores_other_assets_until_one_is_restored(project):
    ue, env, sparse, full, store = project
    assert list(store.record("workspace", sparse["id"])["files"]) == [MATERIAL]
    assert store.record("workspace", sparse["id"])["sparse"] is True
    path = Path(full["file_paths"][SECOND])
    path.write_text(path.read_text(encoding="utf-8").replace("Constant(R=0.000001)", "Constant(R=0.42)"), encoding="utf-8")
    call(project, "commit", full, all=True, message="B changes the second asset")
    assert call(project, "push", full)["status"] == "published"

    pulled = call(project, "pull", sparse)
    assert pulled["status"] == "completed", pulled
    root = Path(sparse["files_root"])
    assert sorted(item.name for item in root.rglob("*.nexus")) == ["M_Glass.mat.nexus"]

    restored = call(project, "restore", sparse, [SECOND], revision="HEAD")
    assert restored["unstaged"] == [] and restored["staged"] == []
    files = store.record("workspace", sparse["id"])["files"]
    assert SECOND in files
    assert "R=0.42" in (root / files[SECOND]).read_text(encoding="utf-8")
    assert store.record("workspace", sparse["id"])["sparse"] is True


def test_unstaging_a_registered_deletion_restores_the_indexed_version(tmp_path):
    store, root = repository(tmp_path)
    workspace = checkout(store, root)
    added = workspace.root / "Test" / "Extra.asset.nexus"
    added.parent.mkdir(parents=True, exist_ok=True)
    added.write_text((workspace.root / workspace.state["files"][ASSET]).read_text(encoding="utf-8")
                     .replace("/Game/Test/Data", "/Game/Test/Extra"), encoding="utf-8")
    workspace.stage()
    assert sorted(workspace.history.entries(workspace.state["index"])) == ["/Game/Test/Data.Data", "/Game/Test/Extra.Extra"]
    workspace.unstage(["/Game/Test/Extra.Extra"])
    assert list(workspace.history.entries(workspace.state["index"])) == [ASSET]

    (workspace.root / workspace.state["files"][ASSET]).unlink()
    workspace.stage(delete=True)
    assert ASSET not in workspace.history.entries(workspace.state["index"])
    workspace.unstage([ASSET])
    assert ASSET in workspace.history.entries(workspace.state["index"])
    assert workspace.status()["local_deleted"] == [ASSET]


def test_a_stuck_projection_blocks_local_edits_until_it_is_recovered(tmp_path):
    from ue_node_nexus_mcp.transcode.collaboration.workspace.projection import execute, prepare

    store, root = repository(tmp_path)
    workspace = checkout(store, root)
    relative = workspace.state["files"][ASSET]
    journal = prepare(workspace, dict(workspace.state), dict([(relative, b"replacement")]), "test")
    (workspace.root / relative).write_bytes(b"a newer local draft")
    with pytest.raises(SyncError):
        execute(workspace, journal)
    from ue_node_nexus_mcp.transcode.collaboration.service import ensure_idle

    with pytest.raises(SyncError) as failure:
        ensure_idle(workspace, "commit")
    assert failure.value.code == "projection_pending"
    assert failure.value.details["projection_ids"] == [journal["id"]]
    journal = store.record("projection", journal["id"])
    (workspace.root / relative).write_bytes(b"replacement")
    execute(workspace, journal)
    ensure_idle(workspace, "commit")


def material_repository(tmp_path):
    from ue_node_nexus_mcp.transcode.collaboration.history import History

    store = Store(tmp_path)
    history = History(store)
    snapshot = from_raw(material_raw())
    asset = snapshot["raw"]["asset_path"]
    root = history.create(history.tree(dict([(asset, store.objects.put("snapshot", snapshot))])), [], "import")
    store.move("refs/heads/main", root, None)
    return store, root, asset


def rewrite(workspace, asset, old, new):
    path = workspace.root / workspace.state["files"][asset]
    text = path.read_text(encoding="utf-8")
    assert old in text, text
    path.write_text(text.replace(old, new), encoding="utf-8")
    return workspace.commit(new, all_files=True)["commit_id"]


def test_a_resolution_that_breaks_a_constraint_reopens_the_session(tmp_path):
    from ue_node_nexus_mcp.transcode.collaboration.history.integrate import integrate

    store, root, asset = material_repository(tmp_path)
    a, b = checkout(store, root), checkout(store, root)
    source = rewrite(a, asset, "constant  : Constant(R=0.000001)", "constant  : Constant(R=0.5)")
    rewrite(b, asset, "constant  : Constant(R=0.000001)", "constant  : Multiply(ConstA=2)")
    report = integrate(b, "merge", source)
    assert [item["conflict_type"] for item in report["conflicts"]] == ["type-conflict"], report
    sessions = Sessions(Workspace(store, b.state["id"]))

    collision = sessions.resolve(report["merge_id"], report["conflicts"][0]["conflict_id"], dict(choice="rename", name="add"))
    assert collision["status"] == "conflict", collision
    assert collision["conflicts"][0]["conflict_type"] == "name-collision"
    assert collision["conflicts"][0]["allowed_resolutions"] == ["custom", "delete", "rename"]

    ready = sessions.resolve(report["merge_id"], collision["conflicts"][0]["conflict_id"], dict(choice="rename", name="scale"))
    assert ready["status"] == "ready", ready
    sessions.finish(report["merge_id"])
    from ue_node_nexus_mcp.transcode.collaboration.semantic.validation import validate

    workspace = sessions.workspace
    snapshot = store.objects.data(workspace.history.entries(workspace.state["head"])[asset], "snapshot")
    graph = snapshot["semantic"]["sections"]["graph:"]
    aliases = sorted(entity["alias"] for entity in graph["entities"].values())
    assert aliases == sorted(set(aliases)) and "scale" in aliases
    assert validate(snapshot) == []
    endpoints = set(key for link in graph["links"].values() for key in (link["src"], link["dst"]))
    assert all(key in graph["entities"] for key in endpoints if not key.startswith("@"))
    assert store.record("session", report["merge_id"])["status"] == "completed"


def test_a_custom_resolution_must_match_the_conflicting_field_shape(tmp_path):
    from ue_node_nexus_mcp.transcode.collaboration.history.integrate import integrate

    store, root = repository(tmp_path)
    a, b = checkout(store, root), checkout(store, root)
    source = commit(a, "A", "1")
    commit(b, "A", "2")
    report = integrate(b, "merge", source)
    sessions = Sessions(Workspace(store, b.state["id"]))
    conflict = report["conflicts"][0]
    with pytest.raises(SyncError) as failure:
        sessions.resolve(report["merge_id"], conflict["conflict_id"], dict(choice="custom", value="7"))
    assert failure.value.code == "resolution_type"
    assert store.record("session", report["merge_id"])["status"] == "conflict"
    ready = sessions.resolve(report["merge_id"], conflict["conflict_id"],
                             dict(choice="custom", value=dict(type="float", state="explicit", value="7")))
    assert ready["status"] == "ready", ready
    sessions.finish(report["merge_id"])
    assert values(sessions.workspace)["A"] == "7"


def test_log_filters_by_asset_author_entity_and_time(tmp_path):
    store, root = repository(tmp_path)
    workspace = checkout(store, root)
    history = workspace.history
    first = commit(workspace, "A", "1")
    second = commit(workspace, "B", "2")
    third = commit(workspace, "A", "3")
    every = query.log(history, third, limit=50)
    assert [item["commit_id"] for item in every["commits"]] == [third, second, first, root]

    only_b = query.log(history, third, entity="B")
    assert [item["commit_id"] for item in only_b["commits"]] == [second, root]
    assert query.log(history, third, asset=ASSET, entity="A")["commits"][0]["commit_id"] == third
    assert query.log(history, third, asset="/Game/Missing.Missing")["commits"] == []
    assert query.log(history, third, author="nobody")["commits"] == []

    boundary = history.commit(second)["time"]
    assert [item["commit_id"] for item in query.log(history, third, since=boundary)["commits"]] == [third, second]
    assert [item["commit_id"] for item in query.log(history, third, until=boundary)["commits"]] == [second, first, root]
    assert query.log(history, third, since=boundary, until=boundary)["commits"][0]["commit_id"] == second


def test_log_and_show_page_on_exact_boundaries(tmp_path):
    store, root = repository(tmp_path)
    workspace = checkout(store, root)
    identifiers = [commit(workspace, "A", str(index)) for index in range(1, 5)]
    ordered = [*reversed(identifiers), root]
    page = query.log(workspace.history, workspace.state["head"], limit=2)
    assert [item["commit_id"] for item in page["commits"]] == ordered[:2]
    following = query.log(workspace.history, workspace.state["head"], limit=2, cursor=page["cursor"])
    assert [item["commit_id"] for item in following["commits"]] == ordered[2:4]
    last = query.log(workspace.history, workspace.state["head"], limit=2, cursor=following["cursor"])
    assert [item["commit_id"] for item in last["commits"]] == ordered[4:]
    assert query.log(workspace.history, workspace.state["head"], limit=2, cursor=last["cursor"])["commits"] == []
    assert query.log(workspace.history, workspace.state["head"], limit=0)["commits"]

    shown = command(workspace, "show", None, dict(limit=1))
    assert shown["total"] == 1 and shown["cursor"] is None
    assert shown["rows"][0]["asset"] == ASSET and shown["rows"][0]["text"]
    beyond = command(workspace, "show", None, dict(limit=1, cursor=1))
    assert beyond["rows"] == [] and beyond["total"] == 1
