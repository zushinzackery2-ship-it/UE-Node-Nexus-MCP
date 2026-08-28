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


@default_tool()
def anim_state_machine_state_add(
    asset_path: str,
    state_name: str,
    machine_name: str | None = None,
    x: float = 0.0,
    y: float = 0.0,
    set_as_entry: bool = False,
    dry_run: bool = True,
) -> dict[str, Any]:
    """Add a state to an AnimBlueprint state machine.

    machine_name may be omitted when the blueprint contains exactly one state
    machine. set_as_entry rewires the machine entry node to the new state.
    """
    require_non_empty_string(asset_path, "asset_path")
    require_non_empty_string(state_name, "state_name")
    payload: dict[str, Any] = {
        "asset_path": asset_path,
        "state_name": state_name,
        "x": x,
        "y": y,
        "set_as_entry": set_as_entry,
        "dry_run": dry_run,
    }
    if machine_name is not None:
        require_non_empty_string(machine_name, "machine_name")
        payload["machine_name"] = machine_name
    return _call("anim_state_machine_state_add", payload)


@default_tool()
def anim_state_machine_transition_add(
    asset_path: str,
    from_state: str,
    to_state: str,
    machine_name: str | None = None,
    dry_run: bool = True,
) -> dict[str, Any]:
    """Add a transition between two named states of an AnimBlueprint state machine.

    Duplicate from->to transitions are rejected; the created transition starts
    with an empty rule graph (author the condition afterwards in the editor or
    via graph operations).
    """
    require_non_empty_string(asset_path, "asset_path")
    require_non_empty_string(from_state, "from_state")
    require_non_empty_string(to_state, "to_state")
    payload: dict[str, Any] = {
        "asset_path": asset_path,
        "from_state": from_state,
        "to_state": to_state,
        "dry_run": dry_run,
    }
    if machine_name is not None:
        require_non_empty_string(machine_name, "machine_name")
        payload["machine_name"] = machine_name
    return _call("anim_state_machine_transition_add", payload)
