from pathlib import Path

from ue_node_nexus_mcp.transcode.emitter import emit
from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.sync import run_sync

from .fake_scene import FakeScene
from .fixtures import ACTOR, MAP, SCHEMA


def mixed_workspace(tmp_path: Path):
    bridge = FakeScene()
    mesh = bridge.scene_actors[ACTOR]["components"][0]
    mesh["defaults"]["OverrideMaterials"] = "()"
    mesh["property_schema"]["OverrideMaterials"] = dict(type="array")
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(tmp_path / "Mirror"))
    run_sync(bridge, "init", options=dict(pull_all=False), env=env)
    run_sync(bridge, "pull", options=dict(scene=dict(map_path=MAP, name="Group", actor_paths=[bridge.scene_actors[ACTOR]["actor_path"]])), env=env)
    project = tmp_path / "Mirror/Shadetest"
    scene = project / "Scenes/Maps/World/Group.scene.nexus"
    document, sink = parse(scene.read_text(encoding="utf-8"))
    assert not sink.has_errors
    mesh_decl = next(section for section in document.sections if section.name == "components").decls()[0]
    mesh_decl.props.append(("OverrideMaterials", "(/Game/Resources/M_Local.M_Local)"))
    scene.write_text(emit(document), encoding="utf-8")
    material = project / "Resources/M_Local.mat.nexus"
    material.parent.mkdir(parents=True, exist_ok=True)
    material.write_text(f"nexus: 1\nasset: /Game/Resources/M_Local\nclass: Material\nschema: {SCHEMA}\n\n[graph]\nv : Constant(R=0.8) @ 0,0\nv -> out.Roughness\n", encoding="utf-8")
    return bridge, env, scene


def test_new_asset_dependency_is_committed_before_scene(tmp_path):
    bridge, env, scene = mixed_workspace(tmp_path)
    report = run_sync(bridge, "push", [str(scene)], dict(dry_run=False), env=env)
    assert report["error_count"] == 0, report
    operations = [name for name, _ in bridge.calls]
    assert operations.index("transcode_apply") < operations.index("scene_apply")
    assert report["scene_counts"]["pushed"] == 1
    again = run_sync(bridge, "push", [str(scene)], dict(dry_run=False), env=env)
    assert again["error_count"] == 0, again


def test_failed_dependency_prevents_scene_writes(tmp_path):
    bridge, env, scene = mixed_workspace(tmp_path)
    original = scene.read_bytes()
    bridge.fail_apply_index = 0
    report = run_sync(bridge, "push", [str(scene)], dict(dry_run=False), env=env)
    assert report["error_count"] > 0
    assert not bridge.scene_plans
    assert scene.read_bytes() == original


def test_dry_run_reports_future_dependencies_without_mutation(tmp_path):
    bridge, env, scene = mixed_workspace(tmp_path)
    original = scene.read_bytes()
    report = run_sync(bridge, "push", [str(scene)], dict(dry_run=True), env=env)
    assert report["error_count"] == 0, report
    row = report["scene_rows"][0]
    assert row["preflight"] == "pending_dependencies"
    assert row["dependencies"] == ["/Game/Resources/M_Local.M_Local"]
    assert row["verb_counts"]["update_component"] == 1
    assert scene.read_bytes() == original and not bridge.scene_plans
