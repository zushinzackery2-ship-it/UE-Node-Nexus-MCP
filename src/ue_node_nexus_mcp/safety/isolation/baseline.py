"""Overlay verified live package snapshots onto the private saved-disk copy."""

from __future__ import annotations

import hashlib
from pathlib import Path
import shutil

from ...instances.errors import require
from ...transcode.storage.io import digest
from .snapshot import real_path


def overlay(snapshot: dict, baseline: dict) -> dict:
    source = Path(snapshot["source"]).resolve()
    require(Path(baseline["project"]).resolve() == source, "isolation_baseline_identity", "baseline names a different source project")
    root = Path(baseline["root"]).resolve()
    require(root.parent == source.parent / "Saved/Nexus/SafetyBaseline", "isolation_baseline_identity", "baseline must use the source project's private snapshot directory")
    require(not baseline.get("dry_run"), "isolation_baseline_identity", "baseline must contain serialized live packages")
    target = Path(snapshot["project"]).parent
    rows = baseline.get("files", [])
    require(len(rows) <= 4096 * 5, "isolation_baseline_budget", "baseline exceeds the package file budget")
    seen = set()
    for row in rows:
        relative = Path(row["relative"])
        require(not relative.is_absolute() and relative.parts[0] == "Content" and ".." not in relative.parts,
                "isolation_path_rejected", "baseline files must remain inside copied Content")
        require(relative.as_posix() not in seen, "isolation_baseline_identity", "baseline has duplicate file targets")
        seen.add(relative.as_posix())
        original = Path(row["source"]).absolute()
        require(original == root / relative, "isolation_path_rejected", "baseline source differs from its owned target")
        destination = target / relative
        if row["hash"] == "absent":
            if destination.exists():
                destination.unlink()
            continue
        for path in (original, *original.parents):
            real_path(path)
            if path == root:
                break
        require(hashlib.md5(original.read_bytes()).hexdigest() == row["hash"].lower(),
                "isolation_baseline_changed", "baseline bytes changed after serialization", path=str(original))
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, destination)
        require(hashlib.md5(destination.read_bytes()).hexdigest() == row["hash"].lower(),
                "isolation_baseline_changed", "copied baseline bytes differ", path=str(destination))
    snapshot.update(baseline="live_memory", baseline_digest=digest(baseline), baseline_files=rows)
    return snapshot
