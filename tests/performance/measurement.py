"""Scoped instrumentation and JUnit-compatible measurements."""

import logging
import statistics
import tracemalloc
from contextlib import contextmanager

from ue_node_nexus_mcp.transcode.collaboration.workspace import files as worktree

from .fixtures import FANOUT


@contextmanager
def encodes():
    original, calls = worktree.capture, []
    worktree.capture = lambda *args, **options: (calls.append(args[0]), original(*args, **options))[1]
    try:
        yield calls
    finally:
        worktree.capture = original


@contextmanager
def locks():
    records = []
    handler = logging.Handler()
    handler.emit = lambda record: records.append(record.args)
    logger = logging.getLogger("ue_nexus.lock")
    level = logger.level
    logger.addHandler(handler)
    logger.setLevel(logging.INFO)
    try:
        yield records
    finally:
        logger.removeHandler(handler)
        logger.setLevel(level)
        handler.close()


@contextmanager
def peak_bytes(result: list):
    tracing = tracemalloc.is_tracing()
    tracemalloc.start()
    tracemalloc.reset_peak()
    before = tracemalloc.get_traced_memory()[0]
    try:
        yield
    finally:
        result.append(tracemalloc.get_traced_memory()[1] - before)
        if not tracing:
            tracemalloc.stop()


def report(project, peaks: dict, record_testsuite_property):
    measured = dict(entities=project.entities,
                    relations=(project.entities - max(project.entities // 10, FANOUT)) * FANOUT,
                    exported_assets=len(project.ue.exported), applied_assets=len(project.ue.applied),
                    cold_checkout_seconds=round(project.checkout_seconds, 3), **peaks)
    for label, samples in sorted(project.timings.items()):
        measured[f"{label}_median_seconds"] = round(statistics.median(samples), 4)
        measured[f"{label}_max_seconds"] = round(max(samples), 4)
    for name, value in measured.items():
        record_testsuite_property(f"scale_{project.entities}.{name}", value)
    print("\nscale report: " + "  ".join(f"{name}={value}" for name, value in measured.items()))
