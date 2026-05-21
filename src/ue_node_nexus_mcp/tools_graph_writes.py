from __future__ import annotations

from typing import Any, Literal

from .contracts import require_list, require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import mcp


@mcp.tool()
def graph_patch_apply(
    asset_path: str,
    operations: list[dict[str, Any]],
    graph_name: str | None = None,
    graph_kind: Literal["material", "material_function", "blueprint", "auto"] = "auto",
    dry_run: bool = True,
    compile_after: bool = True,
    format: Literal["compact", "full"] = "compact",
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
            "format": format,
        },
    )


@mcp.tool()
def graph_build_apply(
    asset_path: str,
    nodes: list[dict[str, Any]],
    links: list[dict[str, Any]] | None = None,
    material_outputs: list[dict[str, Any]] | None = None,
    graph_name: str | None = None,
    graph_kind: Literal["material", "material_function"] = "material",
    dry_run: bool = True,
    compile_after: bool = True,
    format: Literal["compact", "full"] = "compact",
) -> dict[str, Any]:
    """Build a material or material function graph from compact node/link specs in one bridge transaction."""
    require_non_empty_string(asset_path, "asset_path")
    require_list(nodes, "nodes")
    if links is not None:
        require_list(links, "links")
    if material_outputs is not None:
        require_list(material_outputs, "material_outputs")
    return _call(
        "graph_build_apply",
        {
            "asset_path": asset_path,
            "nodes": nodes,
            "links": links or [],
            "material_outputs": material_outputs or [],
            "graph_name": graph_name,
            "graph_kind": graph_kind,
            "dry_run": dry_run,
            "compile_after": compile_after,
            "format": format,
        },
    )
