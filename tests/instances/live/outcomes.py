"""Real queued writes survive timeout and release while the UE GameThread is paused."""

import argparse
import json
from pathlib import Path
import sys
import time
import uuid

import ue_node_nexus_mcp
from ue_node_nexus_mcp.bridge import UeBridgeClient
from ue_node_nexus_mcp.instances.broker.client import BrokerClient
from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.identity.processes import is_alive
from ue_node_nexus_mcp.instances.session.binding import EditorSession
from ue_node_nexus_mcp.transport import named_pipe_transport
from tests.instances.support.threads import paused_game_thread
from .host import prepare
from .protection import close_clean, require, save_asset, wait_state


class CountTransport:
    def __init__(self):
        self.creates = []

    def send(self, target, envelope, timeout):
        if envelope["operation"] == "asset_create":
            self.creates.append(envelope["request_id"])
        return named_pipe_transport.send(target, envelope, timeout)


def run(name, engine):
    project = prepare(name)
    root = project.parent / "Runtime"
    session = EditorSession(BrokerClient(root, str(project.parent)), str(project))
    transport = CountTransport()
    bridge = UeBridgeClient(instances=session, transport=transport)
    report = dict(project=str(project), python=sys.executable, package_path=ue_node_nexus_mcp.__file__)
    try:
        session.ensure(dict(mode="reuse_or_start", engine_path=engine, dry_run=False))
        state = wait_state(session, ("READY",), 240)
        report["builds"] = require(bridge, "bridge_capabilities_get")["build"]
        identifier = state["instance_id"]
        asset = "/Game/InstanceOutcomes/M_Unknown_" + uuid.uuid4().hex[:8]
        asset += "." + asset.rsplit("/", 1)[1]
        report["asset"] = asset
        with paused_game_thread(state) as paused:
            try:
                bridge.call("asset_create", dict(asset_path=asset, asset_kind="material", dry_run=False, save=False), timeout_seconds=0.25)
            except InstanceError as error:
                assert error.code == "operation_outcome_unknown", error.envelope()
                report["interrupted_write"] = error.envelope()
            else:
                raise AssertionError("a paused game thread unexpectedly answered the write")
            started = time.monotonic()
            native = named_pipe_transport.send("\\\\.\\pipe\\UeNodeNexusLifecycle." + str(state["pid"]),
                                               dict(operation="status", payload=dict()), 2)
            report["native_status_seconds"] = time.monotonic() - started
            assert native["ok"] and native["data"]["pending"] + native["data"]["inflight"] >= 1, native
            assert report["native_status_seconds"] < 0.5
            report["paused_native_status"] = native["data"]
            assert session.status()["scope_count"] == 1
            assert session.ensure(dict(dry_run=False))["instance"]["instance_id"] == identifier
            preview = session.call("close", dict(instance_id=identifier, dry_run=True))
            assert preview["blockers"] and is_alive(state), preview
            report["paused_close"] = preview
        report["game_thread"] = paused
        deadline = time.monotonic() + 30
        while session.status()["scope_count"] and time.monotonic() < deadline:
            time.sleep(1)
        assert session.status()["scope_count"] == 0
        require(bridge, "asset_compile", asset_path=asset)
        save_asset(bridge, asset)
        assert len(transport.creates) == 1, transport.creates
        file = project.parent / "Content" / (asset.split(".")[0].removeprefix("/Game/") + ".uasset")
        assert file.is_file()
        final = close_clean(session, identifier)
        assert final["state"] == "EXITED" and final["exit_code"] == 0, final
        report.update(ok=True, final=final, actual_create_requests=transport.creates,
                      assertions=["native control remains responsive", "queued write retained after timeout",
                                  "release remains pending until completion", "no duplicate editor or write", "normal exit"])
        print(json.dumps(dict(ok=True, assertions=report["assertions"], native_status_ms=report["native_status_seconds"] * 1000)), flush=True)
    finally:
        session.close()
        (project.parent / "outcomes-result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--engine", required=True)
    args = parser.parse_args()
    run(args.name, args.engine)
