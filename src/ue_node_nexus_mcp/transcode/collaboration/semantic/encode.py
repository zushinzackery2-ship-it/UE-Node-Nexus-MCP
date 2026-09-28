"""Document-to-state adapters for asset graphs, stacks and scene entities."""

from __future__ import annotations

from dataclasses import dataclass

from ...blueprint.classes import node_identity
from ...text.model import Document
from ...raw.simple import INSTANCE_TYPES
from ...errors import SyncError
from .blueprint import declaration_default, input_metadata, positional_values, signature, signature_contracts
from .identity import Identities
from .links import encode_links
from ...text.semantic import field_values, value

# Sections whose declarations carry a property block instead of named arguments.
PROPERTY_SECTIONS = ("components", "actors", "instances", "renderers")


@dataclass
class Encoding:
    """What every section of one document is encoded against."""

    document: Document
    kind: str
    identities: Identities
    schema: object
    evidence: dict
    contracts: dict
    locations: dict


def property_metadata(metadata: dict, schema, family: str, class_name: str) -> tuple[dict, dict]:
    types = dict(metadata.get("prop_types", dict()), **metadata.get("input_types", dict()))
    defaults = dict(metadata.get("prop_defaults", dict()), **metadata.get("input_defaults", dict()))
    if schema and family:
        info = schema.resolve_class(family, class_name)
        if info:
            for name, item in info.props.items():
                types.setdefault(name, item.get("value_schema") or item.get("type", "text"))
                if "default" in item:
                    defaults.setdefault(name, item["default"])
    if family == "k2node":
        input_metadata(metadata, types, defaults)
    return types, defaults


def family_for(kind: str, section: str) -> str:
    if section in ("components", "actors"):
        return "component" if section == "components" else "asset"
    if kind in ("material", "material_function"):
        return "material_expression"
    if section in ("graph", "function", "macro") and kind == "blueprint":
        return "k2node"
    return "niagara_renderer" if section == "renderers" else ""


def section_types(section, document, schema, raw):
    info = schema.resolve_class("asset", document.header.cls) if schema and section.name == "asset" else None
    types = dict((name, item.get("value_schema") or item.get("type")) for name, item in info.props.items()) if info else dict()
    rows = raw.get("props", []) if section.name == "asset" else []
    if section.name == "defaults":
        rows = raw.get("blueprint", dict()).get("defaults", [])
    for item in rows:
        types[item["name"]] = item.get("value_schema") or item.get("type", "text")
    return types


def encode(document: Document, kind: str, previous: dict | None, namespace: str, schema=None, *, raw=None) -> tuple[dict, dict, dict]:
    identities = Identities(previous, namespace, document.header.asset)
    evidence = raw if raw is not None else (previous or dict()).get("raw", dict())
    context = Encoding(document, kind, identities, schema, evidence, signature_contracts(evidence), dict())
    sections = dict()
    for section in document.sections:
        scope = identities.scope(section)
        if scope in sections:
            raise SyncError("duplicate_section", section.header(), dict(asset=document.header.asset, line=section.line))
        try:
            sections[scope] = encode_section(section, scope, context)
        except SyncError as exc:
            raise exc.attribute(asset=document.header.asset, section=section.header(), line=section.line)
    normalize_references(sections, kind)
    header = dict(nexus=document.header.nexus, asset=document.header.asset, cls=document.header.cls, extra=document.header.extra)
    return dict(kind=kind, header=header, sections=sections), identities.bindings, context.locations


def encode_section(section, scope: str, context: Encoding) -> dict:
    identities, locations = context.identities, context.locations
    before = identities.sections.get(scope, dict())
    locations["@section:" + scope] = section.line
    for prop in section.props():
        locations["@prop:" + scope + ":" + prop.key] = prop.line
    entities, aliases = dict(), dict()
    for decl in section.decls():
        if decl.id in aliases:
            raise SyncError("duplicate_id", decl.id, dict(entity=decl.id, line=decl.line))
        identifier, metadata = identities.entity(scope, decl)
        aliases[decl.id] = identifier
        try:
            entities[identifier] = entity_state(decl, section.name, metadata, before.get("entities", dict()).get(identifier, dict()), context)
        except SyncError as exc:
            raise exc.attribute(entity=decl.id, line=decl.line)
        locations[identifier] = decl.line
    section_type = INSTANCE_TYPES.get(section.name) if context.kind == "material_instance" else None
    asset_types = section_types(section, context.document, context.schema, context.evidence)
    props = dict((prop.key, value(prop.value, asset_types.get(prop.key) or prop.type_name or section_type or
                  before.get("props", dict()).get(prop.key, dict()).get("type") or "text")) for prop in section.props())
    if len(props) != len(section.props()):
        raise SyncError("duplicate_property", section.header())
    links = encode_links(section, aliases, identities.bindings, context.document, context.schema, context.kind)
    bare = dict()
    for entry in section.bares():
        if section.name == "dispatchers":
            parsed = signature(entry.text, entry.line, contracts=context.contracts)
            bare[parsed.name] = parsed.text()
        else:
            bare[entry.text] = entry.text
    args = signature(section.args, section.line, function=True, contracts=context.contracts).text() if section.name == "function" else section.args
    item = dict(name=section.name, args=args, props=props, entities=entities, links=links, bare=bare)
    if section.name == "stack":
        item["order"] = list(entities)
    return item


def entity_state(decl, section: str, metadata: dict, old: dict, context: Encoding) -> dict:
    family = "" if decl.modifier == "local" else family_for(context.kind, section)
    types, defaults = property_metadata(metadata, context.schema, family, decl.type_name)
    class_name = node_identity(decl.type_name, context.schema) if family == "k2node" else decl.type_name
    # A declaration's named arguments and property block are different
    # namespaces. Defaults belong to the one used by its adapter.
    prop_style = section in PROPERTY_SECTIONS
    # Host spellings of one call are one state; the bridge picks the host.
    entity = dict(alias=decl.id, type=class_name,
                  positional=[value(text) for text in positional_values(decl, family, metadata, context.schema)],
                  args=field_values(decl.keyed(), types, dict() if prop_style else defaults, old.get("args")),
                  props=field_values(decl.prop_map(), types, defaults if prop_style else dict(), old.get("props")),
                  default=declaration_default(decl, context.kind, section, class_name), position=list(decl.pos) if decl.pos else None,
                  flags=dict.fromkeys(decl.flags, True), annotations=dict(decl.annotations), modifier=decl.modifier)
    if decl.opaque:
        entity["opaque"] = metadata.get("t3d", old.get("opaque", ""))
    return entity


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
        local_variables = dict((entity["alias"], identifier) for identifier, entity in section["entities"].items()
                               if entity.get("modifier") == "local")
        visible = dict(variables, **local_variables)
        for entity in section["entities"].values():
            if entity["type"] in ("VariableGet", "VariableSet") and entity["positional"]:
                field = entity["positional"][0]
                if field.get("value") in visible:
                    field["ref"] = visible[field.pop("value")]
