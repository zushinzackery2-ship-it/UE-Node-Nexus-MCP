from __future__ import annotations

from copy import deepcopy
import hashlib
import json

from ue_node_nexus_mcp.transcode.scene.model import IDENTITY_TRANSFORM

MAP = "/Game/Maps/World"
ACTOR = "11111111111111111111111111111111"
COMPONENT = "22222222222222222222222222222222"
INSTANCE_A = "33333333333333333333333333333333"
INSTANCE_B = "44444444444444444444444444444444"
SCHEMA = "5.5.4-abcd1234"


def transform(x: int) -> str:
    return IDENTITY_TRANSFORM.replace("Translation=(X=0,", f"Translation=(X={x},")


def component(identifier: str = COMPONENT, name: str = "Mesh", instanced: bool = True) -> dict:
    row = dict(id=identifier, name=name, parent="", transform=IDENTITY_TRANSFORM, is_root=True,
               editable=True, removable=False, properties=dict(), defaults=dict(bVisible="True"),
               property_schema=dict(bVisible=dict(type="bool")))
    row["class"] = "/Script/Engine.InstancedStaticMeshComponent" if instanced else "/Script/Engine.StaticMeshComponent"
    if instanced:
        row["instance_data"] = dict(custom_data_count=2, read_only=False, identity_bound=False,
                                    instances=[dict(id=INSTANCE_A, transform=transform(10), custom_data=[1.0, 2.0]),
                                               dict(id=INSTANCE_B, transform=transform(20), custom_data=[3.0, 4.0])])
    return row


def actor(identifier: str = ACTOR, label: str = "Owner", components: list | None = None) -> dict:
    row = dict(id=identifier, actor_path=f"{MAP}.World:PersistentLevel.{label}", level_path=MAP,
               label=label, folder="", parent="", parent_path="", scene_owner="", transform=IDENTITY_TRANSFORM,
               properties=dict(), defaults=dict(Tags="()"), property_schema=dict(Tags=dict(type="array")),
               components=[component()] if components is None else components)
    row["class"] = "/Script/Engine.Actor"
    return row


def digest(value: dict) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def snapshot(actors: list | None = None, name: str = "Group", missing: list | None = None,
             unavailable: list | None = None, dirty: bool = False) -> dict:
    rows = deepcopy([actor()] if actors is None else actors)
    for owner in rows:
        for entry in owner["components"]:
            data = entry.get("instance_data")
            if data is not None:
                data["revision"] = digest(dict(instances=data["instances"], custom_data_count=data["custom_data_count"]))
                data["total"] = len(data["instances"])
                data["next_offset"] = -1
    raw = dict(kind="scene", map_path=MAP, name=name, schema_key=SCHEMA, actors=sorted(rows, key=lambda item: item["id"]))
    raw["revision"] = digest(raw)
    raw.update(missing=missing or [], unavailable=unavailable or [], dirty=dirty)
    return raw
