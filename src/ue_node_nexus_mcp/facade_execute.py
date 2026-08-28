from __future__ import annotations

from typing import Any, Callable

from .contracts import BRIDGE_OPERATIONS
from .operation_registry import get_operation_spec
from .runtime import call_bridge as _call

EXECUTE_RESPONSE_MODES = {"silent", "brief", "ids_only", "delta", "summary", "full", "debug"}


def execute_operation(operation: str, payload: dict[str, Any]) -> dict[str, Any]:
    spec = get_operation_spec(operation)
    if spec.local_mcp:
        return execute_local_operation(operation, payload)
    if operation not in BRIDGE_OPERATIONS:
        raise ValueError(f"operation is not a bridge operation: {operation}")
    handler = _client_side_handler(operation)
    if handler is not None:
        return handler(payload)
    return _call(operation, payload)


def _client_side_handler(operation: str) -> Callable[[dict[str, Any]], dict[str, Any]] | None:
    """Bridge operations that carry Python-side behavior on top of the raw call.

    This dispatch table is the single place that guarantees client-side logic
    (material client_id expansion, diagnostics log enrichment, Niagara verbose
    field trimming) actually runs on the live ue_execute/ue_read path instead
    of only inside unregistered legacy wrappers.
    """
    if operation == "graph_patch_apply":
        from .tools_graph_writes import execute_graph_patch_apply

        return execute_graph_patch_apply
    if operation == "diagnostics_get":
        from .tools_system import execute_diagnostics_get

        return execute_diagnostics_get
    if operation.startswith("niagara_"):
        from .tools_niagara_common import call_niagara_trimmed

        return lambda call_payload: call_niagara_trimmed(operation, call_payload)
    return None


def execute_local_operation(operation: str, payload: dict[str, Any]) -> dict[str, Any]:
    """Dispatch session-local control-plane ops handled inside the MCP server."""
    if operation == "batch_execute":
        from .batch_execute import batch_execute

        continue_on_error = payload.get("continue_on_error", False)
        if not isinstance(continue_on_error, bool):
            raise ValueError("continue_on_error must be a boolean")
        return batch_execute(payload.get("operations"), continue_on_error=continue_on_error)
    if operation in ("task_submit", "task_status", "task_result", "task_cancel"):
        from . import task_queue

        if operation == "task_submit":
            inner_operation = payload.get("operation")
            inner_payload = payload.get("payload")
            if not isinstance(inner_operation, str) or not inner_operation.strip():
                raise ValueError("operation must be a non-empty string")
            if inner_payload is not None and not isinstance(inner_payload, dict):
                raise ValueError("payload must be an object")
            return task_queue.task_submit(inner_operation, inner_payload)
        task_id = payload.get("task_id")
        if operation == "task_status":
            if task_id is not None and not isinstance(task_id, str):
                raise ValueError("task_id must be a string")
            return task_queue.task_status(task_id)
        if not isinstance(task_id, str) or not task_id.strip():
            raise ValueError("task_id must be a non-empty string")
        if operation == "task_result":
            return task_queue.task_result(task_id)
        return task_queue.task_cancel(task_id)
    if operation == "viewport_capture_status":
        from .tools_viewport import viewport_capture_status

        file_path = payload.get("file_path")
        if not isinstance(file_path, str) or not file_path.strip():
            raise ValueError("file_path must be a non-empty string")
        return viewport_capture_status(file_path)
    if operation == "workflow_guide_get":
        from .workflow_guides import workflow_guide_get

        category = payload.get("category")
        query = payload.get("query")
        if category is not None and not isinstance(category, str):
            raise ValueError("category must be a string")
        if query is not None and not isinstance(query, str):
            raise ValueError("query must be a string")
        return workflow_guide_get(category=category, query=query)
    if operation == "log_tail_get":
        from .tools_system import log_tail_get

        tail_kb = payload.get("tail_kb", 64)
        match = payload.get("match")
        max_lines = payload.get("max_lines", 200)
        if not isinstance(tail_kb, int) or isinstance(tail_kb, bool):
            raise ValueError("tail_kb must be an integer")
        if not isinstance(max_lines, int) or isinstance(max_lines, bool):
            raise ValueError("max_lines must be an integer")
        if match is not None and not isinstance(match, str):
            raise ValueError("match must be a string")
        return log_tail_get(tail_kb=tail_kb, match=match, max_lines=max_lines)
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
