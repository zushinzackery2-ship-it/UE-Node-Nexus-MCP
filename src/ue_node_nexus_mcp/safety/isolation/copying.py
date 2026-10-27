"""Execute an admitted copy layout without changing its write/link decisions."""

import hashlib
import logging
import os

from ...instances.errors import require

LOG = logging.getLogger(__name__)


def create_link(source, destination, directory):
    destination.parent.mkdir(parents=True, exist_ok=True)
    try:
        os.symlink(source, destination, target_is_directory=directory)
    except OSError as error:
        LOG.error("snapshot link failed source=%s target=%s reason=%s", source, destination, error)
        require(False, "isolation_link_failed", "snapshot links require filesystem support and symlink permission; select full copy explicitly",
                source=str(source), target_path=str(destination), reason=str(error))


def apply_layout(layout, source, target):
    from .snapshot import real_path

    records, linked = [], set()
    copied_files, copied_bytes, linked_files = 0, 0, 0
    for row in layout["rows"]:
        path, key = row["source"], row["path"]
        real_path(path)
        before = path.stat()
        record = dict(path=key, size=before.st_size, modified_ns=before.st_mtime_ns, sha256=None)
        if row["mode"] == "link":
            directory = row["link_directory"]
            if directory and directory not in linked:
                create_link(source / directory, target / directory, True)
                linked.add(directory)
            elif not directory:
                create_link(path, target / key, False)
            record["link"] = directory or str(path)
            linked_files += 1
        else:
            destination = target / key
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
            record["sha256"] = checksum.hexdigest()
            copied_files += 1
            copied_bytes += before.st_size
        records.append(record)
    return dict(files=records, bytes=copied_bytes, copied_files=copied_files,
                linked_files=linked_files, linked_directories=len(linked))
