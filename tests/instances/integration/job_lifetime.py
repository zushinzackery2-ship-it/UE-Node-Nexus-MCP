"""The MCP process that bootstraps the Broker may exit before another MCP client."""

import argparse
import asyncio
import ctypes
from ctypes import wintypes as w
import importlib.metadata
import json
from pathlib import Path
import sys
import time

from mcp.os.win32 import utilities as sdk_windows

from ue_node_nexus_mcp.instances.broker.detached import api
from ue_node_nexus_mcp.instances.identity.processes import is_alive
from tests.instances.live.host import ROOT
from tests.instances.live.stdio import connect


async def run(name):
    host = (ROOT / "build" / name).resolve()
    host.relative_to((ROOT / "build").resolve())
    host.mkdir()
    project = host / "Fixture.uproject"
    project.write_text("{}", encoding="utf-8")
    root = host / "Runtime"
    root.mkdir()
    (root / "policy.json").write_text(json.dumps(dict(broker_idle_seconds=1, sweep_seconds=0.5)), encoding="utf-8")
    report = dict(python=sys.executable, mcp_version=importlib.metadata.version("mcp"))
    try:
        async with connect(project, root, host / "Remaining") as remaining:
            async with connect(project, root, host / "Creator") as creator:
                await creator.execute("bridge_instance_list")
                manager = json.loads((root / "manager.json").read_text())["identity"]
                report["manager"] = manager
                await remaining.execute("bridge_instance_list")
                native = api()
                handle = native.OpenProcess(0x1000, False, manager["pid"])
                assert handle
                try:
                    jobs = list(sdk_windows._process_jobs.items())
                    assert len(jobs) == 2, "both real MCP processes must have SDK lifetime Jobs"
                    report["sdk_job_pids"] = [process.pid for process, _ in jobs]
                    for _, sdk_job in jobs:
                        contained = w.BOOL()
                        assert native.IsProcessInJob(handle, int(sdk_job), ctypes.byref(contained))
                        assert not contained.value, "the shared Broker inherited an MCP lifetime Job"
                    report["outside_both_mcp_jobs"] = True
                finally:
                    native.CloseHandle(handle)
            assert is_alive(manager), "closing the creator's MCP Job terminated the shared Broker"
            await remaining.execute("bridge_instance_list")
            assert json.loads((root / "manager.json").read_text())["identity"] == manager
            report["creator_closed_consumer_continued"] = True
        deadline = time.monotonic() + 20
        while is_alive(manager) and time.monotonic() < deadline:
            await asyncio.sleep(0.5)
        assert not is_alive(manager)
        report.update(ok=True, manager_idle_exit=True)
        print(json.dumps(report), flush=True)
    finally:
        (host / "job-result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    asyncio.run(run(parser.parse_args().name))
