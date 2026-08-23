from __future__ import annotations

from typing import Any, Literal

from .facade_auto_read import execute_auto_read
from .facade_capabilities import ue_capability_get, ue_context_get
from .facade_execute import VALID_RESPONSE_MODES, execute_operation, preflight_execute_request
from .facade_plan import ue_plan_validate
from .facade_response import (
    artifact_handle,
    compact_data_summary,
    diagnostic_counts,
    diff_changes,
    minimal_error,
    summarize_response,
    with_optional_remaining_errors,
)
from .facade_read import apply_read_format_defaults, resolve_read_operation, unsupported_target_details
from .facade_state import facade_state
from .operation_registry import get_operation_spec
from .contracts import require_mapping, require_non_empty_string
from .runtime import enabled_features, thin_tool


VALID_RESPONSE_OPTION_FIELDS = {"allow_heavy", "mode"}


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
