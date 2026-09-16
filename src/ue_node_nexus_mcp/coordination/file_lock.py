"""A byte-range OS lock with a readable owner record and bounded waiting."""

from __future__ import annotations

import json
import os
import socket
import time
from pathlib import Path
from threading import Lock
from weakref import WeakValueDictionary

_registry_lock = Lock()
_locks = WeakValueDictionary()
HOLDER_BYTES = 4096


class LockBusy(RuntimeError):
    def __init__(self, details: dict) -> None:
        super().__init__(f"lock is held: {details['lock']}")
        self.details = details


class FileLock:
    def __init__(self, path: Path, timeout: float = 0, holder: dict | None = None) -> None:
        self.path = path.resolve()
        self.timeout = max(0.0, float(timeout))
        self.holder = dict(holder or dict())
        self.waited = self.held = 0.0
        self._file = None
        self._acquired_at = 0.0
        with _registry_lock:
            key = os.path.normcase(str(self.path))
            mutex = _locks.get(key)
            if mutex is None:
                mutex = Lock()
                _locks[key] = mutex
            self._mutex = mutex

    def describe(self) -> dict:
        record = dict()
        try:
            with self.path.open("rb") as stream:
                stream.seek(1)
                raw = stream.read(HOLDER_BYTES).split(b"\0", 1)[0]
            if raw:
                record = json.loads(raw.decode("ascii"))
        except (OSError, ValueError, UnicodeDecodeError):
            pass
        return dict(lock=str(self.path), holder=record)

    def __enter__(self) -> FileLock:
        started = time.monotonic()
        acquired = self._mutex.acquire(True, self.timeout) if self.timeout else self._mutex.acquire(False)
        if not acquired:
            raise LockBusy(self.describe())
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            descriptor = os.open(self.path, os.O_RDWR | os.O_CREAT, 0o600)
            self._file = os.fdopen(descriptor, "r+b")
            self._file.seek(0, os.SEEK_END)
            if self._file.tell() == 0:
                self._file.write(b"\0")
                self._file.flush()
            while True:
                try:
                    self._lock()
                    break
                except OSError as exc:
                    if time.monotonic() - started >= self.timeout:
                        raise LockBusy(self.describe()) from exc
                    time.sleep(min(0.02, max(0, self.timeout - (time.monotonic() - started))))
            self._write_holder()
        except BaseException:
            if self._file is not None:
                self._file.close()
                self._file = None
            self._mutex.release()
            raise
        finally:
            self.waited = time.monotonic() - started
        self._acquired_at = time.monotonic()
        return self

    def _lock(self) -> None:
        self._file.seek(0)
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(self._file.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(self._file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)

    def _write_holder(self, clear: bool = False) -> None:
        record = dict(self.holder, pid=os.getpid(), host=socket.gethostname(), acquired_at=time.time())
        payload = b"" if clear else json.dumps(record, ensure_ascii=True, sort_keys=True).encode("ascii")
        if len(payload) >= HOLDER_BYTES:
            raise ValueError("lock owner metadata exceeds its budget")
        self._file.seek(1)
        self._file.write(payload.ljust(HOLDER_BYTES, b"\0"))
        self._file.flush()

    def __exit__(self, *_: object) -> None:
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
            self._file = None
            self._mutex.release()
