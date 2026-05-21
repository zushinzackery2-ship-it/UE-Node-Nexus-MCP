from __future__ import annotations

from typing import Any, Literal

from .contracts import require_list, require_non_empty_string
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


@default_tool()
def asset_move(
    source_asset_path: str,
    destination_asset_path: str,
    dry_run: bool = True,
    save: bool = False,
    fix_redirectors: bool = True,
) -> dict[str, Any]:
    """Move or rename one Unreal asset to a destination object path."""
    require_non_empty_string(source_asset_path, "source_asset_path")
    require_non_empty_string(destination_asset_path, "destination_asset_path")
    return _call(
        "asset_move",
        {
            "source_asset_path": source_asset_path,
            "destination_asset_path": destination_asset_path,
            "dry_run": dry_run,
            "save": save,
            "fix_redirectors": fix_redirectors,
        },
    )


@default_tool()
def asset_rename(
    source_asset_path: str,
    destination_asset_path: str,
    dry_run: bool = True,
    save: bool = False,
    fix_redirectors: bool = True,
) -> dict[str, Any]:
    """Rename one Unreal asset to a destination object path."""
    require_non_empty_string(source_asset_path, "source_asset_path")
    require_non_empty_string(destination_asset_path, "destination_asset_path")
    return _call(
        "asset_rename",
        {
            "source_asset_path": source_asset_path,
            "destination_asset_path": destination_asset_path,
            "dry_run": dry_run,
            "save": save,
            "fix_redirectors": fix_redirectors,
        },
    )


@default_tool()
def asset_move_batch(
    items: list[dict[str, Any]],
    dry_run: bool = True,
    save: bool = False,
    fix_redirectors: bool = True,
    continue_on_error: bool = False,
) -> dict[str, Any]:
    """Move or rename multiple Unreal assets with one bridge request."""
    require_list(items, "items")
    return _call(
        "asset_move_batch",
        {
            "items": items,
            "dry_run": dry_run,
            "save": save,
            "fix_redirectors": fix_redirectors,
            "continue_on_error": continue_on_error,
        },
    )


@default_tool()
def asset_rename_batch(
    items: list[dict[str, Any]],
    dry_run: bool = True,
    save: bool = False,
    fix_redirectors: bool = True,
    continue_on_error: bool = False,
) -> dict[str, Any]:
    """Rename multiple Unreal assets with one bridge request."""
    require_list(items, "items")
    return _call(
        "asset_rename_batch",
        {
            "items": items,
            "dry_run": dry_run,
            "save": save,
            "fix_redirectors": fix_redirectors,
            "continue_on_error": continue_on_error,
        },
    )


@default_tool()
def asset_duplicate(
    source_asset_path: str,
    destination_asset_path: str,
    dry_run: bool = True,
    save: bool = False,
) -> dict[str, Any]:
    """Duplicate one Unreal asset to a destination object path."""
    require_non_empty_string(source_asset_path, "source_asset_path")
    require_non_empty_string(destination_asset_path, "destination_asset_path")
    return _call(
        "asset_duplicate",
        {
            "source_asset_path": source_asset_path,
            "destination_asset_path": destination_asset_path,
            "dry_run": dry_run,
            "save": save,
        },
    )


@default_tool()
def folder_create(
    folder_path: str,
    dry_run: bool = True,
) -> dict[str, Any]:
    """Create one Content Browser folder."""
    require_non_empty_string(folder_path, "folder_path")
    return _call(
        "folder_create",
        {
            "folder_path": folder_path,
            "dry_run": dry_run,
        },
    )


@default_tool()
def folder_delete(
    folder_path: str,
    dry_run: bool = True,
    recursive: bool = True,
) -> dict[str, Any]:
    """Delete an empty Content Browser folder tree after assets have been removed."""
    require_non_empty_string(folder_path, "folder_path")
    return _call(
        "folder_delete",
        {
            "folder_path": folder_path,
            "dry_run": dry_run,
            "recursive": recursive,
        },
    )


@default_tool()
def asset_redirectors_fixup(
    folder_path: str = "/Game",
    dry_run: bool = True,
) -> dict[str, Any]:
    """Fix redirectors under one Content Browser folder."""
    require_non_empty_string(folder_path, "folder_path")
    return _call(
        "asset_redirectors_fixup",
        {
            "folder_path": folder_path,
            "dry_run": dry_run,
        },
    )
