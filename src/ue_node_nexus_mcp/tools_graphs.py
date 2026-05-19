from __future__ import annotations

from typing import Any, Literal

from .contracts import require_list, require_mapping, require_non_empty_string
from .server import _call, mcp


@mcp.tool()
def graph_snapshot_get(
    asset_path: str,
    graph_name: str | None = None,
    graph_kind: Literal["material", "blueprint", "auto"] = "auto",
    format: Literal["wires_tiny", "wires_min", "wires", "compact", "full"] = "wires_tiny",
    include_node_params: bool = False,
    include_links: bool = True,
) -> dict[str, Any]:
    """Return a graph snapshot. Minimal wire text is the default to protect model context."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "graph_snapshot_get",
        {
            "asset_path": asset_path,
            "graph_name": graph_name,
            "graph_kind": graph_kind,
            "format": format,
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
