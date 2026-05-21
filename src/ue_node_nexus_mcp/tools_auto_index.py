from __future__ import annotations

from typing import Any, Literal

from .contracts import require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import mcp


@mcp.tool()
def auto_index_enable(root_path: str = "/Game", rebuild: bool = True) -> dict[str, Any]:
    """Enable persistent UE asset/folder auto-indexing and optionally rebuild immediately."""
    require_non_empty_string(root_path, "root_path")
    return _call("auto_index_enable", {"root_path": root_path, "rebuild": rebuild})


@mcp.tool()
def auto_index_disable() -> dict[str, Any]:
    """Disable Auto-Index listeners while keeping the persisted index file."""
    return _call("auto_index_disable", {})


@mcp.tool()
def auto_index_status(format: Literal["indexed", "full"] = "indexed") -> dict[str, Any]:
    """Return Auto-Index status, counts, freshness, and persisted index location."""
    return _call("auto_index_status", {"format": format})


@mcp.tool()
def auto_index_rebuild(root_path: str = "/Game") -> dict[str, Any]:
    """Force a full Auto-Index rebuild from Unreal AssetRegistry."""
    require_non_empty_string(root_path, "root_path")
    return _call("auto_index_rebuild", {"root_path": root_path})


@mcp.tool()
def auto_index_flush() -> dict[str, Any]:
    """Flush the in-memory Auto-Index table to Saved/UeNodeNexusBridge/AutoIndex.json."""
    return _call("auto_index_flush", {})


@mcp.tool()
def auto_index_clear(delete_file: bool = False) -> dict[str, Any]:
    """Clear Auto-Index memory state and optionally delete the persisted index file."""
    return _call("auto_index_clear", {"delete_file": delete_file})


@mcp.tool()
def auto_index_overview(limit: int = 30, format: Literal["indexed", "full"] = "indexed") -> dict[str, Any]:
    """Return a low-cost indexed project asset map for first-pass MCP context gathering."""
    return _call("auto_index_overview", {"limit": limit, "format": format})


@mcp.tool()
def auto_index_tree_get(
    root_path: str = "/Game",
    depth: int = 2,
    limit: int = 120,
    format: Literal["indexed", "full"] = "indexed",
) -> dict[str, Any]:
    """Return an indexed Content Browser folder tree summary with asset counts and class distribution."""
    require_non_empty_string(root_path, "root_path")
    return _call("auto_index_tree_get", {"root_path": root_path, "depth": depth, "limit": limit, "format": format})


@mcp.tool()
def auto_index_query(
    text: str = "",
    class_names: list[str] | None = None,
    package_paths: list[str] | None = None,
    recursive: bool = True,
    include_redirectors: bool = False,
    limit: int = 80,
    cursor: str | None = None,
    format: Literal["indexed", "full"] = "indexed",
) -> dict[str, Any]:
    """Query the persisted asset index using compact indexed output."""
    return _call(
        "auto_index_query",
        {
            "text": text,
            "class_names": class_names or [],
            "package_paths": package_paths or [],
            "recursive": recursive,
            "include_redirectors": include_redirectors,
            "limit": limit,
            "cursor": cursor,
            "format": format,
        },
    )


@mcp.tool()
def auto_index_get(asset_path: str) -> dict[str, Any]:
    """Return one asset record from Auto-Index."""
    require_non_empty_string(asset_path, "asset_path")
    return _call("auto_index_get", {"asset_path": asset_path})


@mcp.tool()
def auto_index_resolve_path(
    path: str,
    limit: int = 20,
    format: Literal["indexed", "full"] = "indexed",
) -> dict[str, Any]:
    """Resolve a fuzzy asset name/package/object path into indexed candidate object paths."""
    require_non_empty_string(path, "path")
    return _call("auto_index_resolve_path", {"path": path, "limit": limit, "format": format})


@mcp.tool()
def auto_index_diff_registry(format: Literal["indexed", "full"] = "indexed") -> dict[str, Any]:
    """Compare Auto-Index with the live AssetRegistry and report drift in compact text form."""
    return _call("auto_index_diff_registry", {"format": format})
