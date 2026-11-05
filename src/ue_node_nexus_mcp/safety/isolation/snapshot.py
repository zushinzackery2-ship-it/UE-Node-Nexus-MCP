"""Copy stable project inputs into a fresh directory with no shared writable files."""

from __future__ import annotations

import errno
import hashlib
import logging
import os
from pathlib import Path, PurePosixPath
import shutil
import stat

from ...instances.errors import require
from ...transcode.storage.io import digest
from ...transcode.storage.paths import content_parts
from .descriptors import read_descriptor
from .progress import advance

LOG = logging.getLogger(__name__)

DIRECTORIES = ("Content", "Config", "Plugins", "Source", "Binaries", "Resources", "Shaders", "Tools")
IGNORED = frozenset(("Saved", "Intermediate", "DerivedDataCache", ".git", "__pycache__", ".auto-index-mcp"))
CONTENT_FIELDS = ("asset_path", "map_path", "level_path", "blueprint_path", "material_path", "system_path",
                  "asset_paths", "package_names", "package_paths")
# Measured on the validation volume: a copied project's actual allocation is
# about 1.42x its logical input size once the worker writes mirrors and caches.
ALLOCATION_FACTOR = 1.5
WORKING_BYTES = 1024 ** 3


def real_path(path: Path) -> None:
    info = path.lstat()
    require(not path.is_symlink() and not getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT,
            "isolation_link_rejected", "project snapshot inputs must be real files and directories", path=str(path))


def quiet_worker_copy(project: Path) -> None:
    """Autosave would write every dirty package, which in an overlay copy means the live project."""
    settings = Path(project).parent / "Config" / "DefaultEditorPerProjectUserSettings.ini"
    text = settings.read_text(encoding="utf-8") if settings.is_file() else ""
    section = "[/Script/UnrealEd.EditorLoadingSavingSettings]"
    lines, active, found = [], False, False
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            if active and not found:
                lines.append("bAutoSaveEnable=False")
                found = True
            active = stripped.casefold() == section.casefold()
        key, separator, _value = stripped.partition("=")
        if active and separator and key.strip().casefold() == "bautosaveenable":
            line, found = "bAutoSaveEnable=False", True
        lines.append(line)
    if not found:
        if not active:
            lines.extend(["", section])
        lines.append("bAutoSaveEnable=False")
    text = "\n".join(lines) + "\n"
    settings.parent.mkdir(parents=True, exist_ok=True)
    settings.write_text(text, encoding="utf-8")


def inventory(project: Path, additional=(), *, content_directories=()) -> dict:
    additional = tuple(additional)
    project = project.absolute()
    real_path(project)
    real_path(project.parent)
    require(project.suffix.lower() == ".uproject" and project.is_file(), "invalid_request", "project_path must name a uproject")
    descriptor = read_descriptor(project)
    require(not descriptor.get("AdditionalPluginDirectories") and not descriptor.get("AdditionalRootDirectories"),
            "isolation_external_root", "copy external project/plugin roots into the project before isolation")
    files = [project]
    for name in dict.fromkeys((*DIRECTORIES, *content_directories)):
        advance(directory=name)
        source = project.parent / name
        if not source.exists():
            continue
        real_path(source)
        for directory, folders, names in os.walk(source, followlinks=False):
            advance(directory=str(directory), files=len(files))
            folders[:] = sorted(folder for folder in folders if name in content_directories or folder not in IGNORED)
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
    files = list(dict.fromkeys(files))
    return dict(project=str(project.resolve()), files=files, additional=list(additional),
                content_directories=list(content_directories), bytes=sum(path.stat().st_size for path in files))


def _candidate_base(project_root: Path, parts: tuple[str, ...], mounts: dict) -> Path | None:
    if parts[0] == "Game":
        base = project_root / "Content"
    else:
        record = mounts.get(parts[0])
        if not record or not record.get("project_local"):
            return None
        base = Path(record["content"])
    try:
        return base.resolve().relative_to(project_root.resolve())
    except ValueError:
        return None


def _candidate_files(project_root: Path, target, mounts: dict) -> set[str]:
    parts = content_parts(target) if isinstance(target, str) else None
    if parts is None:
        return set()
    base = _candidate_base(project_root, parts, mounts)
    if base is None:
        return set()
    relative = PurePosixPath(*parts[1:]).as_posix()
    known = [suffix for suffix in (".uasset", ".umap") if (project_root / base / (relative + suffix)).is_file()]
    # A package the worker will create does not exist yet; its directory must still stay real.
    return set((base / (relative + suffix)).as_posix() for suffix in known or (".uasset",))


