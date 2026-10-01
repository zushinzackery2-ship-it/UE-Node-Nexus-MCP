"""Field-level schema for asset-local centimetre geometry recipes."""

from __future__ import annotations

from typing import Any


def vector(**items: Any) -> dict[str, Any]:
    return dict(type="array", minItems=3, maxItems=3, items=dict(type="number", **items))


def obj(required: list[str], **properties: Any) -> dict[str, Any]:
    return dict(type="object", required=required, additionalProperties=False, properties=properties)


def recipe_schema() -> dict[str, Any]:
    mask = obj(["center", "radius"], center=vector(), radius=vector(exclusiveMinimum=0), invert=dict(type="boolean"))
    common = dict(lock_boundary=dict(type="boolean"), mask=mask)
    lattice = obj(["op", "dimensions", "offsets"], op=dict(const="lattice"),
                  dimensions=dict(type="array", minItems=3, maxItems=3, items=dict(type="integer", minimum=2, maximum=32)),
                  offsets=dict(type="array", maxItems=32768,
                               items=obj(["index", "delta"], index=vector(minimum=0, multipleOf=1), delta=vector())),
                  interpolation=dict(type="string", enum=["linear", "cubic"]), **common)
    noise = obj(["op"], op=dict(const="noise"), amplitude=dict(type="number", minimum=-1e6, maximum=1e6),
                scale=dict(type="number", minimum=0.01), octaves=dict(type="integer", minimum=1, maximum=8),
                seed=dict(type="integer", minimum=-2147483647, maximum=2147483647),
                persistence=dict(type="number", minimum=0, maximum=1), sampling_offset=vector(),
                direction=dict(type="string", enum=["z", "normal"]), **common)
    remesh = obj(["op", "target_edge_length"], op=dict(const="remesh"),
                 target_edge_length=dict(type="number", exclusiveMinimum=0),
                 iterations=dict(type="integer", minimum=1, maximum=20), automatic=dict(type="boolean"),
                 lock_boundary=dict(type="boolean"), region=obj(["min", "max"], min=vector(), max=vector()))
    result = obj(["ops"], source_asset=dict(type="string"), source_revision=dict(type="string", minLength=1),
                 grid=obj(["size", "cells"], size=vector(minimum=0), cells=vector(minimum=0, maximum=512, multipleOf=1)),
                 max_triangles=dict(type="integer", minimum=2, maximum=500000),
                 ops=dict(type="array", maxItems=32, items=dict(oneOf=[lattice, remesh, noise])))
    result["oneOf"] = [dict(required=["grid"], **{"not": dict(anyOf=[dict(required=["source_asset"]), dict(required=["source_revision"])])}),
                       dict(required=["source_asset", "source_revision"], **{"not": dict(required=["grid"])})]
    return result


def augment(operation: str, schema: dict[str, Any]) -> None:
    fields = schema.get("properties", dict())
    if operation == "mesh_geometry_get":
        fields["lattice_dimensions"].update(minItems=3, maxItems=3, items=dict(type="integer", minimum=2, maximum=32))
    elif operation == "mesh_geometry_build":
        fields["output_asset"].update(pattern=r"^/Game/[A-Za-z0-9_/]+$", description="New package path; same recipe retries reuse an unchanged output")
        fields["recipe"] = recipe_schema()


def examples() -> dict[str, Any]:
    return dict(mesh_geometry_get=dict(asset_path="/Game/Terrain/Base", lattice_dimensions=[3, 3, 2]),
                mesh_geometry_build=dict(output_asset="/Game/Terrain/Hill", dry_run=True, save=False,
                                         recipe=dict(grid=dict(size=[4000, 4000, 0], cells=[32, 32, 0]), ops=[
                                             dict(op="noise", amplitude=120, scale=1300, seed=42, lock_boundary=True)])))
