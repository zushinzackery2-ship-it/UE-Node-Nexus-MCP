"""Validate high-risk units before their first mutation of the publishing editor."""

from __future__ import annotations

from copy import deepcopy
import logging
import time

from ...instances.errors import InstanceError
from ...transcode.errors import SyncError
from ...transcode.storage.io import digest
from ...transcode.storage.paths import content_parts
from ...transcode.sync.project import apply_operation, call_ok
from ...transcode.collaboration.apply.transactions import actual_snapshot
from ..isolation.service import validate
from ..isolation.identity import source as verify_source_identity
from ..isolation.baselines import BaselineLease
from .evidence import binding, request_digest, verify_batch
from .mode import isolation_mode
from .risk import reasons
from ...transcode.progress import emit


LOG = logging.getLogger("ue_node_nexus_mcp.safety.publication")


class BatchAdmissionError(SyncError):
    """Keep verified proofs alongside per-asset semantic failures from one worker."""

    def __init__(self, failures, receipts):
        first = next(iter(failures.values()))
        super().__init__(first["code"], first["message"], first["details"])
        self.failures, self.receipts = failures, receipts


def admit(bridge, context, workspace, item: dict, candidate: str, observation: dict, options: dict) -> dict | None:
    mode = isolation_mode(options)
    if mode == "off" or item["empty"]:
        return None
    risks = reasons(item)
    if mode == "auto" and not risks:
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
        if item["kind"] == "scene":
            from . import scene

            operation = scene.operation(context.project_file, item)
            guarded.update(scene.packages(item))
        else:
            operation = dict(operation=apply_operation(item["kind"]), payload=deepcopy(item["payload"]))
            operation["payload"].update(dry_run=False, compile=True, save=True)
            guarded.add(item["asset"])
        operations.append((asset, item, operation))
        guarded.update(item["dependencies"])
    environment = expected[entries[0][0]]["environment"]
    baseline_assets = sorted(asset for asset in guarded if content_parts(asset) is not None)
    started = time.monotonic()
    LOG.info("assets=%d candidate=%s phase=isolated_batch_validation", len(entries), candidate)
    try:
        verify_source_identity(context.project_file, capabilities)
        with BaselineLease(context.project_file) as lease:
            emit("isolation_baseline", dict(phase="baseline", assets=baseline_assets))
            baseline = lease.export(bridge, baseline_assets)
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
    receipts, failures = dict(), dict()
    for index, (asset, item, operation) in enumerate(operations):
        emit("isolation_readback", dict(phase="readback", validation_id=report["data"]["validation_id"],
                                        asset=asset, operation_index=index, operation_count=len(operations)))
        publication = publications[index]
        proof = dict(id=publication["apply_id"], candidate=candidate, asset=asset, kind=item["kind"],
                     request=publication["payload"], receipt=publication["receipt"])
        try:
            actual_snapshot(workspace, proof)
        except SyncError as error:
            if error.code != "apply_result_mismatch":
                raise
            details = dict(error.details)
            # The recover instruction names the worker's transaction, not one
            # the source workspace owns; keep the evidence, drop the false move.
            details.pop("recover", None)
            details["asset"] = asset
            details["batch_admission"] = dict(
                validation_id=report["data"].get("validation_id"),
                receipt_path=report["data"].get("receipt_path"),
                validation_root=report["data"].get("validation_root"),
                baseline_lease=str(lease.journal),
                worker_apply_id=proof.get("id"),
            )
            failures[asset] = dict(code=error.code, message=str(error), details=details)
            continue
        receipts[asset] = dict(binding=expected[asset], binding_digest=digest(expected[asset]), risks=reasons(item),
                              validation_id=report["data"]["validation_id"], receipt_path=report["data"]["receipt_path"],
                              snapshot_digest=report["data"]["snapshot_digest"], baseline_digest=report["data"].get("baseline_digest"),
                              baseline_lease=str(lease.journal),
                              batch_size=len(operations), elapsed_seconds=elapsed)
    for receipt in receipts.values():
        workspace.store.event("safety_validated", **receipt)
    LOG.info("assets=%d phase=batch_validated elapsed=%.3f", len(entries), elapsed)
    if failures:
        raise BatchAdmissionError(failures, receipts)
    return receipts


def receipt_valid(receipt: dict, item: dict, candidate: str, *, bridge=None, observation=None, completed=()) -> bool:
    """Reuse evidence only for the same candidate, revisions and live environment."""
    row = receipt.get("binding")
    if not isinstance(row, dict) or row.get("candidate") != candidate or row.get("payload_digest") != request_digest(item):
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
