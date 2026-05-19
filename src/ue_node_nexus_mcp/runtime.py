from __future__ import annotations

from typing import Any

from mcp.server.fastmcp import FastMCP

from .bridge import BridgeError, UeBridgeClient

mcp = FastMCP("UE Node Nexus MCP")
bridge = UeBridgeClient()


def call_bridge(operation: str, payload: dict[str, Any]) -> dict[str, Any]:
    try:
        return bridge.call(operation, payload)
    except (BridgeError, ValueError) as exc:
        return {
            "ok": False,
            "operation": operation,
            "error": {
                "code": "mcp_bridge_error",
                "message": str(exc),
                "details": {},
            },
            "diagnostics": [],
            "warnings": [],
        }
