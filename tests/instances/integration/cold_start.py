"""Actual MCP processes bootstrap one independent manager without a controller host."""

import argparse
import asyncio
import json
import sys
import time

import ue_node_nexus_mcp
from ue_node_nexus_mcp.instances.identity.processes import is_alive
from tests.instances.live.host import ROOT
from tests.instances.live.stdio import connect


async def client(project, root, index, begin, acquired):
    async with connect(project, root, root.parent / "Workspaces" / str(index)) as peer:
        await begin.wait()
        result = await peer.execute("bridge_instance_list", project_path=str(project))
        assert result["instances"] == [] and result["policy"]["max_editors"] == 2, result
        identity = json.loads((root / "manager.json").read_text())["identity"]
        await acquired.wait()
        assert is_alive(identity)
        return identity


async def run(name, count):
    host = (ROOT / "build" / name).resolve()
    host.relative_to((ROOT / "build").resolve())
    host.mkdir()
    project = host / "Empty.uproject"
    project.write_text("{}", encoding="utf-8")
    root = host / "Runtime"
    root.mkdir()
    (root / "policy.json").write_text(json.dumps(dict(broker_idle_seconds=1, sweep_seconds=0.1)), encoding="utf-8")
    report = dict(clients=count, python=sys.executable, package_path=ue_node_nexus_mcp.__file__)
    try:
        begin, acquired = asyncio.Barrier(count), asyncio.Barrier(count)
        identities = await asyncio.gather(*(client(project, root, index, begin, acquired) for index in range(count)))
        assert all(identity == identities[0] for identity in identities), identities
        manager = identities[0]
        events = [json.loads(line) for line in (root / "Logs/lifecycle.jsonl").read_text().splitlines()]
        assert sum(row["event"] == "recovery_observed" for row in events) == 1
        deadline = time.monotonic() + 20
        while is_alive(manager) and time.monotonic() < deadline:
            await asyncio.sleep(0.5)
        assert not is_alive(manager)
        report.update(ok=True, manager=manager, manager_exited=True)
        print(json.dumps(report), flush=True)
    finally:
        (host / "cold-start-result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--clients", type=int, default=32)
    args = parser.parse_args()
    asyncio.run(run(args.name, args.clients))
