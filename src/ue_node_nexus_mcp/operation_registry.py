from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from .contracts import (
    ALL_OPERATIONS,
    BRIDGE_OPERATIONS,
    DEFAULT_HIDDEN_OPERATIONS,
    LOCAL_MCP_OPERATIONS,
    OPERATION_FEATURES,
    READ_OPERATIONS,
    WRITE_OPERATIONS,
)


@dataclass(frozen=True)
class OperationSpec:
    name: str
    group: str
    kind: str
    risk: str
    summary: str
    bridge_operation: str | None
    hidden_from_legacy: bool = False
    local_mcp: bool = False
    default_response: str = "summary"


# Destructive or irreversible operations: data loss, broad package mutation, or
# editor lifecycle control. Everything else that mutates is "medium"; reads are "low".
HIGH_RISK_OPERATIONS = {
    "asset_delete",
    "folder_delete",
    "auto_index_clear",
    "editor_save_all",
    "editor_request_exit",
}

READ_SUMMARIES = {
    "asset_list": "List Unreal assets through the UE bridge.",
    "asset_get": "Read metadata for one Unreal asset.",
    "auto_index_status": "Read persistent Auto-Index status.",
    "auto_index_overview": "Read compact indexed project asset map.",
    "auto_index_tree_get": "Read Content Browser folder tree summary.",
    "auto_index_query": "Query the persisted asset index.",
    "auto_index_get": "Read one asset record from Auto-Index.",
    "auto_index_resolve_path": "Resolve a fuzzy asset name or path.",
    "auto_index_diff_registry": "Compare Auto-Index with live AssetRegistry.",
    "bridge_capabilities_get": "Read operations supported by loaded bridge modules.",
    "bridge_contract_check": "Compare local contract with loaded bridge modules.",
    "level_current_get": "Read current editor level identity.",
    "level_actors_list": "List actors in the current editor level.",
    "level_actor_get": "Read one level actor.",
    "level_actor_transform_get": "Read one level actor transform.",
    "object_properties_get": "Read selected UObject properties.",
    "level_mesh_instances_list": "List mesh instances in the current level.",
    "component_materials_get": "Read component material slots.",
    "material_interface_resolve": "Resolve a material interface.",
    "material_usage_find": "Find level material usage points.",
    "component_material_instance_params_get": "Read component material instance parameters.",
    "project_context_get": "Read active Unreal project context.",
    "project_input_mappings_get": "Read legacy input mappings.",
    "blueprint_details_get": "Read Blueprint variables and defaults.",
    "anim_blueprint_summary_get": "Read compact animation Blueprint summary.",
    "graph_snapshot_get": "Read graph snapshot.",
    "graph_node_info_get": "Read whole graph node information.",
    "node_info_get": "Read one graph node edit view.",
    "node_position_get": "Read one graph node position.",
    "node_class_params_get": "Read editable node class parameter template.",
    "node_params_get": "Read editable parameters for one graph node.",
    "material_expression_classes_list": "List material expression classes.",
    "material_instance_params_get": "Read material instance parameters.",
    "niagara_system_summary_get": "Read Niagara system readiness summary.",
    "niagara_asset_lint": "Read Niagara authoring risk diagnostics.",
    "niagara_emitters_list": "List Niagara emitters.",
    "niagara_system_properties_get": "Read Niagara system properties.",
    "niagara_emitter_properties_get": "Read Niagara emitter properties.",
    "niagara_modules_list": "List Niagara stack modules.",
    "niagara_module_inputs_get": "Read Niagara module inputs.",
    "niagara_renderers_list": "List Niagara renderers.",
    "niagara_renderer_properties_get": "Read Niagara renderer properties.",
    "niagara_user_params_get": "Read Niagara user parameters.",
    "niagara_materials_get": "Read Niagara renderer materials.",
    "diagnostics_get": "Read recent UE bridge diagnostics.",
}

