"""Environment-neutral immutable records shared by catalogs in one mirror."""

import os
from pathlib import Path, PureWindowsPath

from ...errors import SyncError
from ...storage.io import atomic_write, canonical, confined, digest
from ...storage.metrics import measure

PREFIX = "../.objects/"


def write(directory: Path, relative: str, value: dict) -> str:
    path = confined(directory.parent / ".objects", relative)
    with measure("lookup"):
        exists = path.is_file()
    if exists:
        from ...storage.io import read_json

        if digest(read_json(path)) != digest(value):
            raise SyncError("schema_corrupt", "shared schema object checksum mismatch", dict(path=str(path)))
    else:
        atomic_write(path, canonical(value))
    return PREFIX + relative


def path(directory: Path, entry: dict) -> Path:
    relative = entry["file"]
    if entry.get("storage") != "shared":
        return confined(directory, relative)
    if not isinstance(relative, str):
        raise SyncError("schema_corrupt", "shared schema object path must be a string")
    return confined(directory.parent / ".objects", relative.removeprefix(PREFIX))


def document_link(directory: Path, category: str, entry: dict) -> str:
    """Render a manifest reference without opening its target on disk.

    Reads and writes continue through ``path``/``confined``. Documentation
    refers to the immutable pool's logical path, including through symlinks.
    """
    relative = entry["file"]
    if not isinstance(relative, str):
        raise SyncError("schema_corrupt", "schema object path must be a string")
    shared = entry.get("storage") == "shared"
    member = relative.removeprefix(PREFIX) if shared else relative
    logical = PureWindowsPath(member)
    if not member or logical.drive or logical.root or ".." in logical.parts:
        raise SyncError("invalid_path", "path escapes its registered workspace", dict(path=relative))
    base = directory.parent / ".objects" if shared else directory
    return Path(os.path.relpath(base / member, directory / category)).as_posix()
