"""Apply a planned batch unit by unit, in dependency order, each failure kept with its asset."""

from __future__ import annotations

from ...material.calls import changed_calls
from ...material.interfaces import publish_observed
from ...errors import SyncError
from . import refresh, transactions
from .deletions import prerequisites
from ..jobs.state import checkpoint


def run_units(bridge, context, workspace, source: str, candidate: str, fixed: dict, observation: dict,
              batch: dict, options: dict) -> list[dict]:
    rows, interfaces, completed = [], set(), set()
    order = batch["order"]
    receipts = collect_batch(bridge, context, workspace, candidate, observation, batch, options)
    rows.extend(dict(asset=asset, action="failed", **batch["errors"][asset])
                for asset in order if asset in batch["errors"] and batch["errors"][asset].get("batch_admission"))
    for position, asset in enumerate(order):
        if asset in batch["errors"]:
            continue
        item = batch["units"][asset]
        blocked = prerequisites(item) & batch["errors"].keys()
        if blocked:
            batch["errors"][asset] = dict(code="dependency_failed", message="a required asset failed", assets=sorted(blocked))
            continue
        # Units still to come: those calling a function this one changes rebuild the calls themselves.
        ahead = set(later for later in order[position + 1:] if later not in batch["errors"])
        checkpoint(stage="apply", asset=asset)
        try:
            rows.extend(run_unit(bridge, context, workspace, asset, source, candidate, fixed, observation, batch, options, interfaces, ahead, receipts, completed))
            completed.add(asset)
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


def collect_batch(bridge, context, workspace, candidate: str, observation: dict, batch: dict, options: dict) -> dict:
    """Admit the units this batch validates in an isolated copy, if the caller asked for it.

    ``options["isolation"]`` sets the scope: ``off`` copies nothing, ``auto``
    admits every risky unit with the changed dependencies it compiles with, and
    ``always`` admits every planned unit. Payloads are predicted from the
    interfaces this batch itself changes; a unit whose applied payload or
    candidate ends up different keeps its receipt out of reach and is admitted
    alone at its turn. A risk classification only names the units that must be
    validated, not the units whose live memory the worker needs: a changed
    dependency outside it would still be its old shader there.
    """
    from ....safety.publication.gate import admit_batch
    from ....safety.publication.mode import isolation_mode, requires_reason
    from ....safety.publication.risk import reasons

    mode = isolation_mode(options)
    if mode == "off":
        return dict()
    planned, roots, interfaces = dict(), [], set()
    for asset in batch["order"]:
        if asset in batch["errors"]:
            continue
        item = batch["units"][asset]
        if item["empty"] or prerequisites(item) & batch["errors"].keys():
            continue
        calls = changed_calls(item["kind"], item["dependencies"], interfaces)
        planned[asset] = refresh.with_calls(item, calls) if calls else item
        if reasons(planned[asset]):
            roots.append(asset)
        if item.get("interface_changed"):
            interfaces.add(asset)
    admitted = set()
    if requires_reason(mode):
        for root in roots:
            closure = changed_closure(planned, batch["errors"], root)
            if closure is not None:
                admitted.update(closure)
    else:
        # ``always`` validates every planned unit, not only the classified ones.
        admitted.update(planned)
    entries = [(asset, planned[asset]) for asset in batch["order"] if asset in admitted]
    if not entries:
        return dict()
    try:
        receipts = admit_batch(bridge, context, workspace, entries, batch.get("candidate", candidate), observation, options)
    except SyncError as error:
        for asset, _ in entries:
            batch["errors"][asset] = dict(code=error.code, message=str(error), details=error.details,
                                          batch_admission=True)
        return dict()
    return receipts


def changed_closure(planned: dict, failures: dict, root: str):
    """Return every changed unit ``root`` compiles with, or None when one failed.

    Only units this batch already plans are included: unchanged assets stay in
    the live-memory baseline exactly as before, and a failed unit cannot be
    reproduced, so its consumer is left to its own admission at its turn.
    """
    seen, pending = set(), [root]
    while pending:
        asset = pending.pop()
        if asset in seen:
            continue
        if asset in failures:
            return None
        item = planned.get(asset)
        if item is None:
            continue
        seen.add(asset)
        pending.extend(item["dependencies"])
    return seen


def run_unit(bridge, context, workspace, asset: str, source: str, candidate: str, fixed: dict, observation: dict,
             batch: dict, options: dict, interfaces: set[str], ahead: set[str], receipts: dict, completed: set[str]) -> list[dict]:
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
    from ....safety.publication.gate import admit, receipt_valid
    from ....safety.publication.risk import reasons
    safety = receipts.get(asset)
    if safety is not None and not receipt_valid(safety, item, batch.get("candidate", candidate),
                                              bridge=bridge, observation=observation, completed=completed):
        safety = None
    risks = []
    if safety is None:
        safety = admit(bridge, context, workspace, item, batch.get("candidate", candidate), observation, options)
        if safety is None:
            # Publication proceeds without the isolated proof; keep the reason visible.
            risks = reasons(item)
    record = transactions.request(workspace, item, source, batch.get("candidate", candidate), observation["commit"], observation, options)
    if safety is not None:
        record["safety_validation"] = safety
        transactions.save(workspace, record)
    transactions.execute(bridge, workspace, record)
    published = publish_verified(bridge, context, workspace, record)
    transactions.adopt(observation, record, published, workspace.history)
    rows = [dict(asset=asset, action="pushed", apply_id=record["id"], commit_id=published,
                 **(dict(safety_validation=safety) if safety is not None else dict()),
                 **(dict(risk_unvalidated=risks) if risks else dict()), **refreshed)]
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
