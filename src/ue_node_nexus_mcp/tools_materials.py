from __future__ import annotations

from typing import Any, Literal

from .contracts import require_list, require_non_empty_string
from .server import _call, mcp


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
