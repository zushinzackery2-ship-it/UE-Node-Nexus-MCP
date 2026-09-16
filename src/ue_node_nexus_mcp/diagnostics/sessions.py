"""Reserve bounded log space per live MCP; retain only recent inactive sessions."""

import json
import os
from pathlib import Path
import re
import time
import uuid

from ..coordination.file_lock import FileLock
from ..instances.identity.processes import inspect_process, is_alive
from ..instances.errors import InstanceError
from ..instances.broker.registry import atomic_json

MAX_BYTES = 1024 * 1024
RESERVED_BYTES = 5 * 1024 * 1024
TOTAL_BYTES = 256 * 1024 * 1024
RETENTION_SECONDS = 7 * 86400


def allocate(directory: Path) -> Path:
    directory = directory.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    with FileLock(directory / "retention.lock", timeout=15):
        reserved, active, unknown = RESERVED_BYTES, 0, 0
        retired = []
        for name, files in groups(directory).items():
            record = directory / (name + ".json")
            metadata = read_metadata(record)
            alive = owner_alive(name, metadata)
            size = sum(file.stat().st_size for file in files)
            if alive is not False:
                active += 1
                unknown += alive is None
                # Pre-upgrade live writers used a larger rotation reservation.
                reserved += max(size, metadata.get("reserved_bytes", 9 * 1024 * 1024))
                continue
            stamp = max(file.stat().st_mtime for file in files)
            retired.append((stamp, size, files))
        if reserved > TOTAL_BYTES:
            raise InstanceError("log_capacity_exceeded", "too many live MCP log reservations", dict(active_sessions=active))
        used = reserved + sum(item[1] for item in retired)
        removed = 0
        for stamp, size, files in sorted(retired, key=lambda row: row[0]):
            if time.time() - stamp < RETENTION_SECONDS and used <= TOTAL_BYTES:
                continue
            for path in files:
                path.resolve().relative_to(directory.resolve())
                path.unlink(missing_ok=True)
            used -= size
            removed += 1
        name = f"session-{os.getpid()}-{uuid.uuid4().hex}"
        metadata = dict(identity=inspect_process(os.getpid()), created_at=time.time(), reserved_bytes=RESERVED_BYTES)
        atomic_json(directory / (name + ".json"), metadata)
        atomic_json(directory / "retention-status.json", dict(active=active + 1, unverified=unknown,
                                                              removed=removed, reserved_bytes=reserved, budget_bytes=TOTAL_BYTES))
        return directory / (name + ".log")


def groups(directory: Path) -> dict:
    result = dict()
    for path in directory.iterdir():
        match = re.fullmatch(r"(session-\d+-[a-f0-9]+)(?:\.json|\.log(?:\.\d+)?)", path.name)
        if match and path.is_file():
            result.setdefault(match[1], []).append(path)
    return result


def read_metadata(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(data, dict) and isinstance(data.get("reserved_bytes", 0), int):
            return data
    except (OSError, ValueError):
        pass
    return dict()


def owner_alive(name: str, metadata: dict) -> bool | None:
    try:
        identity = metadata.get("identity")
        if isinstance(identity, dict) and all(key in identity for key in ("pid", "process_created", "executable")):
            return is_alive(identity)
        pid = int(name.split("-")[1])
        return None if inspect_process(pid) else False
    except (OSError, TypeError, ValueError):
        return None
