"""Drain released work scopes and make every dirty package private before saving."""

from __future__ import annotations

from contextlib import nullcontext
import logging
from pathlib import Path
import shutil
import stat
import time

from ...instances.errors import require
from ...transcode.storage.paths import content_parts

LOG = logging.getLogger(__name__)
_REPARSE = stat.FILE_ATTRIBUTE_REPARSE_POINT


def _lock(manager):
    return getattr(manager, "lock", None) or nullcontext()


def is_reparse(path: Path) -> bool:
    try:
        return path.is_symlink() or bool(getattr(path.lstat(), "st_file_attributes", 0) & _REPARSE)
    except OSError:
        return False


def drain_released_scopes(manager, deadline: float) -> None:
    """Release this session's already-releasing scopes before the real close.

    ``Scopes._end`` keeps a scope while Guard still reports pending or inflight
    work; the client heartbeat normally retries it. Validation must not depend
    on the next heartbeat: a copied map save has already returned, so the close
    has to observe the registry drained. Active scopes are never released.
    """
    scopes = getattr(manager, "scopes", None)
    if not scopes:
        return
    with _lock(manager):
        active = [scope for scope in scopes.values() if not getattr(scope, "releasing", False)]
    require(not active, "isolation_active_work", "validation still owns active work scopes",
            scopes=sorted(str(getattr(scope, "scope_id", "")) for scope in active))
    started, attempts = time.monotonic(), 0
    while True:
        with _lock(manager):
            releasing = [scope for scope in scopes.values() if getattr(scope, "releasing", False)]
        if not releasing:
            break
        for scope in releasing:
            scope.release()
            attempts += 1
        with _lock(manager):
            remaining = [scope for scope in scopes.values() if getattr(scope, "releasing", False)]
        if not remaining:
            break
        require(time.monotonic() < deadline, "isolation_cleanup_failed",
                "released work scopes did not drain before the close deadline",
                scopes=sorted(str(getattr(scope, "scope_id", "")) for scope in remaining))
        time.sleep(0.05)
    LOG.info("event=isolation_scopes_drained scopes=%d duration_seconds=%.3f",
             attempts, time.monotonic() - started)


def package_file(project: Path, package: str, mounts: dict | None = None) -> Path:
    """The worker-local package file for one virtual package path."""
    parts = content_parts(package)
    require(parts is not None, "isolation_shared_package_dirty", "dirty package is not a project content package",
            package=package)
    project_root = Path(project).resolve().parent
    if parts[0] == "Game":
        base = project_root / "Content"
    else:
        directory = (mounts or {}).get("/" + parts[0])
        require(directory is not None, "isolation_shared_package_dirty",
                "dirty package belongs to a mount the worker does not own", package=package)
        base = Path(directory)
    base = base.resolve()
    require(base.is_relative_to(project_root), "isolation_shared_package_dirty",
            "dirty package resolves outside the copied project", package=package, directory=str(base))
    relative = Path(*parts[1:])
    candidates = [base / relative.with_suffix(suffix) for suffix in (".uasset", ".umap")]
    file = next((path for path in candidates if path.exists()), candidates[0])
    require(file.is_relative_to(project_root), "isolation_shared_package_dirty",
            "dirty package file escapes the copied project", package=package, path=str(file))
    return file


def _replace_link(path: Path) -> None:
    target = path.resolve(strict=True)
    temporary = path.with_name(path.name + ".nexus-materialize")
    if temporary.exists():
        shutil.rmtree(temporary) if temporary.is_dir() and not temporary.is_symlink() else temporary.unlink()
    try:
        if target.is_dir():
            shutil.copytree(target, temporary, symlinks=False)
        else:
            shutil.copy2(target, temporary)
        if path.is_symlink() or not path.is_dir():
            path.unlink()
        else:
            path.rmdir()
        temporary.rename(path)
        LOG.info("event=isolation_link_materialized path=%s target=%s", path, target)
    except BaseException:
        if temporary.exists() and not is_reparse(temporary):
            shutil.rmtree(temporary) if temporary.is_dir() else temporary.unlink()
        raise


def materialize_package(project: Path, package: str, mounts: dict | None = None) -> Path:
    """Make the package's file and every parent directory real inside the copy."""
    file = package_file(project, package, mounts)
    project_root = Path(project).resolve().parent
    for path in (file, *file.parents):
        if is_reparse(path):
            _replace_link(path)
            break
        if path == project_root:
            break
    for path in (file, *file.parents):
        require(not is_reparse(path), "isolation_shared_package_dirty",
                "dirty package still resolves through a shared link", package=package, path=str(path))
        if path == project_root:
            break
    return file


def save_dirty_packages(session, packages: list[str], deadline: float) -> None:
    """Save each dirty copied package only after its disk target is private."""
    for package in packages:
        file = materialize_package(session.project, package, getattr(session, "content_mounts", dict()))
        LOG.info("event=isolation_package_private package=%s file=%s", package, file)
        saved = session.call("asset_save", dict(asset_path=package))
        require(saved.get("ok"), "isolation_cleanup_failed", "could not save the copied package", response=saved)
        drain_released_scopes(session.manager, deadline)
