"""Dry-run plan validation for the thin MCP facade."""

from __future__ import annotations

from typing import Any, Literal

from .contracts import require_list
from .facade_response import asset_path_from_payload
from .operation_registry import get_operation_spec
from .operation_validation import validate_operation_call
from .runtime import thin_tool

_RISK_RANK = dict(low=0, medium=1, high=2)


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
    for index, item in enumerate(operations):
        operation = item.get("operation")
        payload = item.get("payload", dict())
        problem = validate_operation_call(operation, payload)
        if problem is not None:
            errors.append(dict(index=index, **problem))
            continue
        spec = get_operation_spec(operation)
        if _RISK_RANK[spec.risk] > _RISK_RANK[highest_risk]:
            highest_risk = spec.risk
        estimated_changes.append([spec.kind, spec.name, asset_path_from_payload(payload)])

    return dict(ok=not errors, data=dict(valid=not errors, mode=mode, operation_count=len(operations),
                                        risk=highest_risk, estimated_changes=estimated_changes, errors=errors, warnings=[]))
