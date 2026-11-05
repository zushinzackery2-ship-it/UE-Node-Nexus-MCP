"""Scene isolation uses its map package, native transaction and immutable plan."""

from copy import deepcopy
import json
from pathlib import Path
from types import SimpleNamespace

import pytest

from tests.collaboration.scenes.protocol import SceneProtocol
from tests.scene.fixtures import ACTOR, MAP
from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.safety.isolation.session import ValidationSession
from ue_node_nexus_mcp.safety.publication import gate
from ue_node_nexus_mcp.safety.publication.evidence import binding
from ue_node_nexus_mcp.transcode.scene.diff import build_plan
from ue_node_nexus_mcp.transcode.collaboration.apply.readback import scene_result
from ue_node_nexus_mcp.transcode.errors import SyncError
from .test_publication_evidence import builds, environment
from .test_publication_gate import fixture


def scene_item():
    return dict(asset=MAP + "#Group", kind="scene", empty=False, dependencies=[],
        payload=dict(kind="scene"), scene_plan=dict(map_path=MAP, name="Group", actors=[],
            managed_actors=[], selector=dict(map_path=MAP, name="Group", actors=[]),
            expected_revision="before", allow_delete=False,
            ops=[dict(op="update_actor", id=ACTOR, label="Requested")]))


def test_admission_exports_map_and_executes_pinned_scene_plan(tmp_path, monkeypatch):
    bridge, context, workspace, _, observation, _, calls, _ = fixture(monkeypatch)
    context.project_file = str(tmp_path / "Project/Probe.uproject")
    Path(context.project_file).parent.mkdir()
    original = gate.validate
    submitted = []

    def validate(project, operations, *args, **kwargs):
        submitted.extend(deepcopy(operations))
        result = original(project, operations, *args, **kwargs)
        result["data"]["results"][0]["data"]["isolated_publication"]["construction_verified"] = True
        return result

    monkeypatch.setattr(gate, "validate", validate)
    item = scene_item()
    gate.admit_batch(bridge, context, workspace, [(item["asset"], item)], "candidate", observation,
        dict(isolation="always"))
    exported = next(payload["asset_paths"] for name, payload in calls if name == "safety_baseline_export")
    assert exported == [MAP]
    assert submitted[0]["operation"] == "scene_apply"
    request = submitted[0]["payload"]
    plan_file = Path(context.project_file).parent / request["plan_file"]
    assert not Path(request["plan_file"]).is_absolute()
    assert json.loads(plan_file.read_text())["ops"] == item["scene_plan"]["ops"]


def test_binding_changes_when_scene_operations_change():
    item = scene_item()
    observation = dict(revisions={})
    before = binding(item, "candidate", observation, builds(), "d3d12", environment())
    item["scene_plan"]["ops"][0]["label"] = "Changed"
    after = binding(item, "candidate", observation, builds(), "d3d12", environment())
    assert before["payload_digest"] != after["payload_digest"]


def test_cached_scene_proof_rejects_changed_plan():
    item = scene_item()
    proof = dict(binding=binding(item, "candidate", dict(revisions={}), builds(), "d3d12", environment()))
    item["scene_plan"]["ops"][0]["label"] = "Changed"
    assert not gate.receipt_valid(proof, item, "candidate")


def worker(tmp_path, defect=None):
    session = ValidationSession.__new__(ValidationSession)
    session.project = tmp_path / "Project/Probe.uproject"
    session.project.parent.mkdir()
    session.project.write_text("{}")
    transport = SceneProtocol()
    selector = dict(map_path=MAP, name="Group", actors=[dict(id=ACTOR, level_path=MAP)])
    current = transport.scene._snapshot(selector)
    desired = deepcopy(current)
    desired["actors"][0]["label"] = "Requested"
    plan = build_plan(current, desired, selector, False)
    plan["request_token"] = "submitted-token"
    source = session.project.parent / "Saved/ScenePlan.json"
    source.parent.mkdir()
    source.write_text(json.dumps(plan))
    calls = []

    def call(operation, payload):
        calls.append((operation, deepcopy(payload)))
        if operation == "level_open":
            assert payload["map_path"] == MAP
            if defect == "dirty_preview" and not payload.get("discard_changes"):
                return dict(ok=False, error=dict(code="unsaved_changes", details=dict(current_map="/Game/Preview")))
            return dict(ok=True, data=dict(map_path=MAP))
        if operation != "scene_apply":
            return transport(operation, payload)
        assert payload.get("apply_id"), "scene writes require a worker-owned transaction"
        native = deepcopy(payload)
        actual_plan = json.loads(Path(payload["plan_file"]).read_text())
        native.update(scene_plan=actual_plan, selector=dict(actual_plan["selector"], map_path=MAP),
            kind="scene", asset_path=MAP)
        response = transport(operation, native)
        receipt = response["data"]["receipt"]
        after = receipt["after"]
        if defect == "phase":
            receipt["phase"] = "applied"
        if defect == "map":
            after["map_path"] = "/Game/Maps/Wrong"
        if defect == "group":
            after["name"] = "WrongGroup"
        if defect == "token":
            receipt["response_data"]["request_token"] = "wrong-token"
        if defect == "response_token":
            response["data"]["request_token"] = "wrong-token"
        if defect == "save":
            response["data"]["saved"] = False
        exported = deepcopy(after)
        if defect == "file":
            exported["actors"][0]["label"] = "Wrong exported value"
        Path(payload["out_file"]).write_text(json.dumps(exported))
        return response

    session.call = call
    return session, source, calls


