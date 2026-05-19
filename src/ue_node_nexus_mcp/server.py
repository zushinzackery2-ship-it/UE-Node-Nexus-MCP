from __future__ import annotations

from typing import Any

from mcp.server.fastmcp import FastMCP

from .bridge import BridgeError, UeBridgeClient

mcp = FastMCP("UE Node Nexus MCP")
bridge = UeBridgeClient()


def _call(operation: str, payload: dict[str, Any]) -> dict[str, Any]:
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


def main() -> None:
    mcp.run()


from .tools_assets import asset_create, asset_get, asset_list  # noqa: E402,F401
from .tools_blueprints import anim_blueprint_summary_get, blueprint_details_get  # noqa: E402,F401
from .tools_graphs import graph_patch_apply, graph_snapshot_get, node_params_get, node_params_set  # noqa: E402,F401
from .tools_materials import material_instance_params_get, material_instance_params_set  # noqa: E402,F401
from .tools_system import asset_compile, asset_save, asset_validate, diagnostics_get, level_actors_list, level_current_get  # noqa: E402,F401


if __name__ == "__main__":
    main()
