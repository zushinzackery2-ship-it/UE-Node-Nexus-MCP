from __future__ import annotations

import json
from typing import Any

from .facade_state import StoredDiff, facade_state
from .runtime import response_mode

LARGE_RESPONSE_INLINE_BYTE_LIMIT = 16 * 1024


def minimal_error(code: str, message: str, details: dict[str, Any] | None = None) -> dict[str, Any]:
    return {
        "ok": False,
        "error": {
            "code": code,
            "message": message,
            "details": details or {},
        }
    }


def with_optional_remaining_errors(result: dict[str, Any], response: dict[str, Any]) -> dict[str, Any]:
    remaining = response.get("remaining_errors")
    if isinstance(remaining, int):
        result["remaining_errors"] = remaining
    return result


def diagnostic_counts(response: dict[str, Any]) -> dict[str, int]:
    diagnostics = response.get("diagnostics")
    warnings = response.get("warnings")
    error_count = 0
    warning_count = 0

    if isinstance(diagnostics, list):
        for item in diagnostics:
            if not isinstance(item, dict):
                continue
            severity = str(item.get("severity", "")).lower()
            if severity in {"error", "fatal"}:
                error_count += 1
            elif severity == "warning":
                warning_count += 1
    if isinstance(warnings, list):
        warning_count += len(warnings)

    remaining = response.get("remaining_errors")
    if isinstance(remaining, int):
        error_count = max(error_count, remaining)

    return {"errors": error_count, "warnings": warning_count}


def asset_path_from_payload(payload: dict[str, Any]) -> str | None:
    for key in ("asset_path", "source_asset_path", "destination_asset_path", "folder_path"):
        value = payload.get(key)
        if isinstance(value, str) and value:
            return value
    return None


def extract_created_ids(data: Any) -> list[str]:
    if not isinstance(data, dict):
        return []
    ids: list[str] = []
    for key in ("node_id", "asset_path", "emitter_id", "renderer_id"):
        value = data.get(key)
        if isinstance(value, str) and value:
            ids.append(value)
    for key in ("created_ids", "created_nodes", "affected_ids"):
        value = data.get(key)
        if isinstance(value, list):
            ids.extend(str(item) for item in value if isinstance(item, (str, int)))
    return sorted(set(ids))


def artifact_handle(kind: str, response: dict[str, Any]) -> dict[str, Any]:
    artifact = facade_state.store_artifact(kind, response)
    return {
        "id": artifact.artifact_id,
        "kind": kind,
        "expires_in_seconds": facade_state.ttl_seconds,
        "fetch_with": "ue_read",
    }


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
    response_data = diff.response.get("data")
    for key in ("asset_path", "source_asset_path", "destination_asset_path", "node_id"):
        value = payload.get(key)
        if isinstance(value, str) and value:
            changes.append(["payload", key, value])
    if isinstance(response_data, dict):
        for value in extract_created_ids(response_data):
            changes.append(["id", value])
    counts = diagnostic_counts(diff.response)
    changes.append(["diagnostics", counts["errors"], counts["warnings"]])
    return changes


def summarize_response(operation: str, payload: dict[str, Any], response: dict[str, Any], mode: str) -> dict[str, Any]:
    raw_mode = mode if mode in {"full", "debug"} else "full"
    if mode in {"full", "debug"} or response_mode() == "full":
        payload_bytes = response_payload_bytes(response)
        if payload_bytes > LARGE_RESPONSE_INLINE_BYTE_LIMIT:
            return _artifact_summary_for_large_response(operation, payload, response, raw_mode, payload_bytes)
        return response

    if response.get("ok") is False:
        return with_optional_remaining_errors({
            "ok": False,
            "error": response.get("error", {"code": "operation_failed", "message": "Operation failed.", "details": {}}),
            "diagnostics": diagnostic_counts(response),
            "artifact": artifact_handle(f"{operation}_error", response),
        }, response)

    diff = facade_state.store_diff(operation, payload, response)
    data = response.get("data")
    created_ids = extract_created_ids(data)
    diagnostics = diagnostic_counts(response)
    dry_run = payload.get("dry_run")
    affected = _affected_from_payload(payload)
    changed = False if dry_run is True else bool(created_ids or affected["assets"] or affected["nodes"])
    summary = _summary_for_mode(operation, mode, changed, dry_run, affected, created_ids, diagnostics, diff.diff_token, data)

    asset_path = asset_path_from_payload(payload)
    if asset_path:
        summary["state_token"] = f"state:{asset_path}:{diff.diff_token}"
        summary["next_read"] = {
            "tool": "ue_diff_get",
            "args": {
                "scope": "asset",
                "asset_path": asset_path,
                "since_token": diff.diff_token,
            },
        }

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

    asset_path = asset_path_from_payload(payload)
    if asset_path:
        summary["state_token"] = f"state:{asset_path}:{diff.diff_token}"
        summary["next_read"] = {
            "tool": "ue_read",
            "args": {
                "target": "artifact",
                "query": {
                    "artifact_id": artifact["id"],
                },
            },
        }

    return with_optional_remaining_errors({
        "ok": response.get("ok", False),
        "data": summary,
    }, response)


def _affected_from_payload(payload: dict[str, Any]) -> dict[str, list[str]]:
    assets: list[str] = []
    nodes: list[str] = []
    for key in ("asset_path", "source_asset_path", "destination_asset_path"):
        value = payload.get(key)
        if isinstance(value, str) and value and value not in assets:
            assets.append(value)
    node_id = payload.get("node_id")
    if isinstance(node_id, str) and node_id:
        nodes.append(node_id)
    return {"assets": assets, "nodes": nodes}


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
