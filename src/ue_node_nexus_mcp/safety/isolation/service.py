"""Journal isolated validation; publish nothing into the source project."""

from __future__ import annotations

import logging
from dataclasses import asdict
from pathlib import Path
import time
from uuid import uuid4

from ...instances.errors import InstanceError, require
from ...instances.identity.paths import runtime_root
from ...transcode.storage.io import atomic_write, canonical, digest
from .inputs import validate_operations
from .snapshot import copy_project, inventory, unchanged

LOG = logging.getLogger(__name__)


def validate(project_path: str, operations: list[dict], engine_path: str | None, rhi: str,
             timeout_seconds: float, dry_run: bool, *, baseline: dict | None = None,
             allow_low_memory: bool = False) -> dict:
    entered = time.monotonic()
    require(rhi in ("d3d12", "d3d11"), "invalid_request", "rhi must be d3d12 or d3d11")
    require(isinstance(timeout_seconds, (int, float)) and not isinstance(timeout_seconds, bool)
            and 30 <= timeout_seconds <= 600, "invalid_request", "timeout_seconds must be 30..600")
    require(isinstance(dry_run, bool), "invalid_request", "dry_run must be a boolean")
    require(isinstance(allow_low_memory, bool), "invalid_request", "allow_low_memory must be a boolean")
    from .mounts import BASE_MOUNTS, plugin_mounts, requested_mounts
    from ...instances.lifecycle.engine import resolve_engine
    plugin_inputs = dict()
    engine = None
    if requested_mounts(operations) - BASE_MOUNTS:
        engine = resolve_engine(project_path, engine_path)
        plugin_inputs = plugin_mounts(Path(project_path), engine)
        if baseline is not None:
            loaded_mounts = baseline["environment"]["content_mounts"]
            plugin_inputs = dict((name, record) for name, record in plugin_inputs.items() if name in loaded_mounts)
    entries = validate_operations(operations, BASE_MOUNTS | plugin_inputs.keys())
    scene_files = [item["payload"]["plan_file"].replace("\\", "/") for item in entries if item["operation"] == "scene_apply"]
    plan = inventory(Path(project_path), scene_files)
    identifier = uuid4().hex
    root = runtime_root() / "Validation" / identifier
    report = dict(validation_id=identifier, source_project=plan["project"], operations=entries,
                  validation_root=str(root), dry_run=dry_run, baseline="saved_disk", copied_bytes=plan["bytes"],
                  copied_files=len(plan["files"]), state="preview", applied_to_source=False,
                  plugin_mounts=plugin_inputs, operations_digest=digest(entries), allow_low_memory=allow_low_memory)
    report["timings"] = dict(preflight_seconds=time.monotonic() - entered)
    if dry_run:
        return dict(ok=True, operation="safety_validate", data=report)
    from .session import ValidationSession
    engine = engine or resolve_engine(plan["project"], engine_path)
    root.mkdir(parents=True)
    from ...instances.lifecycle.policy import Policy
    atomic_write(root / "Runtime/policy.json", canonical(asdict(Policy.read(runtime_root()))))
    receipt = root / "receipt.json"
    report.update(state="copying", results=[], started_at=time.time(), receipt_path=str(receipt))
    atomic_write(receipt, canonical(report))
    session, snapshot = None, None
    try:
        phase_started = time.monotonic()
        snapshot = copy_project(plan, root / "Project")
        if baseline is not None:
            from .baseline import overlay
            snapshot = overlay(snapshot, baseline)
            report.update(baseline="live_memory", baseline_digest=snapshot["baseline_digest"])
        report["timings"]["copy_seconds"] = time.monotonic() - phase_started
        atomic_write(root / "snapshot.json", canonical(snapshot))
        worker_marker = Path(snapshot["project"]).parent / "Saved/Nexus/validation-worker.json"
        atomic_write(worker_marker, canonical(dict(validation_id=identifier,
                     project_path=str(Path(snapshot["project"]).resolve()).replace("\\", "/"))))
        report.update(state="starting", snapshot_digest=snapshot["snapshot_digest"], copied_project=snapshot["project"])
        atomic_write(receipt, canonical(report))
        session = ValidationSession(Path(snapshot["project"]), engine, root, rhi, timeout_seconds, allow_low_memory)
        phase_started = time.monotonic()
        report["worker"] = session.start()
        report["timings"]["startup_seconds"] = time.monotonic() - phase_started
        phase_started = time.monotonic()
        for index, item in enumerate(entries):
            if item["operation"] == "scene_apply":
                item = dict(item, payload=dict(item["payload"]))
                item["payload"]["plan_file"] = str(Path(snapshot["project"]).parent / item["payload"]["plan_file"])
                output = Path(snapshot["project"]).parent / "Saved/ValidationResults" / (str(index) + ".json")
                output.parent.mkdir(parents=True, exist_ok=True)
                item["payload"]["out_file"] = str(output)
            report.update(state="executing", operation_index=index)
            atomic_write(receipt, canonical(report))
            LOG.info("validation=%s operation=%s phase=begin index=%s", identifier, item["operation"], index)
            report["executing_operation"] = item
            atomic_write(receipt, canonical(report))
            response = session.execute(item)
            report["results"].append(response)
            atomic_write(receipt, canonical(report))
            require(response.get("ok") is True, "isolated_validation_failed", "candidate failed in the isolated editor",
                    index=index, response=response)
        report["timings"]["execution_seconds"] = time.monotonic() - phase_started
        report["state"] = "validated"
    except Exception as error:
        LOG.exception("validation=%s failed", identifier)
        report.update(state="failed", error=error.envelope()["error"] if isinstance(error, InstanceError)
                      else dict(code="isolated_validation_failed", message=str(error)))
    finally:
        if session:
            try:
                close_started = time.monotonic()
                report["cleanup"] = session.close()
                report["timings"]["exit_seconds"] = time.monotonic() - close_started
                require(report["cleanup"].get("exit_confirmed"), "isolation_cleanup_failed", "validation editor exit was not confirmed")
                if report["cleanup"].get("forced"):
                    report["state"] = "failed"
                    report.setdefault("error", dict(code="isolation_cleanup_failed", message=report["cleanup"]["reason"]))
                else:
                    cleanup = report["cleanup"]
                    exit_code = cleanup.get("exit_code", cleanup.get("instance", dict()).get("exit_code"))
                    require(cleanup.get("launched") is False or exit_code == 0, "isolation_worker_exit_failed",
                            "validation requires a confirmed normal worker exit", exit_code=exit_code,
                            instance=cleanup.get("instance"))
            except Exception as error:
                report.update(state="failed", cleanup_error=str(error),
                              cleanup_details=error.envelope()["error"] if isinstance(error, InstanceError) else dict(message=str(error)))
                report.setdefault("error", error.envelope()["error"] if isinstance(error, InstanceError)
                                  else dict(code="isolation_cleanup_failed", message=str(error)))
        if snapshot:
            try:
                check_started = time.monotonic()
                report["source_unchanged"] = unchanged(snapshot)
                report["timings"]["source_check_seconds"] = time.monotonic() - check_started
                if not report["source_unchanged"]:
                    report.update(state="stale", error=dict(code="isolation_source_changed", message="source inputs changed after snapshot"))
            except (OSError, InstanceError) as error:
                report.update(state="stale", source_unchanged=False, source_check_error=str(error))
        report["finished_at"] = time.time()
        atomic_write(receipt, canonical(report))
    return dict(ok=report["state"] == "validated", operation="safety_validate", data=report,
                **(dict(error=report["error"]) if report.get("error") else dict()))
