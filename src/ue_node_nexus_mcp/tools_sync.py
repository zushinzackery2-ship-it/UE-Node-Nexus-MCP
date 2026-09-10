"""``ue_sync`` facade: the text-mirror workflow entry point."""

from __future__ import annotations

from typing import Any, Literal

from .facade_response import LARGE_RESPONSE_INLINE_BYTE_LIMIT, artifact_handle, minimal_error, response_payload_bytes
from .runtime import call_bridge, thin_tool
from .transcode.sync import ACTIONS, run_sync
from .transcode.sync_project import SyncError

SyncAction = Literal["init", "status", "pull", "lint", "push", "schema"]
_OPTION_KEYS = {
    "init": {"pull_all", "include_stubs", "auto_export", "refresh_schema", "scene"},
    "status": {"discover", "include_stubs", "include_clean", "scene"},
    "pull": {"discover", "include_stubs", "force", "scene"},
    "lint": set(),
    "push": {"dry_run", "compile", "save", "force", "allow_delete", "stop_on_error", "scene"},
    "schema": set(),
}
_ROW_INLINE_LIMIT = 40


@thin_tool()
def ue_sync(
    action: SyncAction,
    paths: list[str] | None = None,
    options: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Sync the Content_Transcoded text mirror with the bound UE editor.

    ``status`` classifies assets (clean / local-modified / ue-modified /
    both-modified / local-new / ue-new / ...), ``pull`` renders UE assets to
    ``.nexus`` text, ``lint`` checks text offline against the schema lock,
    ``push`` diffs text against the last synced base and applies the plan in
    one editor transaction (``options.dry_run`` defaults to true), ``init``
    binds the mirror root and pulls everything, ``schema`` refreshes the
    reflection snapshot. ``paths`` accepts /Game asset paths, mirror files or
    directories; empty means every mirrored asset.
    """
    if action not in ACTIONS:
        return minimal_error("invalid_action", f"unknown action {action!r}", {"actions": list(ACTIONS)})
    if paths is not None and (not isinstance(paths, list) or not all(isinstance(item, str) for item in paths)):
        return minimal_error("invalid_request", "paths must be a list of strings", {"action": action})
    if options is not None and not isinstance(options, dict):
        return minimal_error("invalid_request", "options must be an object", {"action": action})
    unknown = sorted(set(options or {}) - _OPTION_KEYS[action])
    if unknown:
        return minimal_error("invalid_option", f"unsupported option(s) for {action}: {', '.join(unknown)}", {"allowed": sorted(_OPTION_KEYS[action])})
    force = (options or {}).get("force")
    if force not in (None, "local", "ue"):
        return minimal_error("invalid_option", "force must be \"local\" or \"ue\"", {"action": action})
    try:
        report = run_sync(_bridge, action, paths, options)
    except SyncError as exc:
        return minimal_error(exc.code, str(exc), exc.details)
    return _finish(report)


def _bridge(operation: str, payload: dict[str, Any]) -> dict[str, Any]:
    return call_bridge(operation, payload)


def _finish(report: dict[str, Any]) -> dict[str, Any]:
    result = {"ok": report.get("error_count", 0) == 0 and not report.get("stopped", False), "data": report}
    if response_payload_bytes(result) <= LARGE_RESPONSE_INLINE_BYTE_LIMIT:
        return result
    artifact = artifact_handle("ue_sync_report", result)
    compact = {key: value for key, value in report.items() if key not in ("rows", "scene_rows", "plans", "all_diagnostics", "diagnostics")}
    rows = report.get("rows") or []
    compact["rows"] = rows[:_ROW_INLINE_LIMIT]
    compact["rows_truncated"] = max(0, len(rows) - _ROW_INLINE_LIMIT)
    if "scene_rows" in report:
        compact["scene_rows"] = report["scene_rows"][:_ROW_INLINE_LIMIT]
        compact["scene_rows_truncated"] = max(0, len(report["scene_rows"]) - _ROW_INLINE_LIMIT)
    compact["diagnostics"] = (report.get("diagnostics") or [])[:20]
    compact["artifact"] = artifact
    compact["next_read"] = {"tool": "ue_read", "args": {"target": "artifact", "query": {"artifact_id": artifact["id"]}}}
    return {"ok": result["ok"], "data": compact}
