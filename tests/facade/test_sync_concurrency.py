"""The MCP event loop stays available while a sync workflow waits on its worker."""

import asyncio
import threading

from ue_node_nexus_mcp import runtime, tools_sync
from ue_node_nexus_mcp import facade_capabilities  # Registers the capability facade exercised below.


def test_registered_sync_does_not_block_polling_or_capability_queries(all_features, monkeypatch):
    entered, release = threading.Event(), threading.Event()

    def managed(bridge, action, paths, options, runner):
        if action == "status":
            entered.set()
            assert release.wait(5)
            return dict(action=action, status="complete")
        return dict(action=action, status="running", job_id="observable")

    monkeypatch.setattr(tools_sync, "run_managed_sync", managed)

    async def verify():
        original = asyncio.create_task(runtime.mcp.call_tool("ue_sync", dict(action="status")))
        try:
            assert await asyncio.to_thread(entered.wait, 3)
            await asyncio.wait_for(runtime.mcp.call_tool("ue_sync", dict(action="job_status", options=dict(job_id="observable"))), 1)
            await asyncio.wait_for(runtime.mcp.call_tool(facade_capabilities.ue_capability_get.__name__, dict()), 1)
            assert not original.done()
        finally:
            release.set()
            await original

    asyncio.run(verify())
