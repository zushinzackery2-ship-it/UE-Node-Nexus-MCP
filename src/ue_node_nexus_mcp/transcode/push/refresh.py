"""Refresh MaterialFunction consumers while preserving uncommitted local text."""

from __future__ import annotations

from pathlib import Path

from ..diff.service import build_plan
from ..errors import Diagnostic
from ..material.calls import CONSUMER_KINDS, refresh_ops
from ..text.parser import parse
from ..storage.paths import display_path, object_path, pending_dir, text_path
from ..sync.state import SyncState
from ..sync.files import canonical_hash, load_base, read_json, read_text
from ..sync.project import BridgeCall, ProjectContext, call_ok
from ..errors import SyncError
from ..sync.status import AssetStatus
from .apply import commit_snapshot
from .diagnostics import apply_diagnostics
from .model import Prepared, PushOptions, PushResult, row
from .recovery import record_failure


def refresh_callers(bridge: BridgeCall, context: ProjectContext, state: SyncState, function: str, options: PushOptions,
                    result: PushResult, own: set[str]) -> list[str]:
    """Refresh the consumers of ``function`` this batch does not apply itself.

    A consumer in ``own`` is still to come in the batch and rebuilds its call nodes
    inside its own apply. Every other one is refreshed here; a failure is that
    consumer's own row, and the consumers that failed are returned so whatever
    depends on them is blocked.
    """
    data = call_ok(bridge, "asset_referencers_get", dict(asset_path=function, limit=10000)).get("data") or dict()
    entries = data.get("items") or data.get("rows") or []
    if data.get("truncated") or int(data.get("total", len(entries))) > len(entries):
        raise SyncError("referencers_incomplete", "function refresh needs a complete referencer set", dict(function=function))
    packages = [str(entry[0]) for entry in entries if isinstance(entry, list) and entry and (len(entry) < 2 or entry[1] == "hard")]
    failed = []
    for caller in dict.fromkeys(object_path(package) for package in packages):
        if caller in own or caller == function:
            continue
        if not refresh_caller(bridge, context, state, caller, set((function,)), options, result):
            failed.append(caller)
    return failed


def refresh_caller(bridge: BridgeCall, context: ProjectContext, state: SyncState, caller: str, functions: set[str],
                   options: PushOptions, result: PushResult) -> bool:
    """Rebuild ``caller``'s call nodes of ``functions`` as an apply of their own."""
    info = call_ok(bridge, "transcode_status", dict(asset_paths=[caller])).get("data") or dict()
    entries = [entry for entry in info.get("assets") or [] if isinstance(entry, list) and len(entry) >= 3]
    if not entries or entries[0][2] not in CONSUMER_KINDS:
        return True
    kind = str(entries[0][2])
    file = text_path(context.project, caller, kind)
    existing = read_text(file)
    record = state.get(caller)
    protected = existing is None or record is None or canonical_hash(existing) != record.base_hash
    payload = dict(
        asset_path=caller, kind=kind, plan=refresh_ops(functions),
        ids=dict(), create=False, dry_run=False, compile=options.compile, save=options.save,
        out_dir=str(pending_dir(context.project)),
    )
    response = bridge("transcode_apply", payload)
    document, _ = parse(existing or "")
    status = AssetStatus(caller, kind, "local-modified" if protected else "clean", file, None, record, None)
    item = Prepared(status, document, build_plan(document, document, kind, schema=context.schema), file, existing or "", load_base(context.project, caller))
    label = display_path(context.project, file)
    errors = apply_diagnostics(response, item, label, result)
    data = response.get("data") if isinstance(response, dict) else None
    data = data if isinstance(data, dict) else dict()
    raw_file = str(data.get("file") or "")
    raw = read_json(Path(raw_file)) if raw_file else None
    if errors or raw is None:
        if not errors:
            diagnostic = Diagnostic("error", "refresh_export_failed", f"no readable refresh export for {caller}", str(file))
            result.diagnostics.append(diagnostic)
            errors = [diagnostic.format()]
        recovery = record_failure(context, item, response, errors)
        result.rows.append(row(caller, kind, status.state, "failed", functions=sorted(functions), errors=errors[:5], recovery_file=recovery))
        return False
    if protected:
        result.diagnostics.append(Diagnostic("warning", "caller_local_preserved", f"refreshed {caller} in UE; local text and base preserved", label))
    else:
        commit_snapshot(context, state, item, raw, data)
        Path(raw_file).unlink(missing_ok=True)
    result.refreshed.append(caller)
    return True
