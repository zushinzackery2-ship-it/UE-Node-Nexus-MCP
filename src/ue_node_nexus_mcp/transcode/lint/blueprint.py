"""Blueprint lint: variables, components and the orchestration of graph checks."""

from __future__ import annotations

from ..blueprint.callables import local_records
from ..blueprint.members import lint_members
from ..blueprint.node_ids import lint_node_ids
from ..blueprint.pins import parent_class
from ..blueprint.signature import parse_signature
from ..blueprint.types import join_type_text, parse_type_text
from ..blueprint.delegates import binding_links
from .blueprint_graph import lint_graph, lint_linked_defaults
from .blueprint_links import lint_links
from .lookup import AMBIGUOUS, lookup
from ..errors import DiagnosticSink
from ..raw.blueprint import VARIABLE_FLAG_ORDER
from ..schema.lock import ClassInfo, SchemaLock
from ..text.lexer import LexError
from ..text.model import Decl, Document

VARIABLE_META_VALUES = {"Category", "Tooltip", "RepNotify"}


def lint_blueprint(document: Document, schema: SchemaLock | None, sink: DiagnosticSink) -> None:
    lint_members(document, sink)
    lint_node_ids(document, sink)
    variables = document.section("variables")
    components = document.section("components")
    variable_names = set(variables.decl_map()) if variables else set()
    component_names = set(components.decl_map()) if components else set()
    functions = [parse_signature_or_none(section.args, section.line, sink) for section in document.find_sections("function")]
    function_names = {signature.name for signature in functions if signature is not None} | set(local_records(document))
    if variables is not None:
        for decl in variables.decls():
            _lint_variable(decl, sink)
    if components is not None:
        for decl in components.decls():
            _lint_component(decl, component_names, schema, sink)
    for section in document.sections:
        if section.name in ("graph", "function", "macro"):
            lint_graph(section, variable_names | component_names, function_names, schema, sink, parent_class(document), document)
            lint_linked_defaults(section, document, schema, sink)
            lint_links(section, document, schema, sink)
            binding_links(section, document, schema, sink)


def parse_signature_or_none(args: str, line: int, sink: DiagnosticSink):
    try:
        return parse_signature(args)
    except LexError as exc:
        sink.error("invalid_signature", f"bad function signature: {exc}", line=line, col=exc.col)
        return None


def _lint_variable(decl: Decl, sink: DiagnosticSink) -> None:
    try:
        parse_type_text(join_type_text(decl.type_name, decl.args))
    except LexError as exc:
        sink.error("invalid_type", f"variable {decl.id}: {exc}", line=decl.line)
    for key, value in decl.props:
        if value is None and key not in VARIABLE_FLAG_ORDER:
            sink.error("unknown_variable_flag", f"unknown variable flag {key!r}; known: {', '.join(VARIABLE_FLAG_ORDER)}", line=decl.line)
        elif value is not None and key not in VARIABLE_META_VALUES:
            sink.error("unknown_variable_meta", f"unknown variable metadata {key!r}; known: {', '.join(sorted(VARIABLE_META_VALUES))}", line=decl.line)


def _lint_component(decl: Decl, component_names: set[str], schema: SchemaLock | None, sink: DiagnosticSink) -> None:
    keyed = decl.keyed()
    parent = keyed.get("parent")
    if parent and parent not in component_names:
        sink.error("unknown_component_parent", f"component {decl.id}: parent {parent!r} is not declared", line=decl.line)
    for key in keyed:
        if key not in ("parent", "socket"):
            sink.error("unknown_component_arg", f"component {decl.id}: unknown argument {key!r} (parent=, socket=)", line=decl.line)
    if schema is None:
        return
    class_name = decl.marker_arg() if decl.inherited else decl.type_name
    info = lookup(sink, decl.line, "ambiguous_class", f"component class {class_name!r}", schema.resolve_class, "component", class_name or "")
    if info is AMBIGUOUS:
        return
    if not isinstance(info, ClassInfo):
        if not decl.inherited:
            sink.error("unknown_class", f"unknown component class {class_name!r}", line=decl.line)
        return
    for key, _ in decl.props:
        if key not in info.props:
            sink.error("unknown_property", f"{class_name} has no editable property {key!r}", line=decl.line)