def test_worker_scene_has_private_commit_and_saved_readback(tmp_path):
    session, source, calls = worker(tmp_path)
    before = source.read_bytes()
    response = session.execute(dict(operation="scene_apply", payload=dict(
        plan_file=str(source), out_file=str(session.project.parent / "Saved/Unused.json"), dry_run=False, save=True)))
    assert response["ok"]
    proof = response["data"]["isolated_publication"]
    assert proof["construction_verified"] and proof["saved"] and proof["readback_verified"]
    assert proof["raw"]["map_path"] == MAP and proof["raw"]["name"] == "Group"
    assert "request_token" not in proof["raw"]
    assert proof["receipt"]["response_data"]["request_token"] == proof["apply_id"]
    request = next(payload for name, payload in calls if name == "scene_apply")
    assert request["collaboration_version"] == 1 and request["apply_id"]
    for field in ("plan_file", "out_file", "repository"):
        Path(request[field]).resolve().relative_to(session.project.parent.resolve())
    assert source.read_bytes() == before


def test_worker_opens_candidate_after_its_private_preview_marks_default_map_dirty(tmp_path):
    session, source, calls = worker(tmp_path, "dirty_preview")
    response = session.execute(dict(operation="scene_apply", payload=dict(
        plan_file=str(source), out_file=str(session.project.parent / "Saved/Unused.json"), dry_run=False, save=True)))
    assert response["ok"]
    opened = next(payload for name, payload in calls if name == "level_open")
    assert opened == dict(map_path=MAP, dry_run=False, discard_changes=True)
    assert response["data"]["isolated_publication"]["saved"]


@pytest.mark.parametrize("defect", ["phase", "map", "group", "token", "response_token", "file", "save"])
def test_worker_refuses_incomplete_or_different_scene_evidence(tmp_path, defect):
    session, source, _ = worker(tmp_path, defect)
    with pytest.raises(InstanceError):
        session.execute(dict(operation="scene_apply", payload=dict(
            plan_file=str(source), out_file=str(session.project.parent / "Saved/Unused.json"), dry_run=False, save=True)))


@pytest.mark.parametrize("verb", ["delete_actor", "remove_component"])
@pytest.mark.parametrize("retained", [False, True])
def test_scene_readback_verifies_deletion_after_the_worker_plan_is_unavailable(tmp_path, verb, retained):
    workspace = SimpleNamespace(store=SimpleNamespace(root=tmp_path / "PublishingRepository"))
    component_id = "55555555555555555555555555555555"
    operation = dict(op=verb, id=ACTOR if verb == "delete_actor" else component_id)
    if verb == "remove_component":
        operation["actor_id"] = ACTOR
    actors = []
    if retained or verb == "remove_component":
        actors = [dict(id=ACTOR, components=[dict(id=component_id)] if retained else [])]
    record = dict(id="committed-worker", request=dict(
        plan_file=str(tmp_path / "ExitedWorker/plan.json"),
        scene_plan=dict(map_path=MAP, name="Group", ops=[operation])))
    rows = scene_result(workspace, dict(semantic={}), dict(semantic={}, raw=dict(actors=actors)), record)
    assert bool(rows) == retained


@pytest.mark.parametrize("plan", [None, {}, {"ops": None}])
def test_scene_readback_rejects_a_receipt_without_its_inline_plan(tmp_path, plan):
    workspace = SimpleNamespace(store=SimpleNamespace(root=tmp_path))
    record = dict(id="unbound-worker", request=dict(scene_plan=plan, plan_file="plan.json"))
    with pytest.raises(SyncError, match="committed inline plan"):
        scene_result(workspace, dict(semantic={}), dict(semantic={}, raw=dict(actors=[])), record)
