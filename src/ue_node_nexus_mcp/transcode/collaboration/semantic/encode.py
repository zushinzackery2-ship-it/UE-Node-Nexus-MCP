"""Document-to-state adapters for asset graphs, stacks and scene entities."""

from __future__ import annotations

from ...bp_types import type_text
from ...model import Document, Section
from ...sync_project import SyncError
from .identity import Identities
from .values import field_values, value


def property_metadata(metadata: dict, schema, family: str, class_name: str) -> tuple[dict, dict]:
    types = dict(metadata.get("prop_types", dict()), **metadata.get("input_types", dict()))
    defaults = dict(metadata.get("prop_defaults", dict()), **metadata.get("input_defaults", dict()))
    if schema and family:
        info = schema.resolve_class(family, class_name)
        if info:
            for name, item in info.props.items():
                types.setdefault(name, item.get("type", "text"))
                if "default" in item:
                    defaults.setdefault(name, item["default"])
    for pin in metadata.get("pins", []):
        if pin.get("dir") == "in":
            types[pin["name"]] = type_text(pin.get("type"))
    return types, defaults


def family_for(kind: str, section: str) -> str:
    if section in ("components", "actors"):
        return "component" if section == "components" else "asset"
    if kind in ("material", "material_function"):
        return "material_expression"
    if section in ("graph", "function", "macro") and kind == "blueprint":
        return "k2node"
    return "niagara_renderer" if section == "renderers" else ""


def encode(document: Document, kind: str, previous: dict | None, namespace: str, schema=None) -> tuple[dict, dict, dict]:
    identities = Identities(previous, namespace, document.header.asset)
    sections, locations, scopes = dict(), dict(), dict()
    for section in document.sections:
        scope = identities.scope(section)
        if scope in sections:
            raise SyncError("duplicate_section", section.header())
        scopes[(section.name, section.args)] = scope
        before = identities.sections.get(scope, dict())
        entities, aliases = dict(), dict()
        for decl in section.decls():
            if decl.id in aliases:
                raise SyncError("duplicate_id", decl.id)
            identifier, metadata = identities.entity(scope, decl)
            aliases[decl.id] = identifier
            old = before.get("entities", dict()).get(identifier, dict())
            types, defaults = property_metadata(metadata, schema, family_for(kind, section.name), decl.type_name)
            # A declaration's named arguments and property block are different
            # namespaces. Defaults belong to the one used by its adapter.
            prop_style = section.name in ("components", "actors", "instances", "renderers")
            entities[identifier] = dict(alias=decl.id, type=decl.type_name,
                positional=[value(text) for text in decl.positional()],
                args=field_values(decl.keyed(), types, dict() if prop_style else defaults, old.get("args")),
                props=field_values(decl.prop_map(), types, defaults if prop_style else dict(), old.get("props")),
                default=value(decl.default, decl.type_name), position=list(decl.pos) if decl.pos else None,
                flags=dict.fromkeys(decl.flags, True), annotations=dict(decl.annotations), modifier=decl.modifier)
            if decl.opaque:
                entities[identifier]["opaque"] = metadata.get("t3d", old.get("opaque", ""))
            locations[identifier] = decl.line
        props = dict((prop.key, value(prop.value, prop.type_name or before.get("props", dict()).get(prop.key, dict()).get("type", "text"))) for prop in section.props())
        if len(props) != len(section.props()):
            raise SyncError("duplicate_property", section.header())
        links = encode_links(section, aliases, identities.bindings)
        bare = dict((entry.text.split("(", 1)[0] if section.name == "dispatchers" else entry.text, entry.text) for entry in section.bares())
        item = dict(name=section.name, args=section.args, props=props, entities=entities, links=links, bare=bare)
        if section.name == "stack":
            item["order"] = list(entities)
        sections[scope] = item
    normalize_references(sections, kind)
    header = dict(nexus=document.header.nexus, asset=document.header.asset, cls=document.header.cls, extra=document.header.extra)
    return dict(kind=kind, header=header, sections=sections), identities.bindings, locations


def encode_links(section: Section, aliases: dict, bindings: dict) -> dict:
    result = dict()
    for link in section.links():
        src, dst = aliases.get(link.src, "@" + link.src), aliases.get(link.dst, "@" + link.dst)
        record = dict(src=src, dst=dst, src_pin=link.src_pin, dst_pin=link.dst_pin)
        pins = bindings.get(src, dict()).get("meta", dict()).get("pins", [])
        is_exec = any(pin.get("dir") == "out" and (pin.get("name") == link.src_pin or link.src_pin is None) and (pin.get("type") or dict()).get("category") == "exec" for pin in pins)
        slot = f"out:{src}:{link.src_pin or ''}" if is_exec else f"in:{dst}:{link.dst_pin or ''}"
        if slot in result and result[slot] != record:
            raise SyncError("pin_cardinality", f"multiple connections occupy {link.dst}.{link.dst_pin or ''}")
        result[slot] = record
    return result


def normalize_references(sections: dict, kind: str) -> None:
    actor_aliases = dict()
    variables = dict()
    for section in sections.values():
        aliases = dict((entity["alias"], identifier) for identifier, entity in section["entities"].items())
        if section["name"] == "actors":
            actor_aliases.update(aliases)
        if section["name"] == "variables":
            variables.update(aliases)
        for entity in section["entities"].values():
            for namespace in ("args", "props"):
                for key in ("Parent", "parent"):
                    field = entity[namespace].get(key)
                    if field and field.get("value") in aliases:
                        field["ref"] = aliases[field["value"]]
                        field.pop("value")
    for section in sections.values():
        if kind == "scene" and section["name"] == "components" and section["args"] in actor_aliases:
            section["owner"] = actor_aliases[section["args"]]
        for entity in section["entities"].values():
            if entity["type"] in ("VariableGet", "VariableSet") and entity["positional"]:
                field = entity["positional"][0]
                if field.get("value") in variables:
                    field["ref"] = variables[field.pop("value")]
