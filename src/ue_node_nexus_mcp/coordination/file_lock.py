"""A byte-range OS lock with a readable owner record and bounded waiting."""

from __future__ import annotations

import errno
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
            try:
                self._file = os.fdopen(descriptor, "r+b", buffering=0)
            except BaseException:
                os.close(descriptor)
                raise
            # Windows permits locking a byte beyond EOF. Never initialise the
            # shared byte before locking: another process may already own it.
            while True:
                try:
                    self._lock()
                    break
                except OSError as exc:
                    if exc.errno not in (errno.EACCES, errno.EAGAIN, errno.EDEADLK):
                        raise
                    if time.monotonic() - started >= self.timeout:
                        raise LockBusy(self.describe()) from exc
                    time.sleep(min(0.02, max(0, self.timeout - (time.monotonic() - started))))
            self._write_holder()
        except BaseException:
            try:
                self._close()
            except OSError:
                pass  # Preserve the acquisition error; close releases OS locks.
            finally:
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

    def _close(self) -> None:
        stream, self._file = self._file, None
        if stream is not None:
            stream.close()

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        cleanup_error = None
        try:
            self._write_holder(clear=True)
            self._file.seek(0)
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(self._file.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl
                fcntl.flock(self._file.fileno(), fcntl.LOCK_UN)
        except OSError as error:
            cleanup_error = error
        finally:
            self.held = time.monotonic() - self._acquired_at
            try:
                self._close()
            except OSError as error:
                cleanup_error = cleanup_error or error
            finally:
                self._mutex.release()
        if cleanup_error is not None and exc_type is None:
            raise cleanup_error
