"""Derive field-level payload schemas for internal operations.

The typed legacy wrapper functions in ``tools_*`` modules already encode every
operation's payload fields (parameter names map 1:1 to bridge payload keys),
their JSON types, ``Literal`` enums, optionality, and defaults. This module
introspects those signatures so the thin facade can serve a real
``payload_schema`` through ``ue_capability_get(operation=..., detail="schema")``
instead of a placeholder. There is no second hand-maintained schema source.
"""

from __future__ import annotations

import importlib
import inspect
import types
from typing import Any, Literal, Union, get_args, get_origin

from .contracts import ALL_OPERATIONS

# Modules where the typed operation wrappers are defined. A wrapper is matched to
# its operation by function name; the ``__module__`` filter keeps re-exported
# names (e.g. tools_niagara re-exporting tools_niagara_properties) from being
# counted twice.
_WRAPPER_MODULES = (
    "tools_assets",
    "tools_auto_index",
    "tools_blueprints",
    "tools_graphs",
    "tools_graph_writes",
    "tools_level_materials",
    "tools_materials",
    "tools_niagara",
    "tools_niagara_modules",
    "tools_niagara_properties",
    "tools_project",
    "tools_system",
)

_PRIMITIVE_JSON_TYPES: dict[type, str] = {
    str: "string",
    bool: "boolean",
    int: "integer",
    float: "number",
    dict: "object",
    list: "array",
}

_GENERIC_OBJECT_SCHEMA: dict[str, Any] = {
    "type": "object",
    "description": "No typed wrapper is registered for this operation; pass bridge payload fields directly.",
}

_wrapper_index: dict[str, Any] | None = None
_schema_cache: dict[str, dict[str, Any]] = {}


def _build_wrapper_index() -> dict[str, Any]:
    index: dict[str, Any] = {}
    for module_name in _WRAPPER_MODULES:
        module = importlib.import_module(f"{__package__}.{module_name}")
        for attr_name, obj in vars(module).items():
            if (
                attr_name in ALL_OPERATIONS
                and callable(obj)
                and getattr(obj, "__module__", None) == module.__name__
            ):
                index[attr_name] = obj
    return index


def _get_wrapper_index() -> dict[str, Any]:
    global _wrapper_index
    if _wrapper_index is None:
        _wrapper_index = _build_wrapper_index()
    return _wrapper_index


def _enum_json_type(values: list[Any]) -> str | None:
    if values and all(isinstance(value, str) for value in values):
        return "string"
    if values and all(isinstance(value, bool) for value in values):
        return "boolean"
    if values and all(isinstance(value, int) and not isinstance(value, bool) for value in values):
        return "integer"
    return None


def _property_schema(annotation: Any) -> dict[str, Any]:
    origin = get_origin(annotation)

    if origin in (Union, types.UnionType):
        args = get_args(annotation)
        non_none = [arg for arg in args if arg is not type(None)]
        nullable = len(non_none) != len(args)
        base = _property_schema(non_none[0]) if non_none else {}
        if nullable:
            base = {**base, "nullable": True}
        return base

    if origin is Literal:
        values = list(get_args(annotation))
        schema: dict[str, Any] = {"enum": values}
        json_type = _enum_json_type(values)
        if json_type is not None:
            schema["type"] = json_type
        return schema

    if origin is not None:
        json_type = _PRIMITIVE_JSON_TYPES.get(origin)
        return {"type": json_type} if json_type else {}

    if isinstance(annotation, type) and annotation in _PRIMITIVE_JSON_TYPES:
        return {"type": _PRIMITIVE_JSON_TYPES[annotation]}

    return {}


def derive_schema(func: Any) -> dict[str, Any]:
    signature = inspect.signature(func, eval_str=True)
    properties: dict[str, Any] = {}
    required: list[str] = []
    for name, param in signature.parameters.items():
        if param.kind in (inspect.Parameter.VAR_POSITIONAL, inspect.Parameter.VAR_KEYWORD):
            continue
        if param.annotation is inspect.Parameter.empty:
            prop: dict[str, Any] = {}
        else:
            prop = _property_schema(param.annotation)
        if param.default is inspect.Parameter.empty:
            required.append(name)
        else:
            prop["default"] = param.default
        properties[name] = prop

    schema: dict[str, Any] = {"type": "object", "properties": properties}
    if required:
        schema["required"] = required
    return schema


def payload_schema_for(operation: str) -> dict[str, Any]:
    cached = _schema_cache.get(operation)
    if cached is not None:
        return cached
    func = _get_wrapper_index().get(operation)
    schema = dict(_GENERIC_OBJECT_SCHEMA) if func is None else derive_schema(func)
    _schema_cache[operation] = schema
    return schema
