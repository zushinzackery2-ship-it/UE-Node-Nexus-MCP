from __future__ import annotations

from typing import Any, Literal

from .contracts import require_list, require_mapping, require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import mcp


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
def graph_node_info_get(
    asset_path: str,
    graph_name: str | None = None,
    graph_kind: Literal["material", "blueprint", "auto"] = "auto",
    section: Literal["all", "brief", "input", "output", "param", "links"] = "all",
    max_nodes: int | None = None,
    format: Literal["indexed", "grouped", "text"] = "indexed",
    id_mode: Literal["alias", "real", "both"] = "alias",
) -> dict[str, Any]:
    """Return a whole graph in dense indexed form; grouped is readable by node type."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "graph_node_info_get",
        {
            "asset_path": asset_path,
            "graph_name": graph_name,
            "graph_kind": graph_kind,
            "section": section,
            "max_nodes": max_nodes,
            "format": format,
            "id_mode": id_mode,
        },
    )


@mcp.tool()
def graph_node_info_get_w_pos(
    asset_path: str,
    graph_name: str | None = None,
    graph_kind: Literal["material", "blueprint", "auto"] = "auto",
    section: Literal["all", "brief", "input", "output", "param", "links"] = "all",
    max_nodes: int | None = None,
    format: Literal["indexed", "grouped", "text"] = "indexed",
    id_mode: Literal["alias", "real", "both"] = "alias",
) -> dict[str, Any]:
    """Return a whole graph in dense indexed or grouped form, including node positions."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "graph_node_info_get_w_pos",
        {
            "asset_path": asset_path,
            "graph_name": graph_name,
            "graph_kind": graph_kind,
            "section": section,
            "max_nodes": max_nodes,
            "format": format,
            "id_mode": id_mode,
        },
    )


@mcp.tool()
def node_class_params_get(
    graph_kind: Literal["material", "blueprint"],
    node_class: str,
) -> dict[str, Any]:
    """Return editable parameter template for a material or Blueprint node class."""
    require_non_empty_string(graph_kind, "graph_kind")
    require_non_empty_string(node_class, "node_class")
    return _call(
        "node_class_params_get",
        {
            "graph_kind": graph_kind,
            "node_class": node_class,
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
def node_info_get(
    asset_path: str,
    node_id: str,
    graph_name: str | None = None,
    graph_kind: Literal["material", "blueprint", "auto"] = "auto",
    section: Literal["all", "brief", "input", "output", "param", "links"] = "all",
    index: int | None = None,
    format: Literal["text", "compact_json"] = "text",
) -> dict[str, Any]:
    """Return one node as a compact readable edit view."""
    require_non_empty_string(asset_path, "asset_path")
    require_non_empty_string(node_id, "node_id")
    return _call(
        "node_info_get",
        {
            "asset_path": asset_path,
            "node_id": node_id,
            "graph_name": graph_name,
            "graph_kind": graph_kind,
            "section": section,
            "index": index,
            "format": format,
        },
    )


@mcp.tool()
def node_position_get(
    asset_path: str,
    node_id: str,
    graph_name: str | None = None,
    graph_kind: Literal["material", "blueprint", "auto"] = "auto",
) -> dict[str, Any]:
    """Return one graph node position without compiling or modifying the asset."""
    require_non_empty_string(asset_path, "asset_path")
    require_non_empty_string(node_id, "node_id")
    return _call(
        "node_position_get",
        {
            "asset_path": asset_path,
            "node_id": node_id,
            "graph_name": graph_name,
            "graph_kind": graph_kind,
        },
    )


@mcp.tool()
def node_position_set(
    asset_path: str,
    node_id: str,
    x: int,
    y: int,
    graph_name: str | None = None,
    graph_kind: Literal["material", "blueprint", "auto"] = "auto",
    dry_run: bool = True,
) -> dict[str, Any]:
    """Set one graph node position without triggering compile."""
    require_non_empty_string(asset_path, "asset_path")
    require_non_empty_string(node_id, "node_id")
    return _call(
        "node_position_set",
        {
            "asset_path": asset_path,
            "node_id": node_id,
            "x": x,
            "y": y,
            "graph_name": graph_name,
            "graph_kind": graph_kind,
            "dry_run": dry_run,
        },
    )


@mcp.tool()
def node_position_offset(
    asset_path: str,
    node_id: str,
    dx: int,
    dy: int,
    graph_name: str | None = None,
    graph_kind: Literal["material", "blueprint", "auto"] = "auto",
    dry_run: bool = True,
) -> dict[str, Any]:
    """Offset one graph node position without triggering compile."""
    require_non_empty_string(asset_path, "asset_path")
    require_non_empty_string(node_id, "node_id")
    return _call(
        "node_position_offset",
        {
            "asset_path": asset_path,
            "node_id": node_id,
            "dx": dx,
            "dy": dy,
            "graph_name": graph_name,
            "graph_kind": graph_kind,
            "dry_run": dry_run,
        },
    )


@mcp.tool()
def node_create(
    asset_path: str,
    node_class: str,
    graph_name: str | None = None,
    graph_kind: Literal["material", "blueprint", "auto"] = "auto",
    name: str | None = None,
    position: dict[str, Any] | None = None,
    params: dict[str, Any] | None = None,
    dry_run: bool = True,
) -> dict[str, Any]:
    """Create one material or Blueprint graph node with typed validation."""
    require_non_empty_string(asset_path, "asset_path")
    require_non_empty_string(node_class, "node_class")
    if position is not None:
        require_mapping(position, "position")
    if params is not None:
        require_mapping(params, "params")
    return _call(
        "node_create",
        {
            "asset_path": asset_path,
            "node_class": node_class,
            "graph_name": graph_name,
            "graph_kind": graph_kind,
            "name": name,
            "position": position,
            "params": params or {},
            "dry_run": dry_run,
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
