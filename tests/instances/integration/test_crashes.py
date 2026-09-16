"""Abrupt broker death around the durable-intent/CreateProcess boundary."""

import json
import time

import pytest

from ue_node_nexus_mcp.errors import BridgeError
from ue_node_nexus_mcp.instances.broker.bootstrap import ping
from ue_node_nexus_mcp.instances.broker.client import BrokerClient
from ue_node_nexus_mcp.instances.broker.registry import atomic_json
from tests.instances.support.processes import start_broker


def ready(root, process):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        try:
            result = ping(root)
            if result["ready"]:
                return result
        except BridgeError:
            pass
        assert process.poll() is None
        time.sleep(0.02)
    pytest.fail("fixture manager did not become ready")


def await_state(client, identifier, expected):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        result = client.call("status", dict(instance_id=identifier))
        if result["state"] == expected:
            return result
        assert result["state"] not in ("BLOCKED", "UNRESPONSIVE"), result
        time.sleep(0.02)
    pytest.fail(str(result))


@pytest.mark.parametrize("stage", ["before_intent", "after_intent", "before_spawn", "after_spawn", "after_pid"])
def test_crash_does_not_duplicate_editor_and_recovers_single_manager(tmp_path, stage):
    root = tmp_path / "Runtime"
    root.mkdir()
    project = tmp_path / "Fault.uproject"
    project.write_text("{}")
    atomic_json(root / "fault.json", dict(stage=stage))
    first = start_broker(root)
    previous = ready(root, first)
    client = BrokerClient(root, str(tmp_path))
    request = dict(project_path=str(project), mode="reuse_or_start", dry_run=False)
    try:
        client.call("ensure", request)
    except BridgeError:
        pass
    assert first.wait(timeout=15) == 81
    assert json.loads((root / "fault.json").read_text())["fired"]
    second = start_broker(root)
    try:
        current = ready(root, second)
        assert current["manager_epoch"] == previous["manager_epoch"] + 1
        result = client.call("ensure", request)
        identifier = result["instance"]["instance_id"]
        await_state(client, identifier, "READY")
        creations = list((root / "Creations").glob("*.json"))
        assert len(creations) == 1, [json.loads(file.read_text()) for file in creations]
        client.call("release")
        time.sleep(0.25)
        client.call("close", dict(instance_id=identifier, dry_run=False))
        await_state(client, identifier, "EXITED")
    finally:
        client.close()
        second.wait(timeout=15)
