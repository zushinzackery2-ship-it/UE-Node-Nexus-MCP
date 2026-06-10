from __future__ import annotations

from typing import Any, Literal

from .contracts import require_list, require_mapping, require_non_empty_string
from .errors import BridgeError
from .instance import instance_manager
from .facade_auto_read import execute_auto_read
from .facade_execute import VALID_RESPONSE_MODES, execute_operation, preflight_execute_request
from .facade_response import (
    artifact_handle,
    asset_path_from_payload,
    compact_data_summary,
    diagnostic_counts,
    diff_changes,
    minimal_error,
    summarize_response,
    with_optional_remaining_errors,
)
from .facade_read import apply_read_format_defaults, resolve_read_operation, unsupported_target_details
from .facade_state import facade_state
from .operation_registry import capability_index, enabled_operation_specs, get_operation_spec, operation_schema
from .payload_schema import example_payload_for, payload_schema_for
from .runtime import enabled_features, thin_tool


VALID_RESPONSE_OPTION_FIELDS = {"allow_heavy", "mode"}


@thin_tool()
def ue_context_get(include_counts: bool = True) -> dict[str, Any]:
    """Return thin facade status, enabled groups, and the recommended first capability query."""
    features = enabled_features()
    specs = enabled_operation_specs(features)
    groups: dict[str, int] = {}
    for spec in specs.values():
        groups[spec.group] = groups.get(spec.group, 0) + 1
    try:
        available_instances = instance_manager.list_instances()
    except BridgeError:
        available_instances = []
    data: dict[str, Any] = {
        "bridge": "named_pipe",
        "active_instance": instance_manager.current(),
        "available_instances": available_instances,
        "groups": sorted(groups.items()) if include_counts else sorted(groups),
        "facade_tools": [
            "ue_context_get",
            "ue_capability_get",
            "ue_execute",
            "ue_read",
            "ue_diff_get",
            "ue_plan_validate",
        ],
        "recommended_next": "ue_capability_get",
    }
    return {"ok": True, "data": data}


@thin_tool()
def ue_capability_get(
    group: str | None = None,
    operation: str | None = None,
    detail: Literal["index", "schema", "examples", "full"] = "index",
) -> dict[str, Any]:
    """Return thin facade operation index or one internal operation schema."""
    features = enabled_features()
    if group is not None and group not in features:
        return minimal_error("feature_disabled", f"feature group is not enabled: {group}", {"group": group})
    if operation:
        try:
            spec = get_operation_spec(operation)
        except ValueError as exc:
            return minimal_error("invalid_operation", str(exc), {"operation": operation})
        if spec.group not in features:
            return minimal_error("feature_disabled", f"feature group is not enabled: {spec.group}", {"operation": operation})
        schema = operation_schema(spec)
        if detail == "index":
            data: dict[str, Any] = {
                "operation": spec.name,
                "group": spec.group,
                "kind": spec.kind,
                "risk": spec.risk,
                "summary": spec.summary,
            }
        elif detail == "examples":
            data = {
                "operation": spec.name,
                "examples": [{"payload": example_payload_for(spec.name)}],
                "next_read": {"tool": "ue_capability_get", "args": {"operation": spec.name, "detail": "schema"}},
            }
        else:
            data = schema
        return {"ok": True, "data": data}

    return {
        "ok": True,
        "data": {
            "group": group,
            "detail": "index",
            "operations": capability_index(features, group),
        }
    }


