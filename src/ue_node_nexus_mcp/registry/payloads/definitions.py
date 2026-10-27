"""Static metadata used while deriving operation payload schemas."""

from __future__ import annotations

from typing import Any

WRAPPER_MODULES = (
    "facade.batch",
    "tasks.queue",
    "tools.animation",
    "tools.assets",
    "tools.asset_paths",
    "tools.audio",
    "tools.auto_index",
    "tools.blueprints",
    "tools.graphs",
    "tools.graph_writes",
    "tools.level_materials",
    "tools.level_instances",
    "geometry.tools",
    "tools.scene",
    "tools.material_lint",
    "tools.materials",
    "tools.niagara.systems",
    "tools.niagara.modules",
    "tools.niagara.properties",
    "tools.project",
    "tools.system",
    "tools.texture",
    "tools.transcode",
    "tools.viewport",
    "guides.catalog",
    "instances.tools.operations",
    "safety.tools",
    "safety.runtime.tools",
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


def _text(description: str) -> dict[str, Any]:
    return {"type": "string", "description": description}


def _choice(*values: str) -> dict[str, Any]:
    return {"type": "string", "enum": list(values)}


def _item(required: list[str], **properties: dict[str, Any]) -> dict[str, Any]:
    return {"type": "object", "required": required, "properties": properties}


OPERATION_ITEM_SCHEMAS: dict[str, dict[str, Any]] = {
    "batch_execute": _item(
        ["operation"],
        operation=_text("Internal registry operation name (batch_execute itself is rejected)"),
        payload={"type": "object", "description": "Payload for that operation; per-item dry_run keeps its normal meaning"},
    ),
    "blueprint_components_patch": _item(
        ["op"],
        op=_choice("add_component", "remove_component", "set_component_defaults", "set_component_properties"),
        component_class=_text("Module.ClassName or /Script path, e.g. /Script/Niagara.NiagaraComponent (add_component)"),
        name=_text("Component variable name"),
        parent=_text("Parent component name to attach to (add_component)"),
        defaults={"type": "object", "description": "Component template defaults; supports reflected properties plus RelativeTransform/material conveniences"},
        properties={"type": "object", "description": "Alias for defaults"},
    ),
    "graph_patch_apply": _item(
        ["op"],
        op=_choice("connect_pins", "disconnect_pins", "set_node_param", "create_node", "delete_node", "set_node_position"),
        from_node_id=_text("Source node GUID, alias, object name, or same-patch client_id (connect/disconnect)"),
        from_node=_text("Alias for from_node_id"),
        from_pin_id=_text("Source pin GUID (connect/disconnect); omit if using from_pin"),
        from_pin=_text("Source pin name as fallback when from_pin_id is not available (connect/disconnect)"),
        to_node_id=_text("Target node GUID, alias, object name, or same-patch client_id (connect/disconnect)"),
        to_node=_text("Alias for to_node_id"),
        to_pin_id=_text("Target pin GUID (connect/disconnect); omit if using to_pin"),
        to_pin=_text("Target pin name as fallback when to_pin_id is not available (connect/disconnect)"),
        node_id=_text("Node GUID, alias, object name, or same-patch client_id (set_node_param/delete_node/set_node_position)"),
        node=_text("Alias for node_id"),
        name=_text("Pin name (set_node_param) or custom event name (K2Node_CustomEvent create_node fallback)"),
        value={"description": "Pin value as string/number/boolean (set_node_param)"},
        class_path=_text("Blueprint node class path (create_node); node_class is also accepted"),
        node_class=_text("Blueprint node class short name or path, e.g. Branch, K2Node_CallFunction, /Script/BlueprintGraph.K2Node_VariableGet (create_node)"),
        client_id=_text("Stable temporary id for a node created earlier in the same patch"),
        params={"type": "object", "description": "Create-node config object; accepts the same fields as top-level create_node fields"},
        variable_name=_text("K2Node_VariableGet/K2Node_VariableSet variable or component name"),
        function_name=_text("K2Node_CallFunction or K2Node_Event function name"),
        function_owner=_text("Owner class path for K2Node_CallFunction or K2Node_Event"),
        event_name=_text("K2Node_CustomEvent event name"),
        delegate_name=_text("Multicast dispatcher property name"),
        delegate_owner=_text("Multicast dispatcher owner class path or self"),
        input_key=_text("K2Node_InputKey key name"),
        input_action_name=_text("K2Node_InputAction action name"),
        axis_name=_text("K2Node_InputAxisEvent axis name"),
        position={"type": "object", "description": "Node position object with x/y"},
        x={"type": "integer"},
        y={"type": "integer"},
    ),
    "project_input_mappings_patch": _item(
        ["op"],
        op=_choice("add_action_mapping", "remove_action_mapping", "add_axis_mapping", "remove_axis_mapping"),
        action_name=_text("Action mapping name (action ops)"),
        axis_name=_text("Axis mapping name (axis ops)"),
        key=_text("UE key name, e.g. RightMouseButton, W, Space"),
        scale={"type": "number", "description": "Axis scale factor (axis ops, default 1.0)"},
        shift={"type": "boolean", "default": False},
        ctrl={"type": "boolean", "default": False},
        alt={"type": "boolean", "default": False},
        cmd={"type": "boolean", "default": False},
    ),
    "landscape_layer_info_set": _item(
        ["name", "layer_info_asset_path"],
        name=_text("Landscape paint target layer name; must match the material layer name exactly"),
        layer_info_asset_path=_text("LandscapeLayerInfoObject asset path to load or create"),
        create_if_missing={"type": "boolean", "default": False},
        no_weight_blend={"type": "boolean", "description": "Optional bNoWeightBlend value for new or empty LayerInfo assets"},
    ),
}
