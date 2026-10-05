"""Discoverable, project-copy validation of high-risk native asset operations."""

from typing import Literal

from ..instances.errors import InstanceError
from .isolation.service import validate


def safety_baseline_export(asset_paths: list[str], dry_run: bool = True) -> dict:
    """Serialize the live baseline to private files without saving original packages."""
    from ..runtime import call_bridge
    return call_bridge("safety_baseline_export", dict(asset_paths=asset_paths, dry_run=dry_run))


def safety_validate(project_path: str, operations: list[dict], engine_path: str | None = None,
                    rhi: Literal["d3d12", "d3d11"] = "d3d12", timeout_seconds: float = 180,
                    dry_run: bool = True, allow_low_memory: bool = False) -> dict:
    """Execute asset operations only in a copied project and separate UE process.

    Uses saved disk inputs; unsaved source-editor changes are outside this snapshot.
    Returns the exact input identity, per-operation results, source checks and
    cleanup evidence. Failed copies and logs remain inspectable. No automatic
    application to the source editor occurs. Use task_submit for a background run.
    """
    try:
        return validate(project_path, operations, engine_path, rhi, timeout_seconds, dry_run,
                        allow_low_memory=allow_low_memory)
    except (InstanceError, OSError, ValueError) as error:
        detail = error.envelope()["error"] if isinstance(error, InstanceError) else dict(code="invalid_request", message=str(error))
        return dict(ok=False, operation="safety_validate", error=detail, diagnostics=[], warnings=[])
