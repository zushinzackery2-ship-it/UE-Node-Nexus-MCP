"""Blueprint lint: variables, components, graph nodes and pins."""

from __future__ import annotations

from ..blueprint.call_host import call_spelling, host_name, required_host
from ..blueprint.signature import parse_signature
from ..blueprint.types import join_type_text, parse_type_text
from ..blueprint.classes import node_name
from ..blueprint.members import lint_members
from ..blueprint.node_ids import lint_node_ids
from ..blueprint.pins import parent_class
from ..errors import DiagnosticSink
from ..text.lexer import LexError
from .lookup import AMBIGUOUS, lookup
from ..text.model import Decl, Document, Section
from ..raw.blueprint import VARIABLE_FLAG_ORDER
from ..schema.functions import callable_record, covers_calls, covers_events, event_record
from ..schema.lock import ClassInfo, SchemaLock

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
# How each node uses the native function it names.
FUNCTION_REFERENCES = {"CallFunction": "call", "Message": "call", "CallParentFunction": "event", "Event": "event"}


def lint_blueprint(document: Document, schema: SchemaLock | None, sink: DiagnosticSink) -> None:
    lint_members(document, sink)
    lint_node_ids(document, sink)
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
            _lint_graph(section, variable_names | component_names, function_names, schema, sink, parent_class(document))


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
    if info is None:
        if not decl.inherited:
            sink.error("unknown_class", f"unknown component class {class_name!r}", line=decl.line)
        return
    for key, _ in decl.props:
        if key not in info.props:
            sink.error("unknown_property", f"{class_name} has no editable property {key!r}", line=decl.line)


def _lint_graph(section: Section, member_names: set[str], function_names: set[str], schema: SchemaLock | None, sink: DiagnosticSink, parent: str = "") -> None:
    infos: dict[str, ClassInfo | None] = {}
    records: dict[str, object] = {}
    decls = section.decl_map()
    if section.name == "function":
        member_names = member_names | set(decl.id for decl in section.decls() if decl.modifier == "local")
    for decl in section.decls():
        if decl.modifier == "local":
            try:
                parse_type_text(join_type_text(decl.type_name, decl.args))
            except LexError as exc:
                sink.error("invalid_type", f"local {decl.id}: {exc}", line=decl.line)
            continue
        infos[decl.id], records[decl.id] = _lint_node(decl, member_names, function_names, schema, sink, parent)
    for link in section.links():
        for endpoint, pin in ((link.src, link.src_pin), (link.dst, link.dst_pin)):
            decl = decls.get(endpoint)
            if decl is None or decl.opaque:
                continue
            _lint_pin(decl, infos.get(endpoint), records.get(endpoint), pin, link.line, sink)


def _lint_node(decl: Decl, member_names: set[str], function_names: set[str], schema: SchemaLock | None,
               sink: DiagnosticSink, parent: str = "") -> tuple[ClassInfo | None, object]:
    """Check one node; returns its class and the function it names, for its pins."""
    if decl.props:
        sink.error("unsupported_node_properties",
                   "graph node parameters belong inside parentheses, for example CallFunction(Owner.Function, B=10)",
                   line=decl.line)
    if decl.opaque:
        if any(key is not None for key, _ in decl.args):
            sink.error("opaque_param", "@opaque nodes cannot carry parameters", line=decl.line)
        return None, None
    positional = decl.positional()
    spelled = node_name(decl.type_name)
    record = _function_record(decl, spelled, schema, sink)
    if call_spelling(decl.type_name) != decl.type_name:
        _lint_call_host(decl, record, sink)
    required = POSITIONAL_REQUIRED.get(spelled)
    if required and not positional:
        sink.error("missing_positional", f"{decl.type_name} needs a positional argument: {required}", line=decl.line)
    if spelled in ("VariableGet", "VariableSet") and positional:
        target = positional[0]
        if "." not in target and target not in member_names:
            sink.error("unknown_variable", f"{target!r} is not a declared variable or component", line=decl.line)
    if spelled == "CallFunction" and positional and positional[0].startswith("self."):
        _lint_self_call(decl, positional[0][5:], function_names, schema, sink, parent)
    pins = decl.keyed().get("pins")
    if pins is not None and not pins.isdigit():
        sink.error("invalid_pins", "pins=N must be a positive integer", line=decl.line)
    if schema is None:
        return None, record
    if spelled in FUNCTION_REFERENCES and positional:
        _lint_function_reference(decl, spelled, positional[0], record, schema, sink)
    info = lookup(sink, decl.line, "ambiguous_class", f"Blueprint node class {decl.type_name!r}", schema.resolve_class, "k2node", spelled)
    if info is AMBIGUOUS:
        return None, record
    if info is None:
        sink.error("unknown_class", f"unknown Blueprint node class {decl.type_name!r}", line=decl.line)
        return None, record
    for key, _ in decl.args:
        if key is None or key == "pins":
            continue
        if key.startswith("prop:"):
            if key[5:] not in info.props:
                sink.error("unknown_param", f"{decl.type_name} has no editable property {key[5:]!r}", line=decl.line)
    return info, record


