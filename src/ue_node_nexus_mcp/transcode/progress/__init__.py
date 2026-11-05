"""Scoped observation callbacks without coupling domain code to job orchestration."""

from contextlib import contextmanager
from contextvars import ContextVar

_listener = ContextVar("nexus_progress_listener", default=None)
_deferred = ContextVar("nexus_progress_cancellation_deferred", default=False)


@contextmanager
def atomic():
    """Report progress while deferring cancellation until a transaction is recorded."""
    token = _deferred.set(True)
    try:
        yield
    finally:
        _deferred.reset(token)


def cancellation_deferred():
    return _deferred.get()


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

