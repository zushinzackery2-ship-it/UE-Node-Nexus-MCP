"""Additional field constraints and usable payload examples for scene operations."""

from __future__ import annotations

from typing import Any


def augment(operation: str, schema: dict[str, Any]) -> None:
    fields = schema.get("properties", dict())
    if operation == "component_instances_get":
        fields["offset"].update(minimum=0)
        fields["limit"].update(minimum=1, maximum=10000)
    elif operation == "component_instances_patch":
        fields["custom_data_count"].update(minimum=0, maximum=1024)
        fields["ops"]["items"] = dict(type="object", required=["op"], additionalProperties=False,
                                      properties=dict(op=dict(type="string", enum=["add", "update", "remove"]),
                                                      id=dict(type="string", description="Persistent GUID; required for update/remove, optional for add"),
                                                      transform=dict(type="string", description="Local UE ExportText FTransform; identity when omitted on add"),
                                                      custom_data=dict(type="array", items=dict(type="number"), description="One finite float per custom-data channel")))
    elif operation in ("scene_export", "scene_status"):
        fields["name"].update(pattern=r"^[A-Za-z_][A-Za-z0-9_-]*$")
        fields["actors"]["items"] = dict(type="object", required=["id"], additionalProperties=False,
                                         properties=dict(id=dict(type="string"), level_path=dict(type="string")))
        fields["actor_paths"]["items"] = dict(type="string")
    if operation in ("scene_export", "scene_apply"):
        fields["out_file"].update(pattern=r"\.json$", description="JSON output inside pending storage in the registered mirror root")


def examples() -> dict[str, dict[str, Any]]:
    actor = "/Game/Maps/World.World:PersistentLevel.Tiles"
    return dict(
        component_instances_get=dict(component_path=actor + ".Mesh", offset=0, limit=100),
        component_instances_patch=dict(component_path=actor + ".Mesh", revision="revision_from_component_instances_get",
                                       ops=[dict(op="add", custom_data=[])], dry_run=True, save=False),
        scene_export=dict(map_path="/Game/Maps/World", name="Block", actor_paths=[actor],
                          out_file="D:/Mirror/Project/.nexus/scenes/pending/block.json"),
        scene_status=dict(map_path="/Game/Maps/World", name="Block", actor_paths=[actor]),
        scene_apply=dict(plan_file="D:/Mirror/Project/.nexus/scenes/plans/block.json",
                         out_file="D:/Mirror/Project/.nexus/scenes/after/block.json", dry_run=True, save=True),
    )
