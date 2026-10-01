"""Apply a planned batch unit by unit, in dependency order, each failure kept with its asset."""

from __future__ import annotations

from ...material.calls import changed_calls
from ...material.interfaces import publish_observed
from ...errors import SyncError
from . import refresh, transactions
from ..jobs.state import checkpoint


def run_units(bridge, context, workspace, source: str, candidate: str, fixed: dict, observation: dict,
              batch: dict, options: dict) -> list[dict]:
    rows, interfaces = [], set()
    order = batch["order"]
    for position, asset in enumerate(order):
        if asset in batch["errors"]:
            continue
        item = batch["units"][asset]
        if set(item["dependencies"]) & batch["errors"].keys():
            batch["errors"][asset] = dict(code="dependency_failed", message="a required asset failed")
            continue
        # Units still to come: those calling a function this one changes rebuild the calls themselves.
        ahead = set(later for later in order[position + 1:] if later not in batch["errors"])
        checkpoint(stage="apply", asset=asset)
        try:
            rows.extend(run_unit(bridge, context, workspace, asset, source, candidate, fixed, observation, batch, options, interfaces, ahead))
            checkpoint(stage="asset_completed", asset=asset, completed=True)
        except (SyncError, OSError) as exc:
            detail = dict(code=exc.code if isinstance(exc, SyncError) else "publication_io_failed", message=str(exc), details=getattr(exc, "details", dict()))
            detail.update((key, detail["details"][key]) for key in ("function", "functions") if key in detail["details"])
            detail["asset"] = asset
            batch["errors"][asset] = detail
            rows.append(dict(action="failed", **detail))
            if options.get("stop_on_error", True):
                break
    return rows


def run_unit(bridge, context, workspace, asset: str, source: str, candidate: str, fixed: dict, observation: dict,
             batch: dict, options: dict, interfaces: set[str], ahead: set[str]) -> list[dict]:
    item = batch["units"][asset]
    if not item["empty"]:
        refresh.settle(bridge, context, workspace, asset, source, candidate, fixed, observation, batch, options)
        item = batch["units"][asset]
    calls = changed_calls(item["kind"], item["dependencies"], interfaces)
    refreshed = dict(refreshed=sorted(calls)) if calls else dict()
    if item["empty"]:
        if calls:
            refresh.consumer(bridge, context, workspace, asset, calls, source, observation, options)
        consume_unchanged(workspace, source, candidate, observation["commit"], asset)
        return [dict(asset=asset, action="unchanged", **refreshed)]
    if calls:
        item = refresh.with_calls(item, calls)
    record = transactions.request(workspace, item, source, batch.get("candidate", candidate), observation["commit"], observation, options)
    transactions.execute(bridge, workspace, record)
    published = publish_verified(bridge, context, workspace, record)
    transactions.adopt(observation, record, published, workspace.history)
    rows = [dict(asset=asset, action="pushed", apply_id=record["id"], commit_id=published, **refreshed)]
    if item.get("interface_changed"):
        snapshot_id = workspace.history.entries(published).get(asset)
        if snapshot_id:
            publish_observed(workspace.schema, workspace.store.objects.data(snapshot_id, "snapshot"))
        interfaces.add(asset)
        for failure in refresh.callers(bridge, context, workspace, asset, source, observation, batch, options, ahead):
            batch["errors"][failure["asset"]] = failure
            rows.append(dict(failure, action="failed"))
    return rows


def publish_verified(bridge, context, workspace, record) -> str:
    try:
        return transactions.publish(workspace, record)
    except SyncError as exc:
        if record["phase"] == "result_rejected":
            from .recover import reconcile

            try:
                reconcile(bridge, context, workspace, record, restore=True)
            except SyncError as recovery:
                exc.details["recovery_error"] = dict(code=recovery.code, message=str(recovery), details=recovery.details)
            exc.details["phase"] = record["phase"]
        raise


def consume_unchanged(workspace, source: str, candidate: str, target: str, asset: str) -> None:
    store = workspace.store
    key = workspace.state["id"] + ":" + asset
    previous = store.record("integration", key)
    record = dict(workspace_id=workspace.state["id"], asset=asset, source_commit=source,
                  source_snapshot=workspace.history.entries(source).get(asset), candidate_snapshot=workspace.history.entries(candidate).get(asset), published_commit=target)
    store.put_record("integration", key, record, [source, candidate, target], previous["generation"] if previous else 0)
