"""Request-scoped timings for the actual durable file operations."""

from contextlib import contextmanager
from contextvars import ContextVar
from dataclasses import dataclass, field
from time import perf_counter

_active = ContextVar("nexus_storage_metrics", default=None)


@dataclass
class IOMetrics:
    seconds: dict = field(default_factory=dict)
    writes: int = 0
    bytes: int = 0

    def report(self):
        return dict(writes=self.writes, bytes=self.bytes,
                    **dict((phase + "_seconds", self.seconds.get(phase, 0.0))
                           for phase in ("create", "lookup", "write", "flush", "replace")))


@contextmanager
def capture(metrics):
    token = _active.set(metrics)
    try:
        yield metrics
    finally:
        _active.reset(token)


@contextmanager
def measure(phase):
    metrics = _active.get()
    if metrics is None:
        yield
        return
    started = perf_counter()
    try:
        yield
    finally:
        metrics.seconds[phase] = metrics.seconds.get(phase, 0.0) + perf_counter() - started


def written(size):
    metrics = _active.get()
    if metrics is not None:
        metrics.writes += 1
        metrics.bytes += size

