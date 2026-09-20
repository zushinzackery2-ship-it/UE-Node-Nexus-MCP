"""Recover saved execution receipts without replaying creation or rename verbs."""

from __future__ import annotations

from ...sync_project import SyncError, call_ok
from ..store.repository import PUBLICATION_WAIT_SECONDS
from ..workspace.service import Workspace
from . import transactions
from .observe import capture


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
        current = capture(bridge, context, workspace.store, [record["asset"]], reference=record["candidate"], selectors=selectors, persist=False, force_export=True)
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
    return record


def pending_records(workspace) -> list[dict]:
    return [record for record in workspace.store.records("apply")
            if record["phase"] not in ("completed", "rolled_back", "rejected")]


def pending(bridge, context, workspace) -> None:
    for record in pending_records(workspace):
        owner = Workspace(workspace.store, record["workspace_id"], context.schema)
        if record["phase"] == "prepared":
            record["phase"] = "rejected"
            record["reason"] = "interrupted before submitting to UE"
            transactions.save(owner, record)
            continue
        try:
            result = reconcile(bridge, context, owner, record)
        except SyncError as exc:
            if exc.code != "apply_not_found":
                raise
            record["phase"] = "rejected"
            record["receipt"] = None
            record["reason"] = "durable receipt is missing; transaction cannot be resumed"
            record["recovery_error"] = str(exc)
            transactions.save(owner, record)
            continue
        if result["phase"] != "completed":
            raise SyncError("recovery_required", "finish the pending execution before publishing", dict(apply_id=result["id"], phase=result["phase"]))


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
    record = workspace.store.record("apply", identifier) if identifier else None
    if not record or record["workspace_id"] != workspace.state["id"]:
        raise SyncError("apply_not_found", str(identifier))
    if options.get("dry_run", True) or record["phase"] == "completed":
        return record
    with workspace.store.lock("publication", PUBLICATION_WAIT_SECONDS, workspace_id=workspace.state["id"]):
        record = workspace.store.record("apply", identifier)
        if record["phase"] == "completed":
            return record
        return reconcile(bridge, context, workspace, record, options.get("restore", False), options.get("preserve_current", False))
