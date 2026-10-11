"""Discover authored files while pruning ignored subtrees before traversal."""

from __future__ import annotations

import logging
import os
from pathlib import Path

from ...errors import SyncError
from .policy import IgnorePolicy

LOGGER = logging.getLogger("ue_nexus.discovery")


def scan_error(error: OSError) -> None:
    LOGGER.error("mirror scan failed path=%s error=%s", error.filename, error)
    raise SyncError("mirror_scan_failed", str(error), dict(file=error.filename)) from error


def files(root: Path, suffix: str = ".nexus", directory: Path | None = None) -> list[Path]:
    if not root.is_dir():
        return []
    policy = IgnorePolicy.load(root)
    scope = directory if directory is not None else root
    relative = scope.relative_to(root)
    if policy.ignored(relative.parts, directory=True) or not scope.is_dir():
        return []
    if not scope.resolve().is_relative_to(root.resolve()):
        raise SyncError("invalid_path", f"scan directory leaves mirror root: {scope}")
    result = []
    visited, skipped_directories, skipped_files = 0, 0, 0
    for current, directories, filenames in os.walk(scope, topdown=True, onerror=scan_error, followlinks=False):
        visited += 1
        parent = Path(current)
        parts = parent.relative_to(root).parts
        kept = []
        for name in directories:
            if policy.excluded((*parts, name), directory=True):
                skipped_directories += 1
                LOGGER.debug("pruned ignored directory path=%s", parent / name)
            else:
                kept.append(name)
        directories[:] = kept
        for name in filenames:
            if not name.endswith(suffix):
                continue
            if policy.excluded((*parts, name)):
                skipped_files += 1
            else:
                result.append(parent / name)
    LOGGER.debug("mirror scan root=%s scope=%s visited=%d ignored_directories=%d ignored_files=%d candidates=%d",
                 root, scope, visited, skipped_directories, skipped_files, len(result))
    return sorted(result)
