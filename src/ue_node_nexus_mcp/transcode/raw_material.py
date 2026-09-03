"""Material / MaterialFunction raw export -> Document.

Raw ``graph`` shape::

    {"nodes": [{"guid", "class", "class_short", "name", "x", "y",
                "props": [{"name","type","value","default"}],
                "inputs": ["A","B"], "outputs": ["", "R", ...]}],
     "links": [{"from": guid, "from_out": 0, "to": guid, "to_in": 1}],
     "outputs": [{"from": guid, "from_out": 0, "property": "BaseColor"}]}
"""

from __future__ import annotations

from typing import Any

from .ids import material_node_candidate
from .model import Decl, Document, Link, Section
from .raw_common import (
    allocator_for,
    base_order,
    header_from_raw,
    non_default_params,
    order_ids,
    prop_defaults,
    prop_types,
    prop_values,
    props_section,
    require_kind,
)

MATERIAL_OUTPUT_ID = "out"
_CHANNEL_ALIASES = ("RGB", "R", "G", "B", "A")


def output_pin_name(node: dict[str, Any], index: int) -> str | None:
    """Pin label for a link source; None means the node's default (first) output.

    Vector-style nodes (TextureSample, Constant3/4Vector, ...) expose unnamed
    outputs [full, R, G, B(, A)]: the full output is implicit, channels use the
    R/G/B/A aliases the bridge already resolves.
    """
    outputs = [str(item) for item in node.get("outputs") or []]
    if len(outputs) <= 1 or index == 0 and not outputs[0]:
        return None
    if len(outputs) in (4, 5) and not any(outputs):
        return _CHANNEL_ALIASES[index] if 0 <= index < len(outputs) else str(index)
    if 0 <= index < len(outputs) and outputs[index]:
        return outputs[index]
    return str(index)


def input_pin_name(node: dict[str, Any], index: int) -> str | None:
    inputs = [str(item) for item in node.get("inputs") or []]
    if len(inputs) <= 1:
        return None
    if 0 <= index < len(inputs) and inputs[index]:
        return inputs[index]
    return str(index)


def node_decl(identifier: str, node: dict[str, Any]) -> Decl:
    params = non_default_params(node.get("props"))
    position = (int(node.get("x", 0)), int(node.get("y", 0)))
    meta = {
        "guid": str(node.get("guid", "")),
        "class": str(node.get("class", "")),
        "name": str(node.get("name", "")),
        "inputs": [str(item) for item in node.get("inputs") or []],
        "outputs": [str(item) for item in node.get("outputs") or []],
        "prop_types": prop_types(node.get("props")),
        "prop_defaults": prop_defaults(node.get("props")),
        "prop_values": prop_values(node.get("props")),
    }
    return Decl(id=identifier, type_name=str(node.get("class_short", "")), args=[(key, value) for key, value in params], pos=position, meta=meta)


def material_document(
    raw: dict[str, Any],
    previous_ids: dict[str, str] | None = None,
    previous_order: dict[str, list[str]] | None = None,
) -> tuple[Document, dict[str, str], dict[str, list[str]]]:
    kind = require_kind(raw, "material", "material_function")
    document = Document(header=header_from_raw(raw))
    document.sections.append(props_section(raw.get("props")))

    graph = raw.get("graph") or {}
    nodes: list[dict[str, Any]] = list(graph.get("nodes") or [])
    allocator = allocator_for(raw, previous_ids)
    if kind == "material":
        allocator.reserve(MATERIAL_OUTPUT_ID)

    ids: dict[str, str] = {}
    for node in nodes:
        guid = str(node.get("guid", ""))
        if guid in allocator.by_guid:
            ids[guid] = allocator.allocate(guid, "")
    for node in nodes:
        guid = str(node.get("guid", ""))
        if guid not in ids:
            candidate = material_node_candidate(str(node.get("class_short", "")), prop_values(node.get("props")))
            ids[guid] = allocator.allocate(guid, candidate, str(node.get("class_short", "node")))

    by_guid = {str(node.get("guid", "")): node for node in nodes}
    decls = {ids[guid]: node_decl(ids[guid], node) for guid, node in by_guid.items()}
    links: list[tuple[tuple[int, int], Link]] = []
    depends_on: dict[str, set[str]] = {}
    for link in graph.get("links") or []:
        src_guid, dst_guid = str(link.get("from", "")), str(link.get("to", ""))
        if src_guid not in by_guid or dst_guid not in by_guid:
            continue
        src_id, dst_id = ids[src_guid], ids[dst_guid]
        to_in = int(link.get("to_in", 0))
        depends_on.setdefault(dst_id, set()).add(src_id)
        links.append(((0, to_in), Link(src_id, output_pin_name(by_guid[src_guid], int(link.get("from_out", 0))), dst_id, input_pin_name(by_guid[dst_guid], to_in))))
    output_links: list[Link] = []
    for index, output in enumerate(graph.get("outputs") or []):
        src_guid = str(output.get("from", ""))
        if src_guid not in by_guid:
            continue
        output_links.append(Link(ids[src_guid], output_pin_name(by_guid[src_guid], int(output.get("from_out", 0))), MATERIAL_OUTPUT_ID, str(output.get("property", ""))))

    previous = (previous_order or {}).get("graph") if previous_order is not None else base_order(raw, "graph")
    sort_key = {identifier: (decl.pos[0] if decl.pos else 0, decl.pos[1] if decl.pos else 0, identifier) for identifier, decl in decls.items()}
    order = order_ids(list(decls), depends_on, sort_key, previous)
    position = {identifier: index for index, identifier in enumerate(order)}

    section = Section(name="graph")
    section.entries.extend(decls[identifier] for identifier in order)
    links.sort(key=lambda item: (position.get(item[1].dst, 0), item[0][1]))
    section.entries.extend(link for _, link in links)
    section.entries.extend(output_links)
    document.sections.append(section)
    return document, dict(allocator.by_guid), {"graph": order}
