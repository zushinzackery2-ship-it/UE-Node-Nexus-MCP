from __future__ import annotations

from typing import Any, Literal

from .contracts import (
    BRIDGE_OPERATIONS,
    DEFAULT_HIDDEN_OPERATIONS,
    OPERATION_FEATURES,
    require_non_empty_string,
)
from .diagnostics_logs import enrich_with_material_log_diagnostics, read_latest_project_log
from .errors import BridgeError
from .instances.errors import InstanceError
from .instances.session import instance_manager
from .instances.tools.operations import bridge_instance_list, bridge_instance_select
from .runtime import call_bridge as _call
from .runtime import default_tool, enabled_features, hidden_tool


@default_tool()
def bridge_capabilities_get() -> dict[str, Any]:
    """Return operations actually supported by the loaded Unreal bridge modules."""
    return _call("bridge_capabilities_get", {})


@default_tool()
def log_tail_get(
    tail_kb: int = 64,
    match: str | None = None,
    max_lines: int = 200,
) -> dict[str, Any]:
    """MCP-local read of the newest UE project log tail, optionally filtered by substring."""
    if not isinstance(tail_kb, int) or isinstance(tail_kb, bool) or not 1 <= tail_kb <= 1024:
        raise ValueError("tail_kb must be an integer between 1 and 1024")
    if not isinstance(max_lines, int) or isinstance(max_lines, bool) or not 1 <= max_lines <= 2000:
        raise ValueError("max_lines must be an integer between 1 and 2000")
    if match is not None and (not isinstance(match, str) or not match.strip()):
        raise ValueError("match must be a non-empty string")

    try:
        project_context = dict(data=dict(project_file_path=instance_manager.project()["project_path"]))
    except InstanceError as exc:
        return dict(exc.envelope(), operation="log_tail_get", diagnostics=[], warnings=[])
    log_text, log_path = read_latest_project_log(project_context, tail_bytes=tail_kb * 1024)
    if log_path is None:
        return {
            "ok": False,
            "operation": "log_tail_get",
            "error": {"code": "log_not_found", "message": "No project log file could be located", "details": {}},
            "diagnostics": [],
            "warnings": [],
        }

    lines = log_text.splitlines()
    total_lines = len(lines)
    if match is not None:
        needle = match.lower()
        lines = [line for line in lines if needle in line.lower()]
    matched_lines = len(lines)
    truncated = len(lines) > max_lines
    lines = lines[-max_lines:]

    return {
        "ok": True,
        "operation": "log_tail_get",
        "data": {
            "log_path": str(log_path),
            "match": match,
            "scanned_lines": total_lines,
            "matched_lines": matched_lines,
            "returned_lines": len(lines),
            "truncated": truncated,
            "text": "\n".join(lines),
        },
        "diagnostics": [],
        "warnings": [],
    }


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
    save_before_exit: bool = False,
    force: bool = False,
    dry_run: bool = True,
    save_packages: list[str] | None = None,
    proposal_id: str | None = None,
) -> dict[str, Any]:
    """Request a normal Unreal Editor exit, optionally saving dirty packages first."""
    return _call(
        "editor_request_exit",
        {
            "save_before_exit": save_before_exit,
            "force": force,
            "dry_run": dry_run,
            "save_packages": save_packages or [],
            "proposal_id": proposal_id,
        },
    )


def execute_diagnostics_get(payload: dict[str, Any]) -> dict[str, Any]:
    """Facade entrypoint for diagnostics_get.

    Forwards the payload to the bridge unchanged, then enriches ok responses
    with recent material compile entries parsed from the UE project log
    (``data.related_log_items``).
    """
    response = _call("diagnostics_get", payload)
    if response.get("ok") is not True:
        return response

    project_context = _call("project_context_get", {})
    asset_path = payload.get("asset_path")
    severity = payload.get("severity", "all")
    return enrich_with_material_log_diagnostics(
        response,
        project_context,
        asset_path=asset_path if isinstance(asset_path, str) else None,
        severity=severity if isinstance(severity, str) else "all",
    )


@default_tool()
def diagnostics_get(
    asset_path: str | None = None,
    severity: Literal["info", "warning", "error", "all"] = "all",
) -> dict[str, Any]:
    """Return UE message-log and asset compile diagnostics, optionally filtered by asset and severity."""
    return execute_diagnostics_get(
        {
            "asset_path": asset_path,
            "severity": severity,
        }
    )
