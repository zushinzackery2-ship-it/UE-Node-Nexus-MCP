"""Workflow-local observations, including jobs running on reused worker threads."""

from contextlib import contextmanager
from contextvars import ContextVar
from dataclasses import dataclass, field
from functools import wraps
from threading import get_ident

from .notice import normalize
from .response import finalize

_current = ContextVar("nexus_diagnostic_observation", default=None)


@dataclass
class Observation:
    last: dict | None = None
    thread_id: int = field(default_factory=get_ident)

    def record(self, response: dict) -> None:
        if not isinstance(response, dict):
            return
        notice = response.get("runtime_diagnostics")
        if isinstance(notice, dict) and notice.get("session_id"):
            self.last = normalize(notice)

    def fields(self) -> dict:
        return dict(runtime_diagnostics=dict(self.last), runtime_observed=True) if self.last else dict(runtime_observed=False)


@contextmanager
def capture():
    observed = Observation()
    parent = _current.get()
    token = _current.set(observed)
    try:
        yield observed
    finally:
        _current.reset(token)
        if parent is not None and parent.thread_id == observed.thread_id and observed.last is not None:
            parent.last = observed.last


def record(response: dict) -> dict:
    current = _current.get()
    if current is not None:
        current.record(response)
    return response


def fields() -> dict:
    current = _current.get()
    return current.fields() if current is not None else dict(runtime_observed=False)


def tool_wrapper(function):
    @wraps(function)
    def invoke(*args, **kwargs):
        with capture() as observed:
            result = function(*args, **kwargs)
            return finalize(result, function.__name__, observed.last)
    return invoke
