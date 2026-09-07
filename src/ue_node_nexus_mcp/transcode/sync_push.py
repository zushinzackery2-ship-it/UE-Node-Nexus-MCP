"""Batch push orchestration: preflight, dependency ordering and application."""

from __future__ import annotations

import logging

from .errors import Diagnostic
from .paths import display_path
from .push.apply import accept_unchanged, apply_item
from .push.model import Prepared, PushOptions, PushResult, row
from .push.prepare import prepare_item, replan_refreshed
from .push.refresh import refresh_callers
from .state import SyncState
from .sync_deps import document_dependencies, order_assets
from .sync_files import mirrored_assets
from .sync_project import BridgeCall, ProjectContext, SyncError, ensure_root_registered
from .sync_status import AssetStatus, query_ue

LOGGER = logging.getLogger(__name__)
FAILED_ACTIONS = ("failed", "conflict", "pull-first", "orphan", "blocked", "pushed-with-errors")


def _failure(context: ProjectContext, status: AssetStatus, result: PushResult, exc: SyncError, action: str = "failed") -> None:
    label = display_path(context.project, status.file) if status.file else status.asset_path
    diagnostic = Diagnostic("error", exc.code, str(exc), label)
    result.diagnostics.append(diagnostic)
    existing = next((entry for entry in result.rows if entry["asset"] == status.asset_path), None)
    if existing is not None:
        existing["action"] = "pushed-with-errors" if existing["action"] == "pushed" else action
        existing.setdefault("errors", []).append(diagnostic.format())
        return
    result.rows.append(row(status.asset_path, status.kind, status.state, action, errors=[diagnostic.format()]))


def _missing_dependencies(bridge: BridgeCall, context: ProjectContext, selected: set[str], dependencies: dict[str, set[str]]) -> set[str]:
    external = set().union(*dependencies.values()) - selected
    local = set(mirrored_assets(context.project))
    external &= local
    if not external or not context.bridge_available:
        return set()
    infos, known = query_ue(bridge, context, sorted(external))
    if not known:
        raise SyncError("dependency_status_unavailable", "could not verify referenced assets in UE")
    return external - set(infos)


def push_assets(bridge: BridgeCall, context: ProjectContext, state: SyncState, statuses: list[AssetStatus], options: PushOptions) -> PushResult:
    result = PushResult()
    prepared: dict[str, Prepared] = dict()
    for status in statuses:
        try:
            item = prepare_item(bridge, context, status, options, result)
            if item is not None:
                prepared[status.asset_path] = item
        except SyncError as exc:
            _failure(context, status, result, exc)
    if not prepared:
        return result
    documents = dict((asset, (item.status.kind, item.document)) for asset, item in prepared.items())
    dependencies = dict((asset, document_dependencies(item.document, item.status.kind)) for asset, item in prepared.items())
    required = set(asset for asset, item in prepared.items() if item.plan.creates_asset or item.status.kind in ("material_function", "material_instance"))
    order = order_assets(documents, required)
    blocked = set(entry["asset"] for entry in result.rows if entry["action"] in FAILED_ACTIONS)
    missing = _missing_dependencies(bridge, context, set(status.asset_path for status in statuses), dependencies)
    for asset in order:
        unavailable = sorted(dependencies[asset] & (blocked | missing))
        if unavailable:
            code = "dependency_not_selected" if set(unavailable) & missing else "dependency_failed"
            _failure(context, prepared[asset].status, result, SyncError(code, "blocked by: " + ", ".join(unavailable)), "blocked")
            blocked.add(asset)
    if options.dry_run:
        for asset in order:
            if asset in blocked:
                continue
            item = prepared[asset]
            action = "unchanged" if item.plan.empty and not item.reconcile else "planned"
            if action == "planned":
                result.plans.append(item.plan.summary())
            result.rows.append(row(asset, item.status.kind, item.status.state, action, count=len(item.plan.verbs)))
        return result
    result.stopped = options.stop_on_error and bool(blocked)
    if not result.stopped:
        ensure_root_registered(bridge, context)
    for asset in order:
        item = prepared[asset]
        if asset in blocked:
            continue
        if result.stopped:
            result.rows.append(row(asset, item.status.kind, item.status.state, "skipped"))
            continue
        unavailable = sorted(dependencies[asset] & blocked)
        if unavailable:
            _failure(context, item.status, result, SyncError("dependency_failed", "blocked by: " + ", ".join(unavailable)), "blocked")
            blocked.add(asset)
            continue
        try:
            if asset in result.refreshed:
                replan_refreshed(bridge, context, item)
            if item.plan.empty and not item.reconcile:
                accept_unchanged(context, state, item, result)
                continue
            succeeded = apply_item(bridge, context, state, item, options, result)
            if not succeeded:
                blocked.add(asset)
                continue
            if item.plan.interface_changed and item.status.kind == "material_function" and not item.plan.creates_asset:
                refresh_callers(bridge, context, state, asset, options, result)
        except (OSError, SyncError) as exc:
            error = exc if isinstance(exc, SyncError) else SyncError("sync_io_failed", str(exc))
            _failure(context, item.status, result, error)
            blocked.add(asset)
            result.stopped = options.stop_on_error or error.code == "mcp_bridge_error"
    LOGGER.info("sync batch finished assets=%d blocked=%d stopped=%s", len(prepared), len(blocked), result.stopped)
    return result
