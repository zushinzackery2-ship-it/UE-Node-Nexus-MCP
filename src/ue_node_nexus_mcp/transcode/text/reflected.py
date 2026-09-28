"""Normalize ExportText containers using their reflected member contracts."""

from .lexer import split_top_level
from .values import unquote, quote
from .scalar import normalize


def nested_value(text, contract):
    result = normalize_reflected(text, contract)
    kind = contract.get("type", "").lower()
    return quote(result) if kind in ("fstring", "string", "str", "fname", "name", "ftext", "text") else result


def normalize_reflected(text, contract):
    kind = contract.get("kind")
    source = str(text).strip()
    if kind == "object":
        return normalize(source, "object")
    if kind not in ("struct", "array") or not source.startswith("(") or not source.endswith(")"):
        return normalize(text, contract.get("type", "text"))
    entries = split_top_level(source[1:-1], ",") if source[1:-1].strip() else []
    if kind == "array":
        element = contract["element"]
        values = [nested_value(unquote(entry.strip()), element) for entry in entries]
        return "(" + ",".join(values) + ")"
    fields = contract.get("fields", dict())
    supplied = dict()
    for entry in entries:
        name, separator, value = entry.partition("=")
        if not separator or name.strip() in supplied:
            return normalize(text, contract.get("type", "text"))
        supplied[name.strip()] = value.strip()
    values = dict()
    for name in fields.keys() | supplied.keys():
        member = fields.get(name, dict(type="text"))
        value = supplied.get(name, member.get("default"))
        if value is not None:
            values[name] = nested_value(unquote(value), member)
    if contract.get("path") == "/Script/Engine.MaterialInstanceBasePropertyOverrides":
        for name in list(values):
            flag = "bOverride_" + name
            if flag not in values and name.startswith("b"):
                flag = "bOverride_" + name[1:]
            if flag in values and values[flag] == "false":
                values.pop(name)
    return "(" + ",".join(name + "=" + values[name] for name in sorted(values)) + ")"
