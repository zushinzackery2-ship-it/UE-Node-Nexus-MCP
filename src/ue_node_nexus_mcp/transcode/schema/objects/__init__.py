"""Environment-neutral immutable records shared by catalogs in one mirror."""

from pathlib import Path

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
