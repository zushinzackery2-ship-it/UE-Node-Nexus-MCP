"""``ue_sync`` orchestration: init / status / pull / lint / push / schema."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from ..errors import Diagnostic, error
from ..lint.service import lint_document
from ..text.parser import parse
from ..storage.paths import display_path
from .state import SyncState
from .files import mirrored_assets, read_text
from .project import BridgeCall, ProjectContext, resolve_context, stored_project_name, write_project_info
from .schema import ensure_schema
from ..errors import SyncError
from .pull import pull_assets
from .push import PushOptions, push_assets
from .status import compute_status, query_ue, resolve_selection
from ..scene.sync import SceneBatch
from ..transaction.lock import MirrorLock
from ..collaboration.report.options import ACTIONS as COLLABORATION_ACTIONS
from ..collaboration.report.options import BACKGROUND_ACTIONS, TRANSPORT_KEYS
from .lifecycle import requires_editor
from ..storage.paths import resolve_root, project_dir

LEGACY_ACTIONS = ("init", "status", "pull", "lint", "push", "schema")
ACTIONS = tuple(dict.fromkeys((*LEGACY_ACTIONS, *COLLABORATION_ACTIONS)))
MAX_INLINE_DIAGNOSTICS = 60


def run_sync(bridge: BridgeCall, action: str, paths: list[str] | None = None, options: dict[str, Any] | None = None, env: dict[str, str] | None = None, cwd: Path | None = None) -> dict[str, Any]:
    from ...diagnostics.contracts.observation import capture, record
    from ...diagnostics.contracts.workflows import attach_report

    with capture() as observed:
        def observed_bridge(operation, payload):
            return record(bridge(operation, payload))

        report = _run_sync(observed_bridge, action, paths, options, env, cwd)
        return attach_report(report, action, observed.last)


def _run_sync(bridge: BridgeCall, action: str, paths: list[str] | None = None, options: dict[str, Any] | None = None, env: dict[str, str] | None = None, cwd: Path | None = None) -> dict[str, Any]:
    options = dict(options or {})
    if action not in ACTIONS:
        raise SyncError("invalid_action", f"unknown action {action!r}; expected one of {', '.join(ACTIONS)}")
    root = resolve_root(env, cwd)
    project_name = options.get("project")
    if action == "continue" and not project_name:
        project_name = stored_project_name(root)
    hinted = project_dir(root, project_name) if project_name else None
    offline = not requires_editor(action, options, hinted)
    context = resolve_context(bridge, env=env, cwd=cwd, require_bridge=action == "init",
                              project_hint=options.get("project"), offline=offline)
    if action in BACKGROUND_ACTIONS and set(options) & TRANSPORT_KEYS:
        from .background import run

        options.setdefault("project", context.project_name)
        runner = lambda queued_action, queued_paths, queued_options: run_sync(bridge, queued_action, queued_paths, queued_options, env, cwd)
        return run(action, paths, options, runner, context.root)
    if action == "schema":
        from .schema import run
        from ..collaboration.report.options import validate

        validate(action, options)
        with MirrorLock(context.root, context.root / ".nexus" / "schema.lock"):
            return dict(action=action, **run(bridge, context, options))
    if action == "lint" and options.get("files_root"):
        # A directory of mirror files needs the cached schema, not a registered
        # mirror root and not an owning workspace; it is checkable on its own.
        from ..collaboration.report.options import validate
        from ..collaboration.workspace.lint import lint_root

        validate(action, options)
        return dict(context.info(), **lint_root(context, options["files_root"], paths))
    from ..collaboration.store.migration import enabled

    if enabled(context) or options.get("workspace_id") or action not in LEGACY_ACTIONS:
        if action == "init":
            raise SyncError("workspace_required", "collaboration is enabled; use checkout to create a workspace")
        from ..collaboration.service import run

        result = run(bridge, context, action, paths, options)
        return dict(context.info(), **result)
    with MirrorLock(context.root):
        return _run_locked(bridge, context, action, paths, options)


def _run_locked(bridge: BridgeCall, context: ProjectContext, action: str, paths: list[str] | None, options: dict[str, Any]) -> dict[str, Any]:
    state = SyncState.load(context.project)
    report: dict[str, Any] = {"action": action, **context.info(), "warnings": list(context.warnings)}
    scenes = SceneBatch(context, paths, options, action)
    paths = scenes.prepare_push() if action == "push" else scenes.asset_paths
    has_assets = paths is None or bool(paths)
    if action == "init":
        init_options = dict(options)
        if options.get("scene") is not None:
            init_options["pull_all"] = False
        _init(bridge, context, state, init_options, report)
    elif action == "schema":
        schema = ensure_schema(bridge, context, force=True)
        report.update({"schema_key": schema.key, "schema_info": schema.info()})
    elif action == "status" and has_assets:
        _status(bridge, context, state, paths, options, report)
    elif action == "pull" and has_assets:
        _pull(bridge, context, state, paths, options, report)
    elif action == "lint" and has_assets:
        _lint(context, state, paths, report)
    elif action == "push" and has_assets and not scenes.abort_assets():
        _push(bridge, context, state, paths, options, report)
    scenes.run(bridge, action, report)
    report["schema_key"] = context.schema_key
    report["warnings"] = list(dict.fromkeys([*report.get("warnings", []), *context.warnings]))
    return report


def _init(bridge: BridgeCall, context: ProjectContext, state: SyncState, options: dict[str, Any], report: dict[str, Any]) -> None:
    context.project.mkdir(parents=True, exist_ok=True)
    schema = ensure_schema(bridge, context, force=bool(options.get("refresh_schema", False)))
    write_project_info(context, {"auto_export": bool(options.get("auto_export", False))})
    report.update({"schema_key": schema.key, "schema_available": schema.available, "project_dir": str(context.project)})
    if options.get("auto_export"):
        from .project import call_ok

        call_ok(bridge, "transcode_watch_set", {"enabled": True, "out_dir": str(context.project / ".nexus" / "pending" / "watch")})
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
    report["columns"] = ["asset", "kind", "state", "ue_dirty", "ue_saved_changed"]
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
            # Only an explicit selection can name an asset with no text; a silent
            # skip here once let `lint /Game/Folder` report 0 files as success.
            if paths:
                diagnostics.append(error("not_mirrored", f"{asset_path} has no mirror file to lint; pull it first"))
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
    if not known and not push_options.dry_run:
        raise SyncError("ue_status_unavailable", "push requires current UE asset status")
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
