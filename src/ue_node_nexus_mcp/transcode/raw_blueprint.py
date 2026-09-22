"""Blueprint raw export -> Document (variables, components, defaults, graphs)."""

from __future__ import annotations

from typing import Any

from .bp_signature import signature_from_raw
from .bp_types import is_zero_default, split_type_text, type_text
from .ids import short_class_name
from .model import Bare, Decl, Document, Prop, Section
from .raw_blueprint_nodes import IMPLICIT_ID_NAMES, assign_node_ids, build_links, node_decl
from .raw_common import allocator_for, base_order, claim_section_ids, header_from_raw, non_default_params, order_ids, prop_defaults, prop_types, prop_values, props_section, require_kind, section_allocator

VARIABLE_FLAG_ORDER = (
    "InstanceEditable",
    "BlueprintReadOnly",
    "ExposeOnSpawn",
    "Private",
    "ExposeToCinematics",
    "Replicated",
    "Transient",
    "SaveGame",
    "Config",
    "Multiline",
)


def variable_decl(variable: dict[str, Any]) -> Decl:
    props: list[tuple[str, str | None]] = []
    category = str(variable.get("category", "") or "")
    if category and category != "Default":
        props.append(("Category", category))
    tooltip = str(variable.get("tooltip", "") or "")
    if tooltip:
        props.append(("Tooltip", tooltip))
    flags = set(variable.get("flags") or [])
    rep_notify = str(variable.get("rep_notify", "") or "")
    for flag in VARIABLE_FLAG_ORDER:
        if flag == "Replicated" and rep_notify:
            props.append(("RepNotify", rep_notify))
            continue
        if flag in flags:
            props.append((flag, None))
    default = variable.get("default")
    full_type = type_text(variable.get("type"))
    type_name, type_args = split_type_text(full_type)
    # the bridge reads the effective value off the CDO, so zero/empty means "no default"
    if default in (None, "") or is_zero_default(str(default), full_type):
        default = None
    return Decl(
        id=str(variable.get("name", "")),
        type_name=type_name,
        args=type_args,
        default=None if default is None else str(default),
        props=props,
        meta={"guid": str(variable.get("guid", "")), "type": variable.get("type"), "metadata": variable.get("metadata") or {}},
    )


def typed_decl(name: str, pin_type: dict[str, Any] | None, default: Any, modifier: str | None = None) -> Decl:
    type_name, type_args = split_type_text(type_text(pin_type))
    return Decl(id=name, type_name=type_name, args=type_args, default=None if default in (None, "") else str(default), modifier=modifier, meta={"type": pin_type})


def component_decl(component: dict[str, Any]) -> Decl:
    class_short = str(component.get("class_short") or short_class_name(str(component.get("class", ""))))
    args: list[tuple[str | None, str]] = []
    inherited = bool(component.get("inherited"))
    if inherited:
        type_name = "@inherited"
        args.append((None, class_short))
    else:
        type_name = class_short
        parent = str(component.get("parent", "") or "")
        socket = str(component.get("socket", "") or "")
        if parent:
            args.append(("parent", parent))
        if socket:
            args.append(("socket", socket))
    props = [(key, value) for key, value in non_default_params(component.get("props"))]
    return Decl(
        id=str(component.get("name", "")),
        type_name=type_name,
        args=args,
        props=props,
        meta={
            "guid": str(component.get("guid", "")),
            "class": str(component.get("class", "")),
            "inherited": inherited,
            "prop_types": prop_types(component.get("props")),
            "prop_defaults": prop_defaults(component.get("props")),
            "prop_values": prop_values(component.get("props")),
        },
    )


def signature_text(name: str, signature: dict[str, Any]) -> str:
    return signature_from_raw(name, signature).text()


def _is_ghost_event(node: dict[str, Any]) -> bool:
    """Editor template scaffolding: a disabled, unlinked override event (BeginPlay/Tick...).

    Hidden from the text; declaring the event in text adopts the ghost on push.
    """
    if node.get("class_short") != "Event" or node.get("enabled", True):
        return False
    return not any(pin.get("linked") for pin in node.get("pins") or [])


def _visible_nodes(graph: dict[str, Any]) -> list[dict[str, Any]]:
    """Nodes the text carries, in raw order; editor template scaffolding stays hidden."""
    return [node for node in graph.get("nodes") or [] if not _is_ghost_event(node)]


def _graph_guids(graphs: list[dict[str, Any]]) -> list[list[str]]:
    """Guids of every section ``graph_section`` allocates, in allocation order."""
    return [[str(node.get("guid", "")) for node in _visible_nodes(graph)] for graph in graphs]


