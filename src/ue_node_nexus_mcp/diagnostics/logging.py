"""Bounded UTF-8 request logs on stderr and disk."""

from __future__ import annotations

import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
from .sessions import allocate, MAX_BYTES


class BoundedFormatter(logging.Formatter):
    def format(self, record) -> str:
        text = super().format(record)
        return text if len(text) <= 16384 else text[:16384] + " [log entry truncated]"


def configure_logging() -> Path:
    parent = Path(os.environ.get("LOCALAPPDATA") or (Path.home() / ".cache"))
    directory = Path(os.environ.get("UE_NEXUS_LOG_DIR") or (parent / "UE-Node-Nexus-MCP" / "Logs"))
    directory.mkdir(parents=True, exist_ok=True)
    log = logging.getLogger("ue_node_nexus_mcp")
    if not any(getattr(handler, "nexus_owned", False) for handler in log.handlers):
        formatter = BoundedFormatter("%(asctime)s %(levelname)s %(name)s %(message)s")
        destination = allocate(directory / "Sessions")
        for handler in (logging.StreamHandler(), RotatingFileHandler(destination, maxBytes=MAX_BYTES, backupCount=3, encoding="utf-8")):
            handler.setFormatter(formatter)
            handler.nexus_owned = True
            log.addHandler(handler)
            logging.getLogger("ue_nexus.collaboration").addHandler(handler)
        log.setLevel(logging.INFO)
        log.propagate = False
        logging.getLogger("ue_nexus.collaboration").setLevel(logging.INFO)
        logging.getLogger("ue_nexus.collaboration").propagate = False
    return directory
