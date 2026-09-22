from __future__ import annotations

from typing import Any, Literal

from .contracts import require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import default_tool


@default_tool()
def asset_list(
    class_names: list[str] | None = None,
    package_paths: list[str] | None = None,
    recursive: bool = True,
    limit: int = 100,
    cursor: str | None = None,
    format: Literal["indexed", "compact", "full"] = "indexed",
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


@default_tool()
def asset_get(asset_path: str) -> dict[str, Any]:
    """Return metadata for one Unreal asset."""
    require_non_empty_string(asset_path, "asset_path")
    return _call("asset_get", {"asset_path": asset_path})


@default_tool()
def asset_create(
    asset_path: str,
    asset_kind: Literal[
        "material",
        "material_instance",
        "blueprint",
        "material_function",
        "data_asset",
        "texture_render_target_2d",
    ],
    parent_asset_path: str | None = None,
    parent_class_path: str | None = None,
    blueprint_type: Literal[
        "normal",
        "const",
        "macro_library",
        "interface",
        "function_library",
    ]
    | None = None,
    dry_run: bool = True,
    save: bool = False,
) -> dict[str, Any]:
    """Create a fixed supported asset type with validation, dry-run, and optional save.

    ``blueprint_type`` only applies to ``asset_kind="blueprint"``. Leave it unset to
    take the type the parent class implies: a ``BlueprintFunctionLibrary`` parent
    makes a function library, an ``Interface`` parent makes an interface, anything
    else makes a normal Blueprint. Macro libraries have no distinguishing parent, so
    they must be asked for by name.
    """
    require_non_empty_string(asset_path, "asset_path")
    require_non_empty_string(asset_kind, "asset_kind")
    return _call(
        "asset_create",
        {
            "asset_path": asset_path,
            "asset_kind": asset_kind,
            "parent_asset_path": parent_asset_path,
            "parent_class_path": parent_class_path,
            "blueprint_type": blueprint_type,
            "dry_run": dry_run,
            "save": save,
        },
    )


@default_tool()
def asset_delete(
    asset_path: str,
    dry_run: bool = True,
    allow_referenced: bool = False,
    cleanup_after_delete: bool = True,
) -> dict[str, Any]:
    """Delete one Unreal asset with dry-run and reference diagnostics."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "asset_delete",
        {
            "asset_path": asset_path,
            "dry_run": dry_run,
            "allow_referenced": allow_referenced,
            "cleanup_after_delete": cleanup_after_delete,
        },
    )


def _asset_links_payload(asset_path: str, include_soft: bool, include_engine: bool, limit: int, cursor: str | None) -> dict[str, Any]:
    require_non_empty_string(asset_path, "asset_path")
    return {
        "asset_path": asset_path,
        "include_soft": include_soft,
        "include_engine": include_engine,
        "limit": limit,
        "cursor": cursor,
    }


@default_tool()
def asset_dependencies_get(
    asset_path: str,
    include_soft: bool = True,
    include_engine: bool = False,
    limit: int = 200,
    cursor: str | None = None,
) -> dict[str, Any]:
    """List packages the asset depends on (hard, and optionally soft) from the AssetRegistry."""
    return _call("asset_dependencies_get", _asset_links_payload(asset_path, include_soft, include_engine, limit, cursor))


@default_tool()
def asset_referencers_get(
    asset_path: str,
    include_soft: bool = True,
    include_engine: bool = False,
    limit: int = 200,
    cursor: str | None = None,
) -> dict[str, Any]:
    """List packages that reference the asset (hard, and optionally soft) from the AssetRegistry."""
    return _call("asset_referencers_get", _asset_links_payload(asset_path, include_soft, include_engine, limit, cursor))
