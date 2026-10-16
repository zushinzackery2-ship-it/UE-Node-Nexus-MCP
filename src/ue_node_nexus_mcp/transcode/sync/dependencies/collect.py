"""Per-section dependency collection with the edge class of every reference."""

from __future__ import annotations

from ...blueprint.classes import node_name
from ...blueprint.signature import parse_signature
from ...blueprint.types import join_type_text, parse_type_text
from ...errors import SyncError
from ...storage.paths import object_path
from ...text.lexer import LexError
from ...text.model import Document, Section
from .values import schema_prop_hint, type_hint, type_text_dependencies, value_dependencies

_FUNCTION_NODES = ("CallFunction", "CallParentFunction", "Message", "Event")
_POSITIONAL_REFERENCE_NODES = ("DynamicCast", "ClassDynamicCast", "MakeStruct", "BreakStruct", "SetFieldsInStruct")


def _collect(dependencies: set[str], kind: str, sink: dict[str, set[str]]) -> None:
    for dependency in dependencies:
        sink.setdefault(dependency, set()).add(kind)


def _collect_variable(decl, owner: str, sink: dict[str, set[str]]) -> None:
    full = join_type_text(decl.type_name, decl.args)
    _collect(type_text_dependencies(full, owner), "type", sink)
    try:
        hint = type_hint(parse_type_text(full))
    except LexError:
        hint = None
    _collect(value_dependencies(decl.default, owner, hint), "body", sink)


def _collect_signature(text: str, owner: str, sink: dict[str, set[str]]) -> None:
    try:
        parsed = parse_signature(text)
    except LexError:
        return
    for parameter in (*parsed.inputs, *parsed.outputs):
        _collect(type_text_dependencies(parameter.type_text, owner), "type", sink)
        try:
            hint = type_hint(parse_type_text(parameter.type_text))
        except LexError:
            hint = None
        _collect(value_dependencies(parameter.default, owner, hint), "body", sink)


def _node_function_params(decl, spelled: str, schema):
    """Parameter name -> reflected pin type for the function a call node names."""
    if schema is None or spelled not in _FUNCTION_NODES:
        return None
    positional = decl.positional()
    if not positional:
        return None
    owner_name, separator, function = positional[0].rpartition(".")
    if not separator or owner_name == "self":
        return None
    try:
        record = schema.function(owner_name, function)
    except SyncError:
        return None
    return dict((str(item.get("name", "")), item.get("type")) for item in (record or dict()).get("params", []))


def _node_prop_hint(decl, name: str, schema):
    if schema is None:
        return None
    try:
        info = schema.resolve_class("k2node", decl.type_name)
    except SyncError:
        return None
    return schema_prop_hint(info.props.get(name)) if info is not None else None


def _collect_node(decl, document, schema, owner: str, sink: dict[str, set[str]]) -> None:
    spelled = node_name(decl.type_name)
    params = _node_function_params(decl, spelled, schema)
    if spelled in _FUNCTION_NODES:
        positional = decl.positional()
        call_owner = positional[0].rpartition(".")[0] if positional else ""
        if call_owner.startswith("/Game/"):
            _collect(value_dependencies(call_owner, owner, "reference"), "signature", sink)
    for key, value in decl.args:
        if key is None:
            if spelled in _FUNCTION_NODES:
                # The positional is Owner.Function; its owner reference is a
                # signature edge and was handled above. Scanning it again as a
                # plain body value would mark the same edge both ways.
                continue
            positional_edge = spelled in _POSITIONAL_REFERENCE_NODES
            _collect(value_dependencies(value, owner, "reference" if positional_edge else None),
                     "type" if positional_edge else "body", sink)
        elif key == "pins":
            continue
        elif key.startswith("prop:"):
            _collect(value_dependencies(value, owner, _node_prop_hint(decl, key[5:], schema)), "body", sink)
        else:
            name = key[5:] if key.startswith("pin:") else key
            _collect(value_dependencies(value, owner, type_hint((params or {}).get(name))), "body", sink)
    for _, value in decl.props:
        _collect(value_dependencies(value, owner), "body", sink)
    _collect(value_dependencies(decl.default, owner), "body", sink)


def _collect_section(section: Section, kind: str, owner: str, document, schema, sink: dict[str, set[str]]) -> None:
    if section.name == "interfaces":
        for entry in section.bares():
            _collect(value_dependencies(entry.text, owner, "reference"), "type", sink)
        return
    if section.name == "dispatchers":
        for entry in section.bares():
            _collect_signature(entry.text, owner, sink)
        return
    if section.name == "function":
        _collect_signature(section.args, owner, sink)
    if section.name in ("graph", "function", "macro"):
        for decl in section.decls():
            if decl.modifier == "local":
                _collect_variable(decl, owner, sink)
            else:
                _collect_node(decl, document, schema, owner, sink)
        return
    for prop in section.props():
        edge = "superclass" if section.name == "asset" and prop.key in ("Parent", "ParentClass") else "body"
        _collect(value_dependencies(prop.value, owner, type_hint(prop.type_name)), edge, sink)
    if section.name == "variables":
        for decl in section.decls():
            _collect_variable(decl, owner, sink)
        return
    if section.name == "components":
        for decl in section.decls():
            _collect(value_dependencies(decl.type_name, owner, "reference"), "type", sink)
            for _, value in decl.args:
                _collect(value_dependencies(value, owner), "type", sink)
            for _, value in decl.props:
                _collect(value_dependencies(value, owner), "body", sink)
        return
    for decl in section.decls():
        _collect(type_text_dependencies(join_type_text(decl.type_name, decl.args), owner), "type", sink)
        _collect(value_dependencies(decl.default, owner), "body", sink)
        _collect(value_dependencies(decl.type_name, owner), "body", sink)
        for _, value in decl.args:
            _collect(value_dependencies(value, owner), "body", sink)
        for _, value in decl.props:
            _collect(value_dependencies(value, owner), "body", sink)


def dependency_kinds(document: Document, kind: str, schema=None) -> dict[str, set[str]]:
    """Every referenced asset with the dependency classes of its producing edges.

    ``type`` is a class or member-signature availability edge (variable types,
    implemented interfaces, cast targets), ``signature`` a call into another
    asset's declared function, ``body`` a graph or body value, and
    ``superclass`` a parent or component class. All classes order the same Kahn
    pass; only ``type`` and ``signature`` can be satisfied by publishing
    declaration shells before the full graphs.
    """
    sink: dict[str, set[str]] = {}
    owner = object_path(document.header.asset)
    for section in document.sections:
        _collect_section(section, kind, owner, document, schema, sink)
    if kind in ("blueprint", "asset", "scene"):
        sink.pop(owner, None)
    return sink


def document_dependencies(document: Document, kind: str, schema=None) -> set[str]:
    """Inspect AST values, including UE import-text structs and object arrays."""
    return set(dependency_kinds(document, kind, schema))
