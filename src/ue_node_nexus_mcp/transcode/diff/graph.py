"""Node / link diff shared by material and Blueprint graphs."""

from __future__ import annotations

from typing import Any

from ..blueprint.classes import create_class
from ..blueprint.callables import event_signature
from ..blueprint.pins import class_pin_aliases
from .common import decl_params, diff_params, same_class
from ..text.model import Decl, Link, Section
from .plan import AssetPlan
from ..schema.lock import SchemaLock

TEXTURE_ALIAS_INDEX = {"rgb": "0", "rgba": "0", "r": "1", "g": "2", "b": "3", "a": "4"}
MATERIAL_OUT = "out"


def node_class_family(kind: str) -> str:
    """Schema family that holds this graph kind's node classes."""
    return "k2node" if kind == "blueprint" else "material_expression"


def _replacements(local_decls: dict[str, Decl], base_decls: dict[str, Decl], kind: str, schema) -> dict[str, tuple[str, str]]:
    """Node replacements by id: recreated rather than updated.

    Knowing every replacement before any verb is queued is what lets the plan
    sever the node's old links first.
    """
    result: dict[str, tuple[str, str]] = {}
    for identifier, decl in local_decls.items():
        before = base_decls.get(identifier)
        if before is None:
            continue
        if not (before.opaque and decl.opaque) and not same_class(schema, node_class_family(kind), decl.type_name, before.type_name):
            result[identifier] = ("node_class_changed", f"{identifier}: class changed {before.type_name} -> {decl.type_name}; node is recreated (GUID changes)")
        elif not decl.opaque and decl.positional() != before.positional():
            result[identifier] = ("positional_changed", f"{identifier}: positional arguments changed; node is recreated")
    return result


def diff_graph_section(local: Section, base: Section | None, plan: AssetPlan, kind: str, graph: str | None = None,
                       schema: SchemaLock | None = None) -> None:
    extra: dict[str, Any] = {"graph": graph} if graph else {}
    local_decls = {decl.id: decl for decl in local.decls() if decl.modifier is None}
    base_decls = {decl.id: decl for decl in base.decls() if decl.modifier is None} if base else {}
    implicit: dict[str, Decl | None] = dict(base.meta.get("implicit_nodes", {})) if base else {}
    if local.name == "function":
        implicit.setdefault("entry", None)
        implicit.setdefault("result", None)
    if kind == "material":
        implicit.setdefault(MATERIAL_OUT, None)
    replacements = _replacements(local_decls, base_decls, kind, schema)
    # nodes known to the base give pin lists for canonicalization on both sides
    known: dict[str, Decl] = dict(base_decls)
    known.update({identifier: decl for identifier, decl in implicit.items() if decl is not None})
    alias_map = dict((identifier, class_pin_aliases(schema, decl.type_name)) for identifier, decl in known.items())
    local_links = {_link_key(link, known, kind, alias_map): link for link in local.links()}
    base_links = {_link_key(link, known, kind, alias_map): link for link in base.links()} if base else {}
    removed_nodes = (set(base_decls) - set(local_decls)) | set(replacements)
    # Old links sever before any default is rewritten: executing SetNodeParam
    # against a still-linked pin is refused, and a replaced link must not keep
    # rejecting the new independent default.
    for key, link in base_links.items():
        if key in local_links or link.src in removed_nodes or link.dst in removed_nodes:
            continue
        plan.add("disconnect_pins", **{"from": link.src, "from_pin": link.src_pin, "to": link.dst, "to_pin": link.dst_pin}, **extra)
    for identifier in base_decls:
        if identifier not in local_decls:
            plan.add("delete_node", id=identifier, **extra)
    replaced_nodes: set[str] = set()
    for identifier, decl in local_decls.items():
        before = base_decls.get(identifier)
        replacement = replacements.get(identifier)
        if replacement is not None:
            plan.warn(replacement[0], replacement[1], decl.line)
            plan.add("delete_node", line=decl.line, id=identifier, **extra)
            replaced_nodes.add(identifier)
            before = None
        if before is None:
            if decl.opaque:
                plan.error("opaque_create", f"{identifier}: @opaque nodes cannot be created from text", decl.line)
                continue
            _create_node(decl, plan, kind, extra)
            continue
        _update_node(decl, before, plan, kind, extra)
    _diff_connects(local_links, base_links, local_decls, implicit, plan, kind, extra, replaced_nodes)


