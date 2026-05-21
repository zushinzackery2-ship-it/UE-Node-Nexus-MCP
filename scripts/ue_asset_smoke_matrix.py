from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, Protocol


class BridgeLike(Protocol):
    def call(self, operation: str, payload: dict[str, Any]) -> dict[str, Any]:
        ...


@dataclass(frozen=True)
class SmokeAsset:
    operation_kind: str
    asset_path: str
    payload: dict[str, Any]


def build_asset_matrix(root: str, run_id: str) -> list[SmokeAsset]:
    base = f"{root.rstrip('/')}/Run_{run_id}"
    return [
        SmokeAsset(
            "material_function",
            f"{base}/MF_Smoke",
            {"asset_kind": "material_function", "asset_path": f"{base}/MF_Smoke", "dry_run": False, "save": True},
        ),
        SmokeAsset(
            "data_asset",
            f"{base}/DA_Smoke",
            {
                "asset_kind": "data_asset",
                "asset_path": f"{base}/DA_Smoke",
                "dry_run": False,
                "save": True,
            },
        ),
        SmokeAsset(
            "texture_render_target_2d",
            f"{base}/RT_Smoke",
            {"asset_kind": "texture_render_target_2d", "asset_path": f"{base}/RT_Smoke", "dry_run": False, "save": True},
        ),
        SmokeAsset(
            "blueprint",
            f"{base}/BP_Smoke",
            {
                "asset_kind": "blueprint",
                "asset_path": f"{base}/BP_Smoke",
                "parent_class_path": "/Script/Engine.Actor",
                "dry_run": False,
                "save": True,
            },
        ),
    ]


def run_write_smoke(
    client: BridgeLike,
    project_file: Path,
    package_root: str,
    keep_assets: bool,
    collect_disk_residue: Any,
) -> None:
    run_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    assets = build_asset_matrix(package_root, run_id)
    created_paths: dict[str, str] = {}
    client.call("folder_create", {"folder_path": f"{package_root}/Run_{run_id}", "dry_run": False})
    for asset in assets:
        response = client.call("asset_create", asset.payload)
        created_path = response.get("data", {}).get("asset_path", asset.asset_path)
        created_paths[asset.operation_kind] = created_path
        client.call("asset_get", {"asset_path": created_path})

    blueprint_path = created_paths["blueprint"]
    client.call(
        "node_create",
        {
            "asset_path": blueprint_path,
            "graph_kind": "blueprint",
            "graph_name": None,
            "node_class": "/Script/BlueprintGraph.K2Node_CustomEvent",
            "name": f"SmokeEvent_{run_id}",
            "position": {"x": 128, "y": 96},
            "params": {},
            "dry_run": False,
        },
    )
    client.call("asset_compile", {"asset_path": blueprint_path})
    client.call("asset_save", {"asset_path": blueprint_path, "only_if_dirty": True, "fail_if_open_editor_conflict": True})
    client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})
    client.call("asset_list", {"class_names": [], "package_paths": [package_root], "recursive": True, "limit": 200, "cursor": None, "format": "full"})

    if keep_assets:
        return
    for asset in reversed(assets):
        client.call("asset_delete", {"asset_path": created_paths[asset.operation_kind], "dry_run": False, "allow_referenced": False})
    client.call("asset_redirectors_fixup", {"folder_path": package_root, "dry_run": False})
    client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})
    remaining = collect_disk_residue(project_file, package_root)
    if remaining:
        raise RuntimeError(f"disk residue remained under {package_root}: {remaining}")
