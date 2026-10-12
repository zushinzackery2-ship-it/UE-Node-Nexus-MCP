"""Move finished validation copies into a manual-delete holding area.

The user's retention policy keeps receipts and final evidence in place while
every ended copy is concentrated under ``.del`` for manual deletion; interrupted
runs are marked on their receipt before the same relocation so they stay both
identifiable and reclaimable.
"""

from __future__ import annotations

import json
import logging
from pathlib import Path
import time

from ...instances.identity.processes import is_alive
from ...transcode.storage.io import atomic_write, canonical

LOG = logging.getLogger(__name__)

HOLDING = ".del"
HEAVY = ("Project",)
ACTIVE_STATES = frozenset(("copying", "starting", "executing"))
SWEEP_SECONDS = 3600.0


def holding_directory(validation_directory: Path) -> Path:
    return validation_directory / HOLDING


def _relocate(root: Path, holding: Path) -> list[str]:
    target = holding / root.name
    sources = [root / name for name in HEAVY if (root / name).exists()]
    for source in sources:
        if source.is_symlink() or (target / source.name).exists():
            raise OSError(f"retention requires a real source and an unused destination: {source}")
    target.mkdir(parents=True, exist_ok=True)
    moved = []
    for source in sources:
        destination = target / source.name
        source.rename(destination)
        moved.append(source.name)
    return moved


def exited(root: Path, report: dict) -> bool:
    """Confirm process death from creation identities; age is only a sweep filter."""
    owner = report.get("owner")
    if owner and not report.get("finished_at") and is_alive(owner):
        return False
    identities = []
    worker = report.get("worker", {}).get("instance", {})
    if worker.get("pid"):
        identities.append(worker)
    for path in (root / "Runtime/Instances").glob("*.json"):
        try:
            identity = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, ValueError):
            return False
        if identity.get("pid"):
            identities.append(identity)
    for identity in identities:
        if not all(identity.get(key) for key in ("pid", "process_created", "executable", "project_path")):
            return False
        if not Path(identity["project_path"]).resolve().is_relative_to((root / "Project").resolve()):
            return False
        if is_alive(identity):
            return False
    cleanup = report.get("cleanup", {})
    return bool(identities or cleanup.get("exit_confirmed") or (owner and report.get("state") == "copying"))


def retain(validation_directory: Path, root: Path, report: dict) -> dict:
    """Concentrate one finished copy for manual deletion; receipts stay behind."""
    try:
        if root.resolve().parent != validation_directory.resolve() or not exited(root, report):
            return dict(retained=False, reason="worker_exit_unverified")
        holding = holding_directory(validation_directory)
        holding.mkdir(exist_ok=True)
        moved = _relocate(root, holding)
    except OSError as error:
        LOG.warning("validation=%s retention failed: %s", root.name, error)
        return dict(retained=False, error=str(error))
    LOG.info("validation=%s retention moved=%s", root.name, ",".join(moved) or "none")
    return dict(retained=True, holding=str(holding / root.name), moved=moved)


def sweep(validation_directory: Path, *, now: float | None = None, older_than: float = SWEEP_SECONDS) -> list[dict]:
    """Mark and relocate interrupted runs whose worker can no longer be alive."""
    now = time.time() if now is None else now
    swept = []
    for receipt in sorted(validation_directory.glob("*/receipt.json")):
        root = receipt.parent
        try:
            data = json.loads(receipt.read_text(encoding="utf-8-sig"))
            age = now - receipt.stat().st_mtime
        except (OSError, ValueError):
            continue
        if data.get("state") not in ACTIVE_STATES or data.get("finished_at") or age < older_than:
            continue
        try:
            if not exited(root, data):
                continue
            moved = _relocate(root, holding_directory(validation_directory))
        except OSError as error:
            LOG.warning("validation=%s sweep failed: %s", root.name, error)
            continue
        data.update(state="interrupted", interrupted=True, swept_at=now, finished_at=now,
                    retention=dict(retained=True, holding=str(holding_directory(validation_directory) / root.name), moved=moved))
        atomic_write(receipt, canonical(data))
        swept.append(dict(validation_id=data.get("validation_id"), moved=moved))
        LOG.info("validation=%s interrupted sweep moved=%s", root.name, ",".join(moved) or "none")
    return swept
