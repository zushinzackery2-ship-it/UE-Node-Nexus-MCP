"""A real MCP polling context/status/logs must not keep a managed editor alive."""

import argparse
import asyncio
import json
import sys
import time

import ue_node_nexus_mcp
from ue_node_nexus_mcp.instances.identity.processes import is_alive
from .host import prepare
from .stdio import connect


async def run(name, engine):
    project = prepare(name)
    root = project.parent / "Runtime"
    report = dict(project=str(project), python=sys.executable, package_path=ue_node_nexus_mcp.__file__, samples=[])
    manager = None
    try:
        async with connect(project, root, project.parent / "PollingClient") as peer:
            acquired = await peer.execute("bridge_instance_ensure", mode="reuse_or_start", engine_path=engine, dry_run=False)
            identifier = acquired["instance"]["instance_id"]
            deadline = time.monotonic() + 240
            while True:
                state = await peer.execute("bridge_instance_status")
                if state["state"] == "READY":
                    break
                assert state["state"] not in ("EXITED", "UNRESPONSIVE") and time.monotonic() < deadline, state
                await asyncio.sleep(1)
            report["builds"] = (await peer.execute("bridge_capabilities_get"))["build"]
            await peer.execute("project_context_get")
            manager = json.loads((root / "manager.json").read_text())["identity"]
            report["policy"] = (await peer.execute("bridge_instance_list"))["policy"]
            started, wall_start, next_report = time.monotonic(), time.time(), 0
            while time.monotonic() - started < 500:
                await peer.call("ue_context_get", dict(include_counts=False))
                logs = await peer.execute("log_tail_get", max_lines=5)
                assert "Saved/Nexus/Logs" in logs["log_path"].replace("\\", "/"), logs
                state = await peer.execute("bridge_instance_status")
                assert state["instance_id"] == identifier and state["scope_count"] == 0, state
                elapsed = time.monotonic() - started
                report["samples"].append(dict(elapsed_seconds=elapsed, state=state["state"], use_count=state["use_count"]))
                assert state["state"] not in ("BLOCKED", "UNRESPONSIVE"), state
                if elapsed >= next_report:
                    print(json.dumps(dict(event="idle_poll_progress", **report["samples"][-1])), flush=True)
                    next_report = elapsed + 60
                if state["state"] == "EXITED":
                    break
                await asyncio.sleep(5)
            assert state["state"] == "EXITED" and state["exit_code"] == 0, state
            assert state["use_count"] == state["scope_count"] == 0 and is_alive(manager), state
            await peer.call("ue_context_get", dict(include_counts=False))
            assert (await peer.execute("log_tail_get", max_lines=5))["returned_lines"] > 0
            events = [json.loads(line) for line in (root / "Logs/lifecycle.jsonl").read_text().splitlines()]
            assert sum(row["event"] == "spawned" for row in events) == 1
            close = next(row for row in events if row["event"] == "close_requested")
            assert close["time"] - wall_start <= 440, close
            report.update(final=state, elapsed_seconds=elapsed, close_requested_seconds=close["time"] - wall_start,
                          polling_client_survived=True, logs_read_after_exit=True)
        deadline = time.monotonic() + 80
        while is_alive(manager) and time.monotonic() < deadline:
            await asyncio.sleep(2)
        assert not is_alive(manager)
        report.update(ok=True, manager_exited=True)
        print(json.dumps(dict(ok=True, close_requested_seconds=report["close_requested_seconds"], exit_code=0)), flush=True)
    finally:
        (project.parent / "idle-result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--engine", required=True)
    args = parser.parse_args()
    asyncio.run(run(args.name, args.engine))
