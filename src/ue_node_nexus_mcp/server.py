from __future__ import annotations

from typing import Any, Literal

from mcp.server.fastmcp import FastMCP

from .bridge import BridgeError, UeBridgeClient
from .contracts import require_list, require_mapping, require_non_empty_string

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


@mcp.tool()
def asset_list(
    class_names: list[str] | None = None,
    package_paths: list[str] | None = None,
    recursive: bool = True,
    limit: int = 100,
    cursor: str | None = None,
) -> dict[str, Any]:
    """List Unreal assets through the UE bridge without mutating editor state."""
    return _call(
        "asset_list",
        {
            "class_names": class_names or [],
            "package_paths": package_paths or [],
            "recursive": recursive,
            "limit": limit,
            "cursor": cursor,
        },
    )


@mcp.tool()
def asset_get(asset_path: str) -> dict[str, Any]:
    """Return metadata for one Unreal asset."""
    require_non_empty_string(asset_path, "asset_path")
    return _call("asset_get", {"asset_path": asset_path})


@mcp.tool()
def level_current_get() -> dict[str, Any]:
    """Return the current editor level identity and dirty state."""
    return _call("level_current_get", {})


@mcp.tool()
def level_actors_list(
    include_components: bool = False,
    class_names: list[str] | None = None,
    limit: int = 200,
    cursor: str | None = None,
) -> dict[str, Any]:
    """List actors in the current editor level."""
    return _call(
        "level_actors_list",
        {
            "include_components": include_components,
            "class_names": class_names or [],
            "limit": limit,
            "cursor": cursor,
        },
    )


@mcp.tool()
def graph_snapshot_get(
    asset_path: str,
    graph_name: str | None = None,
    graph_kind: Literal["material", "blueprint", "auto"] = "auto",
    include_node_params: bool = True,
    include_links: bool = True,
) -> dict[str, Any]:
    """Return a complete graph snapshot with nodes, pins, links, and diagnostics hints."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "graph_snapshot_get",
        {
            "asset_path": asset_path,
            "graph_name": graph_name,
            "graph_kind": graph_kind,
            "include_node_params": include_node_params,
            "include_links": include_links,
        },
    )


@mcp.tool()
def node_params_get(
    asset_path: str,
    node_id: str,
    graph_name: str | None = None,
    graph_kind: Literal["material", "blueprint", "auto"] = "auto",
) -> dict[str, Any]:
    """Return typed editable parameters for a graph node."""
    require_non_empty_string(asset_path, "asset_path")
    require_non_empty_string(node_id, "node_id")
    return _call(
        "node_params_get",
        {
            "asset_path": asset_path,
            "node_id": node_id,
            "graph_name": graph_name,
            "graph_kind": graph_kind,
        },
    )


@mcp.tool()
def node_params_set(
    asset_path: str,
    node_id: str,
    params: dict[str, Any],
    graph_name: str | None = None,
    graph_kind: Literal["material", "blueprint", "auto"] = "auto",
    dry_run: bool = True,
    compile_after: bool = True,
) -> dict[str, Any]:
    """Set typed node parameters, then return UE-side validation and compile diagnostics."""
    require_non_empty_string(asset_path, "asset_path")
    require_non_empty_string(node_id, "node_id")
    require_mapping(params, "params")
    return _call(
        "node_params_set",
        {
            "asset_path": asset_path,
            "node_id": node_id,
            "params": params,
            "graph_name": graph_name,
            "graph_kind": graph_kind,
            "dry_run": dry_run,
            "compile_after": compile_after,
        },
    )


@mcp.tool()
def graph_patch_apply(
    asset_path: str,
    operations: list[dict[str, Any]],
    graph_name: str | None = None,
    graph_kind: Literal["material", "blueprint", "auto"] = "auto",
    dry_run: bool = True,
    compile_after: bool = True,
) -> dict[str, Any]:
    """Apply a declarative graph patch, then return pin integrity and compile diagnostics."""
    require_non_empty_string(asset_path, "asset_path")
    require_list(operations, "operations")
    return _call(
        "graph_patch_apply",
        {
            "asset_path": asset_path,
            "operations": operations,
            "graph_name": graph_name,
            "graph_kind": graph_kind,
            "dry_run": dry_run,
            "compile_after": compile_after,
        },
    )


@mcp.tool()
def material_instance_params_get(asset_path: str) -> dict[str, Any]:
    """Return scalar, vector, texture, and static parameters for a material instance."""
    require_non_empty_string(asset_path, "asset_path")
    return _call("material_instance_params_get", {"asset_path": asset_path})


@mcp.tool()
def material_instance_params_set(
    asset_path: str,
    params: list[dict[str, Any]],
    dry_run: bool = True,
    compile_after: bool = True,
) -> dict[str, Any]:
    """Set material instance parameters with typed validation and diagnostics."""
    require_non_empty_string(asset_path, "asset_path")
    require_list(params, "params")
    return _call(
        "material_instance_params_set",
        {
            "asset_path": asset_path,
            "params": params,
            "dry_run": dry_run,
            "compile_after": compile_after,
        },
    )


@mcp.tool()
def asset_compile(asset_path: str) -> dict[str, Any]:
    """Compile or recompile a Blueprint or material asset and return structured diagnostics."""
    require_non_empty_string(asset_path, "asset_path")
    return _call("asset_compile", {"asset_path": asset_path})


@mcp.tool()
def asset_validate(asset_path: str) -> dict[str, Any]:
    """Validate an asset and return machine-readable diagnostics."""
    require_non_empty_string(asset_path, "asset_path")
    return _call("asset_validate", {"asset_path": asset_path})


@mcp.tool()
def asset_save(
    asset_path: str,
    only_if_dirty: bool = True,
    fail_if_open_editor_conflict: bool = True,
) -> dict[str, Any]:
    """Save one asset package while reporting dirty/read-only/editor conflict state."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "asset_save",
        {
            "asset_path": asset_path,
            "only_if_dirty": only_if_dirty,
            "fail_if_open_editor_conflict": fail_if_open_editor_conflict,
        },
    )


@mcp.tool()
def diagnostics_get(
    asset_path: str | None = None,
    severity: Literal["info", "warning", "error", "all"] = "all",
) -> dict[str, Any]:
    """Return recent UE bridge diagnostics, optionally filtered by asset and severity."""
    return _call(
        "diagnostics_get",
        {
            "asset_path": asset_path,
            "severity": severity,
        },
    )


def main() -> None:
    mcp.run()


if __name__ == "__main__":
    main()
