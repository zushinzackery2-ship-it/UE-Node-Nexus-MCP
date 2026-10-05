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
from .evidence import binding, verify
from .risk import reasons


LOG = logging.getLogger("ue_node_nexus_mcp.safety.publication")


def admit(bridge, context, workspace, item: dict, candidate: str, observation: dict, options: dict) -> dict | None:
    risks = reasons(item)
    if not risks:
        return None
    if options.get("compile", True) is not True:
        raise SyncError("safety_compile_required", "high-risk publication requires compilation", dict(asset=item["asset"], risks=risks))
    capabilities = call_ok(bridge, "bridge_capabilities_get", dict())["data"]
    if capabilities.get("safety_contract") != 1:
        raise SyncError("safety_contract_required", "publishing editor requires complete isolation contract 1", dict(asset=item["asset"]))
    rhi = capabilities.get("rhi", "").lower()
    if rhi not in ("d3d12", "d3d11"):
        raise SyncError("safety_rhi_unsupported", "publishing RHI cannot be reproduced by the worker", dict(rhi=rhi))
    expected = binding(item, candidate, observation, capabilities["build"], rhi, capabilities.get("safety_environment"))
    guarded = sorted(set((item["asset"],)) | set(item["dependencies"]))
    game_assets = [asset for asset in guarded if asset.startswith("/Game/")]
    baseline = call_ok(bridge, "safety_baseline_export", dict(asset_paths=game_assets, dry_run=False))["data"]
    if baseline.get("environment") != expected["environment"]:
        raise SyncError("safety_identity_changed", "baseline environment differs from the publishing editor")
    operation = dict(operation=apply_operation(item["kind"]), payload=deepcopy(item["payload"]))
    operation["payload"].update(dry_run=False, compile=True, save=True)
    started = time.monotonic()
    LOG.info("asset=%s candidate=%s risks=%s phase=isolated_validation", item["asset"], candidate, risks)
    try:
        report = validate(context.project_file, [operation], None, rhi, 600, False, baseline=baseline,
                          allow_low_memory=options.get("safety_allow_low_memory", False))
        publication = verify(report, expected, operation)
    except InstanceError as error:
        raise SyncError(error.code, str(error), error.details) from error
    proof = dict(id=publication["apply_id"], candidate=candidate, asset=item["asset"], kind=item["kind"],
                 request=publication["payload"], receipt=publication["receipt"])
    actual_snapshot(workspace, proof)
    latest = call_ok(bridge, "bridge_capabilities_get", dict())["data"]
    if binding(item, candidate, observation, latest["build"], latest.get("rhi", "").lower(), latest.get("safety_environment")) != expected:
        raise SyncError("safety_identity_changed", "publishing environment changed during isolated validation")
    receipt = dict(binding=expected, binding_digest=digest(expected), risks=risks,
                   validation_id=report["data"]["validation_id"], receipt_path=report["data"]["receipt_path"],
                   snapshot_digest=report["data"]["snapshot_digest"], baseline_digest=report["data"].get("baseline_digest"),
                   elapsed_seconds=time.monotonic() - started)
    workspace.store.event("safety_validated", **receipt)
    LOG.info("asset=%s candidate=%s phase=validated elapsed=%.3f", item["asset"], candidate, receipt["elapsed_seconds"])
    return receipt
