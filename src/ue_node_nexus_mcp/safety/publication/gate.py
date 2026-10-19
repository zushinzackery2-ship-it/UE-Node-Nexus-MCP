"""Validate high-risk units before their first mutation of the publishing editor."""

from __future__ import annotations

from copy import deepcopy
import logging
import time

from ...instances.errors import InstanceError
from ...transcode.errors import SyncError
from ...transcode.storage.io import digest
from ...transcode.sync.project import apply_operation, call_ok
from ...transcode.collaboration.apply.transactions import actual_snapshot
from ..isolation.service import validate
from ..isolation.baselines import BaselineLease
from .evidence import binding, verify_batch
from .mode import isolation_mode
from .risk import reasons


LOG = logging.getLogger("ue_node_nexus_mcp.safety.publication")


def admit(bridge, context, workspace, item: dict, candidate: str, observation: dict, options: dict) -> dict | None:
    if isolation_mode(options) == "off":
        return None
    risks = reasons(item)
    if not risks:
        return None
    return admit_batch(bridge, context, workspace, [(item["asset"], item)], candidate, observation, options)[item["asset"]]


def admit_batch(bridge, context, workspace, entries: list[tuple[str, dict]], candidate: str, observation: dict, options: dict) -> dict:
    """Bind one complete worker run to every ordered candidate before publication."""
    if not entries:
        return dict()
    if options.get("compile", True) is not True:
        raise SyncError("safety_compile_required", "high-risk publication requires compilation",
                        dict(assets=[asset for asset, _ in entries]))
    capabilities = call_ok(bridge, "bridge_capabilities_get", dict())["data"]
    if capabilities.get("safety_contract") != 1:
        raise SyncError("safety_contract_required", "publishing editor requires complete isolation contract 1",
                        dict(assets=[asset for asset, _ in entries]))
    rhi = capabilities.get("rhi", "").lower()
    if rhi not in ("d3d12", "d3d11"):
        raise SyncError("safety_rhi_unsupported", "publishing RHI cannot be reproduced by the worker", dict(rhi=rhi))
    expected, operations, guarded = dict(), [], set()
    for asset, item in entries:
        expected[asset] = binding(item, candidate, observation, capabilities["build"], rhi, capabilities.get("safety_environment"))
        operation = dict(operation=apply_operation(item["kind"]), payload=deepcopy(item["payload"]))
        operation["payload"].update(dry_run=False, compile=True, save=True)
        operations.append((asset, item, operation))
        guarded.update((item["asset"], *item["dependencies"]))
    environment = expected[entries[0][0]]["environment"]
    game_assets = sorted(asset for asset in guarded if asset.startswith("/Game/"))
    started = time.monotonic()
    LOG.info("assets=%d candidate=%s phase=isolated_batch_validation", len(entries), candidate)
    try:
        with BaselineLease(context.project_file) as lease:
            baseline = lease.export(bridge, game_assets)
            if baseline.get("environment") != environment:
                raise SyncError("safety_identity_changed", "baseline environment differs from the publishing editor")
            report = validate(context.project_file, [operation for _, _, operation in operations], None, rhi, 600, False,
                              baseline=baseline, allow_low_memory=options.get("safety_allow_low_memory", False))
            lease.bind(report)
    except InstanceError as error:
        raise SyncError(error.code, str(error), error.details) from error
    publications = verify_batch(report, expected, operations)
    latest = call_ok(bridge, "bridge_capabilities_get", dict())["data"]
    for asset, item in entries:
        if binding(item, candidate, observation, latest["build"], latest.get("rhi", "").lower(), latest.get("safety_environment")) != expected[asset]:
            raise SyncError("safety_identity_changed", "publishing environment changed during isolated validation", dict(asset=asset))
    elapsed = time.monotonic() - started
    receipts = dict()
    for index, (asset, item, operation) in enumerate(operations):
        publication = publications[index]
        proof = dict(id=publication["apply_id"], candidate=candidate, asset=asset, kind=item["kind"],
                     request=publication["payload"], receipt=publication["receipt"])
        actual_snapshot(workspace, proof)
        receipts[asset] = dict(binding=expected[asset], binding_digest=digest(expected[asset]), risks=reasons(item),
                              validation_id=report["data"]["validation_id"], receipt_path=report["data"]["receipt_path"],
                              snapshot_digest=report["data"]["snapshot_digest"], baseline_digest=report["data"].get("baseline_digest"),
                              baseline_lease=str(lease.journal),
                              batch_size=len(operations), elapsed_seconds=elapsed)
    for receipt in receipts.values():
        workspace.store.event("safety_validated", **receipt)
    LOG.info("assets=%d phase=batch_validated elapsed=%.3f", len(entries), elapsed)
    return receipts


def receipt_valid(receipt: dict, item: dict, candidate: str, *, bridge=None, observation=None, completed=()) -> bool:
    """Reuse evidence only for the same candidate, revisions and live environment."""
    row = receipt.get("binding")
    if not isinstance(row, dict) or row.get("candidate") != candidate or row.get("payload_digest") != digest(item["payload"]):
        return False
    if bridge is None:
        return True
    capabilities = call_ok(bridge, "bridge_capabilities_get", dict())["data"]
    current = binding(item, candidate, observation, capabilities["build"], capabilities.get("rhi", "").lower(),
                      capabilities.get("safety_environment"))
    for asset in completed:
        if asset in current["revisions"]:
            # These revisions changed through this batch's verified main applies.
            current["revisions"][asset] = row.get("revisions", {}).get(asset)
    return current == row
