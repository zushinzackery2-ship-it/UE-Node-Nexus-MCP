"""Type-aware values. Text, inheritance and missing fields retain meaning."""

from __future__ import annotations

import re
from decimal import Decimal, InvalidOperation

from ...lexer import split_top_level
from ...values import quote, unquote

NUMERIC = re.compile(r"^(?:u?int(?:8|16|32|64)?|byte|float|double|real|F?FloatProperty|F?DoubleProperty)$", re.I)
NUMBER = re.compile(r"^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?[fF]?$")
STRUCTS = set(("vector", "vector2d", "vector4", "rotator", "quat", "transform", "linearcolor", "color"))


def normalize(text: str, type_name: str) -> str:
    kind = type_name.removeprefix("F").lower()
    if kind in ("bool", "boolean", "boolproperty") and text.lower() in ("true", "false", "1", "0"):
        return "true" if text.lower() in ("true", "1") else "false"
    if NUMERIC.fullmatch(type_name) and NUMBER.fullmatch(text.strip()):
        try:
            value = Decimal(text.strip().rstrip("fF"))
            return "0" if value == 0 else format(value.normalize(), "f")
        except InvalidOperation:
            return text
    if kind in ("string", "str", "name", "text"):
        return quote(unquote(text))
    if kind in STRUCTS and text.startswith("(") and text.endswith(")"):
        values = []
        for item in split_top_level(text[1:-1], ","):
            name, separator, value = item.partition("=")
            child = "transform" if value.strip().startswith("(") else "double"
            values.append(name.strip() + separator + normalize(value.strip(), child) if separator else normalize(item.strip(), "double"))
        return "(" + ",".join(values) + ")"
    return text


def value(text: str | None, type_name: str = "text", state: str = "explicit") -> dict:
    if text is None:
        return dict(state="missing", type=type_name)
    return dict(state=state, type=type_name, value=normalize(str(text), type_name))


def render(item: dict) -> str | None:
    if item.get("state") in ("missing", "default"):
        return None
    return item.get("value")


def field_values(current: dict, types: dict, defaults: dict, previous: dict | None = None) -> dict:
    result = dict()
    before = previous or dict()
    for key in sorted(current.keys() | defaults.keys()):
        type_name = str(types.get(key) or before.get(key, dict()).get("type") or "text")
        state = "explicit" if key in current else "default"
        text = current.get(key) if key in current else defaults[key]
        result[key] = value(text, type_name, state)
    return result
