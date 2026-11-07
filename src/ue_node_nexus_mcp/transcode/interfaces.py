"""Use the same candidate interfaces during capture, lint, merge and planning."""

from contextlib import contextmanager

from .blueprint import interfaces as blueprint
from .material import interfaces as material


@contextmanager
def documents(items):
    candidates = list(items)
    with material.documents(candidates), blueprint.documents(candidates):
        yield


def fingerprint():
    return material.fingerprint() + blueprint.fingerprint()
