"""ue_read target routing for the thin facade.

Maps a human ``ue_read(target=...)`` to the underlying internal read operation and
applies per-operation default ``format`` values for the summary/index/detail read
modes. Kept separate from tools_facade so the six facade entrypoints stay thin.
"""

from __future__ import annotations

from typing import Any

# The single mapping from a ue_read target to its backing read operation. ``None``
# marks an in-facade target (e.g. "artifact") handled before this lookup.
READ_TARGET_OPERATIONS: dict[str, str | None] = {
    "artifact": None,
    "anim_blueprint": "anim_blueprint_summary_get",
    "anim_state_machine": "anim_state_machine_summary_get",
    "anim_montage": "anim_montage_summary_get",
    "asset": "asset_get",
    "asset_index": "auto_index_query",
    "blend_space": "blend_space_summary_get",
    "blueprint": "blueprint_details_get",
    "cascade_system": "cascade_system_summary_get",
    "diagnostics": "diagnostics_get",
    "graph": "graph_snapshot_get",
    "level": "level_actors_list",
    "material_instance": "material_instance_params_get",
    "niagara_stack": "niagara_modules_list",
    "niagara_system": "niagara_system_summary_get",
    "node": "node_info_get",
    "project_input": "project_input_mappings_get",
    "sound_cue": "sound_cue_summary_get",
    "texture": "texture_summary_get",
}


def resolve_read_operation(target: str) -> str | None:
    """Return the backing operation for a ue_read target, or None if unsupported.

    Note: targets mapped to ``None`` in READ_TARGET_OPERATIONS (in-facade targets)
    also return None; callers must handle those before calling this.
    """
    return READ_TARGET_OPERATIONS.get(target)


def apply_read_format_defaults(operation: str, read_format: str, query_payload: dict[str, Any]) -> None:
    """Seed the operation's ``format`` payload field for the given ue_read mode.

    Mutates query_payload in place, never overriding a caller-supplied format.
    """
    if read_format in {"summary", "index"}:
        if operation == "graph_snapshot_get":
            query_payload.setdefault("format", "wires_tiny")
        elif operation in {"auto_index_query", "level_actors_list"}:
            query_payload.setdefault("format", "indexed")
        elif operation == "project_input_mappings_get":
            query_payload.setdefault("format", "compact")
    elif read_format == "detail":
        query_payload.setdefault("format", "full")
