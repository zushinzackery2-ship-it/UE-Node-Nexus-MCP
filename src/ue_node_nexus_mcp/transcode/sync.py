"""``ue_sync`` orchestration: init / status / pull / lint / push / schema."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from .errors import Diagnostic
from .lint import lint_document
from .parser import parse
from .paths import display_path, parse_text_path
from .state import SyncState
from .sync_files import mirrored_assets, read_text
from .sync_project import BridgeCall, ProjectContext, SyncError, ensure_schema, resolve_context, write_project_info
from .sync_pull import pull_assets
from .sync_push import PushOptions, push_assets
from .sync_status import compute_status, query_ue, resolve_selection

ACTIONS = ("init", "status", "pull", "lint", "push", "schema")
MAX_INLINE_DIAGNOSTICS = 60


def run_sync(bridge: BridgeCall, action: str, paths: list[str] | None = None, options: dict[str, Any] | None = None, env: dict[str, str] | None = None, cwd: Path | None = None) -> dict[str, Any]:
    options = dict(options or {})
    if action not in ACTIONS:
        raise SyncError("invalid_action", f"unknown action {action!r}; expected one of {', '.join(ACTIONS)}")
    context = resolve_context(bridge, env=env, cwd=cwd, require_bridge=action in ("init", "schema"))
    state = SyncState.load(context.project)
    report: dict[str, Any] = {"action": action, **context.info(), "warnings": list(context.warnings)}
    if action == "init":
        _init(bridge, context, state, options, report)
    elif action == "schema":
        schema = ensure_schema(bridge, context, force=True)
        report.update({"schema_key": schema.key, "schema_info": schema.info()})
    elif action == "status":
        _status(bridge, context, state, paths, options, report)
    elif action == "pull":
        _pull(bridge, context, state, paths, options, report)
    elif action == "lint":
        _lint(context, state, paths, report)
    elif action == "push":
        _push(bridge, context, state, paths, options, report)
    report["schema_key"] = context.schema_key
    return report


def _init(bridge: BridgeCall, context: ProjectContext, state: SyncState, options: dict[str, Any], report: dict[str, Any]) -> None:
    context.project.mkdir(parents=True, exist_ok=True)
    schema = ensure_schema(bridge, context, force=bool(options.get("refresh_schema", False)))
    write_project_info(context, {"auto_export": bool(options.get("auto_export", False))})
    report.update({"schema_key": schema.key, "schema_available": schema.available, "project_dir": str(context.project)})
    if options.get("auto_export"):
        from .sync_project import call_ok

        call_ok(bridge, "transcode_watch_set", {"enabled": True, "out_dir": str(context.project / ".nexus" / "pending")})
        report["auto_export"] = True
    if options.get("pull_all", True):
        _pull(bridge, context, state, None, {"include_stubs": bool(options.get("include_stubs", False)), "discover": True}, report)


def _selection(context: ProjectContext, state: SyncState, paths: list[str] | None) -> list[str]:
    return resolve_selection(context, paths, state)


def _status(bridge: BridgeCall, context: ProjectContext, state: SyncState, paths: list[str] | None, options: dict[str, Any], report: dict[str, Any]) -> None:
    selected = _selection(context, state, paths)
    discover = bool(options.get("discover", not paths))
    ue_infos, known = query_ue(bridge, context, selected, discover=discover, include_stubs=bool(options.get("include_stubs", False)))
    if discover:
        selected = sorted(set(selected) | set(ue_infos))
    statuses = compute_status(context, state, selected, ue_infos, known)
    counts: dict[str, int] = {}
    for status in statuses:
        counts[status.state] = counts.get(status.state, 0) + 1
    report["columns"] = ["asset", "kind", "state"]
    report["rows"] = [status.row() for status in statuses if status.state != "clean" or bool(options.get("include_clean", False))]
    report["counts"] = dict(sorted(counts.items()))
    report["total"] = len(statuses)
    report["ue_known"] = known


def _pull(bridge: BridgeCall, context: ProjectContext, state: SyncState, paths: list[str] | None, options: dict[str, Any], report: dict[str, Any]) -> None:
    if not context.bridge_available:
        raise SyncError("bridge_unavailable", "pull needs a live UE editor")
    ensure_schema(bridge, context)
    selected = _selection(context, state, paths)
    discover = bool(options.get("discover", not paths))
    include_stubs = bool(options.get("include_stubs", False))
    ue_infos, known = query_ue(bridge, context, selected, discover=discover, include_stubs=include_stubs)
    if discover:
        selected = sorted(set(selected) | set(ue_infos))
    statuses = compute_status(context, state, selected, ue_infos, known)
    rows = pull_assets(bridge, context, state, statuses, force=options.get("force"), include_stubs=include_stubs)
    _finish_rows(report, rows)


def _lint(context: ProjectContext, state: SyncState, paths: list[str] | None, report: dict[str, Any]) -> None:
    selected = _selection(context, state, paths)
    local = mirrored_assets(context.project)
    diagnostics: list[Diagnostic] = []
    rows: list[dict[str, Any]] = []
    for asset_path in selected:
        entry = local.get(asset_path)
        if entry is None:
            continue
        kind, file = entry
        label = display_path(context.project, file)
        document, sink = parse(read_text(file) or "", file=label)
        if not sink.has_errors:
            sink.extend(lint_document(document, kind, context.schema, label, context.schema_key or None))
        diagnostics.extend(sink.items)
        rows.append({"asset": asset_path, "kind": kind, "file": label, "errors": len(sink.errors()), "warnings": len(sink.items) - len(sink.errors())})
    report["rows"] = rows
    report["ok_files"] = sum(1 for row in rows if row["errors"] == 0)
    _attach_diagnostics(report, diagnostics)


def _push(bridge: BridgeCall, context: ProjectContext, state: SyncState, paths: list[str] | None, options: dict[str, Any], report: dict[str, Any]) -> None:
    push_options = PushOptions(
        dry_run=bool(options.get("dry_run", True)),
        compile=bool(options.get("compile", True)),
        save=bool(options.get("save", True)),
        force=options.get("force"),
        allow_delete=bool(options.get("allow_delete", False)),
        stop_on_error=bool(options.get("stop_on_error", True)),
    )
    if not push_options.dry_run and not context.bridge_available:
        raise SyncError("bridge_unavailable", "push needs a live UE editor")
    if context.bridge_available:
        ensure_schema(bridge, context)
    selected = _selection(context, state, paths)
    ue_infos, known = query_ue(bridge, context, selected)
    statuses = compute_status(context, state, selected, ue_infos, known)
    result = push_assets(bridge, context, state, statuses, push_options)
    report["dry_run"] = push_options.dry_run
    _finish_rows(report, result.rows)
    if result.plans:
        report["plans"] = result.plans
    if result.normalized:
        report["normalized_files"] = result.normalized
    if result.refreshed:
        report["refreshed_callers"] = result.refreshed
    report["stopped"] = result.stopped
    _attach_diagnostics(report, result.diagnostics)


def _finish_rows(report: dict[str, Any], rows: list[dict[str, Any]]) -> None:
    report["rows"] = rows
    counts: dict[str, int] = {}
    for row in rows:
        counts[str(row.get("action"))] = counts.get(str(row.get("action")), 0) + 1
    report["counts"] = dict(sorted(counts.items()))


def _attach_diagnostics(report: dict[str, Any], diagnostics: list[Diagnostic]) -> None:
    errors = [item for item in diagnostics if item.severity == "error"]
    warnings = [item for item in diagnostics if item.severity != "error"]
    ordered = errors + warnings
    report["error_count"] = len(errors)
    report["warning_count"] = len(warnings)
    report["diagnostics"] = [item.format() for item in ordered[:MAX_INLINE_DIAGNOSTICS]]
    if len(ordered) > MAX_INLINE_DIAGNOSTICS:
        report["diagnostics_truncated"] = len(ordered) - MAX_INLINE_DIAGNOSTICS
        report["all_diagnostics"] = [item.to_json() for item in ordered]


def kind_for_file(project: Path, path: Path) -> str | None:
    parsed = parse_text_path(project, path)
    return parsed[1] if parsed else None
