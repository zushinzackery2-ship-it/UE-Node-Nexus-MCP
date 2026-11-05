"""Bounded validation progress shared by copying, worker execution and reconciliation."""

from contextvars import ContextVar
from functools import wraps
import time

from ...transcode.progress import emit

_current = ContextVar("nexus_validation_progress", default=None)


def tracked(function):
    @wraps(function)
    def run(*args, **kwargs):
        token = _current.set(dict(details={}, last=0))
        try:
            return function(*args, **kwargs)
        finally:
            _current.reset(token)
    return run


def advance(phase=None, *, force=False, **details):
    state = _current.get()
    if state is None:
        return
    now = time.monotonic()
    changed = phase is not None and phase != state["details"].get("phase")
    if changed:
        state["details"] = {key: value for key, value in state["details"].items() if key == "validation_id"}
    state["details"].update(details)
    if phase is not None:
        state["details"]["phase"] = phase
    if force or changed or now - state["last"] >= 0.25:
        emit("isolation_" + state["details"].get("phase", "preflight"), dict(state["details"], advanced_at=time.time()))
        state["last"] = now
