"""Scene raw snapshots to the shared nexus document model."""

from __future__ import annotations

from typing import Any

from ..ids import sanitize_id
from ..model import Decl, Document, Header, Prop, Section
from ..values import quote

RESERVED_FIELDS = set(("Label", "Folder", "Parent", "Level", "Transform", "CustomDataCount", "InstancesReadOnly"))


def actor_key(actor: str) -> str:
    return f"actor/{actor}"


def component_key(actor: str, component: str) -> str:
    return f"component/{actor}/{component}"


def instance_key(actor: str, component: str, instance: str) -> str:
    return f"instance/{actor}/{component}/{instance}"


def _name(key: str, candidate: str, aliases: dict[str, str], used: set[str]) -> str:
    result = aliases.get(key, sanitize_id(candidate))
    stem, index = result, 2
    while result in used:
        result = f"{stem}_{index}"
        index += 1
    aliases[key] = result
    used.add(result)
    return result


def _properties(row: dict[str, Any]) -> list[tuple[str, str]]:
    return [("Property." + key if key in RESERVED_FIELDS else key, str(value))
            for key, value in sorted(row.get("properties", dict()).items())]


def from_raw(raw: dict[str, Any], previous_aliases: dict[str, str] | None = None) -> tuple[Document, dict[str, str]]:
    aliases = dict(previous_aliases or raw.get("aliases") or dict())
    document = Document(Header(asset=raw["map_path"], cls="World", schema=raw.get("schema_key", "")))
    document.sections.append(Section("scene", entries=[Prop("Name", quote(raw["name"]))]))
    actors = document.ensure_section("actors")
    used: set[str] = set()
    for row in raw.get("actors", []):
        _name(actor_key(row["id"]), row.get("label") or "actor", aliases, used)
    for row in raw.get("actors", []):
        actor_id = aliases[actor_key(row["id"])]
        props = [
            ("Label", quote(row.get("label", actor_id))),
            ("Level", row["level_path"]),
            ("Transform", row["transform"]),
        ]
        if row.get("folder") and row["folder"] != "None":
            props.append(("Folder", quote(row["folder"])))
        if row.get("parent"):
            props.append(("Parent", aliases.get(actor_key(row["parent"]), row.get("parent_path", row["parent"]))))
        actors.entries.append(Decl(actor_id, row["class"], props=props + _properties(row)))
        _components(document, row, actor_id, aliases)
    active = set()
    for row in raw.get("actors", []):
        active.add(actor_key(row["id"]))
        for component in row.get("components", []):
            active.add(component_key(row["id"], component["id"]))
            for instance in component.get("instance_data", dict()).get("instances", []):
                active.add(instance_key(row["id"], component["id"], instance["id"]))
    return document, dict((key, aliases[key]) for key in active)


def _components(document: Document, actor: dict[str, Any], actor_id: str, aliases: dict[str, str]) -> None:
    components = document.ensure_section("components", actor_id)
    used: set[str] = set()
    for row in actor.get("components", []):
        _name(component_key(actor["id"], row["id"]), row.get("name", row["id"]), aliases, used)
    for row in actor.get("components", []):
        component_id = aliases[component_key(actor["id"], row["id"])]
        props = _properties(row)
        if row.get("transform") and not row.get("is_root"):
            props.insert(0, ("Transform", row["transform"]))
        if row.get("parent") and not row.get("is_root"):
            props.insert(0, ("Parent", aliases[component_key(actor["id"], row["parent"])]))
        instance_data = row.get("instance_data")
        if instance_data is not None:
            props.append(("CustomDataCount", str(instance_data["custom_data_count"])))
            if instance_data.get("read_only"):
                props.append(("InstancesReadOnly", "true"))
        decl = Decl(component_id, row["class"], props=props)
        if not row.get("editable", True):
            decl.type_name = "@opaque"
            decl.args = [(None, row["class"])]
        components.entries.append(decl)
        if instance_data is not None:
            section = document.ensure_section("instances", f"{actor_id}.{component_id}")
            used_instances: set[str] = set()
            for index, item in enumerate(instance_data["instances"]):
                key = instance_key(actor["id"], row["id"], item["id"])
                instance_id = _name(key, f"instance_{index + 1}", aliases, used_instances)
                values = [("Transform", item["transform"])]
                if item.get("custom_data"):
                    values.append(("CustomData", "(" + ",".join(str(value) for value in item["custom_data"]) + ")"))
                section.entries.append(Decl(instance_id, "Instance", props=values))
