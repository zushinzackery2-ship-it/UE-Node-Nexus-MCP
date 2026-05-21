from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, Protocol


class BridgeLike(Protocol):
    def call(self, operation: str, payload: dict[str, Any]) -> dict[str, Any]:
        ...


@dataclass(frozen=True)
class AutoIndexMatrixPaths:
    root: str
    run_folder: str
    source_asset: str
    renamed_asset: str
    moved_asset: str


def object_path(package_asset_path: str) -> str:
    asset_name = package_asset_path.rsplit("/", 1)[-1]
    return f"{package_asset_path}.{asset_name}"


def build_auto_index_paths(package_root: str, run_id: str) -> AutoIndexMatrixPaths:
    root = package_root.rstrip("/")
    run_folder = f"{root}/Run_{run_id}"
    return AutoIndexMatrixPaths(
        root=root,
        run_folder=run_folder,
        source_asset=f"{run_folder}/M_AutoIndex",
        renamed_asset=f"{run_folder}/M_AutoIndexRenamed",
        moved_asset=f"{run_folder}/Moved/M_AutoIndexMoved",
    )


def response_text(response: dict[str, Any]) -> str:
    data = response.get("data", {})
    if not isinstance(data, dict):
        return ""
    text = data.get("text", "")
    return text if isinstance(text, str) else ""


def response_data(response: dict[str, Any]) -> dict[str, Any]:
    data = response.get("data", {})
    return data if isinstance(data, dict) else {}


def assert_text_contains(response: dict[str, Any], needle: str, label: str) -> None:
    text = response_text(response)
    if needle not in text:
        raise RuntimeError(f"{label} missing {needle}; text={text[:1000]}")


def assert_text_excludes(response: dict[str, Any], needle: str, label: str) -> None:
    text = response_text(response)
    if needle in text:
        raise RuntimeError(f"{label} unexpectedly contained {needle}; text={text[:1000]}")


def assert_resolves_to(response: dict[str, Any], expected_object_path: str, label: str) -> None:
    data = response_data(response)
    text = response_text(response)
    resolved = data.get("resolved_asset_path")
    if resolved == expected_object_path or expected_object_path in text:
        return
    raise RuntimeError(f"{label} did not resolve to {expected_object_path}; data={data}")


def assert_not_resolved_to(response: dict[str, Any], rejected_object_path: str, label: str) -> None:
    data = response_data(response)
    candidate_lines = [line for line in response_text(response).splitlines() if line.startswith("A:")]
    if data.get("resolved_asset_path") == rejected_object_path or any(rejected_object_path in line for line in candidate_lines):
        raise RuntimeError(f"{label} still resolved to {rejected_object_path}; data={data}")


def assert_diff_clean(response: dict[str, Any]) -> None:
    data = response_data(response)
    indexed_only = int(data.get("indexed_only", 0) or 0)
    registry_only = int(data.get("registry_only", 0) or 0)
    if indexed_only != 0 or registry_only != 0:
        raise RuntimeError(f"auto_index_diff_registry reported drift: {data}")


def assert_query_page_shape(response: dict[str, Any], label: str) -> None:
    data = response_data(response)
    if "count" not in data or "total" not in data:
        raise RuntimeError(f"{label} missing count/total: {data}")
    count = int(data.get("count", 0) or 0)
    total = int(data.get("total", 0) or 0)
    if count < 0 or total < count:
        raise RuntimeError(f"{label} invalid count/total: {data}")


def query_asset(client: BridgeLike, package_root: str, text: str, limit: int = 10) -> dict[str, Any]:
    return client.call(
        "auto_index_query",
        {
            "text": text,
            "class_names": [],
            "package_paths": [package_root],
            "recursive": True,
            "include_redirectors": False,
            "limit": limit,
            "cursor": None,
            "format": "indexed",
        },
    )