def _create_node(decl: Decl, plan: AssetPlan, kind: str, extra: dict[str, Any]) -> None:
    params = decl_params(decl)
    positional = decl.positional()
    args: dict[str, Any] = {"id": decl.id, "class": decl.type_name, "params": params}
    if kind == "blueprint":
        args["class"] = create_class(decl.type_name)
        if args["class"].rsplit(".", 1)[-1] in ("CustomEvent", "K2Node_CustomEvent"):
            args["custom_event_signature"] = event_signature(decl).to_raw()
    if positional:
        args["positional"] = positional
    if decl.pos is not None:
        args["x"], args["y"] = decl.pos
    if "disabled" in decl.flags:
        args["enabled"] = False
    if decl.annotations.get("comment"):
        args["comment"] = decl.annotations["comment"]
    plan.add("create_node", line=decl.line, **args, **extra)


def _update_node(decl: Decl, before: Decl, plan: AssetPlan, kind: str, extra: dict[str, Any]) -> None:
    if decl.opaque:
        for key, _ in decl.args:
            if key is not None:
                plan.error("opaque_param", f"{decl.id}: @opaque nodes cannot carry parameters", decl.line)
                break
    else:
        skip = {"pins"}
        diff_params(decl, before, plan, "set_node_param", skip=skip, id=decl.id, **extra)
        local_pins, base_pins = decl.keyed().get("pins"), before.keyed().get("pins")
        if local_pins != base_pins and local_pins is not None:
            plan.add("set_node_pins", line=decl.line, id=decl.id, count=int(local_pins), **extra)
    if decl.pos is not None and decl.pos != before.pos:
        plan.add("set_node_position", line=decl.line, id=decl.id, x=decl.pos[0], y=decl.pos[1], **extra)
    if ("disabled" in decl.flags) != ("disabled" in before.flags):
        plan.add("set_node_enabled", line=decl.line, id=decl.id, enabled="disabled" not in decl.flags, **extra)
    if decl.annotations.get("comment", "") != before.annotations.get("comment", ""):
        plan.add("set_node_comment", line=decl.line, id=decl.id, text=decl.annotations.get("comment", ""), **extra)


def canonical_pin(decl: Decl | None, pin: str | None, direction: str, kind: str,
                  aliases: dict[str, str] | None = None) -> str:
    """Comparison form of a pin reference (lower-case name or index string)."""
    if kind in ("material", "material_function"):
        if pin is None:
            return "0"
        lowered = pin.strip().lower()
        if lowered.isdigit():
            return lowered
        if decl is not None:
            names = [str(item).lower() for item in decl.meta.get("outputs" if direction == "out" else "inputs", [])]
            if lowered in names:
                return str(names.index(lowered))
            if direction == "out" and len(names) in (4, 5) and not any(names) and lowered in TEXTURE_ALIAS_INDEX:
                return TEXTURE_ALIAS_INDEX[lowered]
            if len(names) == 1 and direction == "in":
                return "0"
            if direction == "out" and len(names) <= 1:
                return "0"
        return lowered
    if pin is None:
        if decl is not None:
            from ..raw.nodes import single_visible_pin

            fake = {"pins": decl.meta.get("pins", []), "supported": not decl.opaque}
            single = single_visible_pin(fake, direction)
            if single is not None:
                return single.lower()
        return ""
    lowered = pin.strip().lower()
    if aliases:
        lowered = next((canonical.lower() for alias, canonical in aliases.items() if alias.lower() == lowered), lowered)
    return lowered


def _link_key(link: Link, decls: dict[str, Decl], kind: str, aliases: dict[str, dict[str, str]]) -> tuple[str, str, str, str]:
    src_pin = canonical_pin(decls.get(link.src), link.src_pin, "out", kind, aliases.get(link.src))
    if kind == "material" and link.dst == MATERIAL_OUT and link.dst not in decls:
        dst_pin = (link.dst_pin or "").replace("_", "").lower()
    else:
        dst_pin = canonical_pin(decls.get(link.dst), link.dst_pin, "in", kind, aliases.get(link.dst))
    return (link.src, src_pin, link.dst, dst_pin)


def _diff_connects(local_links: dict, base_links: dict, local_decls: dict, implicit: dict[str, Decl | None],
                   plan: AssetPlan, kind: str, extra: dict[str, Any], replaced_nodes: set[str]) -> None:
    connections = local_links.items()
    if kind == "blueprint":
        # Establish array wildcards from their providers before object consumers
        # can bind an element to a less specific class such as UObject.
        connections = sorted(connections, key=lambda pair: (pair[1].dst_pin or "").casefold()
                             not in ("array", "targetarray", "sourcearray"))
    for key, link in connections:
        if key in base_links and link.src not in replaced_nodes and link.dst not in replaced_nodes:
            continue
        if link.src not in local_decls and link.src not in implicit:
            continue
        plan.add("connect_pins", line=link.line, **{"from": link.src, "from_pin": link.src_pin, "to": link.dst, "to_pin": link.dst_pin}, **extra)
