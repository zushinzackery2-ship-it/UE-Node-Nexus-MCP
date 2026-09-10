import pytest

from ue_node_nexus_mcp.transcode.emitter import emit
from ue_node_nexus_mcp.transcode.model import Decl
from ue_node_nexus_mcp.transcode.scene.files import read_document
from ue_node_nexus_mcp.transcode.scene.paths import storage
from ue_node_nexus_mcp.transcode.scene.pull import pull_one
from ue_node_nexus_mcp.transcode.scene.push import push_one
from ue_node_nexus_mcp.transcode.scene.selection import new_record, select
from ue_node_nexus_mcp.transcode.scene.status import inspect
from ue_node_nexus_mcp.transcode.sync_project import SyncError

from .fixtures import ACTOR, MAP


def change_label(context, item, label="Changed"):
    file = context.project / item.file
    document, _, _ = read_document(context.project, file)
    owner = document.section("actors").decls()[0]
    owner.props = [(key, label if key == "Label" else value) for key, value in owner.props]
    file.write_text(emit(document), encoding="utf-8")
    return file


def test_pull_does_not_bind_editor_metadata(scene_workspace):
    bridge, _, _, _ = scene_workspace
    owner = bridge.scene_actors[ACTOR]
    assert owner["scene_owner"] == ""
    assert owner["components"][0]["instance_data"]["identity_bound"] is False


def test_status_reads_live_revision_and_distinguishes_unavailable(scene_workspace):
    bridge, context, _, item = scene_workspace
    assert inspect(bridge, context, item)["state"] == "clean"
    bridge.scene_actors[ACTOR]["label"] = "UE edit"
    assert inspect(bridge, context, item)["state"] == "ue-modified"
    bridge.unavailable.add(ACTOR)
    assert inspect(bridge, context, item)["state"] == "unavailable"


def test_pull_preserves_local_edits_until_explicit_ue_adoption(scene_workspace):
    bridge, context, state, item = scene_workspace
    file = change_label(context, item)
    edited = file.read_bytes()
    with pytest.raises(SyncError):
        pull_one(bridge, context, state, item, dict())
    assert file.read_bytes() == edited
    pull_one(bridge, context, state, item, dict(force="ue"))
    assert "Changed" not in file.read_text(encoding="utf-8")


def test_unloaded_actor_cannot_be_interpreted_as_a_delete(scene_workspace):
    bridge, context, state, item = scene_workspace
    bridge.unavailable.add(ACTOR)
    with pytest.raises(SyncError) as error:
        push_one(bridge, context, state, item, dict(dry_run=False, allow_delete=True, force="local"))
    assert error.value.code == "scene_unavailable"
    assert bridge.scene_plans == []


def test_external_package_failure_keeps_text_and_base_then_retries(scene_workspace):
    bridge, context, state, item = scene_workspace
    file = change_label(context, item)
    original = file.read_bytes()
    base = context.project / item.base_file
    accepted = base.read_bytes()
    bridge.fail_save = True
    failed = push_one(bridge, context, state, item, dict(dry_run=False))
    assert failed["action"] == "failed" and failed["failed_packages"]
    assert file.read_bytes() == original and base.read_bytes() == accepted
    assert storage(context.project, item.key, "recovery").is_file()
    bridge.fail_save = False
    completed = push_one(bridge, context, state, item, dict(dry_run=False))
    assert completed["action"] in ("pushed", "unchanged")
    assert bridge.scene_actors[ACTOR]["label"] == "Changed"
    assert not storage(context.project, item.key, "recovery").exists()


def test_edits_made_during_apply_are_preserved(scene_workspace):
    bridge, context, state, item = scene_workspace
    file = change_label(context, item)
    accepted = (context.project / item.base_file).read_bytes()
    concurrent = file.read_text(encoding="utf-8") + "\n# concurrent editor change\n"
    bridge.before_result = lambda: file.write_text(concurrent, encoding="utf-8")
    row = push_one(bridge, context, state, item, dict(dry_run=False))
    assert row["action"] == "failed" and row["error"] == "local_changed_during_sync"
    assert file.read_text(encoding="utf-8") == concurrent
    assert (context.project / item.base_file).read_bytes() == accepted


def test_created_actor_and_native_component_are_not_duplicated_on_retry(scene_workspace):
    bridge, context, state, item = scene_workspace
    file = context.project / item.file
    document, _, _ = read_document(context.project, file)
    document.section("actors").entries.append(Decl("new_actor", "/Script/Engine.StaticMeshActor"))
    file.write_text(emit(document), encoding="utf-8")
    bridge.native_default = True
    bridge.fail_save = True
    assert push_one(bridge, context, state, item, dict(dry_run=False))["action"] == "failed"
    ids = set(bridge.scene_actors)
    assert len(ids) == 2
    bridge.fail_save = False
    assert push_one(bridge, context, state, item, dict(dry_run=False))["action"] in ("pushed", "unchanged")
    assert set(bridge.scene_actors) == ids
    assert all(op["op"] != "create_actor" for op in bridge.scene_plans[-1]["ops"])


def test_explicit_groups_cannot_claim_the_same_actor(scene_workspace):
    bridge, context, state, item = scene_workspace
    other = new_record(context, MAP, "Other")
    with pytest.raises(SyncError) as error:
        pull_one(bridge, context, state, other, dict(scene=dict(actor_paths=[bridge.scene_actors[ACTOR]["actor_path"]])))
    assert error.value.code == "scene_membership_conflict"


def test_new_scene_file_is_discovered_by_directory(scene_workspace):
    _, context, state, item = scene_workspace
    file = context.project / "Scenes/Maps/World/New.scene.nexus"
    text = (context.project / item.file).read_text(encoding="utf-8").replace('Name = "Group"', 'Name = "New"')
    file.write_text(text, encoding="utf-8")
    assets, scenes = select(context, state, ["Scenes"], dict())
    assert assets == [] and len(scenes) == 2
