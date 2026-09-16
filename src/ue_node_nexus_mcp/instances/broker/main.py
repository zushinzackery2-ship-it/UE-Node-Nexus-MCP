"""User-level lifecycle service, independent of any MCP host process."""

from __future__ import annotations

import argparse
import json
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path

from ...coordination.file_lock import FileLock, LockBusy
from ...coordination.pipe_server import PipeServer
from ..identity.paths import pipe_address, runtime_root
from ..lifecycle.platform import WindowsPlatform
from ..errors import InstanceError
from .registry import atomic_json
from .service import BrokerService


class EventFormatter(logging.Formatter):
    def format(self, record):
        text = super().format(record)
        if len(text) <= 16384 and not record.exc_info:
            try:
                event = json.loads(text)
                if isinstance(event, dict) and event.get("event"):
                    return json.dumps(event, ensure_ascii=False, separators=(",", ":"))
            except ValueError:
                pass
        return json.dumps(dict(event="manager_diagnostic", level=record.levelname,
                               time=record.created, message=text[:16384]), ensure_ascii=False)


def serve(root: Path, platform_factory=WindowsPlatform) -> None:
    logs = root / "Logs"
    logs.mkdir(parents=True, exist_ok=True)
    handler = RotatingFileHandler(logs / "lifecycle.jsonl", maxBytes=10 * 1024 * 1024, backupCount=3, encoding="utf-8")
    handler.setFormatter(EventFormatter())
    logger = logging.getLogger("ue_node_nexus_mcp")
    logger.addHandler(handler)
    logger.setLevel(logging.INFO)
    with FileLock(root / "manager.lock", holder=dict(role="broker")):
        service = BrokerService(root, platform_factory(root))
        atomic_json(root / "manager.json", dict(identity=service.identity, manager_epoch=service.epoch,
                                                manager_secret=service.secret, protocol=1))
        server = PipeServer(pipe_address(root), service.dispatch).start()
        try:
            service.reconcile()
            while not server.stopped.wait(0.2):
                service.tick()
                if service.idle():
                    break
        except Exception:
            logger.exception("manager loop stopped unexpectedly")
            raise
        finally:
            server.close()
            service.editors.workers.shutdown(wait=True, cancel_futures=True)
            service.platform.close()
            service.event("manager_stopped")
            service.registry.close()
            handler.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path)
    parser.add_argument("--detached", action="store_true", help="Launch outside the caller's process Job")
    args = parser.parse_args()
    root = (args.runtime_dir or runtime_root()).resolve()
    try:
        if args.detached:
            from .detached import launch
            command = json.loads((root / "launcher.json").read_text(encoding="utf-8"))["command"]
            launch([argument for argument in command if argument != "--detached"], root)
        else:
            serve(root)
    except LockBusy:
        return
    except InstanceError as error:
        atomic_json(root / "launch-error.json", error.envelope())
        raise


if __name__ == "__main__":
    main()
