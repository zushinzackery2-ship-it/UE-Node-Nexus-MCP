"""Structural scene validation and linear-time attachment ordering."""

from __future__ import annotations

import re
from typing import Any

from ..model import Decl, Document, Prop
from ..sync_project import SyncError


def unique(values: list[str], label: str) -> None:
    if len(values) != len(set(values)):
        raise SyncError("duplicate_scene_identifier", label)


def validate_structure(document: Document) -> None:
    if document.header.cls != "World":
        raise SyncError("invalid_scene_header", "scene files require class: World")
    identities = [section.identity() for section in document.sections]
    if len(identities) != len(set(identities)):
        raise SyncError("duplicate_scene_section", "scene sections must be unique")
    actors = document.section("actors")
    actor_names = set(decl.id for decl in actors.decls()) if actors else set()
    components = dict()
    for section in document.sections:
        if section.name not in ("scene", "actors", "components", "instances"):
            raise SyncError("unknown_scene_section", section.header())
        if section.name in ("scene", "actors") and section.args:
            raise SyncError("invalid_scene_section", section.header())
        if section.name == "scene":
            if any(not isinstance(entry, Prop) or entry.key != "Name" for entry in section.entries):
                raise SyncError("invalid_scene_property", "[scene] accepts only Name")
            unique([entry.key for entry in section.props()], section.header())
            continue
        if section.name == "components":
            if section.args not in actor_names:
                raise SyncError("unknown_scene_actor", section.args)
            components.update((f"{section.args}.{decl.id}", decl) for decl in section.decls())
        unique([decl.id for decl in section.decls()], section.header())
        for entry in section.entries:
            validate_decl(entry, section.name)
    for section in document.sections:
        if section.name != "instances":
            continue
        component = components.get(section.args)
        if component is None or "CustomDataCount" not in component.prop_values():
            raise SyncError("unknown_instance_component", f"{section.args} requires a declared CustomDataCount")


def validate_decl(entry: Any, section: str) -> None:
    if not isinstance(entry, Decl) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", entry.id):
        raise SyncError("invalid_scene_declaration", f"[{section}] requires named declarations")
    if entry.default is not None or entry.modifier or entry.flags or entry.annotations or entry.pos:
        raise SyncError("invalid_scene_declaration", f"unsupported declaration syntax: {entry.id}")
    if entry.args and not (section == "components" and entry.opaque and len(entry.positional()) == 1):
        raise SyncError("invalid_scene_declaration", f"properties belong in braces: {entry.id}")
    if section == "instances" and entry.type_name != "Instance":
        raise SyncError("invalid_instance_type", entry.id)
    if section == "instances" and set(entry.prop_map()) - set(("Transform", "CustomData")):
        raise SyncError("unknown_instance_property", entry.id)
    unique([name for name, _ in entry.props], entry.id)
    if any(value is None for _, value in entry.props):
        raise SyncError("scene_property_value_required", entry.id)


def hierarchy(rows: list[dict[str, Any]], label: str, allow_external: bool = False) -> list[str]:
    parents = dict((row["id"], row.get("parent", "")) for row in rows)
    unique([row["id"] for row in rows], label)
    children = dict((key, []) for key in parents)
    ready = []
    for key, parent in parents.items():
        if not parent:
            ready.append(key)
        elif parent in parents:
            children[parent].append(key)
        elif allow_external:
            ready.append(key)
        else:
            raise SyncError("unknown_component_parent", f"{label}: {parent}")
    ordered = []
    cursor = 0
    while cursor < len(ready):
        key = ready[cursor]
        cursor += 1
        ordered.append(key)
        ready.extend(children[key])
    if len(ordered) != len(parents):
        raise SyncError("scene_attachment_cycle", label)
    return ordered


def validate_properties(before: dict[str, Any], after: dict[str, Any], label: str) -> None:
    schema = before.get("property_schema")
    if schema is not None:
        unknown = set(after.get("properties", dict())) - set(schema)
        if unknown:
            raise SyncError("property_not_editable", f"{label}: {', '.join(sorted(unknown))}")
