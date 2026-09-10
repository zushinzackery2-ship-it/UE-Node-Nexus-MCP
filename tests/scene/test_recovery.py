import json
from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.scene.files import read_document
from ue_node_nexus_mcp.transcode.scene.paths import storage
from ue_node_nexus_mcp.transcode.scene.push import push_one
from ue_node_nexus_mcp.transcode.sync_project import SyncError

from .fixtures import ACTOR, snapshot
from .test_sync import change_label


def lose_result():
    raise OSError("controlled reply loss")


def test_late_result_is_adopted_only_for_its_attempt(scene_workspace):
    bridge, context, state, item = scene_workspace
    change_label(context, item)
    bridge.before_result = lose_result
    assert push_one(bridge, context, state, item, dict(dry_run=False))["action"] == "failed"
    journal = json.loads(storage(context.project, item.key, "recovery").read_text(encoding="utf-8"))
    assert journal["after_revision"] == ""
    late = snapshot(list(bridge.scene_actors.values()))
    late["request_token"] = journal["plan"]["request_token"]
    bridge._write(str(storage(context.project, item.key, "after")), late)
    bridge.before_result = None
    assert push_one(bridge, context, state, item, dict(dry_run=False))["action"] in ("pushed", "unchanged")


def test_wrong_attempt_cannot_hide_an_external_change(scene_workspace):
    bridge, context, state, item = scene_workspace
    change_label(context, item)
    bridge.before_result = lose_result
    push_one(bridge, context, state, item, dict(dry_run=False))
    bridge.before_result = None
    bridge.scene_actors[ACTOR]["label"] = "External change"
    late = snapshot(list(bridge.scene_actors.values()))
    late["request_token"] = "unrelated-attempt"
    bridge._write(str(storage(context.project, item.key, "after")), late)
    with pytest.raises(SyncError) as error:
        push_one(bridge, context, state, item, dict(dry_run=False))
    assert error.value.code == "scene_conflict"


def test_state_replace_failure_restores_accepted_files(scene_workspace, monkeypatch):
    bridge, context, state, item = scene_workspace
    file = change_label(context, item)
    local, base = file.read_bytes(), (context.project / item.base_file).read_bytes()
    replace = Path.replace
    failed = False

    def fail_state_once(source, target):
        nonlocal failed
        if not failed and Path(target).name == "state.json":
            failed = True
            raise OSError("controlled state replacement failure")
        return replace(source, target)

    monkeypatch.setattr(Path, "replace", fail_state_once)
    row = push_one(bridge, context, state, item, dict(dry_run=False))
    assert row["action"] == "failed" and row["error"] == "mirror_commit_failed"
    assert file.read_bytes() == local
    assert (context.project / item.base_file).read_bytes() == base


def test_actor_construction_precedes_component_overrides(scene_workspace):
    from ue_node_nexus_mcp.transcode.emitter import emit

    bridge, context, state, item = scene_workspace
    file = change_label(context, item)
    document, _, _ = read_document(context.project, file)
    mesh = next(section for section in document.sections if section.name == "components").decls()[0]
    mesh.props.append(("bVisible", "False"))
    file.write_text(emit(document), encoding="utf-8")
    bridge.reconstruct = True
    assert push_one(bridge, context, state, item, dict(dry_run=False))["action"] == "pushed"
    assert bridge.scene_actors[ACTOR]["components"][0]["properties"]["bVisible"] == "False"
