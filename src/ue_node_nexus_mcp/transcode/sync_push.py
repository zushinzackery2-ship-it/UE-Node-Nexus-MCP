"""``ue_sync push`` (L2U): lint -> diff -> plan -> transcode_apply -> re-materialize."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .diff import build_plan
from .errors import Diagnostic
from .lint import lint_document
from .model import Document
from .paths import display_path, object_path, pending_dir
from .parser import parse
from .plan import AssetPlan
from .state import AssetState, SyncState, sha256_text
from .sync_deps import order_assets
from .sync_files import backup_text, base_document, load_base, local_order_from_document, materialize, read_json, read_text, write_text_atomic
from .sync_project import BridgeCall, ProjectContext, SyncError, apply_operation, call_ok, ensure_root_registered, now_iso
from .sync_status import AssetStatus


@dataclass
class PushOptions:
    dry_run: bool = True
    compile: bool = True
    save: bool = True
    force: str | None = None
    allow_delete: bool = False
    stop_on_error: bool = True


@dataclass
class PushResult:
    rows: list[dict[str, Any]] = field(default_factory=list)
    plans: list[dict[str, Any]] = field(default_factory=list)
    diagnostics: list[Diagnostic] = field(default_factory=list)
    normalized: list[str] = field(default_factory=list)
    refreshed: list[str] = field(default_factory=list)
    stopped: bool = False


@dataclass
class Prepared:
    status: AssetStatus
    document: Document
    plan: AssetPlan
    file: Path
    text: str


def push_assets(bridge: BridgeCall, context: ProjectContext, state: SyncState, statuses: list[AssetStatus], options: PushOptions) -> PushResult:
    result = PushResult()
    prepared: dict[str, Prepared] = {}
    for status in statuses:
        item = _prepare(context, state, status, options, result)
        if item is not None:
            prepared[status.asset_path] = item
    if not prepared:
        return result
    order = order_assets({asset: (item.status.kind, item.document) for asset, item in prepared.items()})
    if options.dry_run:
        for asset in order:
            item = prepared[asset]
            result.plans.append(item.plan.summary())
            result.rows.append(_row(asset, item.status.kind, item.status.state, "planned", count=len(item.plan.verbs)))
        return result
    ensure_root_registered(bridge, context)
    for asset in order:
        item = prepared[asset]
        if result.stopped:
            result.rows.append(_row(asset, item.status.kind, item.status.state, "skipped"))
            continue
        try:
            _apply(bridge, context, state, item, options, result)
        except SyncError as exc:
            result.rows.append(_row(asset, item.status.kind, item.status.state, "failed", errors=[f"{exc.code}: {exc}"]))
            result.diagnostics.append(Diagnostic("error", exc.code, str(exc), display_path(context.project, item.file)))
            if options.stop_on_error or exc.code == "mcp_bridge_error":
                result.stopped = True
    state.save(context.project)
    return result


def _prepare(context: ProjectContext, state: SyncState, status: AssetStatus, options: PushOptions, result: PushResult) -> Prepared | None:
    kind = status.kind
    if status.state in ("clean", "ue-modified", "ue-new", "unknown"):
        action = "skipped" if status.state != "ue-modified" else "pull-first"
        result.rows.append(_row(status.asset_path, kind, status.state, action))
        return None
    if status.state == "local-deleted":
        result.rows.append(_row(status.asset_path, kind, status.state, "orphan", warnings=["text file deleted; UE asset kept (pass allow_delete=true to delete it)"]))
        return None
    if status.state == "both-modified" and options.force != "local":
        result.rows.append(_row(status.asset_path, kind, status.state, "conflict", errors=["both the text and the UE asset changed; pull first or push with force=\"local\""]))
        return None
    if status.file is None:
        result.rows.append(_row(status.asset_path, kind, status.state, "skipped"))
        return None
    text = read_text(status.file) or ""
    file_label = display_path(context.project, status.file)
    document, sink = parse(text, file=file_label)
    if sink.has_errors:
        result.diagnostics.extend(sink.items)
        result.rows.append(_row(status.asset_path, kind, status.state, "failed", errors=[item.format() for item in sink.errors()][:5]))
        return None
    lint = lint_document(document, kind, context.schema, file_label, context.schema_key or None)
    result.diagnostics.extend(lint.items)
    if any(item.code == "schema_stale" for item in lint.items) and options.force is None:
        result.rows.append(_row(status.asset_path, kind, status.state, "failed", errors=["schema_stale: run ue_sync schema (or pull) before pushing"]))
        return None
    if lint.has_errors:
        result.rows.append(_row(status.asset_path, kind, status.state, "failed", errors=[item.format() for item in lint.errors()][:5]))
        return None
    base_raw = load_base(context.project, status.asset_path) if status.state not in ("local-new", "ue-deleted") else None
    base = base_document(base_raw) if base_raw else None
    ids = dict((base_raw or {}).get("ids") or {}) if base_raw else {}
    plan = build_plan(document, base, kind, {identifier: guid for guid, identifier in ids.items()})
    plan_diagnostics = [Diagnostic(item.severity, item.code, item.message, file_label, item.line) for item in plan.diagnostics]
    result.diagnostics.extend(plan_diagnostics)
    if plan.has_errors:
        result.rows.append(_row(status.asset_path, kind, status.state, "failed", errors=[item.format() for item in plan_diagnostics if item.severity == "error"][:5]))
        return None
    if plan.empty:
        _refresh_unchanged(context, state, status, document, text, base_raw)
        result.rows.append(_row(status.asset_path, kind, status.state, "unchanged"))
        return None
    return Prepared(status=status, document=document, plan=plan, file=status.file, text=text)


def _refresh_unchanged(context: ProjectContext, state: SyncState, status: AssetStatus, document: Document, text: str, base_raw: dict[str, Any] | None) -> None:
    """Layout-only edits: adopt the new order/hash as the base without touching UE."""
    if base_raw is None:
        return
    order = local_order_from_document(document)
    _, new_text, text_file, _ = materialize(context.project, base_raw, base_raw, order_override=order)
    record = state.get(status.asset_path) or AssetState(status.asset_path, status.kind, text_file.relative_to(context.project).as_posix())
    if new_text != text:
        write_text_atomic(text_file, new_text)
    record.base_hash = sha256_text(new_text)
    record.synced_at = now_iso()
    state.put(record)


def _apply(bridge: BridgeCall, context: ProjectContext, state: SyncState, item: Prepared, options: PushOptions, result: PushResult) -> None:
    status, plan = item.status, item.plan
    payload = plan.to_payload()
    payload.update({"dry_run": False, "compile": options.compile, "save": options.save, "out_dir": str(pending_dir(context.project))})
    response = bridge(apply_operation(status.kind), payload)
    data = response.get("data") if isinstance(response, dict) and isinstance(response.get("data"), dict) else {}
    if not isinstance(response, dict) or (response.get("ok") is not True and not data.get("failed")):
        # a rejected request (not a partially failed plan): surface the bridge error as-is
        error = (response or {}).get("error") if isinstance(response, dict) else None
        raise SyncError(str((error or {}).get("code", "bridge_error")), str((error or {}).get("message", f"{apply_operation(status.kind)} failed")), {"error": error})
    file_label = display_path(context.project, item.file)
    errors = _map_failures(data, plan, item.document, file_label, result)
    raw_file = str(data.get("file", ""))
    normalized = False
    if raw_file:
        raw = read_json(Path(raw_file))
        if raw is not None:
            normalized = _materialize_after_apply(context, state, status, item, raw, data)
            Path(raw_file).unlink(missing_ok=True)
    if plan.interface_changed and status.kind == "material_function" and not plan.creates_asset:
        try:
            result.refreshed.extend(refresh_callers(bridge, context, state, status.asset_path, options))
        except SyncError as exc:
            # callers are refreshed lazily on their next push; do not fail the pushed asset
            result.diagnostics.append(Diagnostic("warning", "refresh_callers_failed", f"{exc.code}: {exc}", file_label, None))
    action = "pushed" if not errors else "pushed-with-errors"
    compile_info = data.get("compile") if isinstance(data.get("compile"), dict) else {}
    result.rows.append(_row(
        status.asset_path, status.kind, status.state, action,
        count=int(data.get("applied", len(plan.verbs))),
        errors=errors[:5] or None,
        warnings=(["file normalized after push"] if normalized else None),
        compile_errors=int(compile_info.get("error_count", 0)) if compile_info else None,
    ))
    if normalized:
        result.normalized.append(file_label)
    if errors and options.stop_on_error:
        result.stopped = True


def _map_failures(data: dict[str, Any], plan: AssetPlan, document: Document, file_label: str, result: PushResult) -> list[str]:
    lines_by_id = {decl.id: decl.line for _, decl in document.iter_decls()}
    errors: list[str] = []
    for failure in data.get("failed") or []:
        if not isinstance(failure, dict):
            continue
        index = failure.get("index")
        line = None
        if isinstance(index, int) and 0 <= index < len(plan.verbs):
            line = plan.verbs[index].line
        message = str(failure.get("message", failure.get("code", "apply failed")))
        diagnostic = Diagnostic("error", str(failure.get("code", "apply_failed")), message, file_label, line)
        result.diagnostics.append(diagnostic)
        errors.append(diagnostic.format())
    id_map = {str(guid): str(identifier) for identifier, guid in (data.get("id_map") or {}).items()}
    id_map.update({guid: identifier for identifier, guid in plan.ids.items()})
    for item in data.get("diagnostics") or []:
        if not isinstance(item, dict):
            continue
        severity = "error" if str(item.get("severity", "error")).lower() == "error" else "warning"
        node_id = id_map.get(str(item.get("node_id", "")), "")
        line = lines_by_id.get(node_id)
        diagnostic = Diagnostic(severity, str(item.get("code", "compile")), str(item.get("message", "")), file_label, line)
        result.diagnostics.append(diagnostic)
        if severity == "error":
            errors.append(diagnostic.format())
    return errors


def _materialize_after_apply(context: ProjectContext, state: SyncState, status: AssetStatus, item: Prepared, raw: dict[str, Any], data: dict[str, Any]) -> bool:
    previous = load_base(context.project, status.asset_path)
    extra_ids = {str(guid): str(identifier) for identifier, guid in (data.get("id_map") or {}).items()}
    order = local_order_from_document(item.document)
    _, text, text_file, _ = materialize(context.project, raw, previous, order_override=order, extra_ids=extra_ids)
    normalized = text != item.text
    if normalized:
        backup_text(context.project, text_file)
        write_text_atomic(text_file, text)
    state.put(AssetState(
        asset_path=status.asset_path,
        kind=str(raw.get("kind", status.kind)),
        file=text_file.relative_to(context.project).as_posix(),
        base_hash=sha256_text(text),
        ue_saved_hash=str(raw.get("saved_hash", "")),
        ue_dirty=bool(raw.get("dirty", False)),
        synced_at=now_iso(),
    ))
    return normalized


def refresh_callers(bridge: BridgeCall, context: ProjectContext, state: SyncState, function_path: str, options: PushOptions) -> list[str]:
    """A MaterialFunction interface changed: update every caller's function-call nodes."""
    refreshed: list[str] = []
    data = call_ok(bridge, "asset_referencers_get", {"asset_path": function_path, "limit": 500}).get("data") or {}
    packages = [str(row[0]) for row in data.get("items") or data.get("rows") or [] if isinstance(row, list) and row and str(row[1] if len(row) > 1 else "hard") == "hard"]
    for package in packages:
        caller = object_path(package)
        info = call_ok(bridge, "transcode_status", {"asset_paths": [caller]}).get("data") or {}
        rows = [row for row in info.get("assets") or [] if isinstance(row, list) and len(row) >= 3]
        if not rows or rows[0][2] not in ("material", "material_function"):
            continue
        payload = {"asset_path": caller, "kind": rows[0][2], "plan": [{"op": "refresh_function_calls", "function": function_path}], "ids": {}, "create": False,
                   "dry_run": False, "compile": options.compile, "save": options.save, "out_dir": str(pending_dir(context.project))}
        response = call_ok(bridge, "transcode_apply", payload)
        raw_file = str((response.get("data") or {}).get("file", ""))
        raw = read_json(Path(raw_file)) if raw_file else None
        if raw is not None:
            previous = load_base(context.project, caller)
            _, text, text_file, _ = materialize(context.project, raw, previous)
            existing = read_text(text_file)
            if existing != text:
                backup_text(context.project, text_file)
                write_text_atomic(text_file, text)
            state.put(AssetState(caller, str(raw.get("kind", "")), text_file.relative_to(context.project).as_posix(), sha256_text(text), str(raw.get("saved_hash", "")), bool(raw.get("dirty", False)), now_iso()))
            Path(raw_file).unlink(missing_ok=True)
        refreshed.append(caller)
    return refreshed


def _row(asset: str, kind: str, state: str, action: str, *, count: int | None = None, errors: list[str] | None = None, warnings: list[str] | None = None, compile_errors: int | None = None) -> dict[str, Any]:
    row: dict[str, Any] = {"asset": asset, "kind": kind, "state": state, "action": action}
    if count is not None:
        row["count"] = count
    if compile_errors is not None:
        row["compile_errors"] = compile_errors
    if errors:
        row["errors"] = errors
    if warnings:
        row["warnings"] = warnings
    return row
