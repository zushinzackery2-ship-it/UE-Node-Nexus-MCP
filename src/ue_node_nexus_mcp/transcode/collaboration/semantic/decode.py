"""Reconstruct Documents and the latest physical ID map from semantic state."""

from __future__ import annotations

from ...model import Bare, Decl, Document, Header, Link, Prop, Section
from .values import render


def to_document(snapshot: dict, *, include_defaults=False) -> Document:
    semantic = snapshot["semantic"]
    head = semantic["header"]
    document = Document(Header(nexus=head["nexus"], asset=head["asset"], cls=head["cls"], schema=snapshot.get("schema_key", ""), extra=dict(head.get("extra", dict()))))
    aliases = dict((identifier, entity["alias"]) for section in semantic["sections"].values() for identifier, entity in section["entities"].items())
    bindings = snapshot.get("bindings", dict())
    locations = snapshot.get("locations", dict())
    for scope, item in semantic["sections"].items():
        section = Section(item["name"], aliases.get(item.get("owner"), item["args"]), line=locations.get("@section:" + scope, 0))
        section.entries.extend(Prop(key, text, type_name=field.get("type"), line=locations.get("@prop:" + scope + ":" + key, section.line))
                               for key, field in item["props"].items() if (text := render_field(field, aliases, include_defaults)) is not None)
        entities = item["entities"]
        order = item.get("order", list(entities))
        ordered = set(order)
        order = [key for key in order if key in entities] + [key for key in entities if key not in ordered]
        for identifier in order:
            entity = entities[identifier]
            args = [(None, render_field(field, aliases)) for field in entity["positional"]]
            args.extend((key, text) for key, field in entity["args"].items() if (text := render_field(field, aliases, include_defaults)) is not None)
            props = [(key, render_field(field, aliases, include_defaults)) for key, field in entity["props"].items() if include_defaults or field.get("state") != "default"]
            binding = bindings.get(identifier, dict())
            meta = dict(binding.get("meta", dict()), semantic_id=identifier)
            if binding.get("physical"):
                meta["guid"] = binding["physical"]
            section.entries.append(Decl(id=entity["alias"], type_name=entity["type"], args=args,
                default=render(entity["default"]), props=props, pos=tuple(entity["position"]) if entity["position"] else None,
                flags=list(entity["flags"]), annotations=dict(entity["annotations"]), modifier=entity["modifier"], meta=meta, line=locations.get(identifier, section.line)))
        section.entries.extend(Link(aliases.get(link["src"], link["src"].removeprefix("@")), link["src_pin"],
                                    aliases.get(link["dst"], link["dst"].removeprefix("@")), link["dst_pin"], line=section.line) for link in item["links"].values())
        section.entries.extend(Bare(text) for text in item["bare"].values())
        document.sections.append(section)
    if not locations:
        from .locations import stamp

        stamp(document)
    return document


def render_field(field: dict, aliases: dict, include_defaults=False) -> str | None:
    if include_defaults and field.get("state") == "default":
        return field.get("value")
    return aliases.get(field["ref"], field["ref"]) if "ref" in field else render(field)


def physical_ids(snapshot: dict) -> dict[str, str]:
    aliases = dict((identifier, entity["alias"]) for section in snapshot["semantic"]["sections"].values() for identifier, entity in section["entities"].items())
    return dict((binding["physical"], aliases[identifier]) for identifier, binding in snapshot.get("bindings", dict()).items() if binding.get("physical") and identifier in aliases)
