"""MCP-local sequential batch execution of internal operations.

Runs a short, ordered list of registry operations in one tool call. The whole
batch is validated up front (an invalid batch executes nothing); execution is
sequential and stops at the first failure unless ``continue_on_error`` is set.
This is a roundtrip saver, not a transaction: applied items stay applied when a
later item fails, and per-item ``dry_run`` flags keep their normal meaning.
"""

from __future__ import annotations

from typing import Any

from .contracts import require_list
from .errors import BridgeError
from .facade_response import asset_path_from_payload, compact_data_summary, diagnostic_counts
from .operation_registry import get_operation_spec
from .payload_schema import payload_schema_for
from .runtime import default_tool, enabled_features

MAX_BATCH_OPERATIONS = 20


def _validate_items(operations: list[dict[str, Any]]) -> list[dict[str, Any]]:
    errors: list[dict[str, Any]] = []
    features = enabled_features()
    for index, item in enumerate(operations):
        name = item.get("operation")
        payload = item.get("payload", {})
        if not isinstance(name, str) or not name.strip():
            errors.append({"index": index, "code": "missing_operation", "message": "operation is required"})
            continue
        if not isinstance(payload, dict):
            errors.append({"index": index, "code": "invalid_payload", "message": "payload must be an object"})
            continue
        if name == "batch_execute":
            errors.append({"index": index, "code": "nested_batch", "message": "batch_execute cannot contain itself"})
            continue
        try:
            spec = get_operation_spec(name)
        except ValueError as exc:
            errors.append({"index": index, "code": "unknown_operation", "message": str(exc)})
            continue
        if spec.group not in features:
            errors.append(
                {"index": index, "code": "feature_disabled", "message": f"feature group is not enabled: {spec.group}"}
            )
            continue
        missing = [field for field in payload_schema_for(name).get("required", []) if field not in payload]
        if missing:
            errors.append(
                {
                    "index": index,
                    "code": "missing_required_field",
                    "message": f"missing required field(s): {', '.join(missing)}",
                    "fields": missing,
                }
            )
    return errors


def _envelope(ok: bool, data: dict[str, Any], error: dict[str, Any] | None = None) -> dict[str, Any]:
    envelope: dict[str, Any] = {"ok": ok, "operation": "batch_execute", "data": data}
    if error is not None:
        envelope["error"] = error
    envelope["diagnostics"] = []
    envelope["warnings"] = []
    return envelope


@default_tool()
def batch_execute(operations: list[dict[str, Any]], continue_on_error: bool = False) -> dict[str, Any]:
    """Validate then sequentially run a list of internal operations."""
    require_list(operations, "operations")
    if not operations:
        raise ValueError("operations must not be empty")
    if len(operations) > MAX_BATCH_OPERATIONS:
        raise ValueError(f"operations exceeds the batch limit of {MAX_BATCH_OPERATIONS}")

    validation_errors = _validate_items(operations)
    if validation_errors:
        return _envelope(
            False,
            {"validated": False, "total": len(operations), "executed": 0},
            {
                "code": "invalid_batch",
                "message": f"{len(validation_errors)} of {len(operations)} batch items failed validation; nothing was executed",
                "details": {"errors": validation_errors},
            },
        )

    from .facade_execute import execute_operation

    items: list[dict[str, Any]] = []
    executed = failed = 0
    first_failed_index: int | None = None
    aborted_by_bridge = False

    for index, item in enumerate(operations):
        name = item["operation"]
        payload = item.get("payload", {})
        try:
            response = execute_operation(name, payload)
        except BridgeError as exc:
            failed += 1
            first_failed_index = first_failed_index if first_failed_index is not None else index
            items.append({"index": index, "operation": name, "ok": False, "error": {"code": "bridge_error", "message": str(exc)}})
            aborted_by_bridge = True
            break
        except ValueError as exc:
            failed += 1
            first_failed_index = first_failed_index if first_failed_index is not None else index
            items.append({"index": index, "operation": name, "ok": False, "error": {"code": "invalid_operation", "message": str(exc)}})
            if not continue_on_error:
                break
            continue
        executed += 1
        if response.get("ok") is True:
            entry: dict[str, Any] = {
                "index": index,
                "operation": name,
                "ok": True,
                "summary": compact_data_summary(response.get("data")),
                "diagnostics": diagnostic_counts(response),
            }
            asset_path = asset_path_from_payload(payload)
            if asset_path:
                entry["asset_path"] = asset_path
            items.append(entry)
            continue
        failed += 1
        first_failed_index = first_failed_index if first_failed_index is not None else index
        error = response.get("error") or {"code": "operation_failed", "message": "operation failed"}
        items.append({"index": index, "operation": name, "ok": False, "error": {"code": error.get("code"), "message": error.get("message")}})
        if not continue_on_error:
            break

    skipped = len(operations) - len(items)
    data = {
        "total": len(operations),
        "executed": executed,
        "failed": failed,
        "skipped": skipped,
        "continue_on_error": continue_on_error,
        "items": items,
    }
    if aborted_by_bridge:
        data["aborted"] = "bridge_error"
    if failed:
        return _envelope(
            False,
            data,
            {
                "code": "batch_item_failed",
                "message": f"{failed} of {len(operations)} batch items failed",
                "details": {"first_failed_index": first_failed_index},
            },
        )
    return _envelope(True, data)
