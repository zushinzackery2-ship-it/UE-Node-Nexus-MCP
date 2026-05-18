from __future__ import annotations

from typing import Any

READ_OPERATIONS = {
    "asset_list",
    "asset_get",
    "level_current_get",
    "level_actors_list",
    "graph_snapshot_get",
    "node_params_get",
    "material_instance_params_get",
    "diagnostics_get",
}

WRITE_OPERATIONS = {
    "graph_patch_apply",
    "node_params_set",
    "material_instance_params_set",
    "asset_compile",
    "asset_validate",
    "asset_save",
}

ALL_OPERATIONS = READ_OPERATIONS | WRITE_OPERATIONS


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

