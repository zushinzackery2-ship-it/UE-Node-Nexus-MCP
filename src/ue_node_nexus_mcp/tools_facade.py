from __future__ import annotations

from typing import Any, Literal

from .contracts import require_non_empty_string
from .facade_auto_read import execute_auto_read
from .facade_execute import (
    EXECUTE_RESPONSE_MODES,
    execute_operation,
    preflight_execute_request,
)
from .facade_read import (
    apply_read_format_defaults,
    resolve_read_operation,
    unsupported_target_details,
)
from .facade_response import (
    artifact_handle,
    compact_data_summary,
    diagnostic_counts,
    diff_changes,
    minimal_error,
    summarize_response,
    with_optional_remaining_errors,
)
from .facade_state import facade_state
from .operation_registry import get_operation_spec
from .operation_validation import unknown_field_error
from .runtime import enabled_features, thin_tool

VALID_RESPONSE_OPTION_FIELDS = {"allow_heavy", "mode"}


@thin_tool()
def ue_execute(
    operation: str,
    payload: dict[str, Any],
    response: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Execute one internal operation through the thin facade and return summary/delta by default.

    Asset content is not edited through this entry point: the ``.nexus`` text files
    under the mirror root are the edit surface, and ``ue_sync`` owns that loop. Use
    this for the typed operations the text mirror does not cover - asset management,
    levels and components, Enhanced Input, diagnostics, compile/save.
    """
    if not isinstance(operation, str) or not operation.strip():
        return minimal_error("invalid_request", "operation must be a non-empty string", {"operation": operation})
    if not isinstance(payload, dict):
        return minimal_error("invalid_request", "payload must be an object", {"operation": operation})
    response_options = response or {}
    if not isinstance(response_options, dict):
        return minimal_error("invalid_request", "response must be an object", {"operation": operation})
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
    if mode not in EXECUTE_RESPONSE_MODES:
        return minimal_error("invalid_response_mode", f"unsupported response mode: {mode}", {"mode": mode})
    unknown = unknown_field_error(operation, payload)
    if unknown is not None:
        return minimal_error(unknown["code"], unknown["message"], {"operation": operation, **unknown})
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
    """Read common UE state through one thin facade entrypoint.

    Targets read live UE objects (assets, graphs, material instances, levels,
    Niagara, diagnostics). There is no target for mirror text: ``.nexus`` files are
    ordinary files on disk, so read them with your own file tools and validate an
    edit with ``ue_sync("lint")``.

    ``target="artifact"`` pages a stored response: ``query={artifact_id, path,
    cursor, limit_bytes}``. ``path`` is a dotted path such as ``data.conflicts``;
    a list there is returned as whole items from ``cursor`` with ``next_cursor``
    and ``total``, anything else as JSON text chunks to concatenate. Pages default
    to 48 KiB and never exceed 4 MiB; follow ``next_read`` or ``page_lists_with``.
    """
    if not isinstance(target, str) or not target.strip():
        return minimal_error("invalid_request", "target must be a non-empty string", {"target": target})
    if query is not None and not isinstance(query, dict):
        return minimal_error("invalid_query", "query must be an object", {"target": target})
    query_payload = dict(query or {})
    if target == "artifact":
        artifact_id = str(query_payload.get("artifact_id", ""))
        if not artifact_id.strip():
            return minimal_error("invalid_request", "artifact_id must be a non-empty string", {"target": target})
        artifact = facade_state.get_artifact(artifact_id)
        if artifact is None:
            return minimal_error("token_expired", "artifact was not found or expired", {"artifact_id": artifact_id})
        from .facade_artifact import read as read_artifact

        return read_artifact(artifact, query_payload)
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
    unknown = unknown_field_error(operation, query_payload, "unknown_query_field")
    if unknown is not None:
        return minimal_error(unknown["code"], unknown["message"], {"target": target, **unknown})

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
    cursor: str | None = None,
) -> dict[str, Any]:
    """Return compact changes stored by a previous thin facade read or execute call.

    ``scope`` and ``asset_path`` label the query and are echoed back; the diff
    itself is always the one recorded under ``since_token``. Pass a returned
    ``next_cursor`` back as ``cursor`` to page through truncated change lists.
    """
    if not isinstance(since_token, str) or not since_token.strip():
        return minimal_error("invalid_request", "since_token must be a non-empty string", {"since_token": since_token})
    if not isinstance(limit, int) or isinstance(limit, bool) or limit < 1:
        return minimal_error("invalid_limit", "limit must be a positive integer", {"limit": limit})
    offset = 0
    if cursor is not None:
        if not isinstance(cursor, str) or not cursor.isdigit():
            return minimal_error("invalid_cursor", "cursor must be a next_cursor value from a previous call", {"cursor": cursor})
        offset = int(cursor)
    diff = facade_state.get_diff(since_token)
    if diff is None:
        return minimal_error("token_expired", "diff token was not found or expired", {"since_token": since_token})
    changes = diff_changes(diff)
    window = changes[offset:offset + limit]
    truncated = offset + limit < len(changes)
    data = {
        "scope": scope,
        "asset_path": asset_path,
        "since_token": since_token,
        "current_token": diff.diff_token,
        "changes": window,
        "diagnostics": diagnostic_counts(diff.response),
        "truncated": truncated,
        "next_cursor": str(offset + limit) if truncated else None,
    }
    return {"ok": True, "data": data}
