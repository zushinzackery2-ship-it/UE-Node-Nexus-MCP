from __future__ import annotations

import json
from typing import Any

from ..diagnostics.counting import count_diagnostic_errors, count_diagnostic_warnings
from .state import StoredDiff, facade_state
from ..runtime import response_mode
from ..diagnostics.summary import attach as attach_diagnostics, counts as data_diagnostic_counts
from ..diagnostics.contracts.response import attach as attach_contract
from .changes import evidence as mutation_evidence

LARGE_RESPONSE_INLINE_BYTE_LIMIT = 16 * 1024


def minimal_error(code: str, message: str, details: dict[str, Any] | None = None) -> dict[str, Any]:
    return dict(ok=False, error=dict(code=code, message=message, details=details or {}))


def with_optional_remaining_errors(result: dict[str, Any], response: dict[str, Any]) -> dict[str, Any]:
    remaining = response.get("remaining_errors")
    if isinstance(remaining, int):
        result["remaining_errors"] = remaining
        if "remaining_errors_scope" in response:
            result["remaining_errors_scope"] = response["remaining_errors_scope"]
    return result


def diagnostic_counts(response: dict[str, Any]) -> dict[str, int]:
    diagnostics = response.get("diagnostics")
    warnings = response.get("warnings")
    error_count = count_diagnostic_errors(diagnostics)
    warning_count = count_diagnostic_warnings(diagnostics)
    if isinstance(warnings, list):
        # The bridge reports both severities in one ordered stream; an import
        # error must not be counted as a warning.
        for item in warnings:
            if isinstance(item, dict) and item.get("severity") == "error":
                error_count += 1
            else:
                warning_count += 1

    remaining = response.get("remaining_errors")
    if isinstance(remaining, int):
        error_count = max(error_count, remaining)

    data_errors, data_warnings = data_diagnostic_counts(response)
    error_count = max(error_count, data_errors)
    warning_count = max(warning_count, data_warnings)

    return {"errors": error_count, "warnings": warning_count}


def asset_path_from_payload(payload: dict[str, Any]) -> str | None:
    for key in ("asset_path", "source_asset_path", "destination_asset_path", "folder_path"):
        value = payload.get(key)
        if isinstance(value, str) and value:
            return value
    return None


def artifact_handle(kind: str, response: dict[str, Any]) -> dict[str, Any]:
    artifact = facade_state.store_artifact(kind, response)
    return dict(id=artifact.artifact_id, kind=kind, expires_in_seconds=facade_state.ttl_seconds, fetch_with="ue_read")


def compact_data_summary(data: Any) -> str:
    if isinstance(data, dict):
        parts: list[str] = []
        for key in ("format", "asset_path", "graph_kind", "graph_name", "total_nodes", "returned_nodes", "truncated"):
            if key in data:
                parts.append(f"{key}={data[key]}")
        if parts:
            return ", ".join(parts)
        return f"{len(data)} fields"
    if isinstance(data, list):
        return f"{len(data)} items"
    if data is None:
        return "no data"
    return str(data)


def response_payload_bytes(response: dict[str, Any]) -> int:
    payload = json.dumps(response, ensure_ascii=False, separators=(",", ":"), default=str)
    return len(payload.encode("utf-8"))


