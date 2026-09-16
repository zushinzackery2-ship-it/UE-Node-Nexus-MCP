"""60-minute acceptance with three real stdio MCP servers in distinct workspaces."""

import argparse
import asyncio
import json
import os
from pathlib import Path
import sys
import time
import importlib.metadata

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from ue_node_nexus_mcp.instances.broker.client import BrokerClient
from ue_node_nexus_mcp.instances.session.binding import EditorSession
from ue_node_nexus_mcp.instances.identity.processes import inspect_process, is_alive
from ue_node_nexus_mcp.instances.identity.resources import sample
from ue_node_nexus_mcp.bridge import UeBridgeClient
import ue_node_nexus_mcp
from .host import prepare


def decoded(result) -> dict:
    text = next(item.text for item in result.content if item.type == "text")
    value = json.loads(text)
    assert value.get("ok"), value
    return value


async def agent(project: Path, root: Path, index: int, seconds: float) -> dict:
    workspace = project.parent / "Workspaces" / str(index)
    workspace.mkdir(parents=True)
    stderr = (workspace / "mcp-stderr.log").open("w", encoding="utf-8")
    parameters = StdioServerParameters(command=sys.executable,
        args=["-m", "ue_node_nexus_mcp.server", "--project", str(project)], cwd=str(workspace),
        env=dict(os.environ, UE_NEXUS_RUNTIME_DIR=str(root), UE_NEXUS_LOG_DIR=str(root / "ClientLogs")))
    samples, identities = [], set()
    try:
        async with stdio_client(parameters, errlog=stderr) as streams:
            async with ClientSession(*streams) as client:
                await client.initialize()
                deadline = time.monotonic() + seconds
                while time.monotonic() < deadline:
                    start = time.monotonic()
                    acquired = decoded(await client.call_tool("ue_execute", dict(operation="bridge_instance_ensure",
                                       payload=dict(dry_run=False), response=dict(mode="full"))))
                    instance = acquired["data"]["instance"]
                    identities.add(instance["instance_id"])
                    assert len(identities) == 1, identities
                    samples.append(dict(ensure_ms=(time.monotonic() - start) * 1000, instance_id=instance["instance_id"], pid=instance["pid"]))
                    decoded(await client.call_tool("ue_execute", dict(operation="project_context_get", payload=dict(), response=dict(mode="full"))))
                    decoded(await client.call_tool("ue_execute", dict(operation="bridge_instance_release", payload=dict(), response=dict(mode="full"))))
                    await asyncio.sleep(min(15, max(0, deadline - time.monotonic())))
        return dict(workspace=str(workspace), cycles=len(samples), samples=samples)
    finally:
        stderr.close()
        (workspace / "samples.json").write_text(json.dumps(samples, indent=2), encoding="utf-8")


async def run(name: str, engine: str, seconds: float) -> None:
    project = prepare(name)
    root = project.parent / "Runtime"
    session = EditorSession(BrokerClient(root, str(project.parent)), str(project))
    result = dict(project=str(project), duration_seconds=seconds, python=sys.executable,
                  package_path=ue_node_nexus_mcp.__file__, version=importlib.metadata.version("ue-node-nexus-mcp"))
    started = time.monotonic()
    monitor = None
    manager = None
    samples = []
    try:
        session.ensure(dict(mode="reuse_or_start", engine_path=engine, dry_run=False))
        while True:
            state = session.status()
            if state["state"] == "READY":
                break
            assert state["state"] not in ("EXITED", "UNRESPONSIVE"), state
            assert time.monotonic() - started < 240, state
            await asyncio.sleep(1)
        identifier = state["instance_id"]
        result["builds"] = UeBridgeClient(instances=session).call("bridge_capabilities_get", dict())["data"]["build"]
        result["plugin_identities"] = dict((name, json.loads(path.read_text())) for name, path in
            ((name, project.parent / "Plugins" / name / "BuildIdentity.json") for name in ("UeNodeNexusBridge", "UeNodeNexusVfxBridge")))
        for name, build in result["builds"].items():
            expected = result["plugin_identities"]["UeNodeNexusBridge" if name == "UeNodeNexusGuard" else name]
            for field in ("version", "source_fingerprint", "contract_version"):
                assert build[field] == expected[field], (name, field, build, expected)
        manager = json.loads((root / "manager.json").read_text())["identity"]
        result["manager_before"] = sample(manager)
        result["harness_before"] = sample(inspect_process(os.getpid()))
        session.release()
        monitor = asyncio.create_task(observe(session, manager, samples, seconds))
        print(json.dumps(dict(event="soak_started", seconds=seconds, instance_id=identifier, pid=state["pid"])), flush=True)
        result["agents"] = await asyncio.gather(*(agent(project, root, index, seconds) for index in range(3)))
        if monitor.done():
            await monitor
        assert len(samples) >= seconds / 20, "resource observation stopped during the soak"
        deadline = time.monotonic() + 145
        while time.monotonic() < deadline:
            state = session.status()
            if state["state"] == "EXITED":
                break
            assert state["state"] != "BLOCKED", state
            await asyncio.sleep(2)
        assert state["state"] == "EXITED", state
        assert state.get("exit_code") == 0, state
        assert state["use_count"] == state["scope_count"] == 0, state
        assert all(row["samples"] and row["samples"][0]["instance_id"] == identifier for row in result["agents"])
        result.update(ok=True, final=state, elapsed_seconds=time.monotonic() - started)
        print(json.dumps(dict(event="soak_complete", cycles=[row["cycles"] for row in result["agents"]], normal_exit=True)), flush=True)
    finally:
        if monitor:
            monitor.cancel()
            await asyncio.gather(monitor, return_exceptions=True)
        session.close()
        result["resources"] = samples
        result["harness_after"] = sample(inspect_process(os.getpid()))
        if manager and result.get("ok"):
            deadline = time.monotonic() + 80
            while is_alive(manager) and time.monotonic() < deadline:
                await asyncio.sleep(2)
            result["manager_exited"] = not is_alive(manager)
            result["ok"] = result["ok"] and result["manager_exited"]
        result["log_bytes"] = sum(file.stat().st_size for file in root.rglob("*")
                                  if file.is_file() and (".log" in file.name or ".jsonl" in file.name))
        (project.parent / "soak-result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    assert result.get("ok"), result


async def observe(session, manager, samples, seconds):
    started = time.monotonic()
    next_report = 0
    while time.monotonic() - started < seconds:
        state = await asyncio.to_thread(session.status)
        samples.append(dict(elapsed=time.monotonic() - started, manager=sample(manager),
                            instance_id=state["instance_id"], scope_count=state["scope_count"],
                            private_working_set_bytes=state.get("private_working_set_bytes"), handle_count=state.get("handle_count")))
        if time.monotonic() >= next_report:
            print(json.dumps(dict(event="soak_progress", elapsed_seconds=time.monotonic() - started,
                                  state=state["state"], instance_id=state["instance_id"],
                                  scope_count=state["scope_count"], manager=samples[-1]["manager"])), flush=True)
            next_report = time.monotonic() + 60
        await asyncio.sleep(10)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--engine", required=True)
    parser.add_argument("--seconds", type=float, default=3600)
    args = parser.parse_args()
    asyncio.run(run(args.name, args.engine, args.seconds))
