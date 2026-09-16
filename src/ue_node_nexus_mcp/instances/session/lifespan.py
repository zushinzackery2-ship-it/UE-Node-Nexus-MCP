"""MCP EOF/cancellation cleanup and explicit startup configuration."""

from contextlib import asynccontextmanager
import sys

from . import instance_manager
from .config import consume_arguments


def configure() -> None:
    project, remaining = consume_arguments(sys.argv)
    sys.argv[:] = remaining
    if project:
        instance_manager.configured_project = project


@asynccontextmanager
async def lifespan(_server):
    instance_manager.start()
    try:
        yield dict()
    finally:
        from ...task_queue import shutdown
        shutdown()
        instance_manager.close()
