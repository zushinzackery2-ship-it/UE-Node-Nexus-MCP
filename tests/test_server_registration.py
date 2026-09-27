from __future__ import annotations

import asyncio

from ue_node_nexus_mcp.contracts import THIN_MCP_OPERATIONS
from ue_node_nexus_mcp.server import mcp


def test_server_registers_exactly_the_thin_facade_tools() -> None:
    registered_tools = set(tool.name for tool in asyncio.run(mcp.list_tools()))

    assert registered_tools == THIN_MCP_OPERATIONS
