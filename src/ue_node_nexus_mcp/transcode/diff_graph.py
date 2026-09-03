"""Node / link diff shared by material and Blueprint graphs."""

from __future__ import annotations

from typing import Any

from .diff_common import decl_params, diff_params
from .model import Decl, Link, Section
from .plan import AssetPlan

TEXTURE_ALIAS_INDEX = {"rgb": "0", "rgba": "0", "r": "1", "g": "2", "b": "3", "a": "4"}
MATERIAL_OUT = "out"


def diff_graph_section(local: Section, base: Section | None, plan: AssetPlan, kind: str, graph: str | None = None) -> None:
    extra: dict[str, Any] = {"graph": graph} if graph else {}
    local_decls = {decl.id: decl for decl in local.decls() if decl.modifier is None}
    base_decls = {decl.id: decl for decl in base.decls() if decl.modifier is None} if base else {}
    implicit: dict[str, Decl | None] = dict(base.meta.get("implicit_nodes", {})) if base else {}
    if local.name == "function":
        implicit.setdefault("entry", None)
        implicit.setdefault("result", None)
    if kind == "material":
        implicit.setdefault(MATERIAL_OUT, None)
    for identifier in base_decls:
        if identifier not in local_decls:
            plan.add("delete_node", id=identifier, **extra)
    for identifier, decl in local_decls.items():
        before = base_decls.get(identifier)
        if before is not None and before.type_name != decl.type_name and not (before.opaque and decl.opaque):
            plan.warn("node_class_changed", f"{identifier}: class changed {before.type_name} -> {decl.type_name}; node is recreated (GUID changes)", decl.line)
            plan.add("delete_node", line=decl.line, id=identifier, **extra)
            before = None
        if before is None:
            if decl.opaque:
                plan.error("opaque_create", f"{identifier}: @opaque nodes cannot be created from text", decl.line)
                continue
            _create_node(decl, plan, kind, extra)
            continue
        _update_node(decl, before, plan, kind, extra)
    _diff_links(local, base, local_decls, base_decls, implicit, plan, kind, extra)


def _create_node(decl: Decl, plan: AssetPlan, kind: str, extra: dict[str, Any]) -> None:
    params = decl_params(decl)
    positional = decl.positional()
    args: dict[str, Any] = {"id": decl.id, "class": decl.type_name, "params": params}
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
        if decl.positional() != before.positional():
            plan.warn("positional_changed", f"{decl.id}: positional arguments changed; node is recreated", decl.line)
            plan.add("delete_node", line=decl.line, id=decl.id, **extra)
            _create_node(decl, plan, kind, extra)
            return
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


def canonical_pin(decl: Decl | None, pin: str | None, direction: str, kind: str) -> str:
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
            from .raw_blueprint_nodes import single_visible_pin

            fake = {"pins": decl.meta.get("pins", []), "supported": not decl.opaque}
            single = single_visible_pin(fake, direction)
            if single is not None:
                return single.lower()
        return ""
    return pin.strip().lower()


def _link_key(link: Link, decls: dict[str, Decl], kind: str) -> tuple[str, str, str, str]:
    src_pin = canonical_pin(decls.get(link.src), link.src_pin, "out", kind)
    if kind == "material" and link.dst == MATERIAL_OUT and link.dst not in decls:
        dst_pin = (link.dst_pin or "").replace("_", "").lower()
    else:
        dst_pin = canonical_pin(decls.get(link.dst), link.dst_pin, "in", kind)
    return (link.src, src_pin, link.dst, dst_pin)


def _diff_links(local: Section, base: Section | None, local_decls: dict[str, Decl], base_decls: dict[str, Decl], implicit: dict[str, Decl | None], plan: AssetPlan, kind: str, extra: dict[str, Any]) -> None:
    # nodes known to the base give pin lists for canonicalization on both sides
    known: dict[str, Decl] = dict(base_decls)
    known.update({identifier: decl for identifier, decl in implicit.items() if decl is not None})
    local_links = {_link_key(link, known, kind): link for link in local.links()}
    base_links = {_link_key(link, known, kind): link for link in base.links()} if base else {}
    removed_nodes = set(base_decls) - set(local_decls)
    for key, link in base_links.items():
        if key in local_links:
            continue
        if link.src in removed_nodes or link.dst in removed_nodes:
            continue
        plan.add("disconnect_pins", **{"from": link.src, "from_pin": link.src_pin, "to": link.dst, "to_pin": link.dst_pin}, **extra)
    for key, link in local_links.items():
        if key in base_links:
            continue
        if link.src not in local_decls and link.src not in implicit:
            continue
        plan.add("connect_pins", line=link.line, **{"from": link.src, "from_pin": link.src_pin, "to": link.dst, "to_pin": link.dst_pin}, **extra)
