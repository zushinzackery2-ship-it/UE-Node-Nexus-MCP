"""Canonical scalar and named-struct values without container dependencies."""

import re

from .lexer import split_top_level
from .values import normalize_display, unquote
from .numbers import normalize_number, struct_member_type

NUMERIC = re.compile(r"^(?:u?int(?:8|16|32|64)?|byte|float|double|real|F?FloatProperty|F?DoubleProperty)$", re.I)
NUMBER = re.compile(r"^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?[fF]?$")
STRUCTS = set(("vector", "vector2d", "vector4", "rotator", "quat", "transform", "linearcolor", "color",
               "vector3f", "vector2f", "vector4f", "quat4f", "staticcomponentmask"))


def normalize(text: str, type_name: str) -> str:
    # Numbers reach the semantic state in the same form the mirror text shows them, so a
    # captured snapshot and the files rendered from it agree whatever the value's type is;
    # unknown struct types (BasePropertyOverrides, platform data) otherwise drifted.
    kind = type_name.removeprefix("F").lower()
    if kind.startswith("struct(") and kind.endswith(")"):
        kind = kind[7:-1].rsplit(".", 1)[-1].removeprefix("f")
    if kind in ("string", "str", "name") or type_name == "FText":
        return unquote(str(text))
    if kind == "object" or kind.startswith(("object(", "softobject(", "tobjectptr<", "tsoftobjectptr<")) or type_name.endswith("*"):
        reference = unquote(str(text)).strip()
        if reference.lower() in ("", "none", "null"):
            return "None"
        if "'" in reference:
            reference = reference.split("'", 2)[1]
        return reference
    text = normalize_display(str(text))
    if kind in ("bool", "boolean", "boolproperty") and text.lower() in ("true", "false", "1", "0"):
        return "true" if text.lower() in ("true", "1") else "false"
    if NUMERIC.fullmatch(type_name) and NUMBER.fullmatch(text.strip()):
        return normalize_number(text, kind)
    if kind in ("string", "str", "name", "text"):
        # Content, not source form: the emitter decides quoting, so storing a
        # quoted value here would escape it again on every round trip.
        return unquote(text)
    if kind in STRUCTS and text.startswith("(") and text.endswith(")"):
        values = []
        for item in split_top_level(text[1:-1], ","):
            name, separator, value = item.partition("=")
            child = struct_member_type(kind, name.strip())
            values.append(name.strip() + separator + normalize(value.strip(), child) if separator else normalize(item.strip(), "double"))
        if all("=" in item for item in values):
            values.sort(key=lambda item: item.partition("=")[0])
        return "(" + ",".join(values) + ")"
    return text
