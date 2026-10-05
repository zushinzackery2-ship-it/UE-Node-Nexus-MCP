"""Constrain isolated validation to project-local asset and rendering operations."""

from __future__ import annotations

from copy import deepcopy
import re

from ...instances.errors import InstanceError, require
from ...operation_validation import validate_operation_call
from ...payload_schema import payload_schema_for

SUPPORTED = frozenset(("transcode_apply", "vfx_transcode_apply", "scene_apply", "asset_create", "asset_compile",
                       "asset_validate", "asset_save", "graph_patch_apply", "graph_build_apply", "node_create",
                       "node_params_set", "material_instance_params_set", "blueprint_components_patch", "viewport_capture",
                       "runtime_smoke_start", "level_open"))
PROTOCOL_FIELDS = frozenset(("repository", "apply_id", "collaboration_version", "receipt_path", "output_dir",
                             "output_path", "file_path", "destination_dir", "root_dir", "transcode_dir"))


def validate_operations(operations, mounts=None) -> list[dict]:
    require(isinstance(operations, list) and 1 <= len(operations) <= 100,
            "invalid_request", "operations must contain 1..100 asset operations")
    result = []
    for index, item in enumerate(operations):
        require(isinstance(item, dict) and set(item) == set(("operation", "payload")),
                "invalid_request", "each operation requires operation and payload", index=index)
        name, payload = item["operation"], item["payload"]
        require(isinstance(name, str) and name in SUPPORTED, "isolation_operation_rejected",
                "operation is outside isolated asset validation", index=index, accepted=sorted(SUPPORTED))
        error = validate_operation_call(name, payload)
        if error:
            raise InstanceError(error["code"], error["message"], dict(index=index, operation=name))
        inspect_payload(payload, mounts=mounts)
        for field in ("asset_path", "map_path", "level_path", "blueprint_path", "material_path", "system_path", "asset_paths", "package_names", "package_paths"):
            if field not in payload:
                continue
            targets = payload[field] if isinstance(payload[field], list) else [payload[field]]
            require(all(isinstance(target, str) and target.startswith("/Game/") and ".." not in target.split("/") for target in targets),
                    "isolation_target_rejected", "write targets must stay in the copied /Game mount", field=field)
        require(payload.get("dry_run") is not True, "invalid_request",
                "isolated validation must execute the supplied operation", index=index)
        admitted = deepcopy(item)
        if "dry_run" in payload_schema_for(name).get("properties", dict()):
            admitted["payload"]["dry_run"] = False
        if name == "scene_apply":
            for field in ("plan_file", "out_file"):
                value = payload[field].replace("\\", "/")
                require(value and not value.startswith("/") and ".." not in value.split("/"),
                        "isolation_path_rejected", "scene files must use project-relative paths", field=field)
        result.append(admitted)
    return result


def inspect_payload(value, key: str = "", *, mounts=None) -> None:
    mounts = set(("Game", "Engine", "Script")) if mounts is None else mounts
    if isinstance(value, dict):
        for field, child in value.items():
            require(field not in PROTOCOL_FIELDS, "isolation_path_rejected",
                    "publication identity and filesystem targets are owned by the validation copy", field=field)
            inspect_payload(child, field, mounts=mounts)
    elif isinstance(value, list):
        for child in value:
            inspect_payload(child, key, mounts=mounts)
    elif isinstance(value, str):
        require(not re.match(r"^[A-Za-z]:[\\/]", value) and not value.startswith(("\\\\", "file://")),
                "isolation_path_rejected", "payload cannot address external filesystem paths", field=key)
        if value.startswith("/"):
            require(value.split("/", 2)[1] in mounts and len(value.split("/", 2)) == 3, "isolation_path_rejected",
                    "object references must use an enabled installed content mount", field=key)
