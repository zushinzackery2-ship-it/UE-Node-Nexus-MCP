"""Apply one asset and commit local state only after verified success."""

from __future__ import annotations

import json
import logging
from pathlib import Path
from typing import Any

from ..errors import Diagnostic
from ..paths import display_path, object_path, pending_dir, state_path
from ..state import AssetState, SyncState, sha256_text
from ..sync_files import backup_text, local_order_from_document, read_json, read_text, render_snapshot
from ..sync_project import BridgeCall, ProjectContext, SyncError, apply_operation, now_iso
from .commit import commit_files
from .diagnostics import apply_diagnostics
from .model import Prepared, PushOptions, PushResult, row
from .recovery import record_failure, recovery_path

LOGGER = logging.getLogger(__name__)


def ensure_source_unchanged(item: Prepared) -> None:
    if read_text(item.file) != item.text:
        raise SyncError("local_changed_during_push", f"local text changed during push: {item.file}; edits were preserved")


def commit_snapshot(context: ProjectContext, state: SyncState, item: Prepared, raw: dict[str, Any], data: dict[str, Any]) -> bool:
    ensure_source_unchanged(item)
    if object_path(str(raw.get("asset_path", ""))) != item.status.asset_path or raw.get("kind") != item.status.kind:
        raise SyncError("invalid_export", "apply returned an export for a different asset or kind")
    extra_ids = dict((str(guid), str(identifier)) for identifier, guid in (data.get("id_map") or dict()).items())
    snapshot = render_snapshot(
        context.project, raw, item.base,
        order_override=local_order_from_document(item.document), extra_ids=extra_ids,
    )
    text, file = snapshot.text, snapshot.text_file
    normalized = text != item.text
    if normalized:
        backup_text(context.project, file)
    updated = SyncState(dict(state.assets))
    updated.put(AssetState(
        item.status.asset_path, item.status.kind, file.relative_to(context.project).as_posix(),
        sha256_text(text), str(raw.get("saved_hash", "")), bool(raw.get("dirty", False)), now_iso(),
    ))
    contents = dict()
    if normalized:
        contents[file] = text
    contents[snapshot.base_file] = json.dumps(snapshot.stored, indent=1, ensure_ascii=False)
    contents[state_path(context.project)] = updated.serialize()
    commit_files(contents)
    state.assets = updated.assets
    return normalized


def accept_unchanged(context: ProjectContext, state: SyncState, item: Prepared, result: PushResult) -> None:
    if item.base is not None:
        normalized = commit_snapshot(context, state, item, item.base, dict())
        if normalized:
            result.normalized.append(display_path(context.project, item.file))
    result.rows.append(row(item.status.asset_path, item.status.kind, item.status.state, "unchanged"))


def apply_item(bridge: BridgeCall, context: ProjectContext, state: SyncState, item: Prepared, options: PushOptions, result: PushResult) -> bool:
    ensure_source_unchanged(item)
    payload = item.plan.to_payload()
    payload.update(dry_run=False, compile=options.compile, save=options.save, out_dir=str(pending_dir(context.project)))
    label = display_path(context.project, item.file)
    LOGGER.info("sync apply asset=%s verbs=%d reconcile=%s", item.status.asset_path, len(item.plan.verbs), item.reconcile)
    record_failure(context, item, dict(phase="applying"), [])
    try:
        response = bridge(apply_operation(item.status.kind), payload)
    except (OSError, SyncError) as exc:
        code = exc.code if isinstance(exc, SyncError) else "mcp_bridge_error"
        response = dict(ok=False, error=dict(code=code, message=str(exc)))
    errors = apply_diagnostics(response, item, label, result)
    data = response.get("data") if isinstance(response, dict) else None
    data = data if isinstance(data, dict) else dict()
    raw_file = str(data.get("file") or "")
    normalized = False
    record_failure(context, item, response, errors)
    if not errors:
        try:
            if options.save and data.get("saved") is False and (data.get("save_required") or data.get("changed") or data.get("dirty") or item.plan.creates_asset):
                raise SyncError("save_failed", "asset changes were not saved")
            raw = read_json(Path(raw_file)) if raw_file else None
            if raw is None:
                raise SyncError("export_failed", str(data.get("export_error") or "apply did not return a readable raw export"))
            normalized = commit_snapshot(context, state, item, raw, data)
        except (OSError, ValueError, SyncError) as exc:
            code = exc.code if isinstance(exc, SyncError) else "mirror_commit_failed"
            diagnostic = Diagnostic("error", code, str(exc), label)
            result.diagnostics.append(diagnostic)
            errors.append(diagnostic.format())
    details: dict[str, Any] = dict(count=int(data.get("applied", 0)))
    if errors:
        details.update(errors=errors[:5], local_preserved=True)
        details["recovery_file"] = record_failure(context, item, response, errors)
        if raw_file:
            details["pending_file"] = raw_file
        result.stopped = options.stop_on_error or any("mcp_bridge_error" in error for error in errors)
        action = "pushed-with-errors" if data else "failed"
        LOGGER.warning("sync incomplete asset=%s recovery=%s errors=%s", item.status.asset_path, details["recovery_file"], errors)
    else:
        Path(raw_file).unlink(missing_ok=True)
        recovery_path(context.project, item.status.asset_path).unlink(missing_ok=True)
        action = "pushed"
        if normalized:
            result.normalized.append(label)
            details["warnings"] = ["file normalized after push"]
        LOGGER.info("sync committed asset=%s normalized=%s", item.status.asset_path, normalized)
    compile_info = data.get("compile")
    if isinstance(compile_info, dict):
        details["compile_errors"] = int(compile_info.get("error_count", 0))
    result.rows.append(row(item.status.asset_path, item.status.kind, item.status.state, action, **details))
    return not errors
