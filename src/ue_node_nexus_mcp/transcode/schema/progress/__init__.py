"""Bounded schema progress, shared by collection and immutable catalog publication."""

from contextlib import contextmanager
from contextvars import ContextVar
import logging
from time import monotonic

from ...storage.metrics import IOMetrics, capture
from ...progress import emit

_current = ContextVar("nexus_schema_progress", default=None)
_log = logging.getLogger("ue_nexus.schema")
INTERVAL_SECONDS = 0.5


class SchemaProgress:
    def __init__(self, key):
        self.key = key
        self.started = monotonic()
        self.last = -float("inf")
        self.phase = "collection"
        self.processed = 0
        self.total = None
        self.family = None
        self.io = IOMetrics()

    def begin(self, phase, total=None):
        self.phase, self.total = phase, total
        self.processed, self.family = 0, None
        self.emit(force=True)

    def advance(self, family=None, force=False):
        self.processed += 1
        self.family = family
        self.emit(force=force)

    def emit(self, force=False):
        now = monotonic()
        if not force and now - self.last < INTERVAL_SECONDS:
            return
        progress = dict(schema_key=self.key, phase=self.phase, processed=self.processed,
                        total=self.total, family=self.family, elapsed_seconds=max(0.0, now - self.started),
                        io=self.io.report())
        emit("schema_" + self.phase, progress)
        _log.info("schema_progress %s", progress)
        self.last = now


@contextmanager
def scope(key):
    current = _current.get()
    if current is not None:
        current.key = key
        yield current
        return
    current = SchemaProgress(key)
    token = _current.set(current)
    try:
        with capture(current.io):
            yield current
    finally:
        _current.reset(token)
