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
    "tools_animation",
    "tools_assets",
    "tools_audio",
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
    "tools_texture",
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

_OPERATION_ITEM_SCHEMAS: dict[str, dict[str, Any]] = {
    "blueprint_components_patch": {
        "type": "object",
        "required": ["op"],
        "properties": {
            "op": {"type": "string", "enum": ["add_component", "remove_component", "set_component_defaults", "set_component_properties"]},
            "component_class": {"type": "string", "description": "Module.ClassName or /Script path, e.g. /Script/Niagara.NiagaraComponent (add_component)"},
            "name": {"type": "string", "description": "Component variable name"},
            "parent": {"type": "string", "description": "Parent component name to attach to (add_component)"},
            "defaults": {"type": "object", "description": "Component template defaults; supports reflected properties plus RelativeTransform/material conveniences"},
            "properties": {"type": "object", "description": "Alias for defaults"},
        },
    },
    "graph_patch_apply": {
        "type": "object",
        "required": ["op"],
        "properties": {
            "op": {"type": "string", "enum": ["connect_pins", "disconnect_pins", "set_node_param", "create_node", "delete_node", "set_node_position"]},
            "from_node_id": {"type": "string", "description": "Source node GUID, alias, object name, or same-patch client_id (connect/disconnect)"},
            "from_node": {"type": "string", "description": "Alias for from_node_id"},
            "from_pin_id": {"type": "string", "description": "Source pin GUID (connect/disconnect); omit if using from_pin"},
            "from_pin": {"type": "string", "description": "Source pin name as fallback when from_pin_id is not available (connect/disconnect)"},
            "to_node_id": {"type": "string", "description": "Target node GUID, alias, object name, or same-patch client_id (connect/disconnect)"},
            "to_node": {"type": "string", "description": "Alias for to_node_id"},
            "to_pin_id": {"type": "string", "description": "Target pin GUID (connect/disconnect); omit if using to_pin"},
            "to_pin": {"type": "string", "description": "Target pin name as fallback when to_pin_id is not available (connect/disconnect)"},
            "node_id": {"type": "string", "description": "Node GUID, alias, object name, or same-patch client_id (set_node_param/delete_node/set_node_position)"},
            "node": {"type": "string", "description": "Alias for node_id"},
            "name": {"type": "string", "description": "Pin name (set_node_param) or custom event name (K2Node_CustomEvent create_node fallback)"},
            "value": {"description": "Pin value as string/number/boolean (set_node_param)"},
            "class_path": {"type": "string", "description": "Blueprint node class path (create_node); node_class is also accepted"},
            "node_class": {"type": "string", "description": "Blueprint node class short name or path, e.g. Branch, K2Node_CallFunction, /Script/BlueprintGraph.K2Node_VariableGet (create_node)"},
            "client_id": {"type": "string", "description": "Stable temporary id for a node created earlier in the same patch"},
            "params": {"type": "object", "description": "Create-node config object; accepts the same fields as top-level create_node fields"},
            "variable_name": {"type": "string", "description": "K2Node_VariableGet/K2Node_VariableSet variable or component name"},
            "function_name": {"type": "string", "description": "K2Node_CallFunction or K2Node_Event function name"},
            "function_owner": {"type": "string", "description": "Owner class path for K2Node_CallFunction or K2Node_Event"},
            "event_name": {"type": "string", "description": "K2Node_CustomEvent event name"},
            "input_key": {"type": "string", "description": "K2Node_InputKey key name"},
            "input_action_name": {"type": "string", "description": "K2Node_InputAction action name"},
            "axis_name": {"type": "string", "description": "K2Node_InputAxisEvent axis name"},
            "position": {"type": "object", "description": "Node position object with x/y"},
            "x": {"type": "integer"},
            "y": {"type": "integer"},
        },
    },
    "project_input_mappings_patch": {
        "type": "object",
        "required": ["op"],
        "properties": {
            "op": {"type": "string", "enum": ["add_action_mapping", "remove_action_mapping", "add_axis_mapping", "remove_axis_mapping"]},
            "action_name": {"type": "string", "description": "Action mapping name (action ops)"},
            "axis_name": {"type": "string", "description": "Axis mapping name (axis ops)"},
            "key": {"type": "string", "description": "UE key name, e.g. RightMouseButton, W, Space"},
            "scale": {"type": "number", "description": "Axis scale factor (axis ops, default 1.0)"},
            "shift": {"type": "boolean", "default": False},
            "ctrl": {"type": "boolean", "default": False},
            "alt": {"type": "boolean", "default": False},
            "cmd": {"type": "boolean", "default": False},
        },
    },
}

_OPERATION_EXAMPLES: dict[str, dict[str, Any]] = {
    "blueprint_components_patch": {
        "asset_path": "/Game/BP/BP_Character.BP_Character",
        "operations": [
            {
                "op": "add_component",
                "component_class": "/Script/Niagara.NiagaraComponent",
                "name": "AimVFX",
                "parent": "Mesh",
                "defaults": {
                    "Asset": "/Game/FX/NS_AimMagicCircle.NS_AimMagicCircle",
                    "bAutoActivate": False,
                    "RelativeTransform": {
                        "location": {"x": 0, "y": 0, "z": 50},
                        "rotation": {"pitch": 0, "yaw": 0, "roll": 0},
                        "scale": {"x": 1, "y": 1, "z": 1},
                    },
                },
            },
        ],
        "dry_run": True,
    },
    "graph_patch_apply": {
        "asset_path": "/Game/BP/BP_Character.BP_Character",
        "graph_kind": "blueprint",
        "operations": [
            {"op": "create_node", "client_id": "branch", "node_class": "Branch", "position": {"x": 300, "y": 0}},
            {"op": "connect_pins", "from_node_id": "<from_node_GUID>", "from_pin": "Then", "to_node_id": "branch", "to_pin": "execute"},
        ],
        "dry_run": True,
    },
    "project_input_mappings_patch": {
        "operations": [
            {"op": "add_action_mapping", "action_name": "Aim", "key": "RightMouseButton"},
            {"op": "add_axis_mapping", "axis_name": "MoveForward", "key": "W", "scale": 1.0},
        ],
        "dry_run": True,
    },
    "node_params_set": {
        "asset_path": "/Game/BP/BP_Character.BP_Character",
        "graph_kind": "blueprint",
        "node_id": "<node_GUID>",
        "params": {"InputPinName": "value_as_string_or_number"},
        "dry_run": True,
    },
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


def _apply_item_schema(operation: str, schema: dict[str, Any]) -> None:
    item_schema = _OPERATION_ITEM_SCHEMAS.get(operation)
    if item_schema is None:
        return
    properties = schema.get("properties", {})
    if "operations" in properties and properties["operations"].get("type") == "array":
        properties["operations"]["items"] = item_schema


def payload_schema_for(operation: str) -> dict[str, Any]:
    cached = _schema_cache.get(operation)
    if cached is not None:
        return cached
    func = _get_wrapper_index().get(operation)
    schema = dict(_GENERIC_OBJECT_SCHEMA) if func is None else derive_schema(func)
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
    manual = _OPERATION_EXAMPLES.get(operation)
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