WRITE_SUMMARIES = {
    "auto_index_enable": "Enable persistent UE asset/folder indexing.",
    "auto_index_disable": "Disable Auto-Index listeners.",
    "auto_index_rebuild": "Force a full Auto-Index rebuild.",
    "auto_index_flush": "Flush Auto-Index state to disk.",
    "auto_index_clear": "Clear Auto-Index state.",
    "asset_create": "Create a supported Unreal asset.",
    "asset_delete": "Delete one Unreal asset.",
    "asset_move": "Move one Unreal asset.",
    "asset_rename": "Rename one Unreal asset.",
    "asset_move_batch": "Move multiple Unreal assets.",
    "asset_rename_batch": "Rename multiple Unreal assets.",
    "asset_duplicate": "Duplicate one Unreal asset.",
    "folder_create": "Create one Content Browser folder.",
    "folder_delete": "Delete one Content Browser folder tree.",
    "asset_redirectors_fixup": "Fix redirectors under a folder.",
    "graph_patch_apply": "Apply a declarative graph patch.",
    "graph_build_apply": "Build a graph from compact specs.",
    "node_create": "Create one graph node.",
    "node_position_set": "Set one graph node position.",
    "node_params_set": "Set typed graph node parameters.",
    "material_instance_params_set": "Set material instance parameters.",
    "niagara_system_create": "Create or copy a Niagara system.",
    "niagara_system_duplicate": "Duplicate a Niagara system.",
    "niagara_user_params_set": "Set Niagara user parameters.",
    "niagara_materials_set": "Assign Niagara renderer material.",
    "niagara_system_properties_set": "Set Niagara system properties.",
    "niagara_emitter_create": "Create a Niagara emitter.",
    "niagara_emitter_properties_set": "Set Niagara emitter properties.",
    "niagara_module_add": "Add a Niagara stack module.",
    "niagara_module_remove": "Remove a Niagara stack module.",
    "niagara_module_set_enabled": "Enable or disable a Niagara stack module.",
    "niagara_module_inputs_set": "Set Niagara module inputs.",
    "niagara_renderer_create": "Create a Niagara renderer.",
    "niagara_renderer_properties_set": "Set Niagara renderer properties.",
    "niagara_compile": "Compile a Niagara system.",
    "component_materials_set": "Set component material slots.",
    "component_material_instance_params_set": "Set component material instance parameters.",
    "project_input_mappings_patch": "Patch legacy project input mappings.",
    "blueprint_components_patch": "Patch Blueprint components.",
    "asset_compile": "Compile or recompile an asset.",
    "asset_validate": "Validate an asset.",
    "asset_save": "Save one asset package.",
    "editor_save_all": "Save all dirty editor packages.",
    "editor_request_exit": "Request Unreal Editor exit.",
}


def _operation_summary(operation: str) -> str:
    if operation in READ_SUMMARIES:
        return READ_SUMMARIES[operation]
    if operation in WRITE_SUMMARIES:
        return WRITE_SUMMARIES[operation]
    return operation.replace("_", " ").capitalize()


def _operation_kind(operation: str) -> str:
    if operation in READ_OPERATIONS:
        return "read"
    if operation in WRITE_OPERATIONS:
        return "write"
    raise ValueError(f"unknown operation kind: {operation}")


def _operation_risk(operation: str, kind: str) -> str:
    if operation in HIGH_RISK_OPERATIONS:
        return "high"
    if kind == "read":
        return "low"
    return "medium"


def _operation_default_response(kind: str) -> str:
    if kind == "read":
        return "summary"
    return "delta"


def _build_registry() -> dict[str, OperationSpec]:
    specs: dict[str, OperationSpec] = {}
    for operation in sorted(ALL_OPERATIONS):
        group = OPERATION_FEATURES[operation]
        kind = _operation_kind(operation)
        specs[operation] = OperationSpec(
            name=operation,
            group=group,
            kind=kind,
            risk=_operation_risk(operation, kind),
            summary=_operation_summary(operation),
            bridge_operation=operation if operation in BRIDGE_OPERATIONS else None,
            hidden_from_legacy=operation in DEFAULT_HIDDEN_OPERATIONS,
            local_mcp=operation in LOCAL_MCP_OPERATIONS,
            default_response=_operation_default_response(kind),
        )
    return specs


OPERATION_REGISTRY = _build_registry()


def operation_specs() -> dict[str, OperationSpec]:
    return dict(OPERATION_REGISTRY)


def get_operation_spec(operation: str) -> OperationSpec:
    try:
        return OPERATION_REGISTRY[operation]
    except KeyError as exc:
        raise ValueError(f"unknown operation: {operation}") from exc


def enabled_operation_specs(enabled_features: set[str]) -> dict[str, OperationSpec]:
    return {
        name: spec
        for name, spec in OPERATION_REGISTRY.items()
        if spec.group in enabled_features
    }


def capability_index(enabled_features: set[str], group: str | None = None) -> list[list[Any]]:
    specs = enabled_operation_specs(enabled_features).values()
    if group:
        specs = [spec for spec in specs if spec.group == group]
    return [
        [spec.name, spec.kind, spec.risk, spec.summary]
        for spec in sorted(specs, key=lambda item: item.name)
    ]


def operation_schema(spec: OperationSpec) -> dict[str, Any]:
    from .payload_schema import payload_schema_for

    return {
        "operation": spec.name,
        "group": spec.group,
        "kind": spec.kind,
        "risk": spec.risk,
        "summary": spec.summary,
        "bridge_operation": spec.bridge_operation,
        "default_response": spec.default_response,
        "payload_schema": payload_schema_for(spec.name),
    }
