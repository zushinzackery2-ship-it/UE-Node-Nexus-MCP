from __future__ import annotations

import argparse
import json
import sys
import time
import uuid
from datetime import datetime
from pathlib import Path
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BRIDGE_URL = "http://127.0.0.1:8765"
DEFAULT_SOURCE = "/Game/YN/Material/大坝母材质"
DEFAULT_TARGET = "/Game/YN/Material/大坝母材质_手动复刻"


class JsonlLog:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.path.parent.mkdir(parents=True, exist_ok=True)

    def write(self, event: str, data: dict[str, Any]) -> None:
        row = {"time": datetime.now().isoformat(timespec="seconds"), "event": event, **data}
        with self.path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")


class BridgeClient:
    def __init__(self, base_url: str, timeout: float, log: JsonlLog) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout
        self.log = log

    def call(self, operation: str, payload: dict[str, Any]) -> dict[str, Any]:
        envelope = {"operation": operation, "request_id": f"{operation}-{time.time_ns()}", "payload": payload}
        self.log.write("request", {"operation": operation, "payload": payload})
        request = Request(
            f"{self.base_url}/mcp",
            data=json.dumps(envelope, separators=(",", ":")).encode("utf-8"),
            headers={"Content-Type": "application/json", "Accept": "application/json"},
            method="POST",
        )
        try:
            with urlopen(request, timeout=self.timeout) as response:
                raw = response.read().decode("utf-8", errors="replace")
        except HTTPError as exc:
            raw = exc.read().decode("utf-8", errors="replace")
            self.log.write("http_error", {"operation": operation, "status": exc.code, "body": raw})
            raise RuntimeError(f"{operation} HTTP {exc.code}: {raw}") from exc
        except URLError as exc:
            self.log.write("connection_error", {"operation": operation, "reason": str(exc.reason)})
            raise RuntimeError(f"{operation} connection failed: {exc.reason}") from exc
        decoded = json.loads(raw)
        self.log.write("response", {"operation": operation, "response": decoded})
        if not isinstance(decoded, dict) or decoded.get("ok") is False:
            raise RuntimeError(f"{operation} failed: {decoded}")
        return decoded


def package_path_to_object_path(package_path: str) -> str:
    if "." in package_path.rsplit("/", 1)[-1]:
        return package_path
    asset_name = package_path.rsplit("/", 1)[-1]
    return f"{package_path}.{asset_name}"