@thin_tool()
def ue_execute(
    operation: str,
    payload: dict[str, Any],
    response: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Execute one internal operation through the thin facade and return summary/delta by default."""
    require_non_empty_string(operation, "operation")
    require_mapping(payload, "payload")
    response_options = response or {}
    require_mapping(response_options, "response")
    response_error = _validate_response_options(response_options)
    if response_error is not None:
        return response_error
    try:
        spec = get_operation_spec(operation)
    except ValueError as exc:
        return minimal_error("invalid_operation", str(exc), {"operation": operation})
    if spec.group not in enabled_features():
        return minimal_error("feature_disabled", f"feature group is not enabled: {spec.group}", {"operation": operation})
    mode = str(response_options.get("mode", spec.default_response))
    if mode not in VALID_RESPONSE_MODES:
        return minimal_error("invalid_response_mode", f"unsupported response mode: {mode}", {"mode": mode})
    preflight_response = preflight_execute_request(operation, payload, response_options)
    if preflight_response is not None:
        return preflight_response
    try:
        raw_response = execute_operation(operation, payload)
    except ValueError as exc:
        return minimal_error("invalid_operation", str(exc), {"operation": operation})
    return summarize_response(operation, payload, raw_response, mode)


@thin_tool()
def ue_read(
    target: str,
    asset_path: str | None = None,
    query: dict[str, Any] | None = None,
    format: Literal["summary", "index", "detail", "debug"] = "summary",
) -> dict[str, Any]:
    """Read common UE state through one thin facade entrypoint."""
    require_non_empty_string(target, "target")
    if query is not None:
        try:
            require_mapping(query, "query")
        except ValueError as exc:
            return minimal_error("invalid_query", str(exc), {"target": target})
    query_payload = dict(query or {})
    if target == "artifact":
        artifact_id = str(query_payload.get("artifact_id", ""))
        require_non_empty_string(artifact_id, "artifact_id")
        artifact = facade_state.get_artifact(artifact_id)
        if artifact is None:
            return minimal_error("token_expired", "artifact was not found or expired", {"artifact_id": artifact_id})
        return {"ok": True, "data": artifact.payload}
    if target == "auto":
        requested_path = asset_path or str(query_payload.get("path") or query_payload.get("asset_path") or "")
        try:
            require_non_empty_string(requested_path, "asset_path")
            route_target, operation, resolved_asset_path, operation_payload, raw_response = execute_auto_read(requested_path, query_payload, format)
        except ValueError as exc:
            return minimal_error("invalid_read", str(exc), {"target": target})
        if format == "debug":
            return raw_response
        artifact = artifact_handle("auto_read", raw_response)
        diff = facade_state.store_diff(operation, operation_payload, raw_response)
        data = {
            "target": target,
            "route_target": route_target,
            "operation": operation,
            "format": format,
            "resolved_asset_path": resolved_asset_path,
            "summary": compact_data_summary(raw_response.get("data")),
            "snapshot_token": artifact["id"],
            "diff_token": diff.diff_token,
            "state_token": f"state:{resolved_asset_path}:{diff.diff_token}",
            "artifact": artifact,
        }
        return with_optional_remaining_errors({"ok": raw_response.get("ok", False), "data": data}, raw_response)

    operation = resolve_read_operation(target)
    if operation is None:
        return minimal_error("unsupported_target", f"unsupported read target: {target}", unsupported_target_details(target))
    if asset_path is not None:
        query_payload.setdefault("asset_path", asset_path)
    apply_read_format_defaults(operation, format, query_payload)

    try:
        raw_response = execute_operation(operation, query_payload)
    except ValueError as exc:
        return minimal_error("invalid_read", str(exc), {"target": target})
    if format == "debug":
        return raw_response
    artifact = artifact_handle(f"{target}_read", raw_response)
    diff = facade_state.store_diff(operation, query_payload, raw_response)
    data = {
        "target": target,
        "operation": operation,
        "format": format,
        "summary": compact_data_summary(raw_response.get("data")),
        "snapshot_token": artifact["id"],
        "diff_token": diff.diff_token,
        "state_token": f"state:{asset_path or target}:{diff.diff_token}",
        "artifact": artifact,
    }
    return with_optional_remaining_errors({"ok": raw_response.get("ok", False), "data": data}, raw_response)


def _validate_response_options(response_options: dict[str, Any]) -> dict[str, Any] | None:
    unknown_fields = sorted(set(response_options) - VALID_RESPONSE_OPTION_FIELDS)
    if not unknown_fields:
        return None
    suggestions: dict[str, str] = {}
    if "format" in unknown_fields:
        suggestions["format"] = 'Use response.mode="full" instead.'
    return minimal_error(
        "invalid_response_field",
        f"unsupported response option field(s): {', '.join(unknown_fields)}",
        {
            "unknown_fields": unknown_fields,
            "allowed_fields": sorted(VALID_RESPONSE_OPTION_FIELDS),
            "suggestions": suggestions,
        },
    )


@thin_tool()
def ue_diff_get(
    scope: Literal["request", "asset"] = "request",
    asset_path: str | None = None,
    since_token: str | None = None,
    limit: int = 50,
) -> dict[str, Any]:
    """Return compact changes stored by a previous thin facade read or execute call."""
    require_non_empty_string(since_token or "", "since_token")
    if not isinstance(limit, int) or limit < 1:
        return minimal_error("invalid_limit", "limit must be a positive integer", {"limit": limit})
    diff = facade_state.get_diff(str(since_token))
    if diff is None:
        return minimal_error("token_expired", "diff token was not found or expired", {"since_token": since_token})
    changes = diff_changes(diff)
    truncated = len(changes) > limit
    data = {
        "scope": scope,
        "asset_path": asset_path,
        "since_token": since_token,
        "current_token": diff.diff_token,
        "changes": changes[:limit],
        "diagnostics": diagnostic_counts(diff.response),
        "truncated": truncated,
        "next_cursor": str(limit) if truncated else None,
    }
    return {"ok": True, "data": data}


@thin_tool()
def ue_plan_validate(
    operations: list[dict[str, Any]],
    mode: Literal["dry_run"] = "dry_run",
) -> dict[str, Any]:
    """Validate a batch of internal operations without mutating UE state."""
    require_list(operations, "operations")
    errors: list[dict[str, Any]] = []
    estimated_changes: list[list[Any]] = []
    highest_risk = "low"
    risk_rank = {"low": 0, "medium": 1, "high": 2}
    for index, item in enumerate(operations):
        operation = item.get("operation")
        payload = item.get("payload", {})
        if not isinstance(operation, str) or not operation:
            errors.append({"index": index, "code": "missing_operation", "message": "operation is required"})
            continue
        if not isinstance(payload, dict):
            errors.append({"index": index, "code": "invalid_payload", "message": "payload must be an object"})
            continue
        try:
            spec = get_operation_spec(operation)
        except ValueError as exc:
            errors.append({"index": index, "code": "unknown_operation", "message": str(exc)})
            continue
        if spec.group not in enabled_features():
            errors.append({"index": index, "code": "feature_disabled", "message": f"feature group is not enabled: {spec.group}"})
            continue
        missing_fields = [field for field in payload_schema_for(operation).get("required", []) if field not in payload]
        if missing_fields:
            errors.append({
                "index": index,
                "code": "missing_required_field",
                "message": f"missing required field(s): {', '.join(missing_fields)}",
                "fields": missing_fields,
            })
            continue
        if risk_rank[spec.risk] > risk_rank[highest_risk]:
            highest_risk = spec.risk
        estimated_changes.append([spec.kind, spec.name, asset_path_from_payload(payload)])
    return {
        "ok": not errors,
        "data": {
            "valid": not errors,
            "mode": mode,
            "operation_count": len(operations),
            "risk": highest_risk,
            "estimated_changes": estimated_changes,
            "errors": errors,
            "warnings": [],
        }
    }
