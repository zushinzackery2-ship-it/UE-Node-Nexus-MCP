"""Copy stable project inputs into a fresh directory with no shared writable files."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import shutil
import stat

from ...instances.errors import require
from ...transcode.storage.io import digest
from .descriptors import read_descriptor

DIRECTORIES = ("Content", "Config", "Plugins", "Source", "Binaries", "Resources", "Shaders", "Tools")
IGNORED = frozenset(("Saved", "Intermediate", "DerivedDataCache", ".git", "__pycache__"))


def real_path(path: Path) -> None:
    info = path.lstat()
    require(not path.is_symlink() and not getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT,
            "isolation_link_rejected", "project snapshot inputs must be real files and directories", path=str(path))


def inventory(project: Path, additional=()) -> dict:
    project = project.absolute()
    real_path(project)
    real_path(project.parent)
    require(project.suffix.lower() == ".uproject" and project.is_file(), "invalid_request", "project_path must name a uproject")
    descriptor = read_descriptor(project)
    require(not descriptor.get("AdditionalPluginDirectories") and not descriptor.get("AdditionalRootDirectories"),
            "isolation_external_root", "copy external project/plugin roots into the project before isolation")
    files = [project]
    for name in DIRECTORIES:
        source = project.parent / name
        if not source.exists():
            continue
        real_path(source)
        for directory, folders, names in os.walk(source, followlinks=False):
            folders[:] = sorted(folder for folder in folders if folder not in IGNORED)
            for folder in folders:
                real_path(Path(directory) / folder)
            for filename in sorted(names):
                path = Path(directory) / filename
                real_path(path)
                if path.is_file():
                    files.append(path)
    for name in additional:
        path = project.parent / name
        require(path.is_file() and path.suffix.lower() == ".json", "isolation_plan_missing", "scene plan must be an existing project-local JSON file", path=str(path))
        for ancestor in (path, *path.parents):
            real_path(ancestor)
            if ancestor == project.parent:
                break
        if path not in files:
            files.append(path)
    return dict(project=str(project.resolve()), files=files, bytes=sum(path.stat().st_size for path in files))


def copy_project(plan: dict, target: Path) -> dict:
    source = Path(plan["project"]).parent
    target = target.absolute()
    require(not target.exists(), "isolation_target_exists", "validation copy must use a new directory")
    require(not target.is_relative_to(source) and not source.is_relative_to(target),
            "isolation_path_rejected", "validation copy and source project must be disjoint")
    ancestor = target.parent
    while not ancestor.exists():
        ancestor = ancestor.parent
    real_path(ancestor)
    require(shutil.disk_usage(ancestor).free >= plan["bytes"] + 1024 ** 3,
            "isolation_disk_budget", "snapshot requires its measured size plus 1 GiB working space", required=plan["bytes"] + 1024 ** 3)
    target.mkdir(parents=True)
    records = []
    for path in plan["files"]:
        real_path(path)
        before = path.stat()
        relative = path.relative_to(source)
        destination = target / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        checksum = hashlib.sha256()
        with path.open("rb") as reader, destination.open("xb") as writer:
            while chunk := reader.read(1024 * 1024):
                writer.write(chunk)
                checksum.update(chunk)
        after = path.stat()
        require((before.st_size, before.st_mtime_ns) == (after.st_size, after.st_mtime_ns)
                and destination.stat().st_size == before.st_size, "isolation_source_changed",
                "project input changed while copying", path=str(path))
        records.append(dict(path=relative.as_posix(), size=before.st_size, modified_ns=before.st_mtime_ns,
                            sha256=checksum.hexdigest()))
    descriptor = target / Path(plan["project"]).name
    return dict(project=str(descriptor), source=plan["project"], source_root=str(source),
                files=records, bytes=plan["bytes"], snapshot_digest=digest(records), baseline="saved_disk")


def unchanged(snapshot: dict) -> bool:
    source = Path(snapshot["source_root"])
    for item in snapshot["files"]:
        path = source / item["path"]
        real_path(path)
        if path.stat().st_size != item["size"] or path.stat().st_mtime_ns != item["modified_ns"]:
            return False
        checksum = hashlib.sha256()
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                checksum.update(chunk)
        if checksum.hexdigest() != item["sha256"]:
            return False
    return True