def _function_record(decl: Decl, spelled: str, schema: SchemaLock | None, sink: DiagnosticSink):
    """The native function a call or event names, looked up once for every check of the node."""
    positional = decl.positional()
    if schema is None or spelled not in FUNCTION_REFERENCES or not positional:
        return None
    owner, separator, name = positional[0].rpartition(".")
    if not separator or owner == "self":
        return None
    return lookup(sink, decl.line, "ambiguous_function", f"{owner}.{name}", schema.function, owner, name)


def _lint_self_call(decl: Decl, name: str, function_names: set[str], schema: SchemaLock | None, sink: DiagnosticSink, parent: str) -> None:
    if name in function_names:
        return
    inherited = lookup(sink, decl.line, "ambiguous_function", f"{parent}.{name}", schema.function, parent, name) if schema and parent else None
    if inherited is None:
        sink.warning("unknown_self_function", f"self.{name} is not declared in this file; it must exist on the parent class", line=decl.line)


def _lint_function_reference(decl: Decl, spelled: str, reference: str, record, schema: SchemaLock, sink: DiagnosticSink) -> None:
    """A native function a node names must exist in the role the node gives it.

    Calls need a Blueprint-callable function. An Event implements, and a parent
    call invokes, a BlueprintEvent, which the callable index does not list; those
    are checked only against a catalog that states it indexed events as well.
    """
    owner, separator, function = reference.rpartition(".")
    if not separator or owner == "self" or owner.startswith("/Game/") or record is AMBIGUOUS:
        return
    calls = FUNCTION_REFERENCES[spelled] == "call"
    if not (covers_calls(schema) if calls else covers_events(schema)):
        return
    if calls and not callable_record(record):
        sink.error("unknown_function", f"{owner}.{function} is not a reflected Blueprint callable function", line=decl.line)
    elif not calls and not event_record(record):
        sink.error("unknown_event", f"{owner}.{function} is not a reflected BlueprintEvent function", line=decl.line)


def _lint_call_host(decl: Decl, record, sink: DiagnosticSink) -> None:
    """A specialised call spelling must be the host the function's metadata picks.

    ``CallFunction`` always is: the bridge chooses. A spelling that disagrees
    names a class that cannot call the function, and the bridge refuses it.
    """
    positional = decl.positional()
    if not positional:
        return
    owner, _, name = positional[0].rpartition(".")
    spelled = host_name(decl.type_name)
    if owner == "self":
        sink.error("node_class_mismatch", f"{decl.id}: a Blueprint function is called by CallFunction, not {spelled}", line=decl.line)
        return
    required = required_host(record) if record is not AMBIGUOUS else None
    if required is not None and required != spelled:
        sink.error("node_class_mismatch", f"{decl.id}: {owner}.{name} is called by {required}, not {spelled}; write CallFunction and the bridge picks it", line=decl.line)


def _lint_pin(decl: Decl, info: ClassInfo | None, record, pin: str | None, line: int, sink: DiagnosticSink) -> None:
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
    if record in (None, AMBIGUOUS) or "params" not in record or node_name(decl.type_name) not in ("CallFunction", "CallParentFunction"):
        return
    params = {str(item.get("name", "")) for item in record.get("params") or []}
    if pin not in params:
        sink.error("unknown_pin", f"{decl.positional()[0]} has no parameter {pin!r}; parameters: {', '.join(sorted(params))}", line=line)
