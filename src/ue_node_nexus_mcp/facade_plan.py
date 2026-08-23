"""Dry-run plan validation for the thin MCP facade."""

from __future__ import annotations

from typing import Any, Literal

from .contracts import require_list
from .facade_response import asset_path_from_payload
from .operation_registry import get_operation_spec
from .payload_schema import payload_schema_for
from .runtime import enabled_features, thin_tool

_RISK_RANK = {"low": 0, "medium": 1, "high": 2}


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
    features = enabled_features()

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
        if spec.group not in features:
            errors.append(
                {
                    "index": index,
                    "code": "feature_disabled",
                    "message": f"feature group is not enabled: {spec.group}",
                }
            )
            continue
        missing_fields = [
            field
            for field in payload_schema_for(operation).get("required", [])
            if field not in payload
        ]
        if missing_fields:
            errors.append(
                {
                    "index": index,
                    "code": "missing_required_field",
                    "message": f"missing required field(s): {', '.join(missing_fields)}",
                    "fields": missing_fields,
                }
            )
            continue
        if _RISK_RANK[spec.risk] > _RISK_RANK[highest_risk]:
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
        },
    }
