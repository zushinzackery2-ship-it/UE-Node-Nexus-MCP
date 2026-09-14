"""One transaction owner per mirror root, across threads and server processes."""

from __future__ import annotations

import json
import logging
import os
import socket
import time
from pathlib import Path
from threading import Lock
from typing import BinaryIO
from weakref import WeakValueDictionary

from ..sync_project import SyncError

LOG = logging.getLogger("ue_nexus.lock")
POLL_SECONDS = 0.02
# Byte 0 carries the OS lock; the holder record lives past it so a blocked
# process can read who owns the transaction without touching the locked range.
HOLDER_OFFSET = 1
HOLDER_BYTES = 512

_REGISTRY_LOCK = Lock()
_LOCKS: WeakValueDictionary = WeakValueDictionary()


class MirrorLock:
    def __init__(self, root: Path, lock_file: Path | None = None, timeout: float = 0.0,
                 holder: dict | None = None) -> None:
        self.root = root.resolve()
        self.path = (lock_file or self.root / ".nexus" / "sync.lock").resolve()
        self.timeout = max(0.0, float(timeout))
        self.holder = dict(holder or dict())
        self.waited = 0.0
        self.held = 0.0
        self._file: BinaryIO | None = None
        self._acquired_at = 0.0
        key = os.path.normcase(str(self.path))
        with _REGISTRY_LOCK:
            mutex = _LOCKS.get(key)
            if mutex is None:
                mutex = Lock()
                _LOCKS[key] = mutex
            self._mutex = mutex

    def describe(self) -> dict:
        """Current owner as last recorded; empty after a clean release."""
        record: dict = dict()
        try:
            with self.path.open("rb") as stream:
                stream.seek(HOLDER_OFFSET)
                raw = stream.read(HOLDER_BYTES).split(b"\0", 1)[0]
            if raw:
                record = json.loads(raw.decode("ascii"))
        except (OSError, ValueError, UnicodeDecodeError):
            record = dict()
        return dict(lock=str(self.path), holder=record)

    def __enter__(self) -> "MirrorLock":
        started = time.monotonic()
        deadline = started + self.timeout
        acquired = self._mutex.acquire(True, self.timeout) if self.timeout else self._mutex.acquire(False)
        if not acquired:
            self.waited = time.monotonic() - started
            raise SyncError("sync_busy", f"another mirror transaction owns {self.root}", self.describe())
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            self._file = self._open()
            while True:
                try:
                    self._lock_file()
                    break
                except OSError as exc:
                    if time.monotonic() >= deadline:
                        raise SyncError("sync_busy", f"another server owns the mirror transaction: {self.root}", self.describe()) from exc
                    time.sleep(POLL_SECONDS)
            self._write_holder()
        except BaseException as exc:
            if self._file is not None:
                self._file.close()
            self.waited = time.monotonic() - started
            self._mutex.release()
            if isinstance(exc, OSError):
                raise SyncError("mirror_lock_failed", str(exc)) from exc
            raise
        self.waited = time.monotonic() - started
        self._acquired_at = time.monotonic()
        return self

    def _open(self) -> BinaryIO:
        descriptor = os.open(self.path, os.O_RDWR | os.O_CREAT, 0o644)
        stream = os.fdopen(descriptor, "r+b")
        stream.seek(0, os.SEEK_END)
        if stream.tell() == 0:
            stream.write(b"\0" * (HOLDER_OFFSET + HOLDER_BYTES))
            stream.flush()
        return stream

    def _lock_file(self) -> None:
        assert self._file is not None
        self._file.seek(0)
        if os.name == "nt":
            import msvcrt

            msvcrt.locking(self._file.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl

            fcntl.flock(self._file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)

    def _write_holder(self, clear: bool = False) -> None:
        assert self._file is not None
        payload = b""
        if not clear:
            record = dict(self.holder, pid=os.getpid(), host=socket.gethostname(), acquired_at=time.time())
            # ASCII keeps every record whole: a budget check can never split a codepoint.
            payload = json.dumps(record, ensure_ascii=True, sort_keys=True).encode("ascii")
            if len(payload) >= HOLDER_BYTES:
                record = dict(action=str(self.holder.get("action", ""))[:64], pid=record["pid"],
                              host=record["host"], acquired_at=record["acquired_at"])
                payload = json.dumps(record, ensure_ascii=True, sort_keys=True).encode("ascii")
        self._file.seek(HOLDER_OFFSET)
        self._file.write(payload.ljust(HOLDER_BYTES, b"\0"))
        self._file.flush()

    def __exit__(self, *_: object) -> None:
        assert self._file is not None
        try:
            self._write_holder(clear=True)
            self._file.seek(0)
            if os.name == "nt":
                import msvcrt

                msvcrt.locking(self._file.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl

                fcntl.flock(self._file.fileno(), fcntl.LOCK_UN)
        finally:
            self.held = time.monotonic() - self._acquired_at
            self._file.close()
            self._mutex.release()
            LOG.info("lock %s waited=%.4f held=%.4f %s", self.path.name, self.waited, self.held,
                     json.dumps(self.holder, ensure_ascii=False, sort_keys=True))
