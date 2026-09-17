"""Run collaboration recovery against a disposable empty UE project."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import time
import uuid

from ue_node_nexus_mcp.bridge import UeBridgeClient
from ue_node_nexus_mcp.instances.broker.client import BrokerClient
from ue_node_nexus_mcp.instances.session.binding import EditorSession
from .host import ROOT, prepare
from .protection import close_clean, require, save_asset, wait_state


def exported_raw(bridge, instance, asset):
    mirror = Path(instance["mirror_root"]) / instance["mirror_project_name"]
    output = mirror / "RecoveryInputs"
    result = bridge.call("transcode_export", dict(asset_paths=[asset], out_dir=str(output)))
    assert result.get("ok"), result
    file = Path(result["data"]["assets"][0]["file"])
    return json.loads(file.read_text(encoding="utf-8"))


def disk_digest(project, asset):
    package = asset.split(".", 1)[0].removeprefix("/Game/")
    file = Path(project).parent / "Content" / (package + ".uasset")
    return hashlib.sha256(file.read_bytes()).hexdigest()


def run(name: str, engine: str) -> None:
    project = prepare(name)
    root = project.parent / "Runtime"
    session = EditorSession(BrokerClient(root, str(project.parent)), str(project))
    report = dict(project=str(project), assertions=[])
    try:
        acquired = session.ensure(dict(mode="reuse_or_start", engine_path=engine, rhi="nullrhi", dry_run=False))
        state = wait_state(session, ("READY",), 240)
        instance = acquired["instance"]
        bridge = UeBridgeClient(instances=session)
        report["build"] = require(bridge, "bridge_capabilities_get")["build"]
        mirror = Path(instance["mirror_root"]) / instance["mirror_project_name"]
        mirror.mkdir(parents=True, exist_ok=True)
        binding = bridge.call("transcode_root_set", dict(
            root=str(mirror), repository=instance["repository"],
            collaboration_root=instance["repository"], project_id=uuid.uuid4().hex))
        assert binding.get("ok"), binding
        asset_name = "M_Recovery_" + uuid.uuid4().hex[:8]
        asset = f"/Game/Recovery/{asset_name}.{asset_name}"
        require(bridge, "asset_create", asset_path=asset, asset_kind="material", dry_run=False, save=False)
        save_asset(bridge, asset)
        before = exported_raw(bridge, instance, asset)
        before_disk = disk_digest(project, asset)
        apply_id = uuid.uuid4().hex
        response = bridge.call("transcode_apply", dict(
            asset_path=asset,
            kind="material",
            plan=[dict(op="unsupported_recovery_verb", id="missing")],
            ids={},
            create=False,
            dry_run=False,
            compile=False,
            save=True,
            apply_id=apply_id,
            repository=instance["repository"],
            collaboration_version=1,
            expected_revision=before["live_revision"],
            read_set=[],
        ))
        assert not response.get("ok"), response
        error = response.get("error", {})
        assert error.get("code") == "apply_rolled_back", response
        receipt = (response.get("data") or {}).get("receipt") or {}
        assert receipt.get("phase") == "rolled_back", response
        after = exported_raw(bridge, instance, asset)
        assert after["content_revision"] == before["content_revision"], (before, after)
        assert disk_digest(project, asset) == before_disk
        report.update(apply_id=apply_id, error=error, recovery_phase=receipt["phase"],
                      assertions=["checkpoint restored while package was loaded",
                                  "original disk bytes restored after in-memory reload",
                                  "content revision preserved"])
        final = close_clean(session, state["instance_id"])
        assert final["state"] == "EXITED" and final["exit_code"] == 0, final
        report["final"] = final
        report["ok"] = True
        print(json.dumps(dict(ok=True, project=str(project), recovery_phase=receipt["phase"])), flush=True)
    finally:
        session.close()
        (project.parent / "recovery-sandbox-result.json").write_text(
            json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--engine", required=True)
    args = parser.parse_args()
    run(args.name, args.engine)
