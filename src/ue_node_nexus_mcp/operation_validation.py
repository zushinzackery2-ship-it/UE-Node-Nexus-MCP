"""Shared upfront validation for wrapped internal-operation calls.

Both MCP-local composition operations (``batch_execute``, ``task_submit``)
accept an inner operation name + payload and must reject bad requests before
anything executes. This module owns that per-call check so the two callers
cannot drift apart.
"""

from __future__ import annotations

from typing import Any

from .operation_registry import get_operation_spec
from .payload_schema import payload_schema_for
from .runtime import enabled_features


def unknown_payload_fields(operation: str, payload: dict[str, Any]) -> list[str]:
    """Fields the operation does not declare, sorted.

    Silently dropping them is worse than refusing them: a caller that mistypes a
    filter name gets an unfiltered result and no way to tell the filter never
    applied. Operations with no typed wrapper declare no fields at all, so their
    payload is passed through unchecked.
    """
    schema = payload_schema_for(operation)
    properties = schema.get("properties")
    if not properties:
        return []
    return sorted(set(payload) - set(properties))


def unknown_field_error(operation: str, payload: dict[str, Any], code: str = "unknown_field") -> dict[str, Any] | None:
    unknown = unknown_payload_fields(operation, payload)
    if not unknown:
        return None
    return {
        "code": code,
        "message": f"unsupported field(s) for {operation}: {', '.join(unknown)}",
        "fields": unknown,
        "accepted_fields": sorted(payload_schema_for(operation).get("properties", {})),
    }


def validate_operation_call(
    name: Any,
    payload: Any,
    forbidden: dict[str, str] | None = None,
) -> dict[str, Any] | None:
    """Return an error dict for one wrapped operation call, or None when valid.

    ``forbidden`` maps operation names the wrapper refuses to run (e.g. itself)
    to the error code reported for them.
    """
    if not isinstance(name, str) or not name.strip():
        return {"code": "missing_operation", "message": "operation is required"}
    if not isinstance(payload, dict):
        return {"code": "invalid_payload", "message": "payload must be an object"}
    if forbidden and name in forbidden:
        return {"code": forbidden[name], "message": f"operation cannot be wrapped here: {name}"}
    try:
        spec = get_operation_spec(name)
    except ValueError as exc:
        return {"code": "unknown_operation", "message": str(exc)}
    if spec.group not in enabled_features():
        return {"code": "feature_disabled", "message": f"feature group is not enabled: {spec.group}"}
    missing = [field for field in payload_schema_for(name).get("required", []) if field not in payload]
    if missing:
        return {
            "code": "missing_required_field",
            "message": f"missing required field(s): {', '.join(missing)}",
            "fields": missing,
        }
    return unknown_field_error(name, payload)
