from __future__ import annotations

from typing import Any

from .contracts import BRIDGE_OPERATIONS
from .operation_registry import get_operation_spec
from .runtime import call_bridge as _call

VALID_RESPONSE_MODES = {"silent", "brief", "ids_only", "delta", "summary", "full", "debug"}


def execute_operation(operation: str, payload: dict[str, Any]) -> dict[str, Any]:
    spec = get_operation_spec(operation)
    if spec.local_mcp:
        return execute_local_operation(operation, payload)
    if operation not in BRIDGE_OPERATIONS:
        raise ValueError(f"operation is not a bridge operation: {operation}")
    return _call(operation, payload)


def execute_local_operation(operation: str, payload: dict[str, Any]) -> dict[str, Any]:
    """Dispatch session-local control-plane ops handled inside the MCP server."""
    if operation == "bridge_contract_check":
        from .tools_system import bridge_contract_check

        mode = payload.get("mode", "enabled")
        if not isinstance(mode, str):
            raise ValueError("mode must be a string")
        return bridge_contract_check(mode=mode)  # type: ignore[arg-type]
    if operation == "bridge_instance_list":
        from .tools_system import bridge_instance_list

        return bridge_instance_list()
    if operation == "bridge_instance_select":
        from .tools_system import bridge_instance_select

        pid = payload.get("pid")
        project = payload.get("project")
        if pid is not None and not isinstance(pid, int):
            raise ValueError("pid must be an integer")
        if project is not None and not isinstance(project, str):
            raise ValueError("project must be a string")
        return bridge_instance_select(pid=pid, project=project)
    if operation == "material_lint":
        from .material_lint import material_lint

        asset_path = payload.get("asset_path")
        if not isinstance(asset_path, str):
            raise ValueError("asset_path must be a string")
        graph_kind = payload.get("graph_kind", "material")
        if not isinstance(graph_kind, str):
            raise ValueError("graph_kind must be a string")
        max_texture_checks = payload.get("max_texture_checks", 48)
        if not isinstance(max_texture_checks, int):
            raise ValueError("max_texture_checks must be an integer")
        return material_lint(
            asset_path=asset_path,
            graph_kind=graph_kind,
            max_texture_checks=max_texture_checks,
        )
    raise ValueError(f"local operation is not supported by ue_execute: {operation}")


def preflight_execute_request(operation: str, payload: dict[str, Any], response_options: dict[str, Any]) -> dict[str, Any] | None:
    if operation != "graph_snapshot_get":
        return None
    if response_options.get("allow_heavy") is True:
        return None

    graph_format = str(payload.get("format", "")).lower()
    include_node_params = payload.get("include_node_params") is True
    node_params_format = str(payload.get("node_params_format", "compact")).lower()
    if graph_format != "full" or not include_node_params or node_params_format != "full":
        return None

    asset_path = payload.get("asset_path")
    graph_kind = payload.get("graph_kind", "auto")
    graph_name = payload.get("graph_name")
    recommended_payload = {
        "asset_path": asset_path,
        "graph_kind": graph_kind,
        "format": "wires_tiny",
        "include_node_params": False,
        "include_links": payload.get("include_links", True),
    }
    if graph_name:
        recommended_payload["graph_name"] = graph_name

    return {
        "ok": False,
        "error": {
            "code": "heavy_graph_snapshot_blocked",
            "message": "graph_snapshot_get(format=\"full\", include_node_params=true, node_params_format=\"full\") is a heavy whole-graph schema read. Use compact node params or fetch full node params by node_id.",
            "details": {
                "operation": operation,
                "asset_path": asset_path,
                "override": {"response": {"allow_heavy": True}},
            },
        },
        "data": {
            "recommended_read": {
                "operation": "graph_snapshot_get",
                "payload": recommended_payload,
                "response": {"mode": "summary"},
            },
            "recommended_param_read": {
                "operation": "node_params_get",
                "payload": {
                    "asset_path": asset_path,
                    "graph_kind": graph_kind,
                    "node_id": "<node_id from graph snapshot>",
                },
                "response": {"mode": "summary"},
            },
        }
    }
