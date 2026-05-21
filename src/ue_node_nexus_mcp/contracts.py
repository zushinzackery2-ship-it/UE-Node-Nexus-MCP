from __future__ import annotations

from typing import Any

READ_OPERATIONS = {
    "asset_list",
    "asset_get",
    "auto_index_status",
    "auto_index_overview",
    "auto_index_tree_get",
    "auto_index_query",
    "auto_index_get",
    "auto_index_resolve_path",
    "auto_index_diff_registry",
    "level_current_get",
    "level_actors_list",
    "level_actor_get",
    "level_actor_transform_get",
    "object_properties_get",
    "level_mesh_instances_list",
    "component_materials_get",
    "material_interface_resolve",
    "material_usage_find",
    "component_material_instance_params_get",
    "project_input_mappings_get",
    "blueprint_details_get",
    "anim_blueprint_summary_get",
    "graph_snapshot_get",
    "graph_node_info_get",
    "graph_node_info_get_w_pos",
    "node_info_get",
    "node_position_get",
    "node_class_params_get",
    "node_params_get",
    "material_expression_classes_list",
    "material_instance_params_get",
    "niagara_system_summary_get",
    "niagara_emitters_list",
    "niagara_user_params_get",
    "niagara_materials_get",
    "diagnostics_get",
}

WRITE_OPERATIONS = {
    "auto_index_enable",
    "auto_index_disable",
    "auto_index_rebuild",
    "auto_index_flush",
    "auto_index_clear",
    "asset_create",
    "asset_delete",
    "asset_move",
    "asset_rename",
    "asset_move_batch",
    "asset_rename_batch",
    "asset_duplicate",
    "folder_create",
    "folder_delete",
    "asset_redirectors_fixup",
    "graph_patch_apply",
    "graph_build_apply",
    "node_create",
    "node_position_set",
    "node_position_offset",
    "node_params_set",
    "material_instance_params_set",
    "niagara_system_create",
    "niagara_template_duplicate",
    "niagara_user_params_set",
    "niagara_materials_set",
    "niagara_compile",
    "object_properties_set",
    "level_actor_transform_set",
    "component_materials_set",
    "component_material_instance_params_set",
    "project_input_mappings_patch",
    "blueprint_components_patch",
    "asset_compile",
    "asset_validate",
    "asset_save",
    "editor_save_all",
    "editor_request_exit",
}

ALL_OPERATIONS = READ_OPERATIONS | WRITE_OPERATIONS

DEFAULT_HIDDEN_OPERATIONS = {
    "auto_index_clear",
    "auto_index_diff_registry",
    "auto_index_disable",
    "auto_index_flush",
    "graph_node_info_get_w_pos",
    "node_position_offset",
}

FEATURE_GROUPS = {
    "core",
    "asset",
    "auto_index",
    "graph",
    "material",
    "blueprint",
    "level",
    "project_input",
    "niagara",
}

DEFAULT_FEATURE_GROUPS = FEATURE_GROUPS - {"niagara"}

OPERATION_FEATURES = {
    "asset_list": "asset",
    "asset_get": "asset",
    "asset_create": "asset",
    "asset_delete": "asset",
    "asset_move": "asset",
    "asset_rename": "asset",
    "asset_move_batch": "asset",
    "asset_rename_batch": "asset",
    "asset_duplicate": "asset",
    "folder_create": "asset",
    "folder_delete": "asset",
    "asset_redirectors_fixup": "asset",
    "auto_index_enable": "auto_index",
    "auto_index_disable": "auto_index",
    "auto_index_status": "auto_index",
    "auto_index_rebuild": "auto_index",
    "auto_index_flush": "auto_index",
    "auto_index_clear": "auto_index",
    "auto_index_overview": "auto_index",
    "auto_index_tree_get": "auto_index",
    "auto_index_query": "auto_index",
    "auto_index_get": "auto_index",
    "auto_index_resolve_path": "auto_index",
    "auto_index_diff_registry": "auto_index",
    "level_current_get": "level",
    "level_actors_list": "level",
    "level_actor_get": "level",
    "level_actor_transform_get": "level",
    "level_actor_transform_set": "level",
    "object_properties_get": "level",
    "object_properties_set": "level",
    "level_mesh_instances_list": "level",
    "component_materials_get": "level",
    "component_materials_set": "level",
    "material_interface_resolve": "level",
    "material_usage_find": "level",
    "component_material_instance_params_get": "level",
    "component_material_instance_params_set": "level",
    "project_input_mappings_get": "project_input",
    "project_input_mappings_patch": "project_input",
    "blueprint_details_get": "blueprint",
    "blueprint_components_patch": "blueprint",
    "anim_blueprint_summary_get": "blueprint",
    "graph_snapshot_get": "graph",
    "graph_node_info_get": "graph",
    "graph_node_info_get_w_pos": "graph",
    "graph_patch_apply": "graph",
    "graph_build_apply": "graph",
    "node_info_get": "graph",
    "node_create": "graph",
    "node_position_get": "graph",
    "node_position_set": "graph",
    "node_position_offset": "graph",
    "node_class_params_get": "graph",
    "node_params_get": "graph",
    "node_params_set": "graph",
    "material_expression_classes_list": "material",
    "material_instance_params_get": "material",
    "material_instance_params_set": "material",
    "niagara_system_create": "niagara",
    "niagara_template_duplicate": "niagara",
    "niagara_system_summary_get": "niagara",
    "niagara_emitters_list": "niagara",
    "niagara_user_params_get": "niagara",
    "niagara_user_params_set": "niagara",
    "niagara_materials_get": "niagara",
    "niagara_materials_set": "niagara",
    "niagara_compile": "niagara",
    "asset_compile": "core",
    "asset_validate": "core",
    "asset_save": "core",
    "diagnostics_get": "core",
    "editor_save_all": "core",
    "editor_request_exit": "core",
}

DEFAULT_EXPOSED_OPERATIONS = {
    operation
    for operation in ALL_OPERATIONS - DEFAULT_HIDDEN_OPERATIONS
    if OPERATION_FEATURES[operation] in DEFAULT_FEATURE_GROUPS
}


def require_non_empty_string(value: str, field_name: str) -> None:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{field_name} must be a non-empty string")


def require_mapping(value: dict[str, Any], field_name: str) -> None:
    if not isinstance(value, dict):
        raise ValueError(f"{field_name} must be an object")


def require_list(value: list[dict[str, Any]], field_name: str) -> None:
    if not isinstance(value, list):
        raise ValueError(f"{field_name} must be an array")
    for index, item in enumerate(value):
        if not isinstance(item, dict):
            raise ValueError(f"{field_name}[{index}] must be an object")
