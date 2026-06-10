from __future__ import annotations

from typing import Any

from .facade_execute import execute_operation


GRAPH_CLASS_KINDS = {
    "/Script/Engine.Material": "material",
    "/Script/Engine.MaterialFunction": "material_function",
}

MATERIAL_INSTANCE_CLASSES = {
    "/Script/Engine.MaterialInstance",
    "/Script/Engine.MaterialInstanceConstant",
}


def execute_auto_read(requested_path: str, query_payload: dict[str, Any], read_format: str) -> tuple[str, str, str, dict[str, Any], dict[str, Any]]:
    """Resolve an asset-like input and route it to the narrowest read operation.

    Returns ``(route_target, operation, resolved_asset_path, operation_payload, raw_response)``.
    """
    resolve_payload = {
        "path": requested_path,
        "limit": int(query_payload.get("limit", 20)),
        "format": "full",
    }
    resolve_response = execute_operation("auto_index_resolve_path", resolve_payload)
    resolved_asset_path = _resolved_asset_path(resolve_response) or requested_path

    asset_response = execute_operation("asset_get", {"asset_path": resolved_asset_path})
    asset_data = asset_response.get("data") if isinstance(asset_response.get("data"), dict) else {}
    class_path = str(asset_data.get("asset_class_path") or asset_data.get("class_path") or "")

    route_target, operation, operation_payload = _route_for_class(class_path, resolved_asset_path, query_payload, read_format)
    result_response = execute_operation(operation, operation_payload)
    wrapped_response = {
        **result_response,
        "data": {
            "auto": {
                "requested_path": requested_path,
                "resolved_asset_path": resolved_asset_path,
                "asset_class_path": class_path,
                "route_target": route_target,
                "operation": operation,
                "operation_payload": operation_payload,
                "resolve": resolve_response.get("data"),
                "asset": asset_data,
            },
            "result": result_response.get("data"),
        },
    }
    return route_target, operation, resolved_asset_path, operation_payload, wrapped_response


def _resolved_asset_path(response: dict[str, Any]) -> str | None:
    data = response.get("data")
    if not isinstance(data, dict):
        return None
    value = data.get("resolved_asset_path")
    if isinstance(value, str) and value:
        return value
    candidates = data.get("candidates")
    if isinstance(candidates, list) and candidates:
        first = candidates[0]
        if isinstance(first, dict):
            for key in ("object_path", "asset_path", "path"):
                candidate = first.get(key)
                if isinstance(candidate, str) and candidate:
                    return candidate
        if isinstance(first, str) and first:
            return first
    return None


def _route_for_class(
    class_path: str,
    asset_path: str,
    query_payload: dict[str, Any],
    read_format: str,
) -> tuple[str, str, dict[str, Any]]:
    if class_path in MATERIAL_INSTANCE_CLASSES:
        return (
            "material_instance",
            "material_interface_resolve",
            {
                "asset_path": asset_path,
                "include_params": read_format == "detail" or bool(query_payload.get("include_params", False)),
            },
        )
    if class_path in GRAPH_CLASS_KINDS:
        graph_kind = GRAPH_CLASS_KINDS[class_path]
        payload = {
            "asset_path": asset_path,
            "graph_kind": graph_kind,
            "format": "full" if read_format == "detail" else "wires_tiny",
        }
        if "include_links" in query_payload:
            payload["include_links"] = query_payload["include_links"]
        return ("graph", "graph_snapshot_get", payload)
    if class_path == "/Script/Engine.Texture2D":
        return ("texture", "texture_summary_get", {"asset_path": asset_path, "format": "full" if read_format == "detail" else "compact"})
    if class_path in {"/Script/Engine.Blueprint", "/Script/Engine.AnimBlueprint"}:
        return ("blueprint", "blueprint_details_get", {"asset_path": asset_path, "format": "full" if read_format == "detail" else "compact"})
    if class_path == "/Script/Niagara.NiagaraSystem":
        return ("niagara_system", "niagara_system_summary_get", {"asset_path": asset_path, "format": "full" if read_format == "detail" else "indexed"})
    if class_path == "/Script/Engine.ParticleSystem":
        return ("cascade_system", "cascade_system_summary_get", {"asset_path": asset_path, "format": "full" if read_format == "detail" else "compact"})
    if class_path == "/Script/Engine.SoundCue":
        return ("sound_cue", "sound_cue_summary_get", {"asset_path": asset_path, "format": "full" if read_format == "detail" else "compact"})
    return ("asset", "asset_get", {"asset_path": asset_path})
