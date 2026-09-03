"""Blueprint pin-type grammar: ``Array<Object(/Script/Engine.Actor)>`` <-> raw dict.

Raw dict (produced by the UE plugin and consumed by ``bp_variable_add``)::

    {"category": "object", "subcategory": "", "subobject": "/Script/Engine.Actor",
     "container": "none|array|set|map", "is_ref": false,
     "value_category": "", "value_subcategory": "", "value_subobject": ""}
"""

from __future__ import annotations

from typing import Any

from .lexer import LexError, read_group, read_type_name, skip_ws

_SIMPLE = {"bool", "byte", "int", "int64", "string", "name", "text", "exec", "wildcard", "float", "double"}
_WITH_OBJECT = {
    "object": "Object",
    "class": "Class",
    "softobject": "SoftObject",
    "softclass": "SoftClass",
    "struct": "Struct",
    "interface": "Interface",
    "delegate": "Delegate",
    "mcdelegate": "MulticastDelegate",
}
_KEYWORD_TO_CATEGORY = {label: category for category, label in _WITH_OBJECT.items()}
_KEYWORD_TO_CATEGORY["Enum"] = "byte"


def type_text(pin_type: dict[str, Any] | None) -> str:
    if not pin_type:
        return "wildcard"
    container = str(pin_type.get("container", "none") or "none").lower()
    base = _base_text(pin_type.get("category"), pin_type.get("subcategory"), pin_type.get("subobject"))
    if container == "array":
        text = f"Array<{base}>"
    elif container == "set":
        text = f"Set<{base}>"
    elif container == "map":
        value = _base_text(pin_type.get("value_category"), pin_type.get("value_subcategory"), pin_type.get("value_subobject"))
        text = f"Map<{base},{value}>"
    else:
        text = base
    if pin_type.get("is_ref"):
        text += "&"
    return text


def _base_text(category: Any, subcategory: Any, subobject: Any) -> str:
    category = str(category or "wildcard")
    subcategory = str(subcategory or "")
    subobject = str(subobject or "")
    if category == "real":
        return subcategory or "float"
    if category == "byte" and subobject:
        return f"Enum({subobject})"
    if category == "int" and subobject:
        return f"Enum({subobject})"
    if category in _WITH_OBJECT:
        return f"{_WITH_OBJECT[category]}({subobject})" if subobject else _WITH_OBJECT[category]
    if category in _SIMPLE:
        return category
    return category


def split_type_text(text: str) -> tuple[str, list[tuple[str | None, str]]]:
    """``Object(/Script/Engine.Actor)`` -> (``Object``, [(None, path)]); matches the parser's split."""
    text = text.strip()
    end = read_type_name(text, 0)
    name = text[:end]
    pos = skip_ws(text, end)
    if pos < len(text) and text[pos] == "(" and read_group(text, pos) == len(text):
        payload = text[pos + 1:-1].strip()
        return name, [(None, payload)] if payload else []
    return text, []


def join_type_text(type_name: str, args: list[tuple[str | None, str]]) -> str:
    positional = [value for key, value in args if key is None]
    if positional and type_name in _KEYWORD_TO_CATEGORY:
        return f"{type_name}({positional[0]})"
    return type_name


def parse_type_text(text: str) -> dict[str, Any]:
    text = text.strip()
    if not text:
        raise LexError("empty type", 1)
    is_ref = text.endswith("&")
    if is_ref:
        text = text[:-1].strip()
    result: dict[str, Any] = {"container": "none", "is_ref": is_ref}
    for container in ("Array", "Set", "Map"):
        if text.startswith(container + "<") and text.endswith(">"):
            inner = text[len(container) + 1:-1]
            result["container"] = container.lower()
            if container == "Map":
                key_text, value_text = _split_map(inner)
                result.update(_parse_base(key_text))
                value = _parse_base(value_text)
                result["value_category"] = value["category"]
                result["value_subcategory"] = value["subcategory"]
                result["value_subobject"] = value["subobject"]
            else:
                result.update(_parse_base(inner))
            return result
    result.update(_parse_base(text))
    return result


def _split_map(inner: str) -> tuple[str, str]:
    depth = 0
    for index, char in enumerate(inner):
        if char in "<(":
            depth += 1
        elif char in ">)":
            depth -= 1
        elif char == "," and depth == 0:
            return inner[:index].strip(), inner[index + 1:].strip()
    raise LexError("Map<K,V> needs two types", 1)


def _parse_base(text: str) -> dict[str, Any]:
    text = text.strip()
    end = read_type_name(text, 0)
    keyword = text[:end]
    pos = skip_ws(text, end)
    payload = ""
    if pos < len(text) and text[pos] == "(":
        close = read_group(text, pos)
        payload = text[pos + 1:close - 1].strip()
        pos = skip_ws(text, close)
    if pos != len(text):
        raise LexError(f"unexpected text in type: {text[pos:]}", pos + 1)
    lowered = keyword.lower()
    if lowered in ("float", "double"):
        return {"category": "real", "subcategory": lowered, "subobject": ""}
    if lowered in _SIMPLE:
        return {"category": lowered, "subcategory": "", "subobject": ""}
    if lowered == "integer":
        return {"category": "int", "subcategory": "", "subobject": ""}
    if keyword in _KEYWORD_TO_CATEGORY:
        if keyword != "Delegate" and not payload and keyword != "MulticastDelegate":
            raise LexError(f"{keyword}(...) needs a path", 1)
        return {"category": _KEYWORD_TO_CATEGORY[keyword], "subcategory": "", "subobject": payload}
    raise LexError(f"unknown type: {keyword}", 1)


def is_zero_default(value: str, type_name: str) -> bool:
    """True when a pin default is just the type's zero value (not worth writing)."""
    text = value.strip()
    if text == "":
        return True
    lowered = text.lower()
    if type_name in ("float", "double", "int", "int64", "byte", "real"):
        try:
            return float(text) == 0.0
        except ValueError:
            return False
    if type_name == "bool":
        return lowered == "false"
    if lowered == "none":
        return True
    if text.startswith("("):
        stripped = text.strip("()")
        return all(part.split("=", 1)[-1].strip().rstrip("0").rstrip(".") in ("", "0", "-0") for part in stripped.split(",") if part)
    return False
