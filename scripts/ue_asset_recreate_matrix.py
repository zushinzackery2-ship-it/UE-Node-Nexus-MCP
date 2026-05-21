from __future__ import annotations

from datetime import datetime
from pathlib import Path
from typing import Any, Protocol


class BridgeLike(Protocol):
    def call(self, operation: str, payload: dict[str, Any]) -> dict[str, Any]:
        ...


def object_path(package_path: str) -> str:
    name = package_path.rsplit("/", 1)[-1]
    return f"{package_path}.{name}"


def assert_no_assets(client: BridgeLike, package_root: str, label: str) -> None:
    response = client.call(
        "asset_list",
        {
            "class_names": [],
            "package_paths": [package_root],
            "recursive": True,
            "limit": 50,
            "cursor": None,
            "format": "full",
        },
    )
    count = int(response.get("data", {}).get("count", 0))
    if count != 0:
        raise RuntimeError(f"{label} expected no assets under {package_root}: {response}")


def run_asset_recreate_matrix(
    client: BridgeLike,
    project_file: Path,
    package_root: str,
    keep_assets: bool,
    collect_disk_residue: Any,
) -> None:
    run_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    run_root = f"{package_root.rstrip('/')}/Recreate_{run_id}"
    package_path = f"{run_root}/M_Recreate"
    asset_path = object_path(package_path)

    existing = client.call(
        "asset_list",
        {
            "class_names": [],
            "package_paths": [package_root],
            "recursive": True,
            "limit": 200,
            "cursor": None,
            "format": "full",
        },
    )
    for item in existing.get("data", {}).get("items", []):
        if isinstance(item, dict) and isinstance(item.get("object_path"), str):
            client.call(
                "asset_delete",
                {
                    "asset_path": item["object_path"],
                    "dry_run": False,
                    "allow_referenced": True,
                    "cleanup_after_delete": True,
                },
            )
    client.call("folder_delete", {"folder_path": package_root, "dry_run": False, "recursive": True})
    client.call("folder_create", {"folder_path": run_root, "dry_run": False})
    client.call("asset_create", {"asset_kind": "material", "asset_path": package_path, "dry_run": False, "save": True})
    delete_response = client.call(
        "asset_delete",
        {
            "asset_path": asset_path,
            "dry_run": False,
            "allow_referenced": False,
            "cleanup_after_delete": True,
        },
    )
    if delete_response.get("data", {}).get("package_still_loaded") is True:
        raise RuntimeError(f"delete left package loaded: {delete_response}")

    assert_no_assets(client, run_root, "after first delete")
    recreate = client.call("asset_create", {"asset_kind": "material", "asset_path": package_path, "dry_run": False, "save": True})
    if recreate.get("ok") is not True:
        raise RuntimeError(f"same package recreate failed: {recreate}")

    client.call("asset_compile", {"asset_path": asset_path})
    client.call("asset_save", {"asset_path": asset_path, "only_if_dirty": True, "fail_if_open_editor_conflict": True})
    client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})

    if keep_assets:
        return

    client.call(
        "asset_delete",
        {
            "asset_path": asset_path,
            "dry_run": False,
            "allow_referenced": False,
            "cleanup_after_delete": True,
        },
    )
    client.call("asset_redirectors_fixup", {"folder_path": package_root, "dry_run": False})
    client.call("folder_delete", {"folder_path": run_root, "dry_run": False, "recursive": True})
    client.call("folder_delete", {"folder_path": package_root, "dry_run": False, "recursive": True})
    client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})

    remaining = collect_disk_residue(project_file, package_root)
    if remaining:
        raise RuntimeError(f"disk residue remained under {package_root}: {remaining}")
