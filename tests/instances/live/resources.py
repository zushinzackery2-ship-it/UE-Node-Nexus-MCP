"""Measure two real idle editors/ten MCP clients, then verify autonomous Guard recovery."""

import argparse
import asyncio
from contextlib import AsyncExitStack
import json
import sys
import time

import ue_node_nexus_mcp
from ue_node_nexus_mcp.instances.broker.client import BrokerClient
from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.identity.discovery import arguments
from ue_node_nexus_mcp.instances.identity.processes import is_alive
from ue_node_nexus_mcp.instances.identity.resources import sample
from ue_node_nexus_mcp.instances.identity.watch import ProcessWatch
from .collaboration import wait_state
from .crash_clients import terminate_owned
from .host import prepare
from .protection import close_clean
from .stdio import connect


async def recovery(root, report, identities):
    record = json.loads((root / "manager.json").read_text())
    original = record["identity"]
    assert str(root) in arguments(original["pid"])
    watches = [ProcessWatch(identity) for identity in identities]
    controller = BrokerClient(root, str(root.parent))
    try:
        terminate_owned(original)
        started = time.monotonic()
        while time.monotonic() - started < 45:
            current = json.loads((root / "manager.json").read_text())
            if current["manager_epoch"] > record["manager_epoch"] and is_alive(current["identity"]):
                break
            await asyncio.sleep(1)
        assert current["manager_epoch"] > record["manager_epoch"] and is_alive(current["identity"]), current
        report["recovery"] = dict(old_manager=original, new_manager=current["identity"],
                                  recovered_seconds=time.monotonic() - started, manager_epoch=current["manager_epoch"])
        for identity in identities:
            state = await wait_state(controller, identity["instance_id"], ("READY", "IDLE"))
            assert state["protected"] and is_alive(identity), state
            assert state["process_created"] == identity["process_created"], state
            final = await asyncio.to_thread(close_clean, controller, identity["instance_id"])
            assert final["state"] == "EXITED" and final["exit_code"] == 0, final
        codes = [watch.poll() for watch in watches]
        deadline = time.monotonic() + 75
        while any(code is None for code in codes) and time.monotonic() < deadline:
            await asyncio.sleep(2)
            codes = [watch.poll() for watch in watches]
        assert codes == [0, 0], codes
        report["recovery"].update(editor_exit_codes=codes, cleanup_seconds=time.monotonic() - started)
        controller.close()
        deadline = time.monotonic() + 80
        while is_alive(current["identity"]) and time.monotonic() < deadline:
            await asyncio.sleep(2)
        assert not is_alive(current["identity"])
        report["recovery"]["manager_exited"] = True
    finally:
        controller.close()
        for watch in watches:
            watch.close()


async def run(name, engine):
    projects = [prepare(name + suffix) for suffix in ("-A", "-B")]
    root = projects[0].parent / "Runtime"
    controller = BrokerClient(root, str(projects[0].parent))
    report = dict(projects=[str(project) for project in projects], python=sys.executable,
                  package_path=ue_node_nexus_mcp.__file__, rhi="d3d12", purpose="idle manager resources with visible editors")
    identities = []
    try:
        for project in projects:
            acquired = await asyncio.to_thread(controller.call, "ensure", dict(project_path=str(project), mode="reuse_or_start",
                                                engine_path=engine, rhi="d3d12", dry_run=False))
            state = await wait_state(controller, acquired["instance"]["instance_id"], ("READY",))
            identities.append(state)
        report["instances"] = identities
        async with AsyncExitStack() as stack:
            for index in range(10):
                peer = await stack.enter_async_context(connect(projects[index % 2], root, root.parent / "Peers" / str(index)))
                acquired = await peer.execute("bridge_instance_ensure", dry_run=False)
                assert acquired["instance"]["instance_id"] == identities[index % 2]["instance_id"]
            controller.close()
            await asyncio.sleep(15)
            manager = json.loads((root / "manager.json").read_text())["identity"]
            samples = [sample(manager)]
            started = time.monotonic()
            for _ in range(6):
                await asyncio.sleep(10)
                samples.append(sample(manager))
            elapsed = time.monotonic() - started
            cpu = (samples[-1]["cpu_seconds"] - samples[0]["cpu_seconds"]) / elapsed * 100
            private = max(item["private_working_set_bytes"] for item in samples)
            report["resources"] = dict(duration_seconds=elapsed, single_core_cpu_percent=cpu,
                                       private_working_set_bytes=private, samples=samples)
            assert cpu <= 0.5 and private <= 128 * 1024 ** 2, report["resources"]
            assert max(item["handle_count"] for item in samples) - min(item["handle_count"] for item in samples) <= 16
            print(json.dumps(dict(event="idle_resources_passed", cpu_percent=cpu, private_mib=private / 1024 ** 2)), flush=True)
        await recovery(root, report, identities)
        report["ok"] = True
        print(json.dumps(dict(ok=True, watchdog=report["recovery"])), flush=True)
    except Exception as error:
        report["error"] = error.envelope() if isinstance(error, InstanceError) else dict(type=type(error).__name__, message=str(error))
        raise
    finally:
        controller.close()
        (root.parent / "resources-result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--engine", required=True)
    args = parser.parse_args()
    asyncio.run(run(args.name, args.engine))
