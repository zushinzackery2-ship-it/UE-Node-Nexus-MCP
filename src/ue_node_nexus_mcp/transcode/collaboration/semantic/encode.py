"""Document-to-state adapters for asset graphs, stacks and scene entities."""

from __future__ import annotations

from dataclasses import dataclass

from ...blueprint.classes import node_identity
from ...text.model import Document
from ...raw.simple import DERIVED_PARAMETER_ARRAYS, INSTANCE_TYPES
from ...errors import SyncError
from .contracts import same_contract
from .blueprint import declaration_default, positional_values, signature, signature_contracts
from ...blueprint.input_values import input_metadata
from .identity import Identities
from .links import encode_links
from ...text.semantic import field_values, value
from ...material.custom_inputs import normalize_args

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
    # Decoding a candidate may infer a scalar type spelling in the other
    # namespace. It must not erase a reflected struct/array member contract.
    for name, contract in metadata.get("prop_types", dict()).items():
        if isinstance(contract, dict) and not isinstance(types.get(name), dict):
            types[name] = contract
    if schema and family:
        info = schema.resolve_class(family, class_name)
        if info:
            for name, item in info.props.items():
                contract = item.get("value_schema") or item.get("type", "text")
                if name not in types or isinstance(contract, dict) and not isinstance(types[name], dict):
                    types[name] = contract
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


def _linked_input_pins(section, document, schema) -> dict[str, set[str]]:
    """Input pins this section's own links connect, by declaration id.

    Raw exports omit the default of a connected input pin, so it is not part of
    the canonical state: keeping an authored default for it would make the
    replay plan write a value UE never keeps, and the readback reject the very
    state it was asked to produce.
    """
    from ...blueprint.pins import canonical_pin, class_pin_aliases, node_pins

    decls = section.decl_map()
    result: dict[str, set[str]] = {}
    for link in section.links():
        decl = decls.get(link.dst)
        if decl is None:
            continue
        pins = node_pins(decl, document, section, schema)
        target = canonical_pin(link.dst_pin, "in", pins, class_pin_aliases(schema, decl.type_name))
        if target:
            result.setdefault(link.dst, set()).add(target.casefold())
    return result


def _strip_linked_defaults(keyed: dict[str, str], linked_pins: set[str] | None) -> dict[str, str]:
    if not linked_pins:
        return keyed
    result = {}
    for key, value in keyed.items():
        if key == "pins" or key.startswith("prop:"):
            result[key] = value
            continue
        name = key[4:] if key.startswith("pin:") else key
        if name.casefold() not in linked_pins:
            result[key] = value
    return result


def encode_section(section, scope: str, context: Encoding) -> dict:
    identities, locations = context.identities, context.locations
    before = identities.sections.get(scope, dict())
    locations["@section:" + scope] = section.line
    for prop in section.props():
        locations["@prop:" + scope + ":" + prop.key] = prop.line
    linked = (_linked_input_pins(section, context.document, context.schema)
              if context.kind == "blueprint" and section.name in ("graph", "function", "macro") else dict())
    entities, aliases = dict(), dict()
    for decl in section.decls():
        if decl.id in aliases:
            raise SyncError("duplicate_id", decl.id, dict(entity=decl.id, line=decl.line))
        family = "" if decl.modifier == "local" else family_for(context.kind, section.name)
        identifier, metadata = identities.entity(scope, decl, family, context.schema)
        aliases[decl.id] = identifier
        try:
            entities[identifier] = entity_state(decl, section.name, metadata, before.get("entities", dict()).get(identifier, dict()),
                                                context, linked.get(decl.id))
        except SyncError as exc:
            raise exc.attribute(entity=decl.id, line=decl.line)
        locations[identifier] = decl.line
    section_type = INSTANCE_TYPES.get(section.name) if context.kind == "material_instance" else None
    asset_types = section_types(section, context.document, context.schema, context.evidence)
    authored = [prop for prop in section.props()
                if not (context.kind == "material_instance" and section.name == "asset"
                        and prop.key in DERIVED_PARAMETER_ARRAYS)]
    props = dict((prop.key, value(prop.value, asset_types.get(prop.key) or prop.type_name or section_type or
                  before.get("props", dict()).get(prop.key, dict()).get("type") or "text")) for prop in authored)
    if len(props) != len(authored):
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


def entity_state(decl, section: str, metadata: dict, old: dict, context: Encoding, linked_pins: set[str] | None = None) -> dict:
    family = "" if decl.modifier == "local" else family_for(context.kind, section)
    previous = old if same_contract(decl, old, family, context.schema, metadata) else dict()
    types, defaults = property_metadata(metadata, context.schema, family, decl.type_name)
    class_name = node_identity(decl.type_name, context.schema) if family == "k2node" else decl.type_name
    # A declaration's named arguments and property block are different
    # namespaces. Defaults belong to the one used by its adapter.
    prop_style = section in PROPERTY_SECTIONS
    # Host spellings of one call are one state; the bridge picks the host.
    keyed = _strip_linked_defaults(decl.keyed(), linked_pins)
    entity = dict(alias=decl.id, type=class_name,
                  positional=[value(text) for text in positional_values(decl, family, metadata, context.schema)],
                  args=field_values(normalize_args(decl, keyed) if context.kind in ("material", "material_function") else keyed,
                                    types, dict() if prop_style else defaults, previous.get("args")),
                  props=field_values(decl.prop_map(), types, defaults if prop_style else dict(), previous.get("props")),
                  default=declaration_default(decl, context.kind, section, class_name),
                  position=list(decl.pos) if decl.pos else old.get("position"),
                  flags=dict.fromkeys(decl.flags, True), annotations=dict(decl.annotations), modifier=decl.modifier)
    if decl.opaque:
        entity["opaque"] = metadata.get("t3d", previous.get("opaque", ""))
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
            parent_references(entity, aliases)
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


def parent_references(entity, aliases):
    for namespace in ("args", "props"):
        for key in ("Parent", "parent"):
            field = entity[namespace].get(key)
            if field and field.get("value") in aliases:
                field["ref"] = aliases[field.pop("value")]
