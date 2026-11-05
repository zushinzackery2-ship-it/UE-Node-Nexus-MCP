"""Value and type hints: which text can name another object."""

from __future__ import annotations

import re

from ...blueprint.types import parse_type_text
from ...storage.paths import asset_relative, object_path
from ...text.lexer import LexError, parse_kv_list
from ...text.values import is_balanced_group, unquote

_REFERENCE = re.compile(r"(?:[\w./]+')?(/[^\s\"'(),{}\[\]<>:=]+)(?::([^\s\"'(),{}\[\]<>:=]+))?'?")

# A reflected value type decides whether its text can name another object.
# ``plain`` types keep their text as data; unknown types stay lenient, because
# raw exports also write object references bare, without an ``Object(...)``
# wrapper, and a string-typed contract is the only proof a value is not one.
_REFERENCE_CATEGORIES = frozenset(("object", "class", "softobject", "softclass", "interface"))
_PLAIN_CATEGORIES = frozenset(("bool", "byte", "int", "int64", "string", "name", "text", "exec",
                               "float", "double", "real", "delegate", "mcdelegate"))
_CPP_REFERENCE_MARKS = ("*", "TSoftObjectPtr", "TSoftClassPtr", "TSubclassOf", "FSoftObjectPath", "FSoftClassPath")
_CPP_PLAIN = re.compile(
    r"^(?:const\s+)?(?:FString|FText|FName|bool|int8|int16|int32|int64|uint8|uint16|uint32|uint64|"
    r"float|double|FGuid|FVector2D|FVector4|FVector|FRotator|FQuat|FTransform|FLinearColor|FColor|FIntPoint|char)\b")
_SCHEMA_REFERENCE_KINDS = frozenset(("object", "class"))
_SCHEMA_PLAIN_KINDS = frozenset(("string", "text", "name", "number", "bool", "enum"))


def type_hint(description) -> str | dict | None:
    """Retain container/member contracts; leaves are reference, plain or unknown."""
    if description is None:
        return None
    if isinstance(description, dict):
        container = description.get("container", "none")
        if container in ("array", "set", "map"):
            key = type_hint(dict(description, container="none"))
            if container == "map":
                value = type_hint({field: description.get("value_" + field) for field in ("category", "subcategory", "subobject")})
                return dict(kind="map", key=key, value=value)
            return dict(kind="array", element=key)
        kind = description.get("kind")
        if kind in ("array", "set"):
            return dict(kind="array", element=type_hint(description.get("element")))
        if kind == "map":
            return dict(kind="map", key=type_hint(description.get("key")), value=type_hint(description.get("value")))
        if kind == "struct" and description.get("fields"):
            return dict(kind="struct", fields={name: type_hint(item) for name, item in description["fields"].items()})
        if kind in _SCHEMA_REFERENCE_KINDS:
            return "reference"
        if kind in _SCHEMA_PLAIN_KINDS:
            return "plain"
        category = str(description.get("category", "")).lower()
        if not category:
            return type_hint(description.get("type"))
        if category in _REFERENCE_CATEGORIES:
            return "reference"
        return "plain" if category in _PLAIN_CATEGORIES else None
    text = str(description)
    if not text:
        return None
    if any(mark in text for mark in _CPP_REFERENCE_MARKS):
        return "reference"
    if _CPP_PLAIN.match(text):
        return "plain"
    return None


def schema_prop_hint(item) -> str | dict | None:
    if not isinstance(item, dict):
        return None
    return type_hint(item.get("value_schema") or item)


def _directory_like(reference: str) -> bool:
    """A folder or fragment the mirror cannot resolve to one asset.

    A trailing slash or an empty segment is a directory string; it must not
    reach ``object_path`` and become a fabricated missing asset.
    """
    return reference.endswith("/") or "//" in reference


def value_dependencies(value: str | None, owner: str, hint: str | dict | None = None) -> set[str]:
    if not value:
        return set()
    value = unquote(value.strip())
    if not value or hint == "plain":
        return set()
    if isinstance(hint, dict) and is_balanced_group(value):
        result = set()
        for name, item in parse_kv_list(value[1:-1]):
            if hint["kind"] == "map":
                pair = parse_kv_list(item[1:-1]) if is_balanced_group(item) else []
                for index, (_name, member) in enumerate(pair):
                    member_hint = hint.get("key" if index == 0 else "value")
                    result.update(value_dependencies(member, owner, member_hint))
                continue
            member_hint = hint.get("fields", {}).get(name) if hint["kind"] == "struct" else hint.get("element")
            result.update(value_dependencies(item, owner, member_hint))
        return result
    if is_balanced_group(value):
        return set().union(*(value_dependencies(item, owner, hint) for _, item in parse_kv_list(value[1:-1])))
    for wrapper in ("Object", "Class", "SoftObject", "SoftClass"):
        if value.startswith(wrapper + "(") and value.endswith(")"):
            return value_dependencies(value[len(wrapper) + 1:-1], owner, "reference")
    match = _REFERENCE.fullmatch(value)
    if match is None:
        return set()
    reference = match.group(1)
    if _directory_like(reference) or asset_relative(reference) is None:
        return set()
    asset = object_path(reference)
    # Serialized node connections reference subobjects owned by this asset.
    if match.group(2) and asset == owner:
        return set()
    # A Blueprint's generated class method is a member of the owning asset,
    # not a publication dependency on a second package.
    if asset == owner and reference.startswith(owner + "_C."):
        return set()
    return set((asset,))


def type_text_dependencies(text: str, owner: str) -> set[str]:
    try:
        parsed = parse_type_text(text)
    except LexError:
        return set()
    return set().union(*(value_dependencies(parsed.get(key) or "", owner, "reference")
                         for key in ("subobject", "value_subobject")))
