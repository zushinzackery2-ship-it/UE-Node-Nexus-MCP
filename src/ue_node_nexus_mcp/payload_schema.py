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
from .payload_schema_definitions import (
    GENERIC_OBJECT_SCHEMA,
    OPERATION_EXAMPLES,
    OPERATION_ITEM_SCHEMAS,
    PRIMITIVE_JSON_TYPES,
    WRAPPER_MODULES,
)

_wrapper_index: dict[str, Any] | None = None
_schema_cache: dict[str, dict[str, Any]] = {}


def _build_wrapper_index() -> dict[str, Any]:
    index: dict[str, Any] = {}
    for module_name in WRAPPER_MODULES:
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
        json_type = PRIMITIVE_JSON_TYPES.get(origin)
        return {"type": json_type} if json_type else {}

    if isinstance(annotation, type) and annotation in PRIMITIVE_JSON_TYPES:
        return {"type": PRIMITIVE_JSON_TYPES[annotation]}

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


def _apply_item_schema(operation: str, schema: dict[str, Any]) -> None:
    item_schema = OPERATION_ITEM_SCHEMAS.get(operation)
    if item_schema is None:
        return
    properties = schema.get("properties", {})
    if "operations" in properties and properties["operations"].get("type") == "array":
        properties["operations"]["items"] = item_schema
    if "layers" in properties and properties["layers"].get("type") == "array":
        properties["layers"]["items"] = item_schema


def payload_schema_for(operation: str) -> dict[str, Any]:
    cached = _schema_cache.get(operation)
    if cached is not None:
        return cached
    func = _get_wrapper_index().get(operation)
    schema = dict(GENERIC_OBJECT_SCHEMA) if func is None else derive_schema(func)
    _apply_item_schema(operation, schema)
    _schema_cache[operation] = schema
    return schema


def _example_value(prop: dict[str, Any], field_name: str) -> Any:
    enum = prop.get("enum")
    if enum:
        return enum[0]
    json_type = prop.get("type")
    if json_type == "string":
        if field_name == "asset_path" or field_name.endswith("_path"):
            return "/Game/Path/Asset.Asset"
        return f"<{field_name}>"
    if json_type == "integer":
        return 0
    if json_type == "number":
        return 0.0
    if json_type == "boolean":
        return False
    if json_type == "array":
        return []
    if json_type == "object":
        return {}
    return None


def example_payload_for(operation: str) -> dict[str, Any]:
    """Return a hand-written example if available, otherwise synthesize a minimal
    call example from the derived schema."""
    manual = OPERATION_EXAMPLES.get(operation)
    if manual is not None:
        return dict(manual)
    schema = payload_schema_for(operation)
    properties = schema.get("properties", {})
    example: dict[str, Any] = {}
    for field in schema.get("required", []):
        example[field] = _example_value(properties.get(field, {}), field)
    if "dry_run" in properties and "dry_run" not in example:
        example["dry_run"] = properties["dry_run"].get("default", True)
    return example
