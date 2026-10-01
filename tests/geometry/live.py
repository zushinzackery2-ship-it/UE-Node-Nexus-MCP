"""Exercise the installed MCP and engine plugin, including an editor restart."""

import argparse
import asyncio
import json
from pathlib import Path
import sys
import time

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def run(project, output):
    reports = []
    parameters = StdioServerParameters(command=sys.executable, args=[
        "-m", "ue_node_nexus_mcp.server", "--project", str(project)],
        env=dict(UE_NEXUS_TIMEOUT_SECONDS="240"))
    async with stdio_client(parameters) as (reader, writer):
        async with ClientSession(reader, writer, read_timeout_seconds=300) as session:
            await session.initialize()

            async def call(operation, allow_recovery=False, **payload):
                result = await session.call_tool("ue_execute", dict(operation=operation, payload=payload, response=dict(mode="full")))
                data = result.structured_content or json.loads(result.content[0].text)
                reports.append(dict(operation=operation, response=data))
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_text(json.dumps(reports, indent=2), encoding="utf-8")
                if not data.get("ok"):
                    blockers = data.get("error", dict()).get("details", dict()).get("blockers")
                    if allow_recovery and blockers == ["recovery_quarantine"]:
                        return None
                    raise RuntimeError(json.dumps(data))
                print(operation + " ok", flush=True)
                return data["data"]

            async def ensure():
                result = await call("bridge_instance_ensure", project_path=str(project), mode="reuse_or_start", dry_run=False)
                deadline = time.monotonic() + 180
                while not result.get("instance", dict()).get("ready"):
                    if time.monotonic() > deadline:
                        raise RuntimeError("editor start timeout")
                    await asyncio.sleep(2)
                    result = await call("bridge_instance_status", instance_id=result["instance"]["instance_id"])

            await ensure()
            capabilities = await call("bridge_capabilities_get")
            assert "mesh_geometry_build" in json.dumps(capabilities)
            path = "/Game/NexusGeometryProof/Surface_" + str(int(time.time()))
            recipe = dict(grid=dict(size=[2400, 2400, 0], cells=[32, 32, 0]), ops=[
                dict(op="lattice", dimensions=[3, 3, 2], interpolation="cubic", offsets=[
                    dict(index=[1, 1, 0], delta=[0, 0, 400]), dict(index=[1, 1, 1], delta=[0, 0, 400])]),
                dict(op="remesh", target_edge_length=80, iterations=3, automatic=True),
                dict(op="noise", amplitude=18, scale=350, seed=29, lock_boundary=True)])
            preview = await call("mesh_geometry_build", output_asset=path, recipe=recipe, dry_run=True)
            result = await call("mesh_geometry_build", output_asset=path, recipe=recipe, dry_run=False, save=True)
            assert result["ready"] and result["saved"] and result["quality"]["degenerate_triangles"] == 0
            assert preview["quality"] == result["quality"]
            revision = result["revision"]
            repeated = await call("mesh_geometry_build", output_asset=path, recipe=recipe, dry_run=False, save=True)
            assert repeated["reused"] and repeated["revision"] == revision
            before = await call("mesh_geometry_get", asset_path=path, lattice_dimensions=[3, 3, 2])
            assert before["recipe"] == recipe and len(before["lattice_points"]) == 18
            deadline = time.monotonic() + 90
            while await call("bridge_instance_close", allow_recovery=True, dry_run=False, wait=True) is None:
                if time.monotonic() > deadline:
                    raise RuntimeError("manager recovery quarantine did not complete")
                await asyncio.sleep(1)
            await ensure()
            after = await call("mesh_geometry_get", asset_path=path)
            assert after["revision"] == revision and after["recipe"] == recipe
            await call("bridge_instance_release")
    print("installed geometry proof passed, including restart", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    asyncio.run(run(arguments.project.resolve(), arguments.output.resolve()))
