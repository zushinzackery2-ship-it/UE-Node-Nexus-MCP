"""Process lifetime checks use creation identity, never PID alone."""

from __future__ import annotations

import os
from pathlib import Path


def inspect_process(pid: int) -> dict | None:
    if os.name == "nt":
        from .windows import process_info
        return process_info(pid)
    root = Path("/proc") / str(pid)
    try:
        fields = (root / "stat").read_text().rsplit(")", 1)[1].split()
        if fields[0] == "Z":
            return None
        return dict(pid=pid, process_created=fields[19], executable=str((root / "exe").resolve(strict=True)))
    except FileNotFoundError:
        return None


def is_alive(identity: dict) -> bool:
    current = inspect_process(int(identity["pid"]))
    return bool(current and current["process_created"] == identity["process_created"]
                and current["executable"] == identity["executable"])


def user_identity() -> str:
    if os.name == "nt":
        from .windows import user_sid
        return user_sid()
    return str(os.getuid())
