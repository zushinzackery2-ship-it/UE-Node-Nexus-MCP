from __future__ import annotations

from typing import Any, Literal

from .contracts import require_list, require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import default_tool


@default_tool()
def blueprint_details_get(
    asset_path: str,
    include_defaults: bool = False,
    include_components: bool = False,
    include_inherited_components: bool = False,
    property_names: list[str] | None = None,
    format: Literal["compact", "full"] = "compact",
) -> dict[str, Any]:
    """Return Blueprint class metadata, variables, selected CDO defaults, and component templates.

    When ``include_inherited_components`` is set (with ``include_components``), SCS
    components defined on ancestor Blueprints are also walked and tagged with their
    originating Blueprint in the ``origin`` column."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "blueprint_details_get",
        {
            "asset_path": asset_path,
            "include_defaults": include_defaults,
            "include_components": include_components,
            "include_inherited_components": include_inherited_components,
            "property_names": property_names or [],
            "format": format,
        },
    )


@default_tool()
def blueprint_components_patch(
    asset_path: str,
    operations: list[dict[str, Any]],
    dry_run: bool = True,
    compile_after: bool = True,
    format: Literal["compact", "full"] = "compact",
) -> dict[str, Any]:
    """Add or remove Blueprint SCS components with one structural patch request."""
    require_non_empty_string(asset_path, "asset_path")
    require_list(operations, "operations")
    return _call(
        "blueprint_components_patch",
        {
            "asset_path": asset_path,
            "operations": operations,
            "dry_run": dry_run,
            "compile_after": compile_after,
            "format": format,
        },
    )


@default_tool()
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


@default_tool()
def anim_state_machine_summary_get(
    asset_path: str,
    format: Literal["compact", "full"] = "compact",
) -> dict[str, Any]:
    """Return AnimBlueprint state machines: states, entry state, and transitions.

    Descends each state machine's editor graph (which graph_snapshot_get does not
    enter) to expose states[], entry_state, and transitions[] with from/to/rule
    and a blend digest (crossfade, blend_mode, logic, priority, automatic rule).
    """
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "anim_state_machine_summary_get",
        {
            "asset_path": asset_path,
            "format": format,
        },
    )
