"""Scene lint, history, native receipt adoption and saved recovery use one public workflow."""

from pathlib import Path

import pytest

from tests.scene.fixtures import ACTOR, MAP, component
from ue_node_nexus_mcp.transcode.sync.service import run_sync
from ue_node_nexus_mcp.transcode.text.emitter import emit
from ue_node_nexus_mcp.transcode.text.parser import parse
from ue_node_nexus_mcp.transcode.text.model import Decl
from .protocol import SceneProtocol

ASSET = MAP + "#Group"


@pytest.fixture
def project(tmp_path):
    ue = SceneProtocol()
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(tmp_path / "mirror"))
    selector = dict(map_path=MAP, name="Group", actors=[dict(id=ACTOR, level_path=MAP)])
    workspace = run_sync(ue, "checkout", [ASSET], dict(dry_run=False, scene=selector), env=env)
    return ue, env, workspace


def run(project, action, **options):
    ue, env, workspace = project
    return run_sync(ue, action, [ASSET], dict(workspace_id=workspace["id"], dry_run=False, **options), env=env)


def edit(project, label):
    path = Path(project[2]["file_paths"][ASSET])
    source = path.read_text(encoding="utf-8")
    document, sink = parse(source)
    assert not sink.has_errors
    owner = document.section("actors").decls()[0]
    owner.props = [(key, f'"{label}"' if key == "Label" else value) for key, value in owner.props]
    path.write_text(emit(document), encoding="utf-8")
    return path


def test_scene_publication_save_readback_and_history_restore(project):
    ue, _, workspace = project
    original = workspace["head"]
    edit(project, "Published")
    assert run(project, "lint")["error_count"] == 0
    run(project, "commit", all=True, message="scene label")
    pushed = run(project, "push")
    assert pushed["status"] == "published", pushed
    assert ue.scene.scene_actors[ACTOR]["label"] == "Published"
    assert ue.receipts[ue.applied[-1]["apply_id"]]["response"]["data"]["saved"] is True
    run(project, "restore", revision=original, destination="files")
    run(project, "commit", all=True, message="restore original scene")
    reverted = run(project, "push")
    assert reverted["status"] == "published", reverted
    assert ue.scene.scene_actors[ACTOR]["label"] == "Owner"
    assert len(ue.applied) == 2


def test_scene_save_failure_keeps_candidate_and_retries(project):
    ue, _, _ = project
    file = edit(project, "Retry")
    run(project, "commit", all=True, message="scene label")
    candidate = file.read_bytes()
    ue.scene.fail_save = True
    failed = run(project, "push")
    assert failed["status"] == "partial", failed
    assert ue.scene.scene_actors[ACTOR]["label"] == "Owner"
    assert file.read_bytes() == candidate
    ue.scene.fail_save = False
    retried = run(project, "push")
    assert retried["status"] == "published", retried
    assert ue.scene.scene_actors[ACTOR]["label"] == "Retry"


def test_scene_incorrect_saved_readback_is_rejected(project):
    ue, _, _ = project
    edit(project, "Requested")
    run(project, "commit", all=True, message="scene label")
    ue.corrupt_readback = True
    rejected = run(project, "push")
    assert rejected["status"] == "partial", rejected
    assert rejected["errors"][ASSET]["code"] == "apply_result_mismatch"


@pytest.mark.parametrize("kind", ["group", "actor", "component"])
@pytest.mark.parametrize("retain", [False, True])
def test_scene_deletion_verifies_saved_objects(project, kind, retain):
    ue, _, workspace = project
    path = Path(workspace["file_paths"][ASSET])
    if kind == "component":
        extra = component("55555555555555555555555555555555", "Extra", False)
        extra.update(is_root=False, removable=True)
        ue.scene.scene_actors[ACTOR]["components"].append(extra)
        run(project, "pull")
    if kind == "group":
        path.unlink()
    else:
        document, sink = parse(path.read_text(encoding="utf-8"))
        assert not sink.has_errors
        if kind == "actor":
            document.sections = [row for row in document.sections if row.name in ("scene", "actors")]
            document.section("actors").entries.clear()
        else:
            section = document.section("components", document.section("actors").decls()[0].id)
            section.entries = [row for row in section.entries if not isinstance(row, Decl) or row.id != "extra"]
        path.write_text(emit(document), encoding="utf-8")
    run(project, "commit", all=True, delete=True, allow_delete=True, message="remove scene object")
    ue.retain_deleted = retain
    result = run(project, "push", allow_delete=True)
    if retain:
        assert result["status"] == "partial", result
        assert result["errors"][ASSET]["code"] == "apply_result_mismatch", result
    else:
        assert result["status"] == "published", result
        if kind == "component":
            assert all(row["name"] != "Extra" for row in ue.scene.scene_actors[ACTOR]["components"])
        else:
            assert ACTOR not in ue.scene.scene_actors


def test_created_actor_accepts_its_native_components_and_is_stable(project):
    ue, _, workspace = project
    path = Path(workspace["file_paths"][ASSET])
    document, sink = parse(path.read_text(encoding="utf-8"))
    assert not sink.has_errors
    document.section("actors").entries.append(Decl("new_actor", "/Script/Engine.StaticMeshActor"))
    path.write_text(emit(document), encoding="utf-8")
    ue.scene.native_default = True
    run(project, "commit", all=True, message="create actor with native component")
    result = run(project, "push")
    assert result["status"] == "published", result
    assert len(ue.scene.scene_actors) == 2
    created = next(row for identifier, row in ue.scene.scene_actors.items() if identifier != ACTOR)
    assert [row["name"] for row in created["components"]] == ["NativeMesh"]
    identifiers = set(ue.scene.scene_actors)
    assert run(project, "push")["status"] == "published"
    assert set(ue.scene.scene_actors) == identifiers
    assert len(ue.applied) == 1