def content_candidates(project: Path, entries: list[dict], mounts: dict | None = None) -> set[str]:
    """Project-relative package files the worker may save; every other input can be a link."""
    project_root = project.absolute().parent
    mounts = dict(mounts or {})
    targets = set()
    for entry in entries:
        payload = entry.get("payload", dict()) if isinstance(entry, dict) else dict()
        for field in CONTENT_FIELDS:
            value = payload.get(field)
            for target in (value if isinstance(value, list) else [value]):
                targets |= _candidate_files(project_root, target, mounts)
        plan_file = payload.get("plan_file")
        if isinstance(plan_file, str) and plan_file:
            targets.add(PurePosixPath(plan_file.replace("\\", "/")).as_posix())
    return targets


def overlay_enabled(candidates) -> bool:
    """Links are the default; ``UE_NEXUS_VALIDATION_COPY=full`` forces a real copy."""
    return bool(candidates) and os.environ.get("UE_NEXUS_VALIDATION_COPY", "overlay").lower() != "full"


def copy_project(plan: dict, target: Path, *, baseline=None, candidates=()) -> dict:
    source = Path(plan["project"]).parent
    target = target.absolute()
    require(not target.exists(), "isolation_target_exists", "validation copy must use a new directory")
    require(not target.is_relative_to(source) and not source.is_relative_to(target),
            "isolation_path_rejected", "validation copy and source project must be disjoint")
    ancestor = target.parent
    while not ancestor.exists():
        ancestor = ancestor.parent
    real_path(ancestor)
    from .capacity import estimate
    from .copying import apply_layout
    from .layout import copy_layout

    overlay = overlay_enabled(candidates)
    layout = copy_layout(plan, set(candidates) if overlay else None)
    budget = estimate(plan, target, ALLOCATION_FACTOR, WORKING_BYTES, baseline, layout=layout)
    LOG.info("snapshot admission target=%s free=%s required=%s copies=%s links=%s measured_at=%s",
             target, budget["free_bytes"], budget["required_bytes"], budget["copied_files"],
             budget["linked_files"], budget["measured_at"])
    require(budget["free_bytes"] >= budget["required_bytes"], "isolation_disk_budget",
            "snapshot requires target-volume allocation, live baseline and 1 GiB working space",
            available=budget["free_bytes"], **budget)
    target.mkdir(parents=True)
    try:
        summary = apply_layout(layout, source, target)
    except OSError as error:
        if error.errno != errno.ENOSPC and getattr(error, "winerror", None) != 112:
            raise
        require(False, "isolation_disk_budget", "target volume ran out of space after admission",
                admission=budget, current_free_bytes=shutil.disk_usage(ancestor).free,
                target_path=str(target), reason=str(error))
    descriptor = target / Path(plan["project"]).name
    return dict(project=str(descriptor), source=plan["project"], source_root=str(source),
                **summary, additional=plan.get("additional", []),
                content_directories=plan.get("content_directories", []),
                copy_mode="overlay" if overlay else "full",
                snapshot_digest=digest(summary["files"]), disk_budget=budget, baseline="saved_disk")


def unchanged(snapshot: dict) -> bool:
    source = Path(snapshot["source_root"])
    if any(not (source / name).is_dir() for name in snapshot.get("content_directories", [])):
        return False
    if not Path(snapshot["source"]).is_file() or any(not (source / name).is_file() for name in snapshot.get("additional", [])):
        return False
    current = inventory(Path(snapshot["source"]), snapshot.get("additional", []),
                        content_directories=snapshot.get("content_directories", []))
    paths = {path.relative_to(source).as_posix() for path in current["files"]}
    if paths != {item["path"] for item in snapshot["files"]}:
        return False
    checked_bytes = 0
    for index, item in enumerate(snapshot["files"]):
        advance("source_check", files=index, total_files=len(snapshot["files"]), checked_bytes=checked_bytes)
        path = source / item["path"]
        real_path(path)
        if path.stat().st_size != item["size"] or path.stat().st_mtime_ns != item["modified_ns"]:
            return False
        if not item.get("sha256"):
            # Linked inputs carry no checksum: a save through the link still moves mtime and size.
            continue
        checksum = hashlib.sha256()
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                checksum.update(chunk)
                checked_bytes += len(chunk)
                advance(checked_bytes=checked_bytes)
        if checksum.hexdigest() != item["sha256"]:
            return False
    return True
