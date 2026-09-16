"""Serialize one logical operation without holding a catalog lock during I/O."""

from contextlib import contextmanager
import threading


class KeyedLocks:
    def __init__(self) -> None:
        self.mutex = threading.Lock()
        self.entries = dict()

    @contextmanager
    def hold(self, key: str):
        with self.mutex:
            entry = self.entries.setdefault(key, [threading.RLock(), 0])
            entry[1] += 1
        try:
            with entry[0]:
                yield
        finally:
            with self.mutex:
                entry[1] -= 1
                if not entry[1]:
                    self.entries.pop(key)
