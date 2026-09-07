"""Material / MaterialFunction graph lint against the schema lock."""

from __future__ import annotations

import re
from typing import Any

from .errors import DiagnosticSink
from .model import Decl, Document, Link
from .schema_lock import ClassInfo, SchemaLock
from .values import is_boolean, is_numeric, normalize_value

TEXTURE_ALIASES = {"RGB": 0, "RGBA": 0, "R": 1, "G": 2, "B": 3, "A": 4}
SYNTHETIC_PARAMS = {"NamedRerouteUsage": {"DeclarationName"}}
SINGLE_OUTPUT_ALIASES = {"out", "output", "result", "value", "color", "rgb", "rgba", "return"}
_CUSTOM_INPUT_NAME_RE = re.compile(r'InputName\s*=\s*"([^"]+)"')
_CUSTOM_OUTPUT_NAME_RE = re.compile(r'OutputName\s*=\s*"([^"]+)"')


def lint_material_graph(document: Document, kind: str, schema: SchemaLock | None, sink: DiagnosticSink) -> None:
    from .lint import MATERIAL_OUTPUT_PROPERTIES

    if schema is not None:
        _lint_asset_props(document, schema, sink)
    graph = document.section("graph")
    if graph is None:
        return
    decls = graph.decl_map()
    infos: dict[str, ClassInfo | None] = {}
    for decl in graph.decls():
        infos[decl.id] = _lint_node(decl, schema, sink)
    normalized_outputs = {name.replace("_", "").lower(): name for name in MATERIAL_OUTPUT_PROPERTIES}
    for link in graph.links():
        if kind == "material" and link.dst == "out" and "out" not in decls:
            if link.dst_pin is None or link.dst_pin.replace("_", "").lower() not in normalized_outputs:
                sink.error("unknown_material_output", f"unknown material output pin {link.dst_pin!r}", line=link.line)
        elif link.dst in decls:
            _lint_input_pin(link, decls[link.dst], infos.get(link.dst), schema, sink)
        if link.src in decls:
            _lint_output_pin(link, decls[link.src], infos.get(link.src), schema, sink)


def _lint_asset_props(document: Document, schema: SchemaLock, sink: DiagnosticSink) -> None:
    info = schema.resolve_class("asset", document.header.cls)
    section = document.section("asset")
    if info is None or section is None:
        return
    for prop in section.props():
        prop_info = info.prop(prop.key)
        if prop_info is None:
            sink.error("unknown_property", f"{document.header.cls} has no editable property {prop.key!r}", line=prop.line)
        else:
            check_value(prop.key, prop.value, prop_info, prop.line, sink)


def _lint_node(decl: Decl, schema: SchemaLock | None, sink: DiagnosticSink) -> ClassInfo | None:
    if decl.opaque:
        for key, _ in decl.args:
            if key is not None:
                sink.error("opaque_param", "@opaque nodes cannot carry parameters; only move/delete/link them", line=decl.line)
                break
        return None
    if decl.positional():
        sink.error("unexpected_positional", f"material node {decl.id!r} does not take positional arguments", line=decl.line)
    if decl.modifier:
        sink.error("syntax", "modifiers are not valid on material nodes", line=decl.line)
    if schema is None:
        return None
    info = schema.resolve_class("material_expression", decl.type_name)
    if info is None:
        sink.error("unknown_class", f"unknown material expression class {decl.type_name!r}", line=decl.line)
        return None
    synthetic = SYNTHETIC_PARAMS.get(info.name.removeprefix("MaterialExpression"), set())
    for key, value in decl.args:
        if key is None or key in synthetic:
            continue
        prop_info = info.prop(key)
        if prop_info is None:
            sink.error("unknown_param", f"{decl.type_name} has no editable property {key!r}", line=decl.line)
            continue
        check_value(key, value, prop_info, decl.line, sink)
    if info.name.endswith("MaterialFunctionCall") and "MaterialFunction" not in decl.keyed():
        sink.error("missing_param", "MaterialFunctionCall needs MaterialFunction=/Game/... to resolve its pins", line=decl.line)
    return info


def check_value(name: str, value: str, prop_info: dict[str, Any], line: int, sink: DiagnosticSink) -> None:
    kind = str(prop_info.get("kind", ""))
    if kind == "enum":
        allowed = [str(item) for item in prop_info.get("enum_values") or []]
        if allowed and value not in allowed and value.rsplit("::", 1)[-1] not in allowed:
            sink.error("invalid_enum_value", f"{name}={value!r}; valid: {', '.join(allowed)}", line=line)
    elif kind == "bool":
        if not is_boolean(value):
            sink.error("invalid_bool", f"{name}={value!r} is not a boolean", line=line)
    elif kind == "number":
        if not is_numeric(value):
            sink.error("invalid_number", f"{name}={value!r} is not a number", line=line)
            return
        number = float(normalize_value(value))
        clamp_min, clamp_max = prop_info.get("clamp_min"), prop_info.get("clamp_max")
        if clamp_min not in (None, "") and number < float(clamp_min):
            sink.error("out_of_range", f"{name}={value} is below ClampMin {clamp_min}", line=line)
        if clamp_max not in (None, "") and number > float(clamp_max):
            sink.error("out_of_range", f"{name}={value} is above ClampMax {clamp_max}", line=line)
    elif kind == "struct" and value and not value.startswith("("):
        sink.error("invalid_struct", f"{name}={value!r} must be a (Field=Value,...) struct literal", line=line)


