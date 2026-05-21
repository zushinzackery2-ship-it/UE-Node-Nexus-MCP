from __future__ import annotations

from typing import Any, Literal

from .contracts import require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import mcp


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
    format: Literal["indexed", "compact", "full"] = "indexed",
) -> dict[str, Any]:
    """List actors in the current editor level."""
    return _call(
        "level_actors_list",
        {
            "include_components": include_components,
            "class_names": class_names or [],
            "limit": limit,
            "cursor": cursor,
            "format": format,
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
def editor_save_all(
    save_map_packages: bool = True,
    save_content_packages: bool = True,
) -> dict[str, Any]:
    """Save all dirty editor packages and report remaining dirty packages."""
    return _call(
        "editor_save_all",
        {
            "save_map_packages": save_map_packages,
            "save_content_packages": save_content_packages,
        },
    )


@mcp.tool()
def editor_request_exit(
    save_before_exit: bool = True,
    force: bool = False,
) -> dict[str, Any]:
    """Request a normal Unreal Editor exit, optionally saving dirty packages first."""
    return _call(
        "editor_request_exit",
        {
            "save_before_exit": save_before_exit,
            "force": force,
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
