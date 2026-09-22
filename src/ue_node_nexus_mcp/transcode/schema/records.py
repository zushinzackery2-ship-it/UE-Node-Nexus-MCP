"""Normalize reflected schema records without inventing unavailable metadata."""

from __future__ import annotations

from ..ids import short_class_name

FAMILIES = dict(material_expression="material", material_functions="material", k2node="blueprint",
                functions="blueprint", callable_functions="blueprint", component="scene", asset="asset",
                niagara_renderer="niagara", niagara_modules="niagara", types="common")


def class_aliases(name: str, path: str) -> list[str]:
    """Every name one class record answers to.

    Class tables are keyed by full UE path, so ``name`` alone does not carry the
    class name. ``short_class_name`` strips engine-family prefixes for the node
    ids the mirror writes (``Constant``, ``Sprite``), which eats the real name of
    classes that start with one of those tokens (``NiagaraComponent``). The real
    class name is therefore added explicitly.
    """
    aliases = {name, path}
    simple = path.rsplit(".", 1)[-1].rsplit("/", 1)[-1] if path else ""
    if simple:
        aliases.add(simple)
        aliases.add(short_class_name(simple))
    return sorted(alias for alias in aliases if alias)


def normalize(family: str, name: str, raw: dict, schema_key: str) -> dict:
    path = raw.get("path") or (name if name.startswith("/") else f"/Unknown/{name}")
    record = dict(raw)
    record.update(name=name, path=path, category=FAMILIES[family], family=family, schema_key=schema_key,
                  aliases=class_aliases(name, path),
                  module=raw.get("module") or (path.removeprefix("/Script/").split(".", 1)[0] if path.startswith("/Script/") else "project"),
                  plugin=raw.get("plugin", "metadata_not_provided"), inheritance=raw.get("inheritance", "metadata_not_provided"),
                  engine_available=raw.get("engine_available", True),
                  coverage=raw.get("coverage", "context_required" if raw.get("dynamic_pins") else "reflected"))
    record["bridge"] = raw.get("bridge") or dict(inspect=True, create="unknown", write="unknown", delete="unknown", source="legacy_export_requires_refresh")
    record["tooltip"] = raw.get("tooltip", "metadata_not_provided")
    record["conditions"] = raw.get("conditions", "metadata_not_provided")
    record["deprecated"] = raw.get("deprecated", False)
    record["syntax"] = raw.get("syntax") or syntax(family, name)
    props = dict()
    for key, prop in raw.get("props", dict()).items():
        item = dict(prop)
        item.setdefault("default_source", "class_default_object" if "default" in item else "metadata_not_provided")
        item.setdefault("tooltip", "metadata_not_provided")
        item.setdefault("conditions", "metadata_not_provided")
        item.setdefault("nullable", "metadata_not_provided")
        item.setdefault("readable", True)
        item.setdefault("writable", raw.get("bridge", dict()).get("write", "unknown"))
        props[key] = item
    record["props"] = props
    return record


def syntax(family: str, name: str) -> dict:
    short = short_class_name(name)
    if family == "material_expression":
        return dict(section="graph", declaration=f"node : {short}", parameter="node : Type(Property=value)", link="source.Pin -> destination.Pin")
    if family in ("k2node", "functions", "callable_functions"):
        return dict(section="graph/function/macro", declaration=f"node : {short}",
                    context='pins come from the owning asset: schema(target="/Game/Path/BP_Thing.BP_Thing")')
    if family in ("component", "niagara_renderer"):
        return dict(section="components" if family == "component" else "renderers", declaration=f"item : {short} {{ Property=value }}")
    if family == "niagara_modules":
        return dict(section="stack Emitter/Group", declaration=f"module : {name.rsplit('.', 1)[-1]}(Input=value)", context="script version, usage and static switches")
    return dict(property="Property = value", context="target asset determines the available fields")
