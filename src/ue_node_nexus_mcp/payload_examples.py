"""Hand-written example payloads served by `ue_capability_get(detail="examples")`.

Only operations whose minimal synthesized example would be misleading need an
entry here; everything else is derived from the wrapper signature.
"""

from __future__ import annotations

from typing import Any

OPERATION_EXAMPLES: dict[str, dict[str, Any]] = {
    "batch_execute": {
        "operations": [
            {"operation": "asset_compile", "payload": {"asset_path": "/Game/Path/M_Example.M_Example"}},
            {"operation": "asset_save", "payload": {"asset_path": "/Game/Path/M_Example.M_Example", "dry_run": False}},
        ],
        "continue_on_error": False,
    },
    "workflow_guide_get": {"category": "getting_started"},
    "task_submit": {
        "operation": "asset_compile",
        "payload": {"asset_path": "/Game/Path/M_Example.M_Example"},
    },
    "viewport_capture": {"filename": "before_fix", "dry_run": False},
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
            {
                "op": "create_node",
                "client_id": "branch",
                "node_class": "Branch",
                "position": {"x": 300, "y": 0},
            },
            {
                "op": "connect_pins",
                "from_node_id": "<from_node_GUID>",
                "from_pin": "Then",
                "to_node_id": "branch",
                "to_pin": "execute",
            },
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
    "landscape_layer_info_set": {
        "actor_path": "/Game/Maps/Demo.Demo:PersistentLevel.Landscape_0",
        "layers": [
            {
                "name": "Cliff",
                "layer_info_asset_path": "/Game/Maps/Demo_sharedassets/Cliff_LayerInfo.Cliff_LayerInfo",
                "create_if_missing": True,
                "no_weight_blend": False,
            },
        ],
        "dry_run": True,
        "save": False,
    },
    "level_actor_properties_set": {
        "actor_path": "/Game/Maps/Demo.Demo:PersistentLevel.PostProcessVolume_0",
        "properties": {"bUnbound": True, "Settings.bOverride_AutoExposureMethod": True, "Settings.AutoExposureMethod": "AEM_Manual",
                       "Settings.bOverride_AutoExposureBias": True, "Settings.AutoExposureBias": 0.0},
        "dry_run": True,
    },
    "node_params_set": {
        "asset_path": "/Game/BP/BP_Character.BP_Character",
        "graph_kind": "blueprint",
        "node_id": "<node_GUID>",
        "params": {"InputPinName": "value_as_string_or_number"},
        "dry_run": True,
    },
    "transcode_apply": {"asset_path": "/Game/Materials/M_Glass.M_Glass", "kind": "material", "ids": {"c_eps": "<node_GUID>"}, "dry_run": True,
                        "plan": [{"op": "set_node_param", "id": "c_eps", "name": "R", "value": "0.000002"}, {"op": "connect_pins", "from": "c_eps", "from_pin": None, "to": "out", "to_pin": "BaseColor"}]},
}
