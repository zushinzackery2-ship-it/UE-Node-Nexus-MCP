"""Mesh inspection and deterministic derivative assets."""

from __future__ import annotations

from typing import Any

from ..runtime import call_bridge, default_tool


@default_tool()
def mesh_geometry_get(asset_path: str, lattice_dimensions: list[int] | None = None) -> dict[str, Any]:
    """Read LOD0 quality, content revision, saved recipe and optional FFD control positions."""
    payload = dict(asset_path=asset_path)
    if lattice_dimensions is not None:
        payload["lattice_dimensions"] = lattice_dimensions
    return call_bridge("mesh_geometry_get", payload)


@default_tool()
def mesh_geometry_build(output_asset: str, recipe: dict[str, Any], dry_run: bool = True, save: bool = False) -> dict[str, Any]:
    """Evaluate grid/source -> lattice/remesh/noise into a new StaticMesh; preserve source and existing outputs."""
    return call_bridge("mesh_geometry_build", dict(output_asset=output_asset, recipe=recipe, dry_run=dry_run, save=save))
