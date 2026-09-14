"""Derived capture memo: never a source of truth, always cheap to discard."""

from __future__ import annotations

import json
from pathlib import Path

from ..store.io import atomic_write, canonical

FORMAT = 1


def location(workspace) -> Path:
    return workspace.root.parent / "captures.json"


def load(workspace) -> dict[str, list]:
    """Every entry carries the inputs that produced it, so a stale file misses.

    The memo lives outside the generation-checked workspace record on purpose: a
    reader may refresh it, two processes may race it, and the worst outcome is
    the work it was meant to save.
    """
    path = location(workspace)
    try:
        stored = json.loads(path.read_bytes())
    except (OSError, ValueError):
        return dict()
    if not isinstance(stored, dict) or stored.get("format") != FORMAT or stored.get("workspace") != workspace.state["id"]:
        return dict()
    entries = stored.get("entries")
    if not isinstance(entries, dict):
        return dict()
    return dict((asset, value) for asset, value in entries.items() if isinstance(value, list) and len(value) == 3)


def save(workspace, entries: dict[str, list]) -> None:
    atomic_write(location(workspace), canonical(dict(format=FORMAT, workspace=workspace.state["id"], entries=entries)))
