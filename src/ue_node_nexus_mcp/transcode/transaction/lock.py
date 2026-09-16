"""Mirror-specific errors and diagnostics over the shared OS lock."""

from __future__ import annotations

import json
import logging
from pathlib import Path

from ...coordination.file_lock import FileLock, LockBusy
from ..sync_project import SyncError

LOG = logging.getLogger("ue_nexus.lock")


class MirrorLock(FileLock):
    def __init__(self, root: Path, lock_file: Path | None = None, timeout: float = 0.0,
                 holder: dict | None = None) -> None:
        self.root = root.resolve()
        super().__init__(lock_file or self.root / ".nexus" / "sync.lock", timeout, holder)

    def __enter__(self) -> MirrorLock:
        try:
            super().__enter__()
        except LockBusy as exc:
            raise SyncError("sync_busy", f"another transaction owns {self.root}", exc.details) from exc
        except OSError as exc:
            raise SyncError("mirror_lock_failed", str(exc)) from exc
        return self

    def __exit__(self, *args: object) -> None:
        super().__exit__(*args)
        LOG.info("lock %s waited=%.4f held=%.4f %s", self.path.name, self.waited, self.held,
                 json.dumps(self.holder, ensure_ascii=False, sort_keys=True))
