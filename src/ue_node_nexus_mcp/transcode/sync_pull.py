"""``ue_sync pull`` (U2L): UE writes raw exports into the base dir, Python renders text."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from .paths import base_path, pending_dir
from .state import AssetState, SyncState, sha256_text
from .sync_files import backup_text, load_base, materialize, read_json, read_text, remove_stale_text, render_snapshot, write_text_atomic
from .sync_project import BridgeCall, ProjectContext, call_ok, ensure_root_registered, export_operation, now_iso
from .sync_status import AssetStatus

CONFLICT_SUFFIX = ".ue.nexus"


def pull_assets(
    bridge: BridgeCall,
    context: ProjectContext,
    state: SyncState,
    statuses: list[AssetStatus],
    force: str | None = None,
    include_stubs: bool = False,
) -> list[dict[str, Any]]:
    """Pull every asset whose status allows it; returns one row per asset."""
    rows: list[dict[str, Any]] = []
    ensure_root_registered(bridge, context)
    to_pull: list[AssetStatus] = []
    for status in statuses:
        if status.state in ("ue-modified", "ue-new", "both-modified"):
            # both-modified without force="ue" lands next to the text as a conflict copy
            to_pull.append(status)
        elif status.state in ("clean", "local-modified"):
            if status.file is None or not base_path(context.project, status.asset_path).is_file():
                to_pull.append(status)
            else:
                rows.append(_row(status.asset_path, status.kind, status.state, "skipped"))
        elif status.state == "ue-deleted":
            rows.append(_row(status.asset_path, status.kind, status.state, "orphan", warnings=["asset no longer exists in UE; delete the text file or recreate the asset"]))
        elif status.state == "local-deleted":
            rows.append(_row(status.asset_path, status.kind, status.state, "orphan", warnings=["text file was deleted; pull again with the asset path to restore it"]))
        else:
            rows.append(_row(status.asset_path, status.kind, status.state, "skipped"))
    if not to_pull:
        return rows
    exported = export_raw(bridge, context, [status.asset_path for status in to_pull], {status.asset_path: status.kind for status in to_pull}, include_stubs)
    stamp = now_iso().replace(":", "").replace("-", "")
    for status in to_pull:
        raw_file = exported.get(status.asset_path)
        if raw_file is None:
            rows.append(_row(status.asset_path, status.kind, status.state, "failed", errors=["UE did not export this asset"]))
            continue
        conflict = status.state == "both-modified" and force != "ue"
        rows.append(_finish_pull(context, state, status, raw_file, conflict, stamp))
    state.save(context.project)
    return rows


def export_raw(bridge: BridgeCall, context: ProjectContext, asset_paths: list[str], kinds: dict[str, str], include_stubs: bool) -> dict[str, Path]:
    """Run the (core / vfx) export ops and return object path -> written raw file.

    UE writes into ``.nexus/pending`` so the previous base (with its id/order
    bookkeeping) stays intact until Python has merged it.
    """
    out_dir = pending_dir(context.project)
    groups: dict[str, list[str]] = {}
    for asset_path in asset_paths:
        groups.setdefault(export_operation(kinds.get(asset_path, "")), []).append(asset_path)
    written: dict[str, Path] = {}
    for operation, paths in groups.items():
        data = call_ok(bridge, operation, {"asset_paths": paths, "out_dir": str(out_dir), "include_stubs": include_stubs}).get("data") or {}
        for row in data.get("assets") or []:
            if isinstance(row, dict):
                asset_path, file = str(row.get("asset_path", "")), str(row.get("file", ""))
            elif isinstance(row, list) and len(row) >= 3:
                asset_path, file = str(row[0]), str(row[2])
            else:
                continue
            if asset_path and file:
                written[_object_path(asset_path)] = Path(file)
    return written


def _object_path(asset_path: str) -> str:
    from .paths import object_path

    return object_path(asset_path)


def _finish_pull(context: ProjectContext, state: SyncState, status: AssetStatus, raw_file: Path, conflict: bool, stamp: str) -> dict[str, Any]:
    raw = read_json(raw_file)
    if raw is None:
        return _row(status.asset_path, status.kind, status.state, "failed", errors=[f"raw export unreadable: {raw_file}"])
    previous = load_base(context.project, status.asset_path)
    if conflict:
        snapshot = render_snapshot(context.project, raw, previous, schema=context.schema)
        conflict_file = snapshot.text_file.with_name(snapshot.text_file.name.replace(".nexus", CONFLICT_SUFFIX))
        write_text_atomic(conflict_file, snapshot.text)
        return _row(status.asset_path, status.kind, status.state, "conflict", warnings=[f"UE version written to {conflict_file.name}; resolve then delete it"])
    _, text, text_file, _ = materialize(context.project, raw, previous, schema=context.schema)
    if raw_file.resolve() != base_path(context.project, status.asset_path).resolve():
        raw_file.unlink(missing_ok=True)
    kind = str(raw.get("kind", ""))
    existing = read_text(text_file)
    if existing is not None and existing != text:
        backup_text(context.project, text_file, stamp)
    write_text_atomic(text_file, text)
    remove_stale_text(context.project, status.asset_path, kind)
    record = AssetState(
        asset_path=status.asset_path,
        kind=kind,
        file=text_file.relative_to(context.project).as_posix(),
        base_hash=sha256_text(text),
        ue_saved_hash=str(raw.get("saved_hash", "")),
        ue_dirty=bool(raw.get("dirty", False)),
        synced_at=now_iso(),
    )
    state.put(record)
    return _row(status.asset_path, kind, status.state, "pulled", file=record.file)


def _row(asset_path: str, kind: str, state: str, action: str, *, file: str | None = None, errors: list[str] | None = None, warnings: list[str] | None = None) -> dict[str, Any]:
    row: dict[str, Any] = {"asset": asset_path, "kind": kind, "state": state, "action": action}
    if file:
        row["file"] = file
    if errors:
        row["errors"] = errors
    if warnings:
        row["warnings"] = warnings
    return row
