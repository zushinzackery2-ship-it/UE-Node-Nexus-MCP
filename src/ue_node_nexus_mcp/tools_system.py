from __future__ import annotations

from typing import Any, Literal

from .contracts import (
    BRIDGE_OPERATIONS,
    DEFAULT_HIDDEN_OPERATIONS,
    OPERATION_FEATURES,
    require_non_empty_string,
)
from .diagnostics_logs import enrich_with_material_log_diagnostics
from .errors import BridgeError
from .instance import instance_manager
from .runtime import call_bridge as _call
from .runtime import default_tool, enabled_features, hidden_tool


@default_tool()
def bridge_capabilities_get() -> dict[str, Any]:
    """Return operations actually supported by the loaded Unreal bridge modules."""
    return _call("bridge_capabilities_get", {})


def _enabled_contract_operations() -> set[str]:
    features = enabled_features()
    return {operation for operation, feature in OPERATION_FEATURES.items() if operation in BRIDGE_OPERATIONS and feature in features}


def _default_exposed_contract_operations() -> set[str]:
    return _enabled_contract_operations() - DEFAULT_HIDDEN_OPERATIONS


@default_tool()
def bridge_contract_check(
    mode: Literal["exposed", "enabled", "all"] = "enabled",
) -> dict[str, Any]:
    """Compare local MCP operation contract with operations loaded in the Unreal bridge."""
    if mode == "all":
        expected_operations = set(BRIDGE_OPERATIONS)
        allowed_extra_operations: set[str] = set()
    elif mode == "exposed":
        expected_operations = _default_exposed_contract_operations()
        allowed_extra_operations = set(BRIDGE_OPERATIONS) - expected_operations
    else:
        expected_operations = _enabled_contract_operations()
        allowed_extra_operations = set(BRIDGE_OPERATIONS) - expected_operations

    return _call(
        "bridge_capabilities_get",
        {
            "expected_operations": sorted(expected_operations),
            "allowed_extra_operations": sorted(allowed_extra_operations),
            "mode": mode,
        },
    )


def _local_instance_response(operation: str, produce: Any) -> dict[str, Any]:
    """Run a session-local instance op and shape it like a bridge response so the
    thin facade's summarizer handles it uniformly. These ops never touch a UE
    instance's data plane — they manage which instance the session targets."""
    try:
        data = produce()
    except BridgeError as exc:
        return {
            "ok": False,
            "operation": operation,
            "error": {"code": "instance_error", "message": str(exc), "details": {}},
            "diagnostics": [],
            "warnings": [],
        }
    return {"ok": True, "operation": operation, "data": data, "diagnostics": [], "warnings": []}


@default_tool()
def bridge_instance_list() -> dict[str, Any]:
    """List live UE editor instances discoverable on named pipes (pid, project, active flag)."""
    return _local_instance_response(
        "bridge_instance_list",
        lambda: {"instances": instance_manager.list_instances(), "active": instance_manager.current()},
    )


@default_tool()
def bridge_instance_select(pid: int | None = None, project: str | None = None) -> dict[str, Any]:
    """Bind this MCP session to one UE editor instance by pid or project-name substring."""
    return _local_instance_response(
        "bridge_instance_select",
        lambda: {"selected": instance_manager.select(pid=pid, project=project), "active": instance_manager.current()},
    )


@default_tool()
def level_current_get() -> dict[str, Any]:
    """Return the current editor level identity and dirty state."""
    return _call("level_current_get", {})


@default_tool()
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


@default_tool()
def asset_compile(asset_path: str) -> dict[str, Any]:
    """Compile or recompile a Blueprint or material asset and return structured diagnostics."""
    require_non_empty_string(asset_path, "asset_path")
    return _call("asset_compile", {"asset_path": asset_path})


@default_tool()
def asset_validate(asset_path: str) -> dict[str, Any]:
    """Validate an asset and return machine-readable diagnostics."""
    require_non_empty_string(asset_path, "asset_path")
    return _call("asset_validate", {"asset_path": asset_path})


@default_tool()
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


@hidden_tool()
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


@hidden_tool()
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


@default_tool()
def diagnostics_get(
    asset_path: str | None = None,
    severity: Literal["info", "warning", "error", "all"] = "all",
) -> dict[str, Any]:
    """Return UE message-log and asset compile diagnostics, optionally filtered by asset and severity."""
    response = _call(
        "diagnostics_get",
        {
            "asset_path": asset_path,
            "severity": severity,
        },
    )
    if response.get("ok") is not True:
        return response

    project_context = _call("project_context_get", {})
    return enrich_with_material_log_diagnostics(
        response,
        project_context,
        asset_path=asset_path,
        severity=severity,
    )