def estimated_tokens_for_bytes(payload_bytes: int) -> int:
    return max(1, (payload_bytes + 3) // 4)


def diff_changes(diff: StoredDiff) -> list[list[Any]]:
    changes: list[list[Any]] = [["operation", diff.operation]]
    payload = diff.payload
    for key in ("asset_path", "source_asset_path", "destination_asset_path", "node_id"):
        value = payload.get(key)
        if isinstance(value, str) and value:
            changes.append(["payload", key, value])
    for kind, values in mutation_evidence(payload, diff.response)["changes"].items():
        changes.extend([kind, value] for value in values)
    counts = diagnostic_counts(diff.response)
    changes.append(["diagnostics", counts["errors"], counts["warnings"]])
    return changes


def summarize_response(operation: str, payload: dict[str, Any], response: dict[str, Any], mode: str) -> dict[str, Any]:
    attach_contract(response, response, operation)
    raw_mode = mode if mode in {"full", "debug"} else "full"
    if mode in {"full", "debug"} or response_mode() == "full":
        payload_bytes = response_payload_bytes(response)
        if payload_bytes > LARGE_RESPONSE_INLINE_BYTE_LIMIT:
            return _artifact_summary_for_large_response(operation, payload, response, raw_mode, payload_bytes)
        return response

    if response.get("ok") is False:
        result = dict(ok=False, error=response.get("error", dict(code="operation_failed", message="Operation failed.", details=dict())),
                      diagnostics=diagnostic_counts(response), artifact=artifact_handle(f"{operation}_error", response),
                      data=dict(operation=operation, **mutation_evidence(payload, response, limit=20)))
        attach_diagnostics(result, response, operation, payload)
        return with_optional_remaining_errors(result, response)

    diff = facade_state.store_diff(operation, payload, response)
    data = response.get("data")
    diagnostics = diagnostic_counts(response)
    evidence = mutation_evidence(payload, response)
    summary = _summary_for_mode(operation, mode, evidence["changed"], evidence["dry_run"], evidence["affected"],
                                evidence["created_ids"], diagnostics, diff.diff_token, data)
    summary.update(evidence)

    asset_path = asset_path_from_payload(payload)
    if asset_path:
        summary["state_token"] = f"state:{asset_path}:{diff.diff_token}"
        summary["next_read"] = {
            "tool": "ue_diff_get",
            "args": {"scope": "asset", "asset_path": asset_path, "since_token": diff.diff_token},
        }

    attach_diagnostics(summary, response, operation, payload)
    return with_optional_remaining_errors({"ok": True, "data": summary}, response)


def _artifact_summary_for_large_response(
    operation: str,
    payload: dict[str, Any],
    response: dict[str, Any],
    mode: str,
    payload_bytes: int,
) -> dict[str, Any]:
    data = response.get("data")
    diff = facade_state.store_diff(operation, payload, response)
    artifact = artifact_handle(f"{operation}_{mode}_response", response)
    summary = {
        "operation": operation,
        "response_mode": mode,
        "summary": compact_data_summary(data),
        "stored_as_artifact": True,
        "truncated": True,
        "payload_bytes": payload_bytes,
        "inline_limit_bytes": LARGE_RESPONSE_INLINE_BYTE_LIMIT,
        "estimated_tokens": estimated_tokens_for_bytes(payload_bytes),
        "artifact": artifact,
        "snapshot_token": artifact["id"],
        "diff_token": diff.diff_token,
        "diagnostics": diagnostic_counts(response),
    }
    summary.update(mutation_evidence(payload, response, limit=20))

    asset_path = asset_path_from_payload(payload)
    if asset_path:
        summary["state_token"] = f"state:{asset_path}:{diff.diff_token}"
    # Paged reads of what did not fit: the response as a whole, and each of its
    # lists item by item. The artifact module serves pages and imports this one.
    from .artifact import follow, list_reads

    summary["next_read"] = follow(artifact["id"])
    summary["page_lists_with"] = list_reads(artifact["id"], response)
    if operation == "workflow_guide_get" and isinstance(data, dict):
        for key in ("category", "title", "query"):
            if isinstance(data.get(key), str):
                summary[key] = data[key]
    attach_diagnostics(summary, response, operation, payload)

    return with_optional_remaining_errors({"ok": response.get("ok", False), "data": summary}, response)


def _summary_for_mode(
    operation: str,
    mode: str,
    changed: bool,
    dry_run: Any,
    affected: dict[str, list[str]],
    created_ids: list[str],
    diagnostics: dict[str, int],
    diff_token: str,
    data: Any,
) -> dict[str, Any]:
    if mode == "silent":
        return {"operation": operation, "changed": changed, "diagnostics": diagnostics, "diff_token": diff_token}
    if mode == "brief":
        return {"operation": operation, "summary": f"{operation} completed", "changed": changed, "diagnostics": diagnostics, "diff_token": diff_token}
    if mode == "ids_only":
        return {"operation": operation, "created_ids": created_ids, "affected": affected, "diagnostics": diagnostics, "diff_token": diff_token}
    if mode == "summary":
        return {"operation": operation, "summary": compact_data_summary(data), "affected": affected, "diagnostics": diagnostics, "diff_token": diff_token}
    return {
        "operation": operation,
        "changed": changed,
        "dry_run": dry_run,
        "affected": affected,
        "created_ids": created_ids,
        "diagnostics": diagnostics,
        "diff_token": diff_token,
    }
