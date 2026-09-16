"""Exercise the actual Broker/Guard/data pipes and normal exit on an owned host."""

import argparse
import json
from pathlib import Path
import time

from ue_node_nexus_mcp.bridge import UeBridgeClient
from ue_node_nexus_mcp.instances.broker.client import BrokerClient
from ue_node_nexus_mcp.instances.session.binding import EditorSession
from .host import prepare


def run(name: str, engine: str) -> None:
    project = prepare(name)
    root = project.parent / "Runtime"
    session = EditorSession(BrokerClient(root, str(project.parent)), str(project))
    bridge = UeBridgeClient(instances=session)
    events = []
    try:
        launch = session.ensure(dict(mode="reuse_or_start", engine_path=engine, dry_run=False))
        events.append(dict(event="launch", result=launch))
        identifier = launch["instance"]["instance_id"]
        deadline = time.monotonic() + 240
        while True:
            state = session.status()
            if state["state"] in ("READY", "IDLE"):
                break
            if state["state"] in ("EXITED", "UNRESPONSIVE") or time.monotonic() >= deadline:
                raise AssertionError(state)
            time.sleep(1)
        print(json.dumps(dict(event="ready", pid=state["pid"], instance_id=identifier)), flush=True)
        for operation in ("project_context_get", "bridge_capabilities_get", "level_current_get"):
            response = bridge.call(operation, dict())
            events.append(dict(event=operation, result=response))
            assert response["ok"], response
        state = session.call("close", dict(instance_id=identifier, dry_run=False))
        events.append(dict(event="close", result=state))
        deadline = time.monotonic() + 75
        while time.monotonic() < deadline:
            state = session.status()
            if state["state"] == "EXITED":
                print(json.dumps(dict(event="exit_confirmed", instance_id=identifier)), flush=True)
                break
            assert state["state"] != "BLOCKED", state
            time.sleep(1)
        assert state["state"] == "EXITED", state
    finally:
        session.close()
        (project.parent / "lifecycle-smoke.json").write_text(json.dumps(events, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--engine", required=True)
    args = parser.parse_args()
    run(args.name, args.engine)
