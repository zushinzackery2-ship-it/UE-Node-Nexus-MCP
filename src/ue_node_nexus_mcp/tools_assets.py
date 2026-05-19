from __future__ import annotations

from typing import Any, Literal

from .contracts import require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import mcp


@mcp.tool()
def asset_list(
    class_names: list[str] | None = None,
    package_paths: list[str] | None = None,
    recursive: bool = True,
    limit: int = 100,
    cursor: str | None = None,
    format: Literal["compact", "full"] = "compact",
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
            "format": format,
        },
    )


@mcp.tool()
def asset_get(asset_path: str) -> dict[str, Any]:
    """Return metadata for one Unreal asset."""
    require_non_empty_string(asset_path, "asset_path")
    return _call("asset_get", {"asset_path": asset_path})


@mcp.tool()
def asset_create(
    asset_path: str,
    asset_kind: Literal["material", "material_instance", "blueprint"],
    parent_asset_path: str | None = None,
    parent_class_path: str | None = None,
    dry_run: bool = True,
    save: bool = False,
) -> dict[str, Any]:
    """Create a fixed supported asset type with validation, dry-run, and optional save."""
    require_non_empty_string(asset_path, "asset_path")
    require_non_empty_string(asset_kind, "asset_kind")
    return _call(
        "asset_create",
        {
            "asset_path": asset_path,
            "asset_kind": asset_kind,
            "parent_asset_path": parent_asset_path,
            "parent_class_path": parent_class_path,
            "dry_run": dry_run,
            "save": save,
        },
    )
