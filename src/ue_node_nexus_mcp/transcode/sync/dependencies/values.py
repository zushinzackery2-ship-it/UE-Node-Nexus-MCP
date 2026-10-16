"""Value and type hints: which text can name another object."""

from __future__ import annotations

import re

from ...blueprint.types import parse_type_text
from ...storage.paths import object_path
from ...text.lexer import LexError, parse_kv_list
from ...text.values import is_balanced_group, unquote

_REFERENCE = re.compile(r"(?:[\w./]+')?(/Game/[^\s\"'(),{}\[\]<>:=]+)(?::([^\s\"'(),{}\[\]<>:=]+))?'?")

# A reflected value type decides whether its text can name another object.
# ``plain`` types keep their text as data; unknown types stay lenient, because
# raw exports also write object references bare, without an ``Object(...)``
# wrapper, and a string-typed contract is the only proof a value is not one.
_REFERENCE_CATEGORIES = frozenset(("object", "class", "softobject", "softclass", "interface"))
_PLAIN_CATEGORIES = frozenset(("bool", "byte", "int", "int64", "string", "name", "text", "exec",
                               "wildcard", "float", "double", "real", "struct", "delegate", "mcdelegate"))
_CPP_REFERENCE_MARKS = ("*", "TSoftObjectPtr", "TSoftClassPtr", "TSubclassOf", "FSoftObjectPath", "FSoftClassPath")
_CPP_PLAIN = re.compile(
    r"^(?:const\s+)?(?:FString|FText|FName|bool|int8|int16|int32|int64|uint8|uint16|uint32|uint64|"
    r"float|double|FGuid|FVector2D|FVector4|FVector|FRotator|FQuat|FTransform|FLinearColor|FColor|FIntPoint|char)\b")
_SCHEMA_REFERENCE_KINDS = frozenset(("object", "class"))
_SCHEMA_PLAIN_KINDS = frozenset(("string", "text", "name", "number", "bool", "enum", "struct"))


def type_hint(description) -> str | None:
    """``'reference'``, ``'plain'`` or None for an unknown value contract."""
    if description is None:
        return None
    if isinstance(description, dict):
        category = str(description.get("category", "")).lower()
        if not category:
            return None
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


def schema_prop_hint(item) -> str | None:
    if not isinstance(item, dict):
        return None
    hint = type_hint(item.get("value_schema"))
    if hint:
        return hint
    kind = str(item.get("kind", ""))
    if kind in _SCHEMA_REFERENCE_KINDS:
        return "reference"
    return "plain" if kind in _SCHEMA_PLAIN_KINDS else None


def _directory_like(reference: str) -> bool:
    """A folder or fragment the mirror cannot resolve to one asset.

    A trailing slash or an empty segment is a directory string; it must not
    reach ``object_path`` and become a fabricated missing asset.
    """
    return reference.endswith("/") or "//" in reference


def value_dependencies(value: str | None, owner: str, hint: str | None = None) -> set[str]:
    if not value:
        return set()
    value = unquote(value.strip())
    if not value or hint == "plain":
        return set()
    if is_balanced_group(value):
        return set().union(*(value_dependencies(item, owner, hint) for _, item in parse_kv_list(value[1:-1])))
    for wrapper in ("Object", "Class", "SoftObject", "SoftClass"):
        if value.startswith(wrapper + "(") and value.endswith(")"):
            return value_dependencies(value[len(wrapper) + 1:-1], owner, "reference")
    match = _REFERENCE.fullmatch(value)
    if match is None:
        return set()
    reference = match.group(1)
    if _directory_like(reference):
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