def cleanup_auto_index_matrix(
    client: BridgeLike,
    project_file: Path,
    paths: AutoIndexMatrixPaths,
    collect_disk_residue: Any,
) -> None:
    for asset_path in (paths.moved_asset, paths.renamed_asset, paths.source_asset):
        try:
            client.call("asset_delete", {"asset_path": asset_path, "dry_run": False, "allow_referenced": True})
        except Exception:
            pass
    client.call("asset_redirectors_fixup", {"folder_path": paths.root, "dry_run": False})
    client.call("folder_delete", {"folder_path": paths.root, "dry_run": False, "recursive": True})
    client.call("auto_index_rebuild", {"root_path": "/Game"})
    client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})
    remaining = collect_disk_residue(project_file, paths.root)
    if remaining:
        raise RuntimeError(f"disk residue remained under {paths.root}: {remaining}")


def run_auto_index_matrix(
    client: BridgeLike,
    project_file: Path,
    package_root: str,
    keep_assets: bool,
    collect_disk_residue: Any,
) -> None:
    run_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    paths = build_auto_index_paths(package_root, run_id)

    client.call("auto_index_enable", {"root_path": "/Game", "rebuild": True})
    client.call("folder_create", {"folder_path": paths.run_folder, "dry_run": False})
    client.call("folder_create", {"folder_path": f"{paths.run_folder}/Moved", "dry_run": False})
    client.call(
        "asset_create",
        {
            "asset_kind": "material",
            "asset_path": paths.source_asset,
            "dry_run": False,
            "save": True,
        },
    )
    client.call("auto_index_flush", {})

    source_object = object_path(paths.source_asset)
    renamed_object = object_path(paths.renamed_asset)
    moved_object = object_path(paths.moved_asset)

    created_query = query_asset(client, paths.root, "M_AutoIndex", limit=5)
    assert_text_contains(created_query, source_object, "created asset query")
    assert_resolves_to(
        client.call("auto_index_resolve_path", {"path": "M_AutoIndex", "limit": 10, "format": "indexed"}),
        source_object,
        "created asset resolve",
    )
    assert_query_page_shape(query_asset(client, paths.root, "", limit=1), "small limit query")

    client.call(
        "asset_rename",
        {
            "source_asset_path": paths.source_asset,
            "destination_asset_path": paths.renamed_asset,
            "dry_run": False,
            "save": True,
            "fix_redirectors": True,
        },
    )
    renamed_query = query_asset(client, paths.root, "M_AutoIndexRenamed", limit=5)
    assert_text_contains(renamed_query, renamed_object, "renamed asset query")
    assert_not_resolved_to(
        client.call("auto_index_resolve_path", {"path": source_object, "limit": 10, "format": "indexed"}),
        source_object,
        "old asset resolve after rename",
    )

    client.call(
        "asset_move",
        {
            "source_asset_path": paths.renamed_asset,
            "destination_asset_path": paths.moved_asset,
            "dry_run": False,
            "save": True,
            "fix_redirectors": True,
        },
    )
    moved_query = query_asset(client, paths.root, "M_AutoIndexMoved", limit=5)
    assert_text_contains(moved_query, moved_object, "moved asset query")
    assert_text_excludes(query_asset(client, paths.root, "M_AutoIndexRenamed", limit=20), renamed_object, "renamed query after move")
    assert_resolves_to(
        client.call("auto_index_resolve_path", {"path": paths.moved_asset, "limit": 10, "format": "indexed"}),
        moved_object,
        "moved asset resolve",
    )

    client.call("asset_delete", {"asset_path": paths.moved_asset, "dry_run": False, "allow_referenced": False})
    client.call("asset_redirectors_fixup", {"folder_path": paths.root, "dry_run": False})
    client.call("auto_index_flush", {})
    assert_text_excludes(query_asset(client, paths.root, "M_AutoIndexMoved", limit=20), moved_object, "deleted asset query")
    assert_not_resolved_to(
        client.call("auto_index_resolve_path", {"path": paths.moved_asset, "limit": 10, "format": "indexed"}),
        moved_object,
        "deleted asset resolve",
    )

    assert_diff_clean(client.call("auto_index_diff_registry", {"format": "indexed"}))
    client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})

    if not keep_assets:
        cleanup_auto_index_matrix(client, project_file, paths, collect_disk_residue)
