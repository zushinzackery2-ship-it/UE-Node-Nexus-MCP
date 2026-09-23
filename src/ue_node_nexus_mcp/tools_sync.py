"""``ue_sync`` facade: the text-mirror workflow entry point."""

from __future__ import annotations

from typing import Any, Literal

from .facade_artifact import follow, list_reads
from .facade_response import LARGE_RESPONSE_INLINE_BYTE_LIMIT, artifact_handle, minimal_error, response_payload_bytes
from .runtime import call_bridge, thin_tool
from .transcode.sync import ACTIONS, run_sync
from .transcode.sync_project import SyncError
from .transcode.collaboration.report.options import COMMON, OPTIONS
from .transcode.lifecycle import run_managed_sync
from .instances.errors import InstanceError

SyncAction = Literal["init", "checkout", "workspaces", "status", "fetch", "pull", "lint", "push", "schema",
                     "stage", "unstage", "commit", "amend", "merge", "resolve", "continue", "abort", "recover", "close",
                     "branch", "switch", "tag", "log", "show", "diff", "blame", "reflog", "stash", "restore", "revert", "reset", "cherry-pick", "rebase"]
_OPTION_KEYS = {
    "init": {"pull_all", "include_stubs", "auto_export", "refresh_schema", "scene"},
    "status": {"discover", "include_stubs", "include_clean", "scene"},
    "pull": {"discover", "include_stubs", "force", "scene"},
    "lint": set(),
    "push": {"dry_run", "compile", "save", "force", "allow_delete", "stop_on_error", "scene"},
    "schema": set(),
}
_ROW_INLINE_LIMIT = 40
for _action, _keys in OPTIONS.items():
    _OPTION_KEYS.setdefault(_action, set()).update(_keys | COMMON)


@thin_tool()
def ue_sync(
    action: SyncAction,
    paths: list[str] | None = None,
    options: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Version and collaborate on UE assets through independent text workspaces.

    The editable surface is text: ``.nexus`` files under the shared project root
    returned by ``bridge_instance_ensure`` mirror Material,
    MaterialFunction, MaterialInstance, Blueprint, Niagara and property-bag assets.
    Edit those files with your own file tools; never generate or patch them with a
    script, and never write conflict markers into them.

    checkout creates a workspace; its absolute files_root is this agent's editable
    copy. Pass options.workspace_id to subsequent actions. stage/commit record
    local history; push merges the committed HEAD with current UE memory and
    publishes with revision checks and durable receipts. resolve/continue/abort
    manage persistent conflicts. log/show/diff/blame, branch/tag, stash,
    restore/revert/reset/cherry-pick/rebase/amend/reflog operate on local history.
    Mutations default to dry_run=true; execute with dry_run=false and optionally
    a proposal_id. schema(category/query/details/target/context) queries the
    current classified parameter catalog, also used by lint and history.
    Legacy init/pull/push remain available until checkout enables collaboration;
    once it is enabled those actions require options.workspace_id, and init is
    refused with workspace_required - create a checkout and retry.
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
        report = run_managed_sync(_bridge, action, paths, options, run_sync)
    except (SyncError, InstanceError) as exc:
        return minimal_error(exc.code, str(exc), exc.details)
    return _finish(report)


def _bridge(operation: str, payload: dict[str, Any]) -> dict[str, Any]:
    return call_bridge(operation, payload)


def _finish(report: dict[str, Any]) -> dict[str, Any]:
    result = {"ok": report.get("error_count", 0) == 0 and not report.get("stopped", False) and report.get("status") not in ("conflict", "stale", "recovery_required"), "data": report}
    if response_payload_bytes(result) <= LARGE_RESPONSE_INLINE_BYTE_LIMIT:
        return result
    artifact = artifact_handle("ue_sync_report", result)
    large = ("rows", "scene_rows", "plans", "all_diagnostics", "diagnostics", "conflicts", "changes", "workspaces", "commits", "entries", "revisions")
    compact = {key: value for key, value in report.items() if key not in large}
    for key in ("conflicts", "changes", "workspaces", "commits", "entries", "plans"):
        if key in report:
            compact[key] = report[key][:_ROW_INLINE_LIMIT]
            compact[key + "_truncated"] = max(0, len(report[key]) - _ROW_INLINE_LIMIT)
    rows = report.get("rows") or []
    compact["rows"] = rows[:_ROW_INLINE_LIMIT]
    compact["rows_truncated"] = max(0, len(rows) - _ROW_INLINE_LIMIT)
    if "scene_rows" in report:
        compact["scene_rows"] = report["scene_rows"][:_ROW_INLINE_LIMIT]
        compact["scene_rows_truncated"] = max(0, len(report["scene_rows"]) - _ROW_INLINE_LIMIT)
    compact["diagnostics"] = (report.get("diagnostics") or [])[:20]
    compact["artifact"] = artifact
    compact["next_read"] = follow(artifact["id"])
    # The whole report can be far larger than a response; each list in it is
    # readable item by item from here, never as one megabyte-sized value.
    compact["page_lists_with"] = list_reads(artifact["id"], result)
    if response_payload_bytes(dict(ok=result["ok"], data=compact)) > LARGE_RESPONSE_INLINE_BYTE_LIMIT:
        keys = ("action", "status", "workspace_id", "merge_id", "apply_id", "proposal_id", "commit_id", "source_commit",
                "published_commit", "candidate", "base", "ours", "theirs", "error_count", "conflict_count", "applied",
                "source_integrated", "workspace_rebase_required", "dry_run", "schema_key")
        summary = dict((key, compact[key]) for key in keys if key in compact)
        summary.update(artifact=artifact, next_read=compact["next_read"], page_lists_with=compact["page_lists_with"],
                       rows_count=len(rows), inline_truncated=True)
        compact = summary
    return {"ok": result["ok"], "data": compact}
