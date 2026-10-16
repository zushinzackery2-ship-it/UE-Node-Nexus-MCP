"""Blueprint graph lint: node classes, positional forms, parameters and pins."""

from __future__ import annotations

from ..blueprint.call_host import call_spelling, host_name, required_host
from ..blueprint.classes import node_name
from ..blueprint.callables import event_signature
from ..blueprint.delegates import DELEGATE_NODES, lint as lint_delegate
from ..blueprint.pins import canonical_pin, node_pins
from ..blueprint.types import join_type_text, parse_type_text
from .lookup import AMBIGUOUS, lookup
from ..errors import DiagnosticSink
from ..text.lexer import LexError
from ..text.model import Decl, Section
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
    "GetInputActionValue": "/Game/Input/IA_X",
}
POSITIONAL_REQUIRED.update(dict((name, "Owner.Dispatcher (or self.Dispatcher)") for name in DELEGATE_NODES))
EXEC_PINS = {"execute", "then"}
IMPLICIT_PINS = {"self", "ReturnValue"}
# How each node uses the native function it names.
FUNCTION_REFERENCES = {"CallFunction": "call", "Message": "call", "CallParentFunction": "event", "Event": "event"}


def lint_graph(section: Section, member_names: set[str], function_names: set[str], schema: SchemaLock | None,
               sink: DiagnosticSink, parent: str = "", document=None) -> None:
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
        infos[decl.id], records[decl.id] = _lint_node(decl, member_names, function_names, schema, sink, parent, document)
    for link in section.links():
        for endpoint, pin in ((link.src, link.src_pin), (link.dst, link.dst_pin)):
            decl = decls.get(endpoint)
            if decl is None or decl.opaque:
                continue
            _lint_pin(decl, infos.get(endpoint), records.get(endpoint), pin, link.line, sink)


def lint_linked_defaults(section: Section, document, schema: SchemaLock | None, sink: DiagnosticSink) -> None:
    """Flag literal defaults on input pins the same graph already links.

    The canonical state omits a connected pin's default, so an authored value
    for it would never be written or kept; the author wants the pin freed or
    the value removed.
    """
    decls = section.decl_map()
    for link in section.links():
        decl = decls.get(link.dst)
        if decl is None or decl.opaque or decl.modifier == "local":
            continue
        target = canonical_pin(link.dst_pin, "in", node_pins(decl, document, section, schema))
        if not target:
            continue
        for key, _ in decl.args:
            if key is None or key == "pins" or key.startswith("prop:"):
                continue
            name = key[4:] if key.startswith("pin:") else key
            if name.casefold() == target.casefold():
                sink.warning("linked_pin_default_ignored",
                             f"{decl.id}: parameter {name!r} targets the linked pin {decl.id}.{target}; "
                             "the default is never written. Disconnect the pin or drop the parameter",
                             line=decl.line)


def _lint_node(decl: Decl, member_names: set[str], function_names: set[str], schema: SchemaLock | None,
               sink: DiagnosticSink, parent: str = "", document=None) -> tuple[ClassInfo | None, object]:
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
    if spelled in ("FunctionEntry", "FunctionResult"):
        sink.error("implicit_function_node", "function entry and result are declared by the function signature; use entry/result links", line=decl.line)
    if spelled == "CustomEvent" and positional:
        try:
            event_signature(decl)
        except LexError as exc:
            sink.error("invalid_signature", str(exc), line=decl.line)
    record = _function_record(decl, spelled, schema, sink)
    _lint_reference_defaults(decl, spelled, record, sink)
    if spelled in DELEGATE_NODES:
        record = lint_delegate(decl, document, schema, sink)
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
    if not isinstance(info, ClassInfo):
        sink.error("unknown_class", f"unknown Blueprint node class {decl.type_name!r}", line=decl.line)
        return None, record
    for key, _ in decl.args:
        if key is None or key == "pins":
            continue
        if key.startswith("prop:"):
            if key[5:] not in info.props:
                sink.error("unknown_param", f"{decl.type_name} has no editable property {key[5:]!r}", line=decl.line)
    return info, record


def _lint_reference_defaults(decl: Decl, spelled: str, record, sink: DiagnosticSink) -> None:
    """Refuse a literal default on a by-reference input pin.

    ``Array_Set``'s ``Item`` is ``int32&``: the editor discards the default at
    execution and exports it empty, so the authored value only comes back as a
    readback mismatch. A value node feeding the pin is the writable form.
    """
    if spelled not in FUNCTION_REFERENCES or record in (None, AMBIGUOUS) or "params" not in record:
        return
    reference = [str(item.get("name", "")) for item in record.get("params") or []
                 if (item.get("type") or dict()).get("is_ref")]
    if not reference:
        return
    for key, _ in decl.args:
        if key is None or key == "pins" or key.startswith("prop:"):
            continue
        name = key[4:] if key.startswith("pin:") else key
        match = next((item for item in reference if item.casefold() == name.casefold()), None)
        if match:
            sink.error("reference_pin_default",
                       f"{decl.id}: {match!r} is a reference input; the editor discards a literal default. "
                       "Connect a value node (for example MakeLiteralInt) instead",
                       line=decl.line)


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
    if record in (None, AMBIGUOUS) or "params" not in record or node_name(decl.type_name) not in ("CallFunction", "CallParentFunction", "CallDelegate"):
        return
    params = {str(item.get("name", "")) for item in record.get("params") or []}
    if pin not in params:
        sink.error("unknown_pin", f"{decl.positional()[0]} has no parameter {pin!r}; parameters: {', '.join(sorted(params))}", line=line)
