"""Reconstruct Documents and the latest physical ID map from semantic state."""

from __future__ import annotations

from ...model import Bare, Decl, Document, Header, Link, Prop, Section
from .values import render


def to_document(snapshot: dict) -> Document:
    semantic = snapshot["semantic"]
    head = semantic["header"]
    document = Document(Header(nexus=head["nexus"], asset=head["asset"], cls=head["cls"], schema=snapshot.get("schema_key", ""), extra=dict(head.get("extra", dict()))))
    aliases = dict((identifier, entity["alias"]) for section in semantic["sections"].values() for identifier, entity in section["entities"].items())
    bindings = snapshot.get("bindings", dict())
    for scope, item in semantic["sections"].items():
        section = Section(item["name"], aliases.get(item.get("owner"), item["args"]))
        section.entries.extend(Prop(key, text, type_name=field.get("type")) for key, field in item["props"].items() if (text := render_field(field, aliases)) is not None)
        entities = item["entities"]
        order = item.get("order", list(entities))
        ordered = set(order)
        order = [key for key in order if key in entities] + [key for key in entities if key not in ordered]
        for identifier in order:
            entity = entities[identifier]
            args = [(None, render_field(field, aliases)) for field in entity["positional"]]
            args.extend((key, text) for key, field in entity["args"].items() if (text := render_field(field, aliases)) is not None)
            props = [(key, render_field(field, aliases)) for key, field in entity["props"].items() if field.get("state") != "default"]
            binding = bindings.get(identifier, dict())
            meta = dict(binding.get("meta", dict()), semantic_id=identifier)
            if binding.get("physical"):
                meta["guid"] = binding["physical"]
            section.entries.append(Decl(id=entity["alias"], type_name=entity["type"], args=args,
                default=render(entity["default"]), props=props, pos=tuple(entity["position"]) if entity["position"] else None,
                flags=list(entity["flags"]), annotations=dict(entity["annotations"]), modifier=entity["modifier"], meta=meta))
        section.entries.extend(Link(aliases.get(link["src"], link["src"].removeprefix("@")), link["src_pin"],
                                    aliases.get(link["dst"], link["dst"].removeprefix("@")), link["dst_pin"]) for link in item["links"].values())
        section.entries.extend(Bare(text) for text in item["bare"].values())
        document.sections.append(section)
    return document


def render_field(field: dict, aliases: dict) -> str | None:
    return aliases.get(field["ref"], field["ref"]) if "ref" in field else render(field)


def physical_ids(snapshot: dict) -> dict[str, str]:
    aliases = dict((identifier, entity["alias"]) for section in snapshot["semantic"]["sections"].values() for identifier, entity in section["entities"].items())
    return dict((binding["physical"], aliases[identifier]) for identifier, binding in snapshot.get("bindings", dict()).items() if binding.get("physical") and identifier in aliases)
