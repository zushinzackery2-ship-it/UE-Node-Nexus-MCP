"""One transaction owner per mirror root, across threads and server processes."""

from __future__ import annotations

import os
from pathlib import Path
from threading import Lock
from typing import BinaryIO
from weakref import WeakValueDictionary

from ..sync_project import SyncError

_REGISTRY_LOCK = Lock()
_LOCKS: WeakValueDictionary = WeakValueDictionary()


class MirrorLock:
    def __init__(self, root: Path, lock_file: Path | None = None) -> None:
        self.root = root.resolve()
        self.path = (lock_file or self.root / ".nexus" / "sync.lock").resolve()
        self._file: BinaryIO | None = None
        key = os.path.normcase(str(self.path))
        with _REGISTRY_LOCK:
            mutex = _LOCKS.get(key)
            if mutex is None:
                mutex = Lock()
                _LOCKS[key] = mutex
            self._mutex = mutex

    def __enter__(self) -> "MirrorLock":
        if not self._mutex.acquire(blocking=False):
            raise SyncError("sync_busy", f"another mirror transaction owns {self.root}")
        try:
            directory = self.path.parent
            directory.mkdir(parents=True, exist_ok=True)
            self._file = self.path.open("a+b")
            if self._file.tell() == 0:
                self._file.write(b"\0")
                self._file.flush()
            self._file.seek(0)
            try:
                self._lock_file()
            except OSError as exc:
                raise SyncError("sync_busy", f"another server owns the mirror transaction: {self.root}") from exc
        except BaseException as exc:
            if self._file is not None:
                self._file.close()
            self._mutex.release()
            if isinstance(exc, OSError):
                raise SyncError("mirror_lock_failed", str(exc)) from exc
            raise
        return self

    def _lock_file(self) -> None:
        assert self._file is not None
        if os.name == "nt":
            import msvcrt

            msvcrt.locking(self._file.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl

            fcntl.flock(self._file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)

    def __exit__(self, *_: object) -> None:
        assert self._file is not None
        try:
            self._file.seek(0)
            if os.name == "nt":
                import msvcrt

                msvcrt.locking(self._file.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl

                fcntl.flock(self._file.fileno(), fcntl.LOCK_UN)
        finally:
            self._file.close()
            self._mutex.release()
