"""Fault-inject only the manager of an explicitly owned test runtime, then drain it."""

import argparse
import json
import os
from pathlib import Path
import signal
import time

from ue_node_nexus_mcp.bridge import UeBridgeClient
from ue_node_nexus_mcp.instances.broker.client import BrokerClient
from ue_node_nexus_mcp.instances.broker.package import install
from ue_node_nexus_mcp.instances.identity.discovery import arguments
from ue_node_nexus_mcp.instances.identity.processes import is_alive
from ue_node_nexus_mcp.instances.session.binding import EditorSession
from .host import ROOT


def run(root: Path, resume: bool = False) -> None:
    root = root.resolve()
    root.relative_to((ROOT / "build").resolve())
    project = root.parent / "NexusValidation.uproject"
    assert project.is_file() and (root.parent / "lifecycle-smoke.json").is_file()
    manager = json.loads((root / "manager.json").read_text(encoding="utf-8"))
    identity = manager["identity"]
    assert is_alive(identity) and str(root) in arguments(identity["pid"])
    if not resume:
        install(root)
        os.kill(identity["pid"], signal.SIGTERM)
    session = EditorSession(BrokerClient(root, str(project.parent)), str(project))
    results = []
    try:
        session.ensure(dict(dry_run=False))
        bridge = UeBridgeClient(instances=session)
        for operation in ("project_context_get", "bridge_capabilities_get", "level_current_get"):
            response = bridge.call(operation, dict())
            assert response["ok"], response
            results.append(dict(operation=operation, response=response))
        identifier = session.current()["instance_id"]
        deadline = time.monotonic() + 160
        while time.monotonic() < deadline:
            preview = session.call("close", dict(instance_id=identifier, dry_run=True))
            if not preview["blockers"]:
                break
            time.sleep(2)
        assert not preview["blockers"], preview
        session.call("close", dict(instance_id=identifier, dry_run=False))
        while time.monotonic() < deadline:
            state = session.status()
            if state["state"] == "EXITED":
                break
            assert state["state"] != "BLOCKED", state
            time.sleep(1)
        assert state["state"] == "EXITED", state
        print(json.dumps(dict(recovered=True, normal_exit=True, manager_epoch=session.broker.epoch)), flush=True)
    finally:
        session.close()
        (root.parent / "lifecycle-recovery.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    run(args.runtime, args.resume)
