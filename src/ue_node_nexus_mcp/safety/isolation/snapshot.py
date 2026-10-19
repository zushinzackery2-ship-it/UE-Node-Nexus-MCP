"""Copy stable project inputs into a fresh directory with no shared writable files."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path, PurePosixPath
import shutil
import stat

from ...instances.errors import require
from ...transcode.storage.io import digest
from .descriptors import read_descriptor

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
    if "bAutoSaveEnable" in text:
        text = "\n".join("bAutoSaveEnable=False" if line.strip().startswith("bAutoSaveEnable") else line
                         for line in text.splitlines())
    else:
        text = text.rstrip() + "\n\n" + section + "\nbAutoSaveEnable=False\n"
    settings.parent.mkdir(parents=True, exist_ok=True)
    settings.write_text(text, encoding="utf-8")


def inventory(project: Path, additional=()) -> dict:
    additional = tuple(additional)
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
    return dict(project=str(project.resolve()), files=files, additional=list(additional), bytes=sum(path.stat().st_size for path in files))


def content_candidates(project: Path, entries: list[dict]) -> set[str]:
    """Project-relative package files the worker may save; every other input can be a link."""
    project_root = project.absolute().parent
    targets = set()
    for entry in entries:
        payload = entry.get("payload", dict()) if isinstance(entry, dict) else dict()
        for field in CONTENT_FIELDS:
            value = payload.get(field)
            for target in (value if isinstance(value, list) else [value]):
                if not isinstance(target, str) or not target.startswith("/Game/"):
                    continue
                relative = target[len("/Game/"):]
                known = [suffix for suffix in (".uasset", ".umap")
                         if (project_root / "Content" / (relative + suffix)).is_file()]
                # A package the worker will create does not exist yet; its directory must still stay real.
                for suffix in known or (".uasset",):
                    targets.add(PurePosixPath("Content", relative + suffix).as_posix())
        plan_file = payload.get("plan_file")
        if isinstance(plan_file, str) and plan_file:
            targets.add(PurePosixPath(plan_file.replace("\\", "/")).as_posix())
    return targets


LINKED_ROOTS = frozenset(("Content", "Tools", "Shaders", "Resources"))


def linkable(relative: str) -> bool:
    """Only data trees are linked: the editor discovers plugins and project binaries by their real path."""
    return relative.split("/", 1)[0] in LINKED_ROOTS


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

    overlay = overlay_enabled(candidates)
    budget = estimate(plan, target, ALLOCATION_FACTOR, WORKING_BYTES, baseline,
                      subset=set(candidates) if overlay else None)
    available = shutil.disk_usage(ancestor).free
    require(available >= budget["required_bytes"], "isolation_disk_budget",
            "snapshot requires target-volume allocation, live baseline and 1 GiB working space",
            available=available, **budget)
    target.mkdir(parents=True)
    records, copied_files, copied_bytes, linked_files, linked_directories = [], 0, 0, 0, 0
    protected, linked = set(), set()
    if overlay:
        for key in candidates:
            parent = PurePosixPath(key).parent
            while parent.as_posix() not in ("", "."):
                protected.add(parent.as_posix())
                parent = parent.parent
    for path in plan["files"]:
        real_path(path)
        before = path.stat()
        relative = path.relative_to(source)
        key = relative.as_posix()
        parent = relative.parent.as_posix() if relative.parent.as_posix() != "." else ""
        destination = target / relative
        shadow = None
        if overlay:
            shadow = parent if parent in linked else next((name for name in linked if parent.startswith(name + "/")), None)
        if overlay and key not in candidates and shadow is not None:
            # Already reachable through a linked directory: never write inside it.
            records.append(dict(path=key, size=before.st_size, modified_ns=before.st_mtime_ns, sha256=None, link=shadow))
            linked_files += 1
            continue
        if overlay and key not in candidates and linkable(key):
            source_dir = source / relative.parent
            destination_dir = destination.parent
            if parent and parent not in protected and not os.path.lexists(destination_dir):
                destination_dir.parent.mkdir(parents=True, exist_ok=True)
                try:
                    os.symlink(source_dir, destination_dir, target_is_directory=True)
                except OSError:
                    pass
                else:
                    # One link per directory beats one per package; the worker only saves candidates.
                    linked.add(parent)
                    linked_directories += 1
                    records.append(dict(path=key, size=before.st_size, modified_ns=before.st_mtime_ns, sha256=None, link=parent))
                    linked_files += 1
                    continue
            destination_dir.mkdir(parents=True, exist_ok=True)
            try:
                os.symlink(path, destination, target_is_directory=False)
            except OSError:
                pass
            else:
                records.append(dict(path=key, size=before.st_size, modified_ns=before.st_mtime_ns, sha256=None, link=str(path)))
                linked_files += 1
                continue
        require(shadow is None, "isolation_path_rejected",
                "a copied package must not live under a linked directory", path=str(path))
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
        records.append(dict(path=key, size=before.st_size, modified_ns=before.st_mtime_ns, sha256=checksum.hexdigest()))
        copied_files += 1
        copied_bytes += before.st_size
    descriptor = target / Path(plan["project"]).name
    return dict(project=str(descriptor), source=plan["project"], source_root=str(source),
                files=records, additional=plan.get("additional", []), bytes=copied_bytes,
                copied_files=copied_files, linked_files=linked_files, linked_directories=linked_directories,
                copy_mode="overlay" if overlay else "full",
                snapshot_digest=digest(records), disk_budget=budget, baseline="saved_disk")


def unchanged(snapshot: dict) -> bool:
    source = Path(snapshot["source_root"])
    if not Path(snapshot["source"]).is_file() or any(not (source / name).is_file() for name in snapshot.get("additional", [])):
        return False
    current = inventory(Path(snapshot["source"]), snapshot.get("additional", []))
    paths = {path.relative_to(source).as_posix() for path in current["files"]}
    if paths != {item["path"] for item in snapshot["files"]}:
        return False
    for item in snapshot["files"]:
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
        if checksum.hexdigest() != item["sha256"]:
            return False
    return True
