"""Refresh MaterialFunction consumers while preserving uncommitted local text."""

from __future__ import annotations

from pathlib import Path

from ..diff import build_plan
from ..errors import Diagnostic
from ..parser import parse
from ..paths import display_path, object_path, pending_dir, text_path
from ..state import SyncState
from ..sync_files import canonical_hash, load_base, read_json, read_text
from ..sync_project import BridgeCall, ProjectContext, SyncError, call_ok
from ..sync_status import AssetStatus
from .apply import commit_snapshot
from .diagnostics import apply_diagnostics
from .model import Prepared, PushOptions, PushResult
from .recovery import record_failure


def refresh_callers(bridge: BridgeCall, context: ProjectContext, state: SyncState, function: str, options: PushOptions, result: PushResult) -> None:
    data = call_ok(bridge, "asset_referencers_get", dict(asset_path=function, limit=500)).get("data") or dict()
    packages = [str(entry[0]) for entry in data.get("items") or data.get("rows") or [] if isinstance(entry, list) and entry and (len(entry) < 2 or entry[1] == "hard")]
    for package in dict.fromkeys(packages):
        caller = object_path(package)
        info = call_ok(bridge, "transcode_status", dict(asset_paths=[caller])).get("data") or dict()
        entries = [entry for entry in info.get("assets") or [] if isinstance(entry, list) and len(entry) >= 3]
        if not entries or entries[0][2] not in ("material", "material_function"):
            continue
        kind = str(entries[0][2])
        file = text_path(context.project, caller, kind)
        existing = read_text(file)
        record = state.get(caller)
        protected = existing is None or record is None or canonical_hash(existing) != record.base_hash
        payload = dict(
            asset_path=caller, kind=kind, plan=[dict(op="refresh_function_calls", function=function)],
            ids=dict(), create=False, dry_run=False, compile=options.compile, save=options.save,
            out_dir=str(pending_dir(context.project)),
        )
        response = bridge("transcode_apply", payload)
        document, _ = parse(existing or "")
        status = AssetStatus(caller, kind, "local-modified" if protected else "clean", file, None, record, None)
        item = Prepared(status, document, build_plan(document, document, kind), file, existing or "", load_base(context.project, caller))
        errors = apply_diagnostics(response, item, display_path(context.project, file), result)
        data = response.get("data") if isinstance(response, dict) else None
        data = data if isinstance(data, dict) else dict()
        raw_file = str(data.get("file") or "")
        raw = read_json(Path(raw_file)) if raw_file else None
        if errors or raw is None:
            if not errors:
                diagnostic = Diagnostic("error", "refresh_export_failed", f"no readable refresh export for {caller}", str(file))
                result.diagnostics.append(diagnostic)
                errors = [diagnostic.format()]
            record_failure(context, item, response, errors)
            raise SyncError("refresh_callers_failed", "; ".join(errors))
        if protected:
            result.diagnostics.append(Diagnostic(
                "warning", "caller_local_preserved", f"refreshed {caller} in UE; local text and base preserved", display_path(context.project, file),
            ))
        else:
            commit_snapshot(context, state, item, raw, data)
            Path(raw_file).unlink(missing_ok=True)
        result.refreshed.append(caller)
