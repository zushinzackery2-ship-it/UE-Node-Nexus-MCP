"""Static metadata used while deriving operation payload schemas."""

from __future__ import annotations

from typing import Any

WRAPPER_MODULES = (
    "batch_execute",
    "task_queue",
    "tools_animation",
    "tools_assets",
    "tools_audio",
    "tools_auto_index",
    "tools_blueprints",
    "tools_graphs",
    "tools_graph_writes",
    "tools_level_materials",
    "material_lint",
    "tools_materials",
    "tools_niagara",
    "tools_niagara_modules",
    "tools_niagara_properties",
    "tools_project",
    "tools_system",
    "tools_texture",
    "tools_transcode",
    "tools_viewport",
    "workflow_guides",
)

PRIMITIVE_JSON_TYPES: dict[type, str] = {
    str: "string",
    bool: "boolean",
    int: "integer",
    float: "number",
    dict: "object",
    list: "array",
}

GENERIC_OBJECT_SCHEMA: dict[str, Any] = {
    "type": "object",
    "description": "No typed wrapper is registered for this operation; pass bridge payload fields directly.",
}

OPERATION_ITEM_SCHEMAS: dict[str, dict[str, Any]] = {
    "batch_execute": {
        "type": "object",
        "required": ["operation"],
        "properties": {
            "operation": {"type": "string", "description": "Internal registry operation name (batch_execute itself is rejected)"},
            "payload": {"type": "object", "description": "Payload for that operation; per-item dry_run keeps its normal meaning"},
        },
    },
    "blueprint_components_patch": {
        "type": "object",
        "required": ["op"],
        "properties": {
            "op": {
                "type": "string",
                "enum": [
                    "add_component",
                    "remove_component",
                    "set_component_defaults",
                    "set_component_properties",
                ],
            },
            "component_class": {
                "type": "string",
                "description": "Module.ClassName or /Script path, e.g. /Script/Niagara.NiagaraComponent (add_component)",
            },
            "name": {"type": "string", "description": "Component variable name"},
            "parent": {"type": "string", "description": "Parent component name to attach to (add_component)"},
            "defaults": {
                "type": "object",
                "description": "Component template defaults; supports reflected properties plus RelativeTransform/material conveniences",
            },
            "properties": {"type": "object", "description": "Alias for defaults"},
        },
    },
    "graph_patch_apply": {
        "type": "object",
        "required": ["op"],
        "properties": {
            "op": {
                "type": "string",
                "enum": [
                    "connect_pins",
                    "disconnect_pins",
                    "set_node_param",
                    "create_node",
                    "delete_node",
                    "set_node_position",
                ],
            },
            "from_node_id": {
                "type": "string",
                "description": "Source node GUID, alias, object name, or same-patch client_id (connect/disconnect)",
            },
            "from_node": {"type": "string", "description": "Alias for from_node_id"},
            "from_pin_id": {
                "type": "string",
                "description": "Source pin GUID (connect/disconnect); omit if using from_pin",
            },
            "from_pin": {
                "type": "string",
                "description": "Source pin name as fallback when from_pin_id is not available (connect/disconnect)",
            },
            "to_node_id": {
                "type": "string",
                "description": "Target node GUID, alias, object name, or same-patch client_id (connect/disconnect)",
            },
            "to_node": {"type": "string", "description": "Alias for to_node_id"},
            "to_pin_id": {
                "type": "string",
                "description": "Target pin GUID (connect/disconnect); omit if using to_pin",
            },
            "to_pin": {
                "type": "string",
                "description": "Target pin name as fallback when to_pin_id is not available (connect/disconnect)",
            },
            "node_id": {
                "type": "string",
                "description": "Node GUID, alias, object name, or same-patch client_id (set_node_param/delete_node/set_node_position)",
            },
            "node": {"type": "string", "description": "Alias for node_id"},
            "name": {
                "type": "string",
                "description": "Pin name (set_node_param) or custom event name (K2Node_CustomEvent create_node fallback)",
            },
            "value": {"description": "Pin value as string/number/boolean (set_node_param)"},
            "class_path": {
                "type": "string",
                "description": "Blueprint node class path (create_node); node_class is also accepted",
            },
            "node_class": {
                "type": "string",
                "description": "Blueprint node class short name or path, e.g. Branch, K2Node_CallFunction, /Script/BlueprintGraph.K2Node_VariableGet (create_node)",
            },
            "client_id": {
                "type": "string",
                "description": "Stable temporary id for a node created earlier in the same patch",
            },
            "params": {
                "type": "object",
                "description": "Create-node config object; accepts the same fields as top-level create_node fields",
            },
            "variable_name": {
                "type": "string",
                "description": "K2Node_VariableGet/K2Node_VariableSet variable or component name",
            },
            "function_name": {
                "type": "string",
                "description": "K2Node_CallFunction or K2Node_Event function name",
            },
            "function_owner": {
                "type": "string",
                "description": "Owner class path for K2Node_CallFunction or K2Node_Event",
            },
            "event_name": {"type": "string", "description": "K2Node_CustomEvent event name"},
            "input_key": {"type": "string", "description": "K2Node_InputKey key name"},
            "input_action_name": {
                "type": "string",
                "description": "K2Node_InputAction action name",
            },
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
            "op": {
                "type": "string",
                "enum": [
                    "add_action_mapping",
                    "remove_action_mapping",
                    "add_axis_mapping",
                    "remove_axis_mapping",
                ],
            },
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
    "landscape_layer_info_set": {
        "type": "object",
        "required": ["name", "layer_info_asset_path"],
        "properties": {
            "name": {
                "type": "string",
                "description": "Landscape paint target layer name; must match the material layer name exactly",
            },
            "layer_info_asset_path": {
                "type": "string",
                "description": "LandscapeLayerInfoObject asset path to load or create",
            },
            "create_if_missing": {"type": "boolean", "default": False},
            "no_weight_blend": {
                "type": "boolean",
                "description": "Optional bNoWeightBlend value for new or empty LayerInfo assets",
            },
        },
    },
}

# Hand-written call examples live in payload_examples.py; re-exported here so
# payload_schema.py keeps a single import site for all static metadata.
from .payload_examples import OPERATION_EXAMPLES  # noqa: E402,F401
