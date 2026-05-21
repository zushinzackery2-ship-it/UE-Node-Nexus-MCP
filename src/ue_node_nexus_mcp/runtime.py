from __future__ import annotations

from pathlib import Path
from typing import Any

from mcp.server.fastmcp import FastMCP

from .bridge import BridgeError, UeBridgeClient

mcp = FastMCP("UE Node Nexus MCP")
bridge = UeBridgeClient()


def _read_remaining_errors() -> str:
    for parent in Path(__file__).resolve().parents:
        status_path = parent / "Task-Status.md"
        if not status_path.exists():
            continue
        for line in status_path.read_text(encoding="utf-8", errors="replace").splitlines():
            stripped = line.strip()
            if stripped.startswith("- remaining_errors:"):
                return stripped.removeprefix("- remaining_errors:").strip()
            if stripped.startswith("remaining_errors:"):
                return stripped.removeprefix("remaining_errors:").strip()
    return ""


def _with_remaining_errors(response: dict[str, Any]) -> dict[str, Any]:
    response.setdefault("remaining_errors", _read_remaining_errors())
    return response


def call_bridge(operation: str, payload: dict[str, Any]) -> dict[str, Any]:
    try:
        return _with_remaining_errors(bridge.call(operation, payload))
    except (BridgeError, ValueError) as exc:
        return _with_remaining_errors({
            "ok": False,
            "operation": operation,
            "error": {
                "code": "mcp_bridge_error",
                "message": str(exc),
                "details": {},
            },
            "diagnostics": [],
            "warnings": [],
        })
