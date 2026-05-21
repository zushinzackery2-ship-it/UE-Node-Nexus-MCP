from __future__ import annotations

from typing import Any, Literal

from .contracts import require_list, require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import mcp


@mcp.tool()
def material_expression_classes_list(
    include_abstract: bool = False,
    include_deprecated: bool = False,
    include_params: bool = False,
    limit: int = 500,
    cursor: str | None = None,
    format: Literal["indexed", "compact", "full"] = "indexed",
) -> dict[str, Any]:
    """List loaded UMaterialExpression classes and their editable property schema counts."""
    return _call(
        "material_expression_classes_list",
        {
            "include_abstract": include_abstract,
            "include_deprecated": include_deprecated,
            "include_params": include_params,
            "limit": limit,
            "cursor": cursor,
            "format": format,
        },
    )


@mcp.tool()
def material_instance_params_get(asset_path: str, format: Literal["compact", "full"] = "compact") -> dict[str, Any]:
    """Return scalar, vector, texture, and static parameters for a material instance."""
    require_non_empty_string(asset_path, "asset_path")
    return _call("material_instance_params_get", {"asset_path": asset_path, "format": format})


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
