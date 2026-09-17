"""Validate local intent and compute plans without committing mirror state."""

from __future__ import annotations

import logging
from typing import Any

from ..codec import bookkeeping_from_base, document_from_raw, with_bookkeeping
from ..diff import build_plan
from ..errors import Diagnostic
from ..lint import lint_document
from ..parser import parse
from ..paths import display_path, object_path
from ..sync_files import base_document, load_base, read_json, read_text
from ..sync_project import BridgeCall, ProjectContext, SyncError, ensure_root_registered
from ..sync_pull import export_raw
from ..sync_status import AssetStatus
from .model import Prepared, PushOptions, PushResult, row
from .recovery import load_recovery

LOGGER = logging.getLogger(__name__)


def prepare_item(bridge: BridgeCall, context: ProjectContext, status: AssetStatus, options: PushOptions, result: PushResult) -> Prepared | None:
    recovery = load_recovery(context.project, status.asset_path)
    if status.state in ("clean", "ue-new", "unknown") and recovery is None:
        result.rows.append(row(status.asset_path, status.kind, status.state, "skipped"))
        return None
    if status.state == "ue-modified" and options.force != "local" and recovery is None:
        result.rows.append(row(status.asset_path, status.kind, status.state, "pull-first"))
        return None
    if status.state == "local-deleted":
        result.rows.append(row(status.asset_path, status.kind, status.state, "orphan", warnings=["text file deleted; UE asset kept"]))
        return None
    if status.state == "both-modified" and options.force != "local":
        message = 'both the text and UE changed; pull first or push with force="local"'
        result.rows.append(row(status.asset_path, status.kind, status.state, "conflict", errors=[message]))
        result.diagnostics.append(Diagnostic("error", "sync_conflict", message, str(status.file)))
        return None
    if status.file is None:
        raise SyncError("not_mirrored", f"{status.asset_path} has no local text")
    text = read_text(status.file)
    if text is None:
        raise SyncError("local_file_missing", f"mirror file disappeared: {status.file}")
    label = display_path(context.project, status.file)
    document, sink = parse(text, file=label)
    if not sink.has_errors:
        sink.extend(lint_document(document, status.kind, context.schema, label, context.schema_key or None))
    if any(diagnostic.code == "schema_stale" for diagnostic in sink.items):
        sink.error("schema_stale", "run ue_sync schema and update the file's schema before pushing")
    result.diagnostics.extend(sink.items)
    if sink.has_errors:
        result.rows.append(row(status.asset_path, status.kind, status.state, "failed", errors=[entry.format() for entry in sink.errors()][:5]))
        return None
    if object_path(document.header.asset) != status.asset_path:
        raise SyncError("asset_path_mismatch", f"header targets {document.header.asset}, but the mirror path targets {status.asset_path}")
    base = load_base(context.project, status.asset_path)
    reconcile = recovery is not None or (options.force == "local" and status.state in ("ue-modified", "both-modified"))
    if status.ue is not None and reconcile:
        base = live_base(bridge, context, status, base, recovery)
    elif status.state in ("local-new", "ue-deleted"):
        base = None
    elif base is None:
        raise SyncError("base_missing", f"{status.asset_path} needs a live base; pull first or push with force=local")
    ids = dict((identifier, guid) for guid, identifier in (base or dict()).get("ids", dict()).items())
    plan = build_plan(document, base_document(base, context.schema) if base is not None else None, status.kind, ids, context.schema)
    diagnostics = [Diagnostic(entry.severity, entry.code, entry.message, label, entry.line) for entry in plan.diagnostics]
    result.diagnostics.extend(diagnostics)
    if plan.has_errors:
        result.rows.append(row(status.asset_path, status.kind, status.state, "failed", errors=[entry.format() for entry in diagnostics if entry.severity == "error"][:5]))
        return None
    return Prepared(status, document, plan, status.file, text, base, reconcile)


def live_base(bridge: BridgeCall, context: ProjectContext, status: AssetStatus, previous: dict[str, Any] | None, recovery: dict[str, Any] | None = None) -> dict[str, Any]:
    if not context.bridge_available:
        raise SyncError("bridge_unavailable", "reconciling a push requires a live editor snapshot")
    ensure_root_registered(bridge, context)
    files = export_raw(bridge, context, [status.asset_path], dict(((status.asset_path, status.kind),)), False)
    path = files.get(status.asset_path)
    raw = read_json(path) if path is not None else None
    if raw is None or object_path(str(raw.get("asset_path", ""))) != status.asset_path or raw.get("kind") != status.kind:
        raise SyncError("export_failed", f"no valid live export for {status.asset_path}")
    ids, order = bookkeeping_from_base(previous)
    ids = dict(ids or dict())
    ids.update((recovery or dict()).get("ids") or dict())
    _, ids, order = document_from_raw(raw, ids, order, context.schema)
    LOGGER.info("sync rebase asset=%s recovery=%s", status.asset_path, recovery is not None)
    return with_bookkeeping(raw, ids, order)


def replan_refreshed(bridge: BridgeCall, context: ProjectContext, item: Prepared) -> None:
    item.base = live_base(bridge, context, item.status, item.base)
    ids = dict((identifier, guid) for guid, identifier in item.base.get("ids", dict()).items())
    item.plan = build_plan(item.document, base_document(item.base, context.schema), item.status.kind, ids, context.schema)
    if item.plan.has_errors:
        raise SyncError("replan_failed", "; ".join(entry.format() for entry in item.plan.diagnostics))
    item.reconcile = True
