"""Typed incremental scene plans; IDs and omitted defaults retain their meaning."""

from __future__ import annotations

from typing import Any

from ..sync_project import SyncError
from ..values import values_equal
from .validation import hierarchy


def _index(rows: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    indexed = dict((row["id"], row) for row in rows)
    if len(indexed) != len(rows):
        raise SyncError("duplicate_scene_identity", "snapshot identities must be unique")
    return indexed


def _properties(before: dict[str, Any], after: dict[str, Any]) -> dict[str, str]:
    old = before.get("properties", dict())
    new = after.get("properties", dict())
    defaults = before.get("defaults", dict())
    changed = dict()
    for name in sorted(set(old) | set(new)):
        if name not in new and name not in defaults:
            raise SyncError("unknown_property_default", name)
        desired = new.get(name, defaults.get(name))
        previous = old.get(name, defaults.get(name))
        if previous is None or not values_equal(previous, desired):
            changed[name] = desired
    return changed


def _update(before: dict[str, Any], after: dict[str, Any], fields: tuple[str, ...]) -> dict[str, Any]:
    changed: dict[str, Any] = dict()
    for field in fields:
        default = "" if field in ("parent", "folder") else None
        previous, desired = before.get(field, default), after.get(field, default)
        if desired is not None and (previous is None or not values_equal(str(previous), str(desired))):
            changed[field] = desired
    properties = _properties(before, after)
    if properties:
        changed["properties"] = properties
    return changed


def _component_ops(actor: str, old_actor: dict[str, Any], new_actor: dict[str, Any], rebind: bool) -> list[dict[str, Any]]:
    before = _index(old_actor.get("components", []))
    after = _index(new_actor.get("components", []))
    ops = []
    for name in hierarchy(list(after.values()), actor):
        row = after[name]
        previous = before.get(name)
        if previous and previous["class"] != row["class"]:
            raise SyncError("component_class_changed", f"{actor}.{name}: remove and create under a new ID")
        changed = _update(previous or dict(), row, ("parent", "transform"))
        if previous and not previous.get("editable", True):
            old_data = previous.get("instance_data", dict())
            new_data = row.get("instance_data", dict())
            if changed or old_data.get("instances") != new_data.get("instances") or old_data.get("custom_data_count") != new_data.get("custom_data_count"):
                raise SyncError("opaque_component_modified", f"{actor}.{name}")
            continue
        if previous is None:
            ops.append(dict(op="create_component", actor_id=actor, id=name, name=row["name"], **{"class": row["class"]}, **changed))
        elif changed:
            ops.append(dict(op="update_component", actor_id=actor, id=name, **changed))
        data = row.get("instance_data")
        if data is None and previous and previous.get("instance_data") is not None:
            raise SyncError("instance_section_required", f"{actor}.{name} requires CustomDataCount and its instance section")
        if data is not None:
            old_data = (previous or dict()).get("instance_data", dict())
            old_instances = _index(old_data.get("instances", []))
            new_instances = _index(data["instances"])
            # Array order is an implementation detail; identity and values determine changes.
            equal = old_instances.keys() == new_instances.keys() and all(
                values_equal(old_instances[item]["transform"], new_instances[item]["transform"])
                and old_instances[item]["custom_data"] == new_instances[item]["custom_data"] for item in new_instances
            )
            if old_data.get("read_only"):
                if not equal or old_data.get("custom_data_count") != data["custom_data_count"] or not data.get("read_only"):
                    raise SyncError("construction_instances_read_only", f"{actor}.{name}")
                continue
            if not equal or old_data.get("custom_data_count") != data["custom_data_count"] or rebind:
                op = dict(op="instances_set", actor_id=actor, id=name, **data)
                op["expected_revision"] = old_data.get("revision", "")
                op["removed_count"] = len(old_instances.keys() - new_instances.keys())
                if rebind and previous:
                    op["rebind_ids"] = [item["id"] for item in old_data.get("instances", [])]
                ops.append(op)
    for name, row in before.items():
        if name not in after:
            if not row.get("removable", False):
                raise SyncError("inherited_component_not_removable", f"{actor}.{name}")
            ops.append(dict(op="remove_component", actor_id=actor, id=name))
    return ops


def build_plan(current: dict[str, Any], desired: dict[str, Any], selector: dict[str, Any], allow_delete: bool, rebind: bool = False) -> dict[str, Any]:
    before = _index(current.get("actors", []))
    after = _index(desired["actors"])
    creates, updates, components, deletes = [], [], [], []
    for actor_id in hierarchy(list(after.values()), desired["name"], allow_external=True):
        row = after[actor_id]
        previous = before.get(actor_id)
        if previous and (previous["class"] != row["class"] or previous["level_path"] != row["level_path"]):
            raise SyncError("actor_class_or_level_changed", "Use a new logical actor ID to change class or owning level")
        changes = _update(previous or dict(), row, ("label", "folder", "parent", "transform"))
        if previous is None:
            # Attach only after all actors and their root components exist.
            create = dict(op="create_actor", id=actor_id, level_path=row["level_path"], **{"class": row["class"]})
            creates.append(create)
        if changes:
            updates.append(dict(op="update_actor", id=actor_id, **changes))
        components.extend(_component_ops(actor_id, previous or dict(), row, rebind))
    for actor_id in reversed(hierarchy(list(before.values()), desired["name"], allow_external=True)):
        if actor_id not in after:
            deletes.append(dict(op="delete_actor", id=actor_id))
    destructive = len(deletes) + sum(op["op"] == "remove_component" or op.get("removed_count", 0) > 0 for op in components)
    if destructive and not allow_delete:
        raise SyncError("delete_not_allowed", "scene deletions require options.allow_delete=true")
    # Create root/component shells before actor construction; apply component overrides
    # after construction and instance arrays after every component property notification.
    shells, component_updates, instance_updates, removals = [], [], [], []
    for op in components:
        if op["op"] == "create_component":
            shells.append(dict((key, value) for key, value in op.items() if key in ("op", "actor_id", "id", "name", "class")))
            changed = dict((key, value) for key, value in op.items() if key in ("actor_id", "id", "properties", "parent", "transform"))
            if len(changed) > 2:
                component_updates.append(dict(changed, op="update_component"))
        elif op["op"] == "instances_set":
            instance_updates.append(op)
        elif op["op"] == "remove_component":
            removals.append(op)
        else:
            component_updates.append(op)
    refs = [dict(id=row["id"], level_path=row["level_path"]) for row in desired["actors"]]
    return dict(
        map_path=desired["map_path"], name=desired["name"], selector=selector,
        expected_revision=current["revision"], allow_delete=allow_delete, actors=refs,
        managed_actors=[dict(id=row["id"], level_path=row["level_path"]) for row in current.get("actors", [])],
        ops=creates + shells + updates + component_updates + instance_updates + removals + deletes,
    )
