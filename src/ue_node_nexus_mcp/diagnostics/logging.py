"""Bounded UTF-8 request logs on stderr and disk."""

from __future__ import annotations

import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path


def configure_logging() -> Path:
    parent = Path(os.environ.get("LOCALAPPDATA") or (Path.home() / ".cache"))
    directory = Path(os.environ.get("UE_NEXUS_LOG_DIR") or (parent / "UE-Node-Nexus-MCP" / "Logs"))
    directory.mkdir(parents=True, exist_ok=True)
    log = logging.getLogger("ue_node_nexus_mcp")
    if not any(getattr(handler, "nexus_owned", False) for handler in log.handlers):
        formatter = logging.Formatter("%(asctime)s %(levelname)s %(name)s %(message)s")
        for handler in (logging.StreamHandler(), RotatingFileHandler(directory / "bridge.log", maxBytes=10 * 1024 * 1024, backupCount=3, encoding="utf-8")):
            handler.setFormatter(formatter)
            handler.nexus_owned = True
            log.addHandler(handler)
        log.setLevel(logging.INFO)
        log.propagate = False
    return directory
