"""Real editor sync regression against the isolated compile-check project."""

from __future__ import annotations

import json
import os
from pathlib import Path
import stat
import sys
import uuid

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "src"))
sys.path.insert(0, str(REPO))

from ue_node_nexus_mcp.runtime import call_bridge
from ue_node_nexus_mcp.transcode.paths import base_path, text_path
from ue_node_nexus_mcp.transcode.sync import run_sync
from tests.live.editor.session import EditorSession


def _write(project: Path, asset: str, kind: str, cls: str, schema: str, body: str) -> Path:
    file = text_path(project, asset, kind)
    file.parent.mkdir(parents=True, exist_ok=True)
    file.write_text(f"nexus: 1\nasset: {asset}\nclass: {cls}\nschema: {schema}\n\n{body}\n", encoding="utf-8", newline="\n")
    return file


def exercise(validation: Path, bridge=call_bridge) -> dict:
    capabilities = bridge("bridge_capabilities_get", dict())
    assert capabilities.get("ok"), capabilities
    assert capabilities["data"]["modules"]["vfx_available"] is True, capabilities
    scenario_id = uuid.uuid4().hex[:8]
    root = validation / "SyncMirror" / scenario_id
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(root))
    initialized = run_sync(bridge, "init", options=dict(pull_all=False), env=env)
    schema = initialized["schema_key"]
    project = root / "NexusValidation"
    package = "/Game/SyncSmoke_" + scenario_id
    material = package + "/M_Base.M_Base"
    instance = package + "/Z_Surface.Z_Surface"
    blueprint = package + "/A_Consumer.A_Consumer"
    file = _write(project, material, "material", "Material", schema,
        "[graph]\nc : Constant(R=0.25) @ 0,0\nc -> out.BaseColor")
    _write(project, instance, "material_instance", "MaterialInstanceConstant", schema,
        f"[asset]\nParent = {material}")
    _write(project, blueprint, "blueprint", "Blueprint", schema,
        "[asset]\nParentClass = /Script/Engine.Actor\n\n[components]\n"
        f"Mesh : StaticMeshComponent {{ OverlayMaterial={instance} }}")
    selected = [blueprint, material, instance]
    dry = run_sync(bridge, "push", selected, env=env)
    assert dry["error_count"] == 0, dry
    assert [plan["asset"] for plan in dry["plans"]] == [material, instance, blueprint], dry
    applied = run_sync(bridge, "push", selected, dict(dry_run=False), env)
    assert applied["error_count"] == 0 and applied["counts"] == dict(pushed=3), applied
    assert run_sync(bridge, "status", selected, env=env)["counts"] == dict(clean=3)
    before = file.read_text(encoding="utf-8")
    assert "R=0.25" in before, before
    file.write_text(before.replace("R=0.25", "R=0.75"), encoding="utf-8", newline="\n")
    intended = file.read_bytes()
    accepted = base_path(project, material).read_bytes()
    package_file = validation / "Content" / package.removeprefix("/Game/") / "M_Base.uasset"
    assert package_file.is_file(), package_file
    package_file.chmod(stat.S_IREAD)
    try:
        failed = run_sync(bridge, "push", [material], dict(dry_run=False), env)
        assert failed["error_count"] >= 1, failed
        assert any("save_blocked_read_only" in item for item in failed["diagnostics"]), failed
        assert file.read_bytes() == intended
        assert base_path(project, material).read_bytes() == accepted
    finally:
        package_file.chmod(stat.S_IREAD | stat.S_IWRITE)
    retried = run_sync(bridge, "push", [material], dict(dry_run=False, force="local"), env)
    assert retried["error_count"] == 0 and retried["counts"] == dict(pushed=1), retried
    assert run_sync(bridge, "status", selected, env=env)["counts"] == dict(clean=3)
    assert "R=0.75" in file.read_text(encoding="utf-8")
    assert not list((project / ".nexus/pending").rglob("*.push.json"))
    return dict(schema=schema, mirror=str(root), dependency_order=[material, instance, blueprint], failed=failed, retried=retried)


def main() -> None:
    validation = Path(os.environ.get("UE_NEXUS_VALIDATION_DIR", REPO / "build/validation")).resolve()
    configured_engine = os.environ.get("UE_NEXUS_ENGINE_DIR")
    if not configured_engine:
        raise RuntimeError("set UE_NEXUS_ENGINE_DIR to your Unreal Engine 5.5 installation")
    engine = Path(configured_engine)
    with EditorSession(validation / "NexusValidation.uproject", engine, "SyncSmoke") as session:
        session.verify_builds()
        result = exercise(validation, session.call)
        assert session.process.poll() is None
        session.report("result", result)
        print(json.dumps(dict(ok=True, schema=result["schema"], rhi=session.rhi,
                              assertions="dependency ordering, read-only failure preservation, empty-plan retry, clean readback")))


if __name__ == "__main__":
    main()
