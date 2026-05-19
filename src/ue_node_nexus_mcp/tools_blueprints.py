from __future__ import annotations

from typing import Any, Literal

from .contracts import require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import mcp


@mcp.tool()
def blueprint_details_get(
    asset_path: str,
    include_defaults: bool = True,
    include_components: bool = True,
    property_names: list[str] | None = None,
    format: Literal["compact", "full"] = "compact",
) -> dict[str, Any]:
    """Return Blueprint class metadata, variables, selected CDO defaults, and component templates."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "blueprint_details_get",
        {
            "asset_path": asset_path,
            "include_defaults": include_defaults,
            "include_components": include_components,
            "property_names": property_names or [],
            "format": format,
        },
    )


@mcp.tool()
def anim_blueprint_summary_get(
    asset_path: str,
    format: Literal["compact", "full"] = "compact",
    max_nodes: int = 200,
) -> dict[str, Any]:
    """Return a compact semantic summary for common AnimBlueprint graph nodes."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "anim_blueprint_summary_get",
        {
            "asset_path": asset_path,
            "format": format,
            "max_nodes": max_nodes,
        },
    )
