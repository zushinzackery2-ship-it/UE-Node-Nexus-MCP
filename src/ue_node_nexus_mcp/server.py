from __future__ import annotations

from .runtime import call_bridge as _call
from .runtime import mcp


def main() -> None:
    mcp.run()


from .tools_assets import asset_create, asset_get, asset_list  # noqa: E402,F401
from .tools_blueprints import anim_blueprint_summary_get, blueprint_details_get  # noqa: E402,F401
from .tools_graphs import graph_node_info_get, graph_node_info_get_w_pos, graph_patch_apply, graph_snapshot_get, node_class_params_get, node_create, node_info_get, node_params_get, node_params_set, node_position_get, node_position_offset, node_position_set  # noqa: E402,F401
from .tools_materials import material_instance_params_get, material_instance_params_set  # noqa: E402,F401
from .tools_system import asset_compile, asset_save, asset_validate, diagnostics_get, level_actors_list, level_current_get  # noqa: E402,F401


if __name__ == "__main__":
    main()
