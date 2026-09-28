"""Pin contracts for authored nodes before an export supplies physical bindings."""

from ..bp_signature import parse_signature
from ..bp_types import join_type_text, parse_type_text
from ..lexer import LexError
from ..raw_blueprint_nodes import single_visible_pin
from .classes import node_name


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
        for section in document.find_sections("function"):
            try:
                signature = parse_signature(section.args)
            except LexError:
                continue
            if signature.name == name:
                params = [dict(item.to_raw(), dir="in") for item in signature.inputs]
                params.extend(dict(item.to_raw(), dir="out") for item in signature.outputs)
                return dict(params=params, pure="Pure" in signature.flags)
        owner = parent_class(document)
    return schema.function(owner, name) if schema and owner else None


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


def node_pins(decl, document, section, schema=None, metadata=None) -> list:
    metadata = metadata if metadata is not None else decl.meta
    if metadata.get("pins"):
        return metadata["pins"]
    name = node_name(decl.type_name)
    info = schema.resolve_class("k2node", decl.type_name) if schema and not decl.opaque else None
    if info and info.pins and not info.dynamic_pins:
        return info.pins
    if name in ("CallFunction", "CallParentFunction", "Message"):
        record = function_record(decl, document, schema)
        if record is not None:
            pins = [dict(item) for item in record.get("params", [])]
            if not record.get("pure", False):
                pins.extend([pin("execute", "in"), pin("then", "out")])
            return pins
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
    if name in ("Event", "CustomEvent", "FunctionEntry"):
        return [pin("then", "out")]
    if name == "EnhancedInputAction":
        return [pin(event, "out") for event in ("Started", "Ongoing", "Triggered", "Canceled", "Completed")]
    if name in ("FunctionResult", "VariableSet"):
        return [pin("execute", "in"), *([pin("then", "out")] if name == "VariableSet" else [])]
    if name in ("DynamicCast", "ClassDynamicCast"):
        return [pin("execute", "in"), pin("then", "out"), pin("CastFailed", "out")]
    return info.pins if info else []


def canonical_pin(name, direction, pins) -> str | None:
    if name is None:
        return single_visible_pin(dict(pins=pins), direction)
    return next((item["name"] for item in pins if item.get("dir") == direction
                 and str(item.get("name", "")).lower() == name.lower()), name)
