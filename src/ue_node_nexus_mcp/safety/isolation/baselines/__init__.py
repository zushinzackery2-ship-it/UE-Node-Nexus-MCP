"""Own live exports before serialization and archive their payload after use."""

from __future__ import annotations

from contextlib import AbstractContextManager
import logging
import os
from pathlib import Path
import time
from uuid import uuid4

from ....coordination.file_lock import FileLock
from ....instances.errors import require
from ....instances.identity.processes import inspect_process, is_alive
from ....transcode.storage.io import atomic_write, canonical, digest, read_json
from ....transcode.sync.project import call_ok
from ..snapshot import real_path

LOG = logging.getLogger(__name__)


def checked_directory(project: Path, path: Path) -> None:
    require(path.is_relative_to(project.parent), "isolation_path_rejected", "baseline storage must remain project-local")
    for ancestor in (path, *path.parents):
        if ancestor.exists() or ancestor.is_symlink():
            real_path(ancestor)
        if ancestor == project.parent:
            break


def archive(project: Path, record: dict) -> dict:
    identifier = record["id"]
    require(len(identifier) == 32 and all(char in "0123456789abcdef" for char in identifier),
            "isolation_baseline_identity", "baseline lease requires its original UUID")
    root = project.parent / "Saved/Nexus/SafetyBaseline" / identifier
    require(record["root"] == str(root) and record["project"] == str(project),
            "isolation_baseline_identity", "baseline lease names different storage")
    holding = project.parent / ".del/SafetyBaseline" / identifier
    checked_directory(project, root)
    checked_directory(project, holding)
    if root.exists():
        require(not holding.exists(), "isolation_baseline_identity", "baseline archive destination already exists")
        holding.parent.mkdir(parents=True, exist_ok=True)
        root.rename(holding)
        record["holding"] = str(holding)
    record.update(state="archived", finished_at=time.time())
    LOG.info("baseline=%s state=archived holding=%s", identifier, record.get("holding", "empty export"))
    return record


def sweep(project: Path) -> list[str]:
    """Recover a dead consumer's export; a live creation identity retains ownership."""
    project = project.resolve()
    leases = project.parent / "Saved/Nexus/SafetyBaseline/.leases"
    checked_directory(project, leases)
    recovered = []
    for journal in leases.glob("*.json"):
        record = read_json(journal)
        if record.get("state") == "archived" or not record.get("owner") or is_alive(record["owner"]):
            continue
        with FileLock(journal.with_suffix(".lock"), holder=dict(action="baseline_recovery")):
            record = read_json(journal)
            if record.get("state") == "archived" or is_alive(record["owner"]):
                continue
            record["interrupted"] = True
            try:
                archive(project, record)
            except Exception:
                LOG.exception("baseline=%s interrupted archive failed", record.get("id"))
                raise
            atomic_write(journal, canonical(record))
            recovered.append(record["id"])
    return recovered


class BaselineLease(AbstractContextManager):
    """A journal and OS lock span native export, preflight and worker consumption."""

    def __init__(self, project: str):
        self.project = Path(project).resolve()
        self.identifier = uuid4().hex
        self.root = self.project.parent / "Saved/Nexus/SafetyBaseline" / self.identifier
        self.journal = self.root.parent / ".leases" / (self.identifier + ".json")
        self.lock = FileLock(self.journal.with_suffix(".lock"), holder=dict(action="baseline_export"))
        self.record = dict(id=self.identifier, project=str(self.project), root=str(self.root),
                           owner=inspect_process(os.getpid()), state="exporting", started_at=time.time())

    def __enter__(self):
        require(self.record["owner"] is not None, "isolation_baseline_identity", "baseline consumer creation identity is required")
        sweep(self.project)
        checked_directory(self.project, self.journal.parent)
        self.lock.__enter__()
        try:
            atomic_write(self.journal, canonical(self.record))
        except BaseException:
            self.lock.__exit__(None, None, None)
            raise
        return self

    def export(self, bridge, assets: list[str]) -> dict:
        baseline = call_ok(bridge, "safety_baseline_export",
                           dict(asset_paths=assets, dry_run=False, baseline_id=self.identifier))["data"]
        require(Path(baseline["root"]).resolve() == self.root and Path(baseline["project"]).resolve() == self.project,
                "isolation_baseline_identity", "native export differs from its owned baseline lease")
        self.record.update(state="consuming", baseline=baseline, baseline_digest=digest(baseline))
        atomic_write(self.journal, canonical(self.record))
        return baseline

    def bind(self, report: dict) -> None:
        data = report.get("data", dict())
        self.record.update(validation_id=data.get("validation_id"), receipt_path=data.get("receipt_path"),
                           validation_state=data.get("state"))
        atomic_write(self.journal, canonical(self.record))

    def __exit__(self, exc_type, exc_value, traceback):
        try:
            if exc_value is not None:
                self.record["error"] = dict(type=exc_type.__name__, message=str(exc_value))
            archive(self.project, self.record)
            atomic_write(self.journal, canonical(self.record))
        except BaseException as error:
            self.record.update(state="archive_failed", archive_error=str(error))
            atomic_write(self.journal, canonical(self.record))
            LOG.exception("baseline=%s archive failed", self.identifier)
            raise
        finally:
            self.lock.__exit__(exc_type, exc_value, traceback)
        return False
