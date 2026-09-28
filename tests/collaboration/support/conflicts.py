"""Shared fixtures used across the regression suites."""

from __future__ import annotations
from tests.collaboration.support.divergence import MATERIAL, call, change


def diverged(project, *assets):
    """Two workspaces that changed the same values, so a push has to merge."""
    ue, env, first, second, store = project
    for asset in assets or (MATERIAL,):
        change(first, asset, "Constant(R=0.000001)", "Constant(R=0.04)")
        change(second, asset, "Constant(R=0.000001)", "Constant(R=0.07)")
    call(project, "commit", first, all=True, message="A")
    call(project, "commit", second, all=True, message="B")
    assert call(project, "push", first)["status"] == "published"
    return first, second
