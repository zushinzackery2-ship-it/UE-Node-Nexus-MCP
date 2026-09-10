"""Typed wrappers for high-volume ISM/HISM editing."""

from __future__ import annotations

from typing import Any

from .runtime import call_bridge, default_tool


@default_tool()
def component_instances_get(component_path: str, offset: int = 0, limit: int = 1000, revision: str | None = None) -> dict[str, Any]:
    """Read a paged ISM or HISM instance set; pass the returned revision for later pages."""
    return call_bridge("component_instances_get", {"component_path": component_path, "offset": offset, "limit": limit, "revision": revision or ""})


@default_tool()
def component_instances_patch(component_path: str, revision: str, ops: list[dict[str, Any]], custom_data_count: int | None = None, dry_run: bool = True, save: bool = False) -> dict[str, Any]:
    """Batch add/update/remove ISM or HISM instances against a revision token."""
    payload: dict[str, Any] = dict(component_path=component_path, revision=revision, ops=ops, dry_run=dry_run, save=save)
    if custom_data_count is not None:
        payload["custom_data_count"] = custom_data_count
    return call_bridge("component_instances_patch", payload)
