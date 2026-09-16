"""MCP-local sequential batch execution of internal operations.

Runs a short, ordered list of registry operations in one tool call. The whole
batch is validated up front (an invalid batch executes nothing); execution is
sequential and stops at the first failure unless ``continue_on_error`` is set.
This is a roundtrip saver, not a transaction: applied items stay applied when a
later item fails, and per-item ``dry_run`` flags keep their normal meaning.
"""

from __future__ import annotations

from typing import Any

from .contracts import ALL_OPERATIONS, require_list
from .errors import BridgeError
from .facade_response import asset_path_from_payload, compact_data_summary, diagnostic_counts
from .operation_validation import validate_operation_call
from .runtime import default_tool
from .instances.session.work import operation_scope

MAX_BATCH_OPERATIONS = 20

# A batch may not contain itself; task_submit is allowed (it returns instantly).
_FORBIDDEN_IN_BATCH = {"batch_execute": "nested_batch"}
_FORBIDDEN_IN_BATCH.update((name, "lifecycle_batch_forbidden") for name in ALL_OPERATIONS
                           if name.startswith("bridge_instance_") or name == "editor_request_exit")


def _validate_items(operations: list[dict[str, Any]]) -> list[dict[str, Any]]:
    errors: list[dict[str, Any]] = []
    for index, item in enumerate(operations):
        error = validate_operation_call(item.get("operation"), item.get("payload", {}), _FORBIDDEN_IN_BATCH)
        if error is not None:
            errors.append({"index": index, **error})
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

    with operation_scope("batch_execute", dict(operations=operations)):
        return _run_batch(operations, continue_on_error)


def _run_batch(operations: list[dict], continue_on_error: bool) -> dict:
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
