"""32 real stdio MCP processes, different cwd, repeated concurrent first ensure."""

import argparse
import asyncio
import json
from pathlib import Path
import sys
import time
import ue_node_nexus_mcp

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from ue_node_nexus_mcp.instances.broker.bootstrap import ping
from ue_node_nexus_mcp.instances.broker.client import BrokerClient
from ue_node_nexus_mcp.errors import BridgeError
from tests.instances.support.processes import ROOT, environment, start_broker


def decoded(result):
    value = json.loads(next(item.text for item in result.content if item.type == "text"))
    assert value.get("ok"), value
    return value["data"]


async def agent(index, project, root, rounds, barriers, answers):
    workspace = root.parent / "Workspaces" / str(index)
    workspace.mkdir(parents=True)
    parameters = StdioServerParameters(command=sys.executable,
        args=["-m", "ue_node_nexus_mcp.server", "--project", str(project)], cwd=str(workspace),
        env=dict(environment(), UE_NEXUS_RUNTIME_DIR=str(root), UE_NEXUS_LOG_DIR=str(root / "ClientLogs")))
    with (workspace / "stderr.log").open("w", encoding="utf-8") as stderr:
        async with stdio_client(parameters, errlog=stderr) as streams:
            async with ClientSession(*streams) as client:
                await client.initialize()
                for round_number in range(rounds):
                    await barriers[0].wait()
                    started = time.monotonic()
                    result = decoded(await client.call_tool("ue_execute", dict(operation="bridge_instance_ensure",
                        payload=dict(mode="reuse_or_start", dry_run=False, idempotency_key=f"round-{round_number}"), response=dict(mode="full"))))
                    await answers.put(dict(instance_id=result["instance"]["instance_id"], client=index,
                                           ensure_ms=(time.monotonic() - started) * 1000))
                    await barriers[1].wait()
                    decoded(await client.call_tool("ue_execute", dict(operation="bridge_instance_release", payload=dict())))
                    await barriers[2].wait()


async def coordinate(project, broker, clients, rounds, barriers, answers, report):
    for number in range(rounds):
        await barriers[0].wait()
        values = [await answers.get() for _ in range(clients)]
        identities = set(row["instance_id"] for row in values)
        assert len(identities) == 1, values
        identifier = identities.pop()
        deadline = time.monotonic() + 15
        while True:
            state = await asyncio.to_thread(broker.call, "status", dict(instance_id=identifier))
            if state["state"] == "READY":
                break
            assert state["state"] == "STARTING" and time.monotonic() < deadline, state
            await asyncio.sleep(0.02)
        assert state["use_count"] == clients, state
        await barriers[1].wait()
        await barriers[2].wait()
        await asyncio.to_thread(broker.call, "close", dict(instance_id=identifier, dry_run=False))
        deadline = time.monotonic() + 10
        while True:
            state = await asyncio.to_thread(broker.call, "status", dict(instance_id=identifier))
            if state["state"] == "EXITED":
                break
            assert state["state"] not in ("BLOCKED", "UNRESPONSIVE") and time.monotonic() < deadline, state
            await asyncio.sleep(0.02)
        assert state["use_count"] == state["scope_count"] == 0
        report.append(dict(round=number, instance_id=identifier, clients=values, final=state["state"]))
        if number % 10 == 0:
            print(json.dumps(dict(event="contention_progress", rounds=number + 1, clients=clients)), flush=True)


async def run(host: Path, clients: int, rounds: int) -> dict:
    host.mkdir(parents=True)
    project = host / "Concurrent.uproject"
    project.write_text("{}", encoding="utf-8")
    root = host / "Runtime"
    process = start_broker(root)
    broker = BrokerClient(root, str(host))
    result, records = dict(clients=clients, rounds=rounds, python=sys.executable,
                           package_path=ue_node_nexus_mcp.__file__, version=ue_node_nexus_mcp.__version__), []
    try:
        deadline = time.monotonic() + 15
        while True:
            try:
                if ping(root)["ready"]:
                    break
            except BridgeError:
                pass
            assert process.poll() is None and time.monotonic() < deadline
            await asyncio.sleep(0.02)
        await asyncio.to_thread(broker.connect)
        barriers = [asyncio.Barrier(clients + 1) for _ in range(3)]
        answers = asyncio.Queue()
        async with asyncio.TaskGroup() as group:
            for index in range(clients):
                group.create_task(agent(index, project, root, rounds, barriers, answers))
            group.create_task(coordinate(project, broker, clients, rounds, barriers, answers, records))
        events = [json.loads(line) for line in (root / "Logs/lifecycle.jsonl").read_text(encoding="utf-8").splitlines()]
        spawned = [row for row in events if row.get("event") == "spawned"]
        assert len(spawned) == rounds
        assert len(set(row["instance_id"] for row in spawned)) == rounds
        result.update(ok=True, spawned=len(spawned), records=records)
    finally:
        broker.close()
        try:
            await asyncio.to_thread(process.wait, timeout=15)
        finally:
            (host / "contention-result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--clients", type=int, default=32)
    parser.add_argument("--rounds", type=int, default=100)
    args = parser.parse_args()
    asyncio.run(run(ROOT / "build" / args.name, args.clients, args.rounds))
