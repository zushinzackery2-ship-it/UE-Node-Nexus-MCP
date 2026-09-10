"""Resolve user identifiers to persistent actor and instance identities."""

from __future__ import annotations

import math
import uuid
from typing import Any

from ..lexer import split_top_level
from ..model import Decl, Document
from ..sync_project import SyncError
from ..values import unquote
from .codec import actor_key, component_key, instance_key
from .paths import identity
from .validation import hierarchy, validate_properties, validate_structure

ACTOR_FIELDS = dict(Label="label", Folder="folder", Parent="parent", Level="level_path", Transform="transform")
COMPONENT_FIELDS = dict(Parent="parent", Transform="transform", CustomDataCount="custom_data_count", InstancesReadOnly="instances_read_only")
IDENTITY_TRANSFORM = "(Rotation=(X=0,Y=0,Z=0,W=1),Translation=(X=0,Y=0,Z=0),Scale3D=(X=1,Y=1,Z=1))"


def scene_name(document: Document) -> str:
    section = document.section("scene")
    value = section.prop_map().get("Name") if section else None
    if value is None:
        raise SyncError("scene_name_required", "[scene] must contain Name")
    return unquote(value.value)


def _id(key: str) -> str:
    return uuid.uuid5(uuid.NAMESPACE_URL, "nexus-scene:" + key).hex


def _binding(aliases: dict[str, str], prefix: str, logical: str, default: str) -> str:
    matches = [key[len(prefix):] for key, value in aliases.items() if key.startswith(prefix) and value == logical]
    if len(matches) > 1:
        raise SyncError("ambiguous_scene_identity", logical)
    return matches[0] if matches else default


def _fields(decl: Decl, fields: dict[str, str]) -> dict[str, Any]:
    row: dict[str, Any] = dict(properties=dict())
    for key, value in decl.props:
        if value is None:
            raise SyncError("scene_property_value_required", f"{decl.id}.{key}")
        if key.startswith("Property."):
            row["properties"][key[len("Property."):]] = value
        elif key in fields:
            row[fields[key]] = unquote(value) if key != "Transform" else value
        else:
            row["properties"][key] = value
    return row


def from_document(document: Document, base: dict[str, Any] | None) -> tuple[dict[str, Any], dict[str, str]]:
    validate_structure(document)
    name = scene_name(document)
    key = identity(document.header.asset, name)
    if base and identity(base["map_path"], base["name"]) != key:
        raise SyncError("scene_identity_changed", "map and group identity cannot change in a managed file")
    aliases = dict((base or dict()).get("aliases") or dict())
    old_actors = dict((row["id"], row) for row in (base or dict()).get("actors", []))
    section = document.section("actors")
    declarations = section.decls() if section else []
    actor_ids = dict()
    for decl in declarations:
        actor_ids[decl.id] = _binding(aliases, "actor/", decl.id, _id(key + "/" + decl.id))
        aliases[actor_key(actor_ids[decl.id])] = decl.id
    actors = []
    for decl in declarations:
        actor = _fields(decl, ACTOR_FIELDS)
        actor.update(id=actor_ids[decl.id], **{"class": decl.type_name})
        previous = old_actors.get(actor["id"], dict())
        actor.setdefault("level_path", key.split("#", 1)[0])
        actor.setdefault("label", decl.id)
        actor.setdefault("folder", "")
        actor.setdefault("transform", IDENTITY_TRANSFORM)
        parent = actor.get("parent", "")
        if parent and parent == previous.get("parent_path"):
            parent = previous.get("parent", "")
        actor["parent"] = actor_ids.get(parent, parent)
        validate_properties(previous, actor, decl.id)
        actor["components"] = _components(document, decl.id, actor["id"], key, aliases, previous)
        actors.append(actor)
    hierarchy(actors, name, allow_external=True)
    return dict(map_path=key.split("#", 1)[0], name=name, actors=actors), aliases


def _components(document: Document, actor_name: str, actor_id: str, key: str,
                aliases: dict[str, str], previous: dict[str, Any]) -> list[dict[str, Any]]:
    section = document.section("components", actor_name)
    decls = section.decls() if section else []
    names = dict()
    old = dict((row["id"], row) for row in previous.get("components", []))
    for decl in decls:
        names[decl.id] = _binding(aliases, f"component/{actor_id}/", decl.id, _id(key + "/component/" + actor_id + "/" + decl.id))
        aliases[component_key(actor_id, names[decl.id])] = decl.id
    result = []
    for decl in decls:
        row = _fields(decl, COMPONENT_FIELDS)
        row.update(id=names[decl.id], **{"class": decl.marker_arg() if decl.opaque else decl.type_name})
        row["name"] = old.get(row["id"], dict()).get("name", decl.id)
        row["opaque"] = decl.opaque
        if "parent" in row:
            row["parent"] = names.get(row["parent"], row["parent"])
        validate_properties(old.get(row["id"], dict()), row, f"{actor_name}.{decl.id}")
        if "custom_data_count" in row:
            try:
                count = int(row.pop("custom_data_count"))
            except ValueError as exc:
                raise SyncError("invalid_custom_data_count", decl.id) from exc
            instances = document.section("instances", f"{actor_name}.{decl.id}")
            row["instance_data"] = dict(
                custom_data_count=count,
                read_only=str(row.pop("instances_read_only", "false")).lower() == "true",
                instances=_instances(instances.decls() if instances else [], actor_id, names[decl.id], count, key, aliases),
            )
        result.append(row)
    hierarchy(result, actor_name)
    return result


def _instances(decls: list[Decl], actor: str, component: str, count: int, key: str, aliases: dict[str, str]) -> list[dict[str, Any]]:
    if not 0 <= count <= 1024:
        raise SyncError("invalid_custom_data_count", "CustomDataCount must be 0..1024")
    result = []
    for decl in decls:
        prefix = f"instance/{actor}/{component}/"
        physical = _binding(aliases, prefix, decl.id, _id(key + "/" + prefix + decl.id))
        aliases[instance_key(actor, component, physical)] = decl.id
        values = decl.prop_values()
        custom = values.get("CustomData", "()")
        if not custom.startswith("(") or not custom.endswith(")"):
            raise SyncError("invalid_custom_data", f"{decl.id}: expected an array")
        try:
            data = [float(item.strip()) for item in split_top_level(custom[1:-1], ",") if item.strip()]
        except ValueError as exc:
            raise SyncError("invalid_custom_data", decl.id) from exc
        if len(data) != count or any(not math.isfinite(value) or abs(value) > 3.402823466e38 for value in data):
            raise SyncError("invalid_custom_data", f"{decl.id}: expected {count} finite floats")
        result.append(dict(id=physical, transform=values.get("Transform", IDENTITY_TRANSFORM), custom_data=data))
    return result