def wait_for_bridge(client: BridgeClient, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    last_error = ""
    while time.monotonic() < deadline:
        try:
            client.call("level_current_get", {})
            return
        except RuntimeError as exc:
            last_error = str(exc)
            time.sleep(1.0)
    raise RuntimeError(f"bridge did not become ready within {timeout:.0f}s: {last_error}")


def params_for_node(node: dict[str, Any]) -> dict[str, Any]:
    params: dict[str, Any] = {}
    for item in node.get("params", []):
        if not isinstance(item, dict) or not item.get("editable", False):
            continue
        name = item.get("name")
        value = item.get("value")
        if not isinstance(name, str) or name == "Desc":
            continue
        if value is None:
            continue
        params[name] = value
    return params


def pin_name_by_id(nodes: list[dict[str, Any]], pin_id: str) -> str:
    for node in nodes:
        for pin in node.get("pins", []):
            if isinstance(pin, dict) and pin.get("pin_id") == pin_id:
                name = pin.get("name")
                if isinstance(name, str) and name:
                    return name
    raise RuntimeError(f"pin not found in source snapshot: {pin_id}")


def node_id_from_create(response: dict[str, Any]) -> str:
    data = response.get("data", {})
    if not isinstance(data, dict):
        raise RuntimeError(f"node_create response missing data: {response}")
    node_id = data.get("node_id")
    if isinstance(node_id, str) and node_id:
        return node_id
    diff = data.get("diff", {})
    created = diff.get("nodes_created", []) if isinstance(diff, dict) else []
    if isinstance(created, list) and created:
        value = created[0].get("node_id") if isinstance(created[0], dict) else None
        if isinstance(value, str) and value:
            return value
    raise RuntimeError(f"node_create response missing node_id: {response}")


def assert_no_patch_errors(response: dict[str, Any], label: str) -> None:
    diagnostics = response.get("diagnostics", [])
    if any(isinstance(item, dict) and item.get("severity") == "error" for item in diagnostics):
        raise RuntimeError(f"{label} reported diagnostics: {diagnostics}")
    data = response.get("data", {})
    post = data.get("post_checks", {}) if isinstance(data, dict) else {}
    compile_data = post.get("compile", {}) if isinstance(post, dict) else {}
    if isinstance(compile_data, dict) and compile_data.get("ok") is False:
        raise RuntimeError(f"{label} compile failed: {compile_data}")


def class_counts(nodes: list[dict[str, Any]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for node in nodes:
        class_name = str(node.get("class_name", ""))
        counts[class_name] = counts.get(class_name, 0) + 1
    return counts


def ensure_material_output_connection(client: BridgeClient, target_asset: str, first_node_id: str) -> None:
    client.call(
        "node_params_set",
        {
            "asset_path": target_asset,
            "graph_kind": "material",
            "graph_name": None,
            "node_id": "MaterialOutput",
            "params": {"bUseMaterialAttributes": "True"},
            "dry_run": False,
            "compile_after": False,
        },
    )
    links = client.call(
        "graph_node_info_get",
        {
            "asset_path": target_asset,
            "graph_kind": "material",
            "format": "indexed",
            "id_mode": "both",
            "max_nodes": 200,
            "section": "links",
        },
    )
    text = str(links.get("data", {}).get("text", ""))
    if "MaterialOutput.MaterialAttributes" in text:
        return
    response = client.call(
        "graph_patch_apply",
        {
            "asset_path": target_asset,
            "graph_kind": "material",
            "graph_name": None,
            "dry_run": False,
            "compile_after": True,
            "operations": [
                {
                    "op": "connect_pins",
                    "from_node_id": first_node_id,
                    "from_pin_id": "Material Attributes",
                    "to_node_id": "MaterialOutput",
                    "to_pin_id": "MaterialAttributes",
                }
            ],
        },
    )
    assert_no_patch_errors(response, "material output connection")


def run(client: BridgeClient, source_package: str, target_package: str, force: bool) -> str:
    source_asset = package_path_to_object_path(source_package)
    target_asset = package_path_to_object_path(target_package)

    source = client.call(
        "graph_snapshot_get",
        {
            "asset_path": source_asset,
            "graph_kind": "material",
            "format": "full",
            "include_links": True,
            "include_node_params": True,
        },
    )["data"]
    nodes = [node for node in source.get("nodes", []) if isinstance(node, dict)]
    links = [link for link in source.get("links", []) if isinstance(link, dict)]
    if not nodes or not links:
        raise RuntimeError("source material snapshot did not contain nodes and links")

    if force:
        try:
            client.call("asset_delete", {"asset_path": target_asset, "dry_run": False, "allow_referenced": True})
        except RuntimeError as exc:
            if "asset_not_found" not in str(exc):
                raise

    client.call("folder_create", {"folder_path": target_package.rsplit("/", 1)[0], "dry_run": False})
    client.call("asset_create", {"asset_kind": "material", "asset_path": target_package, "dry_run": False, "save": False})

    id_map: dict[str, str] = {}
    for index, node in enumerate(nodes, start=1):
        position = node.get("position", {})
        response = client.call(
            "node_create",
            {
                "asset_path": target_asset,
                "graph_kind": "material",
                "graph_name": None,
                "node_class": node.get("class_name"),
                "position": {
                    "x": int(position.get("x", 0)) if isinstance(position, dict) else 0,
                    "y": int(position.get("y", 0)) if isinstance(position, dict) else 0,
                },
                "params": params_for_node(node),
                "dry_run": False,
            },
        )
        old_id = node.get("node_id")
        if not isinstance(old_id, str) or not old_id:
            raise RuntimeError(f"source node missing node_id at index {index}")
        id_map[old_id] = node_id_from_create(response)

    operations: list[dict[str, Any]] = []
    for link in links:
        from_node_id = link.get("from_node_id")
        to_node_id = link.get("to_node_id")
        from_pin_id = link.get("from_pin_id")
        to_pin_id = link.get("to_pin_id")
        if not all(isinstance(value, str) and value for value in (from_node_id, to_node_id, from_pin_id, to_pin_id)):
            continue
        mapped_from = id_map.get(from_node_id)
        if not mapped_from:
            raise RuntimeError(f"source link references unknown from node: {link}")
        mapped_to = "MaterialOutput" if to_node_id == "MaterialOutput" else id_map.get(to_node_id)
        if not mapped_to:
            raise RuntimeError(f"source link references unknown to node: {link}")
        operations.append(
            {
                "op": "connect_pins",
                "from_node_id": mapped_from,
                "from_pin_id": pin_name_by_id(nodes, from_pin_id),
                "to_node_id": mapped_to,
                "to_pin_id": "MaterialAttributes" if to_node_id == "MaterialOutput" else pin_name_by_id(nodes, to_pin_id),
            }
        )

    for start in range(0, len(operations), 40):
        response = client.call(
            "graph_patch_apply",
            {
                "asset_path": target_asset,
                "graph_kind": "material",
                "graph_name": None,
                "dry_run": False,
                "compile_after": False,
                "operations": operations[start:start + 40],
            },
        )
        assert_no_patch_errors(response, f"connect chunk {start // 40 + 1}")

    ensure_material_output_connection(client, target_asset, id_map[str(nodes[0]["node_id"])])

    compile_response = client.call("asset_compile", {"asset_path": target_asset})
    if compile_response.get("ok") is False:
        raise RuntimeError(f"asset_compile failed: {compile_response}")
    client.call("asset_save", {"asset_path": target_asset, "only_if_dirty": True, "fail_if_open_editor_conflict": True})

    target = client.call(
        "graph_snapshot_get",
        {
            "asset_path": target_asset,
            "graph_kind": "material",
            "format": "full",
            "include_links": True,
            "include_node_params": True,
        },
    )["data"]
    target_nodes = [node for node in target.get("nodes", []) if isinstance(node, dict)]
    target_links = [link for link in target.get("links", []) if isinstance(link, dict)]
    if len(target_nodes) != len(nodes):
        raise RuntimeError(f"node count mismatch: source={len(nodes)} target={len(target_nodes)}")
    if len(target_links) != len(links):
        raise RuntimeError(f"link count mismatch: source={len(links)} target={len(target_links)}")
    if class_counts(target_nodes) != class_counts(nodes):
        raise RuntimeError(f"class count mismatch: source={class_counts(nodes)} target={class_counts(target_nodes)}")

    client.call("auto_index_rebuild", {"root_path": "/Game"})
    client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})
    return target_asset


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bridge-url", default=DEFAULT_BRIDGE_URL)
    parser.add_argument("--source", default=DEFAULT_SOURCE)
    parser.add_argument("--target", default=DEFAULT_TARGET)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    log = JsonlLog(REPO_ROOT / "logs" / f"replicate_dam_master_material_{uuid.uuid4().hex}.jsonl")
    client = BridgeClient(args.bridge_url, 30.0, log)
    wait_for_bridge(client, 15.0)
    target_asset = run(client, args.source, args.target, args.force)
    print(json.dumps({"target_asset": target_asset, "log": str(log.path)}, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
