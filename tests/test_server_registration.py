from __future__ import annotations

from ue_node_nexus_mcp.contracts import THIN_MCP_OPERATIONS
from ue_node_nexus_mcp.server import mcp


def test_server_registers_exactly_the_thin_facade_tools() -> None:
    registered_tools = set(mcp._tool_manager._tools)

    assert registered_tools == THIN_MCP_OPERATIONS
