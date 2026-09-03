"""Blueprint lint: variables, components, graph nodes and pins."""

from __future__ import annotations

from .bp_signature import parse_signature
from .bp_types import join_type_text, parse_type_text
from .errors import DiagnosticSink
from .lexer import LexError
from .model import Decl, Document, Section
from .raw_blueprint import VARIABLE_FLAG_ORDER
from .schema_lock import ClassInfo, SchemaLock

POSITIONAL_REQUIRED = {
    "CallFunction": "Owner.Function (or self.Function)",
    "CallParentFunction": "Owner.Function",
    "Message": "Interface.Function",
    "Event": "Owner.Function",
    "CustomEvent": "EventName[, Param: Type...]",
    "VariableGet": "VariableName",
    "VariableSet": "VariableName",
    "MacroInstance": "MacroOwner.MacroName",
    "DynamicCast": "/Script/Module.Class",
    "ClassDynamicCast": "/Script/Module.Class",
    "MakeStruct": "/Script/Module.Struct",
    "BreakStruct": "/Script/Module.Struct",
    "InputKey": "KeyName",
    "InputAction": "ActionName",
    "InputAxisEvent": "AxisName",
    "EnhancedInputAction": "/Game/Input/IA_X",
}
VARIABLE_META_VALUES = {"Category", "Tooltip", "RepNotify"}
EXEC_PINS = {"execute", "then"}
IMPLICIT_PINS = {"self", "ReturnValue"}


def lint_blueprint(document: Document, schema: SchemaLock | None, sink: DiagnosticSink) -> None:
    variables = document.section("variables")
    components = document.section("components")
    variable_names = set(variables.decl_map()) if variables else set()
    component_names = set(components.decl_map()) if components else set()
    functions = [parse_signature_or_none(section.args, section.line, sink) for section in document.find_sections("function")]
    function_names = {signature.name for signature in functions if signature is not None}
    if variables is not None:
        for decl in variables.decls():
            _lint_variable(decl, sink)
    if components is not None:
        for decl in components.decls():
            _lint_component(decl, component_names, schema, sink)
    for section in document.sections:
        if section.name in ("graph", "function", "macro"):
            _lint_graph(section, variable_names | component_names, function_names, schema, sink)


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
    info = schema.resolve_class("component", class_name or "")
    if info is None:
        if not decl.inherited:
            sink.error("unknown_class", f"unknown component class {class_name!r}", line=decl.line)
        return
    for key, _ in decl.props:
        if key not in info.props:
            sink.error("unknown_property", f"{class_name} has no editable property {key!r}", line=decl.line)


def _lint_graph(section: Section, member_names: set[str], function_names: set[str], schema: SchemaLock | None, sink: DiagnosticSink) -> None:
    infos: dict[str, ClassInfo | None] = {}
    decls = section.decl_map()
    for decl in section.decls():
        if decl.modifier == "local":
            try:
                parse_type_text(join_type_text(decl.type_name, decl.args))
            except LexError as exc:
                sink.error("invalid_type", f"local {decl.id}: {exc}", line=decl.line)
            continue
        infos[decl.id] = _lint_node(decl, member_names, function_names, schema, sink)
    for link in section.links():
        for endpoint, pin in ((link.src, link.src_pin), (link.dst, link.dst_pin)):
            decl = decls.get(endpoint)
            if decl is None or decl.opaque:
                continue
            info = infos.get(endpoint)
            _lint_pin(decl, info, pin, link.line, schema, sink)


def _lint_node(decl: Decl, member_names: set[str], function_names: set[str], schema: SchemaLock | None, sink: DiagnosticSink) -> ClassInfo | None:
    if decl.opaque:
        if any(key is not None for key, _ in decl.args):
            sink.error("opaque_param", "@opaque nodes cannot carry parameters", line=decl.line)
        return None
    positional = decl.positional()
    required = POSITIONAL_REQUIRED.get(decl.type_name)
    if required and not positional:
        sink.error("missing_positional", f"{decl.type_name} needs a positional argument: {required}", line=decl.line)
    if decl.type_name in ("VariableGet", "VariableSet") and positional:
        target = positional[0]
        if "." not in target and target not in member_names:
            sink.error("unknown_variable", f"{target!r} is not a declared variable or component", line=decl.line)
    if decl.type_name == "CallFunction" and positional and positional[0].startswith("self."):
        name = positional[0][5:]
        if name not in function_names:
            sink.warning("unknown_self_function", f"self.{name} is not declared in this file; it must exist on the parent class", line=decl.line)
    pins = decl.keyed().get("pins")
    if pins is not None and not pins.isdigit():
        sink.error("invalid_pins", "pins=N must be a positive integer", line=decl.line)
    if schema is None:
        return None
    info = schema.resolve_class("k2node", decl.type_name)
    if info is None:
        sink.error("unknown_class", f"unknown Blueprint node class {decl.type_name!r}", line=decl.line)
        return None
    for key, _ in decl.args:
        if key is None or key == "pins":
            continue
        if key.startswith("prop:"):
            if key[5:] not in info.props:
                sink.error("unknown_param", f"{decl.type_name} has no editable property {key[5:]!r}", line=decl.line)
    return info


def _lint_pin(decl: Decl, info: ClassInfo | None, pin: str | None, line: int, schema: SchemaLock | None, sink: DiagnosticSink) -> None:
    if pin is None:
        # the bridge resolves an omitted pin to the node's single visible data pin and
        # reports pin_not_found otherwise; nothing to check offline
        return
    if info is None or pin in EXEC_PINS or pin in IMPLICIT_PINS or pin.startswith("then_"):
        return
    if info.pins and not info.dynamic_pins:
        names = {str(item.get("name", "")) for item in info.pins}
        if pin not in names:
            sink.error("unknown_pin", f"{decl.id} ({decl.type_name}) has no pin {pin!r}; pins: {', '.join(sorted(names))}", line=line)
        return
    if decl.type_name in ("CallFunction", "CallParentFunction") and schema is not None and decl.positional():
        owner, _, name = decl.positional()[0].rpartition(".")
        record = schema.function(owner, name)
        if record is not None:
            params = {str(item.get("name", "")) for item in record.get("params") or []}
            if pin not in params:
                sink.error("unknown_pin", f"{owner}.{name} has no parameter {pin!r}; parameters: {', '.join(sorted(params))}", line=line)
