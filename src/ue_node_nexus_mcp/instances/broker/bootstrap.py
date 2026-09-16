"""Start arbitration is shared by every workspace and uses process identity."""

from __future__ import annotations

import json
from pathlib import Path
import time

from ...coordination.file_lock import FileLock, LockBusy
from ...errors import BridgeError
from ...transport import named_pipe_transport
from ..errors import InstanceError
from ..identity.paths import pipe_address
from ..identity.processes import is_alive
from ..lifecycle.policy import PROTOCOL_VERSION
from .package import install
from .detached import launch


def ping(root: Path) -> dict:
    response = named_pipe_transport.send(pipe_address(root), dict(protocol=PROTOCOL_VERSION, action="ping"), 1)
    if not response.get("ok"):
        raise InstanceError("manager_version_mismatch", "existing manager uses an incompatible protocol")
    return response["data"]


def ensure_manager(root: Path) -> dict:
    try:
        return wait_ready(root, ping(root))
    except BridgeError as exc:
        if isinstance(exc, InstanceError):
            raise
    root.mkdir(parents=True, exist_ok=True)
    with FileLock(root / "start.lock", timeout=20):
        try:
            return wait_ready(root, ping(root))
        except BridgeError as exc:
            if isinstance(exc, InstanceError):
                raise
        path = root / "manager.json"
        existing = json.loads(path.read_text(encoding="utf-8")) if path.is_file() else None
        running = bool(existing and verified_alive(existing["identity"]))
        if not running:
            try:
                with FileLock(root / "manager.lock"):
                    pass
            except LockBusy:
                running = True
        if not running:
            command = install(root)
            launch(command, root)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            try:
                return wait_ready(root, ping(root))
            except BridgeError as exc:
                if isinstance(exc, InstanceError):
                    raise
                time.sleep(0.05)
        raise InstanceError("manager_unavailable", "manager did not become responsive", dict(log_path=str(root / "Logs")))


def wait_ready(root: Path, result: dict) -> dict:
    deadline = time.monotonic() + 30
    while not result.get("ready"):
        if time.monotonic() >= deadline:
            raise InstanceError("manager_recovering", "manager is still reconciling existing processes",
                                dict(manager_epoch=result.get("manager_epoch")))
        time.sleep(0.05)
        result = ping(root)
    return result


def verified_alive(identity: dict) -> bool:
    deadline = time.monotonic() + 2
    while True:
        try:
            return is_alive(identity)
        except OSError as exc:
            if time.monotonic() >= deadline:
                raise InstanceError("manager_unverified", "manager process identity could not be verified",
                                    dict(pid=identity["pid"], winerror=getattr(exc, "winerror", None))) from exc
            time.sleep(0.05)
