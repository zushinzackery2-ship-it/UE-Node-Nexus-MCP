"""Pin contracts for authored nodes before an export supplies physical bindings."""

from .types import join_type_text, parse_type_text, type_text
from ..text.lexer import LexError
from ..raw.nodes import single_visible_pin
from .classes import node_name
from .callables import event_signature, local_records
from .interfaces import records as candidate_records
from .delegates import DELEGATE_NODES, record as delegate_record
from ..errors import SyncError

# Author spellings for engine-generated execution outputs that the reflected
# class does not expose as pin names. Native exports record the same aliases.
STATIC_PIN_ALIASES = {
    "IfThenElse": {"True": "then", "False": "else"},
}


def pin(name, direction, category="exec", **extra) -> dict:
    return dict(name=name, dir=direction, type=dict(category=category), **extra)


def parent_class(document) -> str:
    asset = document.section("asset")
    prop = asset.prop_map().get("ParentClass") if asset else None
    return prop.value if prop else ""


def function_record(decl, document, schema) -> dict | None:
    positional = decl.positional()
    if not positional:
        return None
    owner, _, name = positional[0].rpartition(".")
    if owner == "self":
        declared = local_records(document)
        if name in declared:
            return declared[name]
        owner = parent_class(document)
    return schema.function(owner, name) if schema and owner else None


def candidate_function(decl, document, schema=None):
    if node_name(decl.type_name) not in ("CallFunction", "Message") or not decl.positional():
        return None
    owner, _, name = decl.positional()[0].rpartition(".")
    records = local_records(document) if owner == "self" and document else candidate_records(owner)
    if records is None or (owner == "self" and name not in records):
        return None
    if name in records:
        return records[name]
    inherited = schema.function(owner, name) if schema else None
    return inherited or dict(missing=True, path=owner + "." + name, params=[])


def function_pins(record, physical=()):
    desired = [dict(item) for item in record.get("params", [])]
    if not record.get("pure", False):
        desired.extend([pin("execute", "in"), pin("then", "out")])
    desired.extend(dict(item) for item in physical if item.get("name") == "self")
    previous = {(item.get("name"), item.get("dir")): item for item in physical}
    result = []
    for item in desired:
        old = previous.get((item["name"], item.get("dir")), {})
        binding = old if type_text(old.get("type")) == type_text(item.get("type")) else {}
        result.append(dict(binding, **item))
    return result


def variable_pins(decl, document, section) -> list:
    name = decl.positional()[0] if decl.positional() else ""
    scopes = [section, document.section("variables"), document.section("components")]
    variable = next((scope.decl_map()[name] for scope in scopes if scope and name in scope.decl_map()), None)
    if variable is None:
        return []
    try:
        data_type = parse_type_text(join_type_text(variable.type_name, variable.args))
    except LexError:
        data_type = dict(category="object")
    if node_name(decl.type_name) == "VariableSet":
        return [pin("execute", "in"), pin("then", "out"), dict(name=name, dir="in", type=data_type),
                dict(name="Output_Get", dir="out", type=data_type)]
    return [dict(name=name, dir="out", type=data_type)]


def macro_reference(decl) -> str:
    """``StandardMacros.ForLoop``: the owner label and macro the positional names."""
    positional = decl.positional()
    return positional[0].strip() if positional else ""


def macro_pins(decl, schema) -> list | None:
    """The tunnel pins the macro graph declares, or None when the catalog lacks it."""
    if schema is None:
        return None
    reference = macro_reference(decl)
    if not reference:
        return None
    try:
        record = schema.macro(reference)
    except SyncError:
        return None
    pins = record.get("pins") if record else None
    return [dict(item) for item in pins] if pins else None


def node_pins(decl, document, section, schema=None, metadata=None) -> list:
    metadata = metadata if metadata is not None else decl.meta
    candidate = candidate_function(decl, document, schema)
    if candidate is not None:
        return function_pins(candidate, metadata.get("pins", []))
    if metadata.get("pins"):
        return metadata["pins"]
    name = node_name(decl.type_name)
    if name == "MacroInstance":
        pins = macro_pins(decl, schema)
        if pins:
            return pins
    info = schema.resolve_class("k2node", decl.type_name) if schema and not decl.opaque else None
    if info and info.pins and not info.dynamic_pins:
        return info.pins
    if name in DELEGATE_NODES:
        pins = [pin("execute", "in"), pin("then", "out"), pin("self", "in", "object")]
        if name in ("AddDelegate", "RemoveDelegate"):
            pins.append(pin("Delegate", "in", "delegate"))
        if name == "CallDelegate":
            try:
                record = delegate_record(decl, document, schema)
            except SyncError:
                record = None
            if record:
                resolved: dict = record
                pins.extend(dict(item) for item in resolved.get("params", []))
        return pins
    if name in ("CallFunction", "CallParentFunction", "Message"):
        record = function_record(decl, document, schema)
        if record is not None:
            return function_pins(record)
    if name in ("VariableGet", "VariableSet"):
        pins = variable_pins(decl, document, section)
        if pins:
            return pins
    if name == "IfThenElse":
        return [pin("execute", "in"), pin("then", "out"), pin("else", "out"), pin("Condition", "in", "bool")]
    if name == "Self":
        return [pin("self", "out", "object")]
    if name == "ExecutionSequence":
        count = decl.keyed().get("pins", "2")
        return [pin("execute", "in"), *[pin(f"then_{index}", "out") for index in range(int(count) if count.isdigit() else 2)]]
    if name == "CustomEvent":
        try:
            return [pin("then", "out"), pin("OutputDelegate", "out", "delegate"),
                    *[dict(item.to_raw(), dir="out") for item in event_signature(decl).inputs]]
        except LexError:
            return []
    if name in ("Event", "FunctionEntry"):
        return [pin("then", "out")]
    if name == "EnhancedInputAction":
        return [pin(event, "out") for event in ("Started", "Ongoing", "Triggered", "Canceled", "Completed")]
    if name in ("FunctionResult", "VariableSet"):
        return [pin("execute", "in"), *([pin("then", "out")] if name == "VariableSet" else [])]
    if name in ("DynamicCast", "ClassDynamicCast"):
        return [pin("execute", "in"), pin("then", "out"), pin("CastFailed", "out")]
    return info.pins if info else []


def class_pin_aliases(schema, spelling: str) -> dict[str, str]:
    """Author alias -> real pin name for the node class a spelling names."""
    if not spelling:
        return dict()
    aliases = dict(STATIC_PIN_ALIASES.get(node_name(spelling), dict()))
    if schema is None:
        return aliases
    try:
        info = schema.resolve_class("k2node", spelling)
    except SyncError:
        return aliases
    if info is not None:
        aliases.update(info.pin_aliases)
    return aliases


def canonical_pin(name, direction, pins, aliases: dict[str, str] | None = None) -> str | None:
    if name is None:
        return single_visible_pin(dict(pins=pins), direction)
    if aliases:
        lowered = name.strip().lower()
        name = next((canonical for alias, canonical in aliases.items() if alias.lower() == lowered), name)
    return next((item["name"] for item in pins if item.get("dir") == direction
                 and str(item.get("name", "")).lower() == str(name).lower()), name)