def graph_section(graph: dict[str, Any], claimed: dict[str, str], used: set[str], previous_order: list[str] | None) -> tuple[Section, dict[str, str]]:
    """Build one graph section; ids stay unique across every graph of the Blueprint."""
    kind = str(graph.get("kind", "ubergraph"))
    name = str(graph.get("name", ""))
    nodes = _visible_nodes(graph)
    allocator = section_allocator([str(node.get("guid", "")) for node in nodes], claimed, used)
    if kind == "function":
        section = Section(name="function", args=signature_text(name, graph.get("signature") or {}))
        for local in (graph.get("signature") or {}).get("locals") or []:
            section.entries.append(typed_decl(str(local.get("name", "")), local.get("type"), local.get("default"), modifier="local"))
    elif kind == "macro":
        section = Section(name="macro", args=name)
    else:
        section = Section(name="graph", args=name)
    ids = assign_node_ids(nodes, allocator)
    decls = {ids[str(node.get("guid", ""))]: node_decl(ids[str(node.get("guid", ""))], node) for node in nodes}
    links, depends_on = build_links(graph, ids)
    sort_key = {identifier: (decl.pos[0] if decl.pos else 0, decl.pos[1] if decl.pos else 0, identifier) for identifier, decl in decls.items()}
    order = order_ids(list(decls), depends_on, sort_key, previous_order)
    position = {identifier: index for index, identifier in enumerate(order)}
    # FunctionEntry / FunctionResult are derived from the signature: keep them
    # addressable as ``entry`` / ``result`` in links but do not print them.
    implicit = {identifier: decl for identifier, decl in decls.items() if decl.meta.get("class_short") in IMPLICIT_ID_NAMES}
    section.meta["implicit_nodes"] = implicit
    section.entries.extend(decls[identifier] for identifier in order if identifier not in implicit)
    pin_index = {identifier: {name: index for index, name in enumerate(decl.meta.get("pin_names", []))} for identifier, decl in decls.items()}
    links.sort(key=lambda link: (position.get(link.dst, 0), pin_index.get(link.dst, {}).get(link.dst_pin or "", 0), position.get(link.src, 0)))
    section.entries.extend(links)
    return section, dict(allocator.by_guid)


def blueprint_document(
    raw: dict[str, Any],
    previous_ids: dict[str, str] | None = None,
    previous_order: dict[str, list[str]] | None = None,
) -> tuple[Document, dict[str, str], dict[str, list[str]]]:
    require_kind(raw, "blueprint")
    blueprint = raw.get("blueprint") or {}
    document = Document(header=header_from_raw(raw))

    asset = props_section(raw.get("props"))
    # Both are fixed at creation and rejected as edits afterwards, but without
    # them the mirror cannot say what to build: a macro library and an actor
    # Blueprint differ only by type, an interface only by parent.
    blueprint_type = str(blueprint.get("blueprint_type", "") or "")
    if blueprint_type:
        asset.entries.insert(0, Prop(key="BlueprintType", value=blueprint_type, type_name="EBlueprintType"))
    parent = str(blueprint.get("parent_class", "") or "")
    if parent:
        asset.entries.insert(0, Prop(key="ParentClass", value=parent, type_name="UClass"))
    document.sections.append(asset)

    variables = Section(name="variables")
    variables.entries.extend(variable_decl(item) for item in blueprint.get("variables") or [])
    if variables.entries:
        document.sections.append(variables)

    components = Section(name="components")
    components.entries.extend(component_decl(item) for item in blueprint.get("components") or [])
    if components.entries:
        document.sections.append(components)

    defaults = props_section(blueprint.get("defaults"), name="defaults")
    if defaults.entries:
        document.sections.append(defaults)

    dispatchers = Section(name="dispatchers")
    for dispatcher in blueprint.get("dispatchers") or []:
        params = ", ".join(f"{param.get('name', '')}: {type_text(param.get('type'))}" for param in dispatcher.get("params") or [])
        dispatchers.entries.append(Bare(text=f"{dispatcher.get('name', '')}({params})"))
    if dispatchers.entries:
        document.sections.append(dispatchers)

    interfaces = Section(name="interfaces")
    interfaces.entries.extend(Bare(text=str(path)) for path in blueprint.get("interfaces") or [])
    if interfaces.entries:
        document.sections.append(interfaces)

    graphs = list(blueprint.get("graphs") or [])
    known_ids = dict(allocator_for(raw, previous_ids).by_guid)
    claimed, used = claim_section_ids(_graph_guids(graphs), known_ids)
    ids: dict[str, str] = {}
    order: dict[str, list[str]] = {}
    for graph in graphs:
        key = f"graph:{graph.get('name', '')}"
        previous = (previous_order or {}).get(key) if previous_order is not None else base_order(raw, key)
        section, graph_ids = graph_section(graph, claimed, used, previous)
        used.update(graph_ids.values())
        ids.update(graph_ids)
        order[key] = [decl.id for decl in section.decls() if decl.modifier != "local"]
        document.sections.append(section)
    return document, ids, order
