from __future__ import annotations

from .runtime import call_bridge as _call
from .runtime import mcp


def main() -> None:
    mcp.run()


from .tools_assets import asset_create, asset_delete, asset_duplicate, asset_get, asset_list, asset_move, asset_move_batch, asset_redirectors_fixup, asset_rename, asset_rename_batch, folder_create, folder_delete  # noqa: E402,F401
from .tools_auto_index import auto_index_clear, auto_index_diff_registry, auto_index_disable, auto_index_enable, auto_index_flush, auto_index_get, auto_index_overview, auto_index_query, auto_index_rebuild, auto_index_resolve_path, auto_index_status, auto_index_tree_get  # noqa: E402,F401
from .tools_blueprints import anim_blueprint_summary_get, blueprint_components_patch, blueprint_details_get  # noqa: E402,F401
from .tools_graphs import graph_build_apply, graph_node_info_get, graph_patch_apply, graph_snapshot_get, node_class_params_get, node_create, node_info_get, node_params_get, node_params_set, node_position_get, node_position_set  # noqa: E402,F401
from .tools_project import project_context_get, project_input_mappings_get, project_input_mappings_patch  # noqa: E402,F401
from .tools_level_materials import component_material_instance_params_get, component_material_instance_params_set, component_materials_get, component_materials_set, level_actor_get, level_actor_transform_get, level_mesh_instances_list, material_interface_resolve, material_usage_find, object_properties_get  # noqa: E402,F401
from .tools_materials import material_expression_classes_list, material_instance_params_get, material_instance_params_set  # noqa: E402,F401
from .tools_niagara import niagara_asset_lint, niagara_compile, niagara_emitter_create, niagara_emitter_properties_get, niagara_emitter_properties_set, niagara_emitters_list, niagara_materials_get, niagara_materials_set, niagara_renderer_create, niagara_renderer_properties_get, niagara_renderer_properties_set, niagara_renderers_list, niagara_system_create, niagara_system_duplicate, niagara_system_properties_get, niagara_system_properties_set, niagara_system_summary_get, niagara_user_params_get, niagara_user_params_set  # noqa: E402,F401
from .tools_niagara_modules import niagara_module_add, niagara_module_inputs_get, niagara_module_inputs_set, niagara_module_remove, niagara_module_set_enabled, niagara_modules_list  # noqa: E402,F401
from .tools_system import asset_compile, asset_save, asset_validate, bridge_capabilities_get, bridge_contract_check, diagnostics_get, editor_request_exit, editor_save_all, level_actors_list, level_current_get  # noqa: E402,F401
from .tools_facade import ue_capability_get, ue_context_get, ue_diff_get, ue_execute, ue_plan_validate, ue_read  # noqa: E402,F401


if __name__ == "__main__":
    main()
