"""Scoped observation callbacks without coupling domain code to job orchestration."""

from contextlib import contextmanager
from contextvars import ContextVar

_listener = ContextVar("nexus_progress_listener", default=None)


@contextmanager
def listen(callback):
    token = _listener.set(callback)
    try:
        yield
    finally:
        _listener.reset(token)


def emit(stage, details):
    callback = _listener.get()
    if callback is not None:
        callback(stage, details)

