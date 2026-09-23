"""Recover saved execution receipts without replaying creation or rename verbs."""

from __future__ import annotations

from ...sync_project import SyncError, call_ok
from ..store.repository import PUBLICATION_WAIT_SECONDS
from ..workspace.service import Workspace
from . import transactions
from .observe import capture, forget

TERMINAL = ("completed", "rolled_back", "rejected")
MISSING_RECEIPT = "durable receipt is missing; transaction cannot be resumed"


def reconcile(bridge, context, workspace, record: dict, restore=False, preserve_current=False) -> dict:
    response = call_ok(bridge, "transcode_recover", dict(apply_id=record["id"], repository=str(workspace.store.root),
                                                         restore=restore, preserve_current=preserve_current))
    receipt = transactions.verify(workspace, record, (response.get("data") or dict()).get("receipt"))
    if not receipt:
        raise SyncError("receipt_invalid", "recovery did not return a receipt for this apply", dict(apply_id=record["id"]))
    record["receipt"] = receipt
    phase = receipt["phase"]
    if phase == "ue_committed":
        # A later manual edit is an independent state, not permission to publish
        # the old receipt over it. Both observations remain inspectable.
        selectors = dict()
        selector = (receipt.get("response_data") or dict()).get("result_selector")
        if selector:
            selectors[record["asset"]] = selector
        current = capture(bridge, context, workspace.store, [record["asset"]], reference=record["candidate"], selectors=selectors, persist=False, fresh=[record["asset"]])
        raw = current["raw"].get(record["asset"], dict(exists=False))
        if receipt["after"].get("exists") is False:
            compatible = raw.get("exists") is False
        else:
            compatible = raw.get("content_revision") == receipt["after"].get("content_revision")
        if not compatible:
            transactions.save(workspace, dict(record, phase="recovery_conflict", recovery_observation=current["id"]))
            raise SyncError("recovery_conflict", "UE changed after the saved receipt", dict(apply_id=record["id"], observation=current["id"]))
        record["phase"] = "ue_committed"
        transactions.save(workspace, record)
        transactions.publish(workspace, record, recovery_target=current["previous"])
    else:
        record["phase"] = phase
        transactions.save(workspace, record)
        # Memory the editor rolled back (or failed to roll back) is no longer the
        # memory this repository remembers; the next observation must re-export.
        forget(workspace.store, [record["asset"]])
    return record


def abandon(workspace, record: dict, reason: str, error: str | None = None) -> dict:
    """End a transaction that can never be resumed, in the shape recovery writes."""
    record.update(phase="rejected", receipt=None, reason=reason)
    if error:
        record["recovery_error"] = error
    transactions.save(workspace, record)
    forget(workspace.store, [record["asset"]])
    return record


def pending_records(workspace) -> list[dict]:
    return [record for record in workspace.store.records("apply") if record["phase"] not in TERMINAL]


def owner_of(store, context, record: dict, caller) -> Workspace:
    """The workspace that submitted this apply; a closed one cannot consume it.

    An apply record belongs to the repository, not to whoever still exists. A
    receipt whose author is gone is still published, but no integration record
    is written back into a workspace that can never read it.
    """
    if caller is not None and caller.state["id"] == record["workspace_id"]:
        return caller
    try:
        return Workspace(store, record["workspace_id"], context.schema)
    except SyncError as exc:
        if exc.code not in ("workspace_not_found", "workspace_closed"):
            raise
        record["consume"] = False
        return caller


# Recovery is three distinct acts, not two booleans. ``inspect`` only reads the
# durable receipt (and publishes one that UE already committed); ``restore``
# replays the checkpoint; ``abandon`` keeps current memory and ends the record.
# ``preserve_current`` finalizes *any* unfinished phase as rejected, so it must
# never run before the receipt has been read: it would discard a committed apply.
RESOLUTIONS = dict(inspect=(False, False), restore=(True, False), abandon=(False, True))


def attempt(bridge, context, owner, record: dict, resolution: str) -> dict:
    restore, preserve = RESOLUTIONS[resolution]
    try:
        return reconcile(bridge, context, owner, record, restore, preserve)
    except SyncError as exc:
        if exc.code not in ("apply_not_found", "receipt_invalid"):
            raise
        return abandon(owner, record, MISSING_RECEIPT, str(exc))


def finish(bridge, context, workspace, record: dict, escalate_to: str = "restore") -> dict:
    """Resolve one pending record, escalating only as far as it is asked to."""
    owner = owner_of(workspace.store, context, record, workspace)
    if record["phase"] == "prepared":
        return abandon(owner, record, "interrupted before submitting to UE")
    result = attempt(bridge, context, owner, record, "inspect")
    if result["phase"] in TERMINAL or escalate_to == "inspect":
        return result
    try:
        result = attempt(bridge, context, owner, result, "restore")
    except SyncError as exc:
        if exc.code not in ("recovery_conflict", "recovery_required") or escalate_to != "abandon":
            raise
        return attempt(bridge, context, owner, workspace.store.record("apply", record["id"]), "abandon")
    if result["phase"] not in TERMINAL and escalate_to == "abandon":
        return attempt(bridge, context, owner, result, "abandon")
    return result