def _function_pins(decl: Decl, schema: SchemaLock | None) -> tuple[list[str], list[str]] | None:
    path = decl.keyed().get("MaterialFunction")
    if not path or schema is None:
        return None
    record = schema.material_function(path)
    if record is None:
        return None
    return [str(item.get("name", "")) for item in record.get("inputs") or []], [str(item.get("name", "")) for item in record.get("outputs") or []]


def _custom_input_names(decl: Decl) -> list[str] | None:
    """Read dynamic MaterialExpressionCustom pins from its Inputs property.

    The reflection schema intentionally exposes Custom.Inputs as a generic
    array and therefore reports a placeholder ``None`` input.  The actual
    names are part of the import-text value and are already required by the
    editor when the node is created.
    """
    raw = decl.keyed().get("Inputs", "")
    names = _CUSTOM_INPUT_NAME_RE.findall(raw)
    return names or None


def _custom_output_names(decl: Decl) -> list[str] | None:
    """Return the editor-visible output names for a dynamic Custom node."""
    raw = decl.keyed().get("AdditionalOutputs", "")
    names = _CUSTOM_OUTPUT_NAME_RE.findall(raw)
    return ["return", *names] if names else [""]


def _input_names(decl: Decl, info: ClassInfo | None) -> list[str] | None:
    if info is None:
        return None
    if info.name == "MaterialExpressionCustom":
        return _custom_input_names(decl) or info.inputs
    return info.inputs


def _output_names(decl: Decl, info: ClassInfo | None) -> list[str] | None:
    if info is None:
        return None
    if info.name == "MaterialExpressionCustom":
        return _custom_output_names(decl)
    return info.outputs


def _lint_input_pin(link: Link, decl: Decl, info: ClassInfo | None, schema: SchemaLock | None, sink: DiagnosticSink) -> None:
    if decl.opaque:
        return
    inputs: list[str] | None = None
    if info is not None and info.name.endswith("MaterialFunctionCall"):
        pins = _function_pins(decl, schema)
        inputs = pins[0] if pins else None
    elif info is not None:
        inputs = _input_names(decl, info)
    if inputs is None:
        return
    if link.dst_pin is None:
        if len(inputs) != 1:
            sink.error("pin_required", f"{decl.id} has {len(inputs)} inputs; name the target pin", line=link.line)
        return
    if link.dst_pin.isdigit():
        if int(link.dst_pin) >= len(inputs):
            sink.error("unknown_pin", f"{decl.id} has no input #{link.dst_pin}", line=link.line)
        return
    names = {_bare_pin(name) for name in inputs if name}
    if _bare_pin(link.dst_pin) not in names and not (len(inputs) == 1 and link.dst_pin.lower() in ("a", "in", "input")):
        sink.error("unknown_pin", f"{decl.id} ({decl.type_name}) has no input {link.dst_pin!r}; inputs: {', '.join(inputs) or '(unnamed)'}", line=link.line)


def _bare_pin(name: str) -> str:
    """``B (S)`` -> ``b``: function-call pins may carry a type suffix."""
    text = name.strip()
    if text.endswith(")") and " (" in text:
        text = text[: text.rfind(" (")]
    return text.lower()


def _lint_output_pin(link: Link, decl: Decl, info: ClassInfo | None, schema: SchemaLock | None, sink: DiagnosticSink) -> None:
    if decl.opaque:
        return
    outputs: list[str] | None = None
    if info is not None and info.name.endswith("MaterialFunctionCall"):
        pins = _function_pins(decl, schema)
        outputs = pins[1] if pins else None
    elif info is not None:
        outputs = _output_names(decl, info)
    if outputs is None:
        return
    if link.src_pin is None:
        if len(outputs) > 1 and any(outputs):
            sink.error("pin_required", f"{decl.id} has {len(outputs)} outputs; name the source pin", line=link.line)
        return
    pin = link.src_pin
    if pin.isdigit():
        if int(pin) >= len(outputs):
            sink.error("unknown_pin", f"{decl.id} has no output #{pin}", line=link.line)
        return
    lowered = pin.lower()
    if lowered in {name.lower() for name in outputs if name}:
        return
    if pin.upper() in TEXTURE_ALIASES and len(outputs) >= 4 and not any(outputs):
        return
    if len(outputs) == 1 and lowered in SINGLE_OUTPUT_ALIASES:
        return
    if len(outputs) > 1 and lowered in ("rgb", "rgba", "color", "vector"):
        return
    sink.error("unknown_pin", f"{decl.id} ({decl.type_name}) has no output {pin!r}; outputs: {', '.join(name or str(index) for index, name in enumerate(outputs))}", line=link.line)
