from __future__ import annotations

from copy import deepcopy
from typing import Any, Literal

from .contracts import require_list, require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import default_tool


_MATERIAL_GRAPH_KINDS = {"material", "material_function"}
_NODE_REFERENCE_FIELDS = {
    "from_node_id",
    "from_node",
    "to_node_id",
    "to_node",
    "node_id",
    "node",
}


def _client_id_for_create_node(operation: dict[str, Any]) -> str | None:
    if operation.get("op") != "create_node":
        return None
    client_id = operation.get("client_id")
    if isinstance(client_id, str) and client_id:
        return client_id
    return None


def _referenced_created_client_ids(operations: list[dict[str, Any]]) -> set[str]:
    created = {
        client_id
        for operation in operations
        if (client_id := _client_id_for_create_node(operation))
    }
    if not created:
        return set()

    referenced: set[str] = set()
    for operation in operations:
        if operation.get("op") == "create_node":
            continue
        for field in _NODE_REFERENCE_FIELDS:
            value = operation.get(field)
            if isinstance(value, str) and value in created:
                referenced.add(value)
    return referenced


def _needs_material_client_id_expansion(graph_kind: str, operations: list[dict[str, Any]]) -> bool:
    return graph_kind in _MATERIAL_GRAPH_KINDS and bool(_referenced_created_client_ids(operations))


def _create_node_payload(
    asset_path: str,
    operation: dict[str, Any],
    graph_name: str | None,
    graph_kind: str,
    dry_run: bool,
) -> dict[str, Any]:
    params = operation.get("params")
    if not isinstance(params, dict):
        params = {}
    return {
        "asset_path": asset_path,
        "node_class": operation.get("class_path") or operation.get("node_class"),
        "graph_name": graph_name,
        "graph_kind": graph_kind,
        "name": operation.get("name"),
        "position": operation.get("position"),
        "params": params,
        "dry_run": dry_run,
    }


def _client_error(operation: str, asset_path: str, code: str, message: str, details: dict[str, Any]) -> dict[str, Any]:
    return {
        "ok": False,
        "operation": operation,
        "error": {
            "code": code,
            "message": message,
            "details": details,
        },
        "diagnostics": [],
        "warnings": [],
        "data": {"asset_path": asset_path},
        "remaining_errors": 1,
    }


def _replace_client_references(operation: dict[str, Any], id_map: dict[str, str]) -> dict[str, Any]:
    rewritten = deepcopy(operation)
    for field in _NODE_REFERENCE_FIELDS:
        value = rewritten.get(field)
        if isinstance(value, str) and value in id_map:
            rewritten[field] = id_map[value]
    return rewritten


def _expand_material_client_ids(
    asset_path: str,
    operations: list[dict[str, Any]],
    graph_name: str | None,
    graph_kind: str,
    dry_run: bool,
    compile_after: bool,
    format: str,
) -> dict[str, Any]:
    referenced = _referenced_created_client_ids(operations)
    if dry_run:
        return _client_error(
            "graph_patch_apply",
            asset_path,
            "material_client_id_dry_run_unsupported",
            "Material graph dry_run cannot validate same-batch create_node client_id references without UE-side transient nodes. Set dry_run=false or split create_node and connect_pins.",
            {"client_ids": sorted(referenced)},
        )

    id_map: dict[str, str] = {}
    created_nodes: list[dict[str, Any]] = []
    remaining_operations: list[dict[str, Any]] = []
    for operation in operations:
        client_id = _client_id_for_create_node(operation)
        if client_id is None:
            remaining_operations.append(_replace_client_references(operation, id_map))
            continue

        response = _call(
            "node_create",
            _create_node_payload(asset_path, operation, graph_name, graph_kind, False),
        )
        if response.get("ok") is not True:
            return response

        data = response.get("data") if isinstance(response.get("data"), dict) else {}
        node_id = data.get("node_id")
        if not isinstance(node_id, str) or not node_id:
            return _client_error(
                "graph_patch_apply",
                asset_path,
                "material_client_id_missing_node_id",
                "node_create succeeded but did not return a node_id for a create_node client_id.",
                {"client_id": client_id},
            )
        id_map[client_id] = node_id
        created_nodes.append(
            {
                "client_id": client_id,
                "node_id": node_id,
                "node_alias": data.get("node_alias"),
                "node_class": operation.get("class_path") or operation.get("node_class"),
            }
        )

    response = _call(
        "graph_patch_apply",
        {
            "asset_path": asset_path,
            "operations": remaining_operations,
            "graph_name": graph_name,
            "graph_kind": graph_kind,
            "dry_run": False,
            "compile_after": compile_after,
            "format": format,
        },
    )
    data = response.get("data")
    if isinstance(data, dict):
        data["client_side_expansion"] = {
            "mode": "material_create_node_client_id",
            "created_nodes": created_nodes,
        }
        diff = data.get("diff")
        if isinstance(diff, dict):
            nodes_created = diff.setdefault("nodes_created", [])
            if isinstance(nodes_created, list):
                nodes_created[:0] = created_nodes
    return response


@default_tool()
def graph_patch_apply(
    asset_path: str,
    operations: list[dict[str, Any]],
    graph_name: str | None = None,
    graph_kind: Literal["material", "material_function", "blueprint", "auto"] = "auto",
    dry_run: bool = True,
    compile_after: bool = True,
    format: Literal["compact", "full"] = "compact",
) -> dict[str, Any]:
    """Apply a declarative graph patch, then return pin integrity and compile diagnostics."""
    require_non_empty_string(asset_path, "asset_path")
    require_list(operations, "operations")
    if _needs_material_client_id_expansion(graph_kind, operations):
        return _expand_material_client_ids(
            asset_path,
            operations,
            graph_name,
            graph_kind,
            dry_run,
            compile_after,
            format,
        )
    return _call(
        "graph_patch_apply",
        {
            "asset_path": asset_path,
            "operations": operations,
            "graph_name": graph_name,
            "graph_kind": graph_kind,
            "dry_run": dry_run,
            "compile_after": compile_after,
            "format": format,
        },
    )


@default_tool()
def graph_build_apply(
    asset_path: str,
    nodes: list[dict[str, Any]],
    links: list[dict[str, Any]] | None = None,
    material_outputs: list[dict[str, Any]] | None = None,
    graph_name: str | None = None,
    graph_kind: Literal["material", "material_function"] = "material",
    dry_run: bool = True,
    compile_after: bool = True,
    format: Literal["compact", "full"] = "compact",
) -> dict[str, Any]:
    """Build a material or material function graph from compact node/link specs in one bridge transaction."""
    require_non_empty_string(asset_path, "asset_path")
    require_list(nodes, "nodes")
    if links is not None:
        require_list(links, "links")
    if material_outputs is not None:
        require_list(material_outputs, "material_outputs")
    return _call(
        "graph_build_apply",
        {
            "asset_path": asset_path,
            "nodes": nodes,
            "links": links or [],
            "material_outputs": material_outputs or [],
            "graph_name": graph_name,
            "graph_kind": graph_kind,
            "dry_run": dry_run,
            "compile_after": compile_after,
            "format": format,
        },
    )