def pending(bridge, context, workspace) -> None:
    """Publication clears what it can; giving up on a transaction stays explicit."""
    for record in pending_records(workspace):
        result = finish(bridge, context, workspace, record, escalate_to="restore")
        if result["phase"] not in TERMINAL:
            raise SyncError("recovery_required", "finish the pending execution before publishing",
                            dict(apply_id=result["id"], phase=result["phase"], **outstanding([result])))


def recovery_call(record: dict) -> dict:
    return dict(action="recover", options=dict(apply_id=record["id"], dry_run=False))


def outstanding(records: list[dict]) -> dict:
    """What blocks publication, and the exact calls that clear it."""
    return dict(pending=[dict(apply_id=row["id"], phase=row["phase"], asset=row["asset"]) for row in records],
                recover=dict(action="recover", options=dict(dry_run=False)),
                abandon=[dict(action="abort", options=dict(apply_id=row["id"], dry_run=False)) for row in records])


def require_record(workspace, identifier: str) -> dict:
    record = workspace.store.record("apply", identifier)
    if not record:
        raise SyncError("apply_not_found", str(identifier), dict(apply_id=identifier, pending=[row["id"] for row in pending_records(workspace)]))
    return record


def discard(workspace, options: dict) -> dict:
    """``abort {apply_id}``: give up on a transaction without asking UE."""
    record = require_record(workspace, options["apply_id"])
    if record["phase"] in TERMINAL:
        return dict(record, action="abort")
    if options.get("dry_run", True):
        return dict(action="abort", dry_run=True, apply_id=record["id"], phase=record["phase"], status="candidate")
    with workspace.store.lock("publication", PUBLICATION_WAIT_SECONDS, workspace_id=workspace.state["id"]):
        record = require_record(workspace, options["apply_id"])
        owner = owner_of(workspace.store, None, record, workspace)
        return dict(abandon(owner, record, "abandoned by request"), action="abort")


def recover(bridge, context, workspace, options: dict) -> dict:
    if options.get("projection_id"):
        from ..workspace.projection import execute

        record = workspace.store.record("projection", options["projection_id"])
        if not record or record["workspace_id"] != workspace.state["id"]:
            raise SyncError("projection_not_found", options["projection_id"])
        if options.get("dry_run", True):
            return record
        with workspace.store.lock("workspace-" + workspace.state["id"]):
            return execute(workspace, record)
    identifier = options.get("apply_id")
    if not identifier:
        return recover_all(bridge, context, workspace, options)
    record = require_record(workspace, identifier)
    if options.get("dry_run", True) or record["phase"] == "completed":
        return record
    with workspace.store.lock("publication", PUBLICATION_WAIT_SECONDS, workspace_id=workspace.state["id"]):
        record = workspace.store.record("apply", identifier)
        if record["phase"] == "completed":
            return record
        owner = owner_of(workspace.store, context, record, workspace)
        if record["phase"] == "prepared":
            return abandon(owner, record, "interrupted before submitting to UE")
        return attempt(bridge, context, owner, record, requested(options))


def requested(options: dict) -> str:
    """The named resolution, or the one the legacy boolean pair asked for."""
    resolution = options.get("resolution")
    if resolution is not None:
        if resolution not in RESOLUTIONS:
            raise SyncError("invalid_option", "resolution must be inspect, restore or abandon",
                            dict(resolution=resolution, allowed=sorted(RESOLUTIONS)))
        return resolution
    if options.get("preserve_current"):
        return "abandon"
    return "restore" if options.get("restore") else "inspect"


def recover_all(bridge, context, workspace, options: dict) -> dict:
    """``recover`` with no apply_id means every transaction this repository owns."""
    records = pending_records(workspace)
    if options.get("dry_run", True):
        return dict(action="recover", dry_run=True, status="candidate", **outstanding(records))
    escalate = options.get("resolution") or ("abandon" if options.get("preserve_current") else "restore")
    rows = []
    with workspace.store.lock("publication", PUBLICATION_WAIT_SECONDS, workspace_id=workspace.state["id"]):
        for record in records:
            current = workspace.store.record("apply", record["id"])
            if not current or current["phase"] in TERMINAL:
                continue
            try:
                result = finish(bridge, context, workspace, current, escalate)
                rows.append(dict(apply_id=result["id"], asset=result["asset"], phase=result["phase"]))
            except SyncError as exc:
                rows.append(dict(apply_id=current["id"], asset=current["asset"], phase=current["phase"],
                                 code=exc.code, message=str(exc)))
    unresolved = [row for row in rows if row["phase"] not in TERMINAL]
    return dict(action="recover", rows=rows, recovered=len(rows) - len(unresolved), error_count=len(unresolved),
                status="recovery_required" if unresolved else "recovered",
                pending=[row["apply_id"] for row in unresolved])
