"""Fixed-TTL admission order and independent LRU capacity, synchronized together."""

from collections import OrderedDict
from dataclasses import dataclass
import logging
import threading
import time

from ..instances.errors import InstanceError
from .footprint import retained_bytes

LOG = logging.getLogger(__name__)


@dataclass
class Entry:
    value: object
    deadline: float
    weight: int


class Retention:
    def __init__(self, ttl: float, max_entries: int, max_bytes: int, clock=None):
        if ttl <= 0 or max_entries <= 0 or max_bytes <= 0:
            raise ValueError("facade retention TTL and capacities must be positive")
        self.ttl, self.max_entries, self.max_bytes = ttl, max_entries, max_bytes
        self.clock = clock or time.monotonic
        self.entries, self.lru = OrderedDict(), OrderedDict()
        self.bytes = 0
        self.hits = self.misses = self.expired = self.evicted = 0
        self.lock = threading.RLock()

    def remove(self, key: str) -> None:
        entry = self.entries.pop(key)
        self.bytes -= entry.weight
        self.lru.pop(key)

    def prune(self) -> None:
        with self.lock:
            now = self.clock()
            while self.entries:
                key = next(iter(self.entries))
                if self.entries[key].deadline > now:
                    break
                self.remove(key)
                self.expired += 1

    def put(self, key: str, value) -> None:
        weight = retained_bytes(value)
        if weight > self.max_bytes:
            raise InstanceError("facade_cache_capacity_exceeded", "record exceeds the facade evidence cache capacity",
                                dict(required_bytes=weight, max_bytes=self.max_bytes, token=key))
        with self.lock:
            self.prune()
            if key in self.entries:
                self.remove(key)
            while self.lru and (len(self.entries) >= self.max_entries or self.bytes + weight > self.max_bytes):
                victim = next(iter(self.lru))
                self.remove(victim)
                self.evicted += 1
                LOG.debug("facade evidence evicted token=%s reason=capacity", victim)
            self.entries[key] = Entry(value, self.clock() + self.ttl, weight)
            self.lru[key] = None
            self.bytes += weight

    def get(self, key: str):
        with self.lock:
            self.prune()
            entry = self.entries.get(key)
            if entry is None:
                self.misses += 1
                return None
            self.lru.move_to_end(key)
            self.hits += 1
            return entry.value

    def statistics(self) -> dict:
        with self.lock:
            self.prune()
            return dict(entries=len(self.entries), retained_bytes=self.bytes, max_entries=self.max_entries,
                        max_bytes=self.max_bytes, hits=self.hits, misses=self.misses,
                        expired=self.expired, evicted=self.evicted)
