from __future__ import annotations

import argparse
import json
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BRIDGE_URL = "http://127.0.0.1:8765"
DEFAULT_ASSET_PACKAGE = "/Game/YN/Material/M_SixFaceMapping"


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
        envelope = {
            "operation": operation,
            "request_id": f"{operation}-{time.time_ns()}",
            "payload": payload,
        }
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


def object_path(package_path: str) -> str:
    name = package_path.rsplit("/", 1)[-1]
    return f"{package_path}.{name}"


def node_id(response: dict[str, Any], label: str) -> str:
    value = response.get("data", {}).get("node_id")
    if not isinstance(value, str) or not value:
        raise RuntimeError(f"{label} did not return node_id: {response}")
    return value


def create_node(
    client: BridgeClient,
    asset_path: str,
    node_class: str,
    x: int,
    y: int,
    params: dict[str, Any] | None = None,
    name: str | None = None,
) -> str:
    response = client.call(
        "node_create",
        {
            "asset_path": asset_path,
            "graph_kind": "material",
            "graph_name": None,
            "node_class": node_class,
            "name": name,
            "position": {"x": x, "y": y},
            "params": params or {},
            "dry_run": False,
        },
    )
    return node_id(response, name or node_class)


def vector_param(name: str, color: str, group: str, priority: int) -> dict[str, Any]:
    return {
        "ParameterName": name,
        "Group": group,
        "SortPriority": priority,
        "DefaultValue": color,
    }


def scalar_param(name: str, value: float, group: str, priority: int, slider_min: float, slider_max: float) -> dict[str, Any]:
    return {
        "ParameterName": name,
        "Group": group,
        "SortPriority": priority,
        "DefaultValue": value,
        "SliderMin": slider_min,
        "SliderMax": slider_max,
    }


def add_connect(ops: list[dict[str, Any]], source: str, source_pin: str, target: str, target_pin: str) -> None:
    ops.append(
        {
            "op": "connect_pins",
            "from_node_id": source,
            "from_pin_id": source_pin,
            "to_node_id": target,
            "to_pin_id": target_pin,
        }
    )


def build_weight_chain(
    client: BridgeClient,
    asset_path: str,
    ops: list[dict[str, Any]],
    normal_node: str,
    axis_pin: str,
    positive: bool,
    x: int,
    y: int,
    label: str,
) -> str:
    mask_flags = {"R": axis_pin == "R", "G": axis_pin == "G", "B": axis_pin == "B"}
    mask = create_node(client, asset_path, "ComponentMask", x, y, mask_flags, f"Mask_{label}")
    add_connect(ops, normal_node, "0", mask, "Input")

    source = mask
    if not positive:
        negate = create_node(client, asset_path, "Multiply", x + 230, y, {"ConstB": -1.0}, f"Negate_{label}")
        add_connect(ops, mask, "0", negate, "A")
        source = negate

    clamp = create_node(client, asset_path, "Max", x + 460, y, {"ConstB": 0.0}, f"Clamp_{label}")
    add_connect(ops, source, "0", clamp, "A")

    sharpen = create_node(client, asset_path, "Power", x + 690, y, {"ConstExponent": 4.0}, f"Weight_{label}")
    add_connect(ops, clamp, "0", sharpen, "Base")
    return sharpen


def add_chain(client: BridgeClient, asset_path: str, ops: list[dict[str, Any]], a: str, b: str, x: int, y: int, label: str) -> str:
    node = create_node(client, asset_path, "Add", x, y, name=f"Add_{label}")
    add_connect(ops, a, "0", node, "A")
    add_connect(ops, b, "0", node, "B")
    return node


def multiply_chain(client: BridgeClient, asset_path: str, ops: list[dict[str, Any]], a: str, b: str, x: int, y: int, label: str) -> str:
    node = create_node(client, asset_path, "Multiply", x, y, name=f"Mul_{label}")
    add_connect(ops, a, "0", node, "A")
    add_connect(ops, b, "0", node, "B")
    return node


def create_six_face_mapping_material(client: BridgeClient, asset_package: str, replace: bool) -> str:
    asset_path = object_path(asset_package)
    if replace:
        try:
            client.call("asset_delete", {"asset_path": asset_path, "dry_run": False, "allow_referenced": True})
        except RuntimeError as exc:
            if "asset_not_found" not in str(exc):
                raise

    client.call("folder_create", {"folder_path": asset_package.rsplit("/", 1)[0], "dry_run": False})
    client.call("asset_create", {"asset_kind": "material", "asset_path": asset_package, "dry_run": False, "save": False})

    ops: list[dict[str, Any]] = []
    normal = create_node(client, asset_path, "PixelNormalWS", -1500, 0, name="PixelNormalWS")
    faces = [
        ("PosX", "R", True, "(R=1.000000,G=0.050000,B=0.020000,A=1.000000)", -920),
        ("NegX", "R", False, "(R=0.020000,G=0.240000,B=1.000000,A=1.000000)", -620),
        ("PosY", "G", True, "(R=0.030000,G=0.850000,B=0.180000,A=1.000000)", -320),
        ("NegY", "G", False, "(R=1.000000,G=0.760000,B=0.030000,A=1.000000)", -20),
        ("PosZ", "B", True, "(R=0.900000,G=0.120000,B=1.000000,A=1.000000)", 280),
        ("NegZ", "B", False, "(R=0.020000,G=0.900000,B=0.950000,A=1.000000)", 580),
    ]

    color_terms: list[str] = []
    weight_nodes: list[str] = []
    for index, (face, axis, positive, color, y) in enumerate(faces):
        color_node = create_node(
            client,
            asset_path,
            "VectorParameter",
            -1320,
            y,
            vector_param(f"{face}_Color", color, "SixFace Colors", index),
            f"{face}_Color",
        )
        weight = build_weight_chain(client, asset_path, ops, normal, axis, positive, -1080, y, face)
        weighted_color = multiply_chain(client, asset_path, ops, color_node, weight, -110, y, f"{face}_ColorWeight")
        color_terms.append(weighted_color)
        weight_nodes.append(weight)

    color_sum_a = add_chain(client, asset_path, ops, color_terms[0], color_terms[1], 180, -770, "Color_X")
    color_sum_b = add_chain(client, asset_path, ops, color_terms[2], color_terms[3], 180, -190, "Color_Y")
    color_sum_c = add_chain(client, asset_path, ops, color_terms[4], color_terms[5], 180, 390, "Color_Z")
    color_sum_ab = add_chain(client, asset_path, ops, color_sum_a, color_sum_b, 430, -430, "Color_XY")
    color_sum = add_chain(client, asset_path, ops, color_sum_ab, color_sum_c, 680, -120, "Color_XYZ")

    weight_sum_a = add_chain(client, asset_path, ops, weight_nodes[0], weight_nodes[1], 180, 790, "Weight_X")
    weight_sum_b = add_chain(client, asset_path, ops, weight_nodes[2], weight_nodes[3], 430, 790, "Weight_Y")
    weight_sum_c = add_chain(client, asset_path, ops, weight_nodes[4], weight_nodes[5], 680, 790, "Weight_Z")
    weight_sum_ab = add_chain(client, asset_path, ops, weight_sum_a, weight_sum_b, 930, 720, "Weight_XY")
    weight_sum_raw = add_chain(client, asset_path, ops, weight_sum_ab, weight_sum_c, 1180, 650, "Weight_XYZ")
    weight_sum = create_node(client, asset_path, "Max", 1430, 650, {"ConstB": 0.0001}, "Weight_SafeDenominator")
    add_connect(ops, weight_sum_raw, "0", weight_sum, "A")

    base_color = create_node(client, asset_path, "Divide", 980, -120, name="Normalized_SixFace_Color")
    add_connect(ops, color_sum, "0", base_color, "A")
    add_connect(ops, weight_sum, "0", base_color, "B")

    roughness = create_node(
        client,
        asset_path,
        "ScalarParameter",
        980,
        110,
        scalar_param("Roughness", 0.72, "Surface", 0, 0.0, 1.0),
        "Roughness",
    )
    specular = create_node(
        client,
        asset_path,
        "ScalarParameter",
        980,
        260,
        scalar_param("Specular", 0.25, "Surface", 1, 0.0, 1.0),
        "Specular",
    )

    add_connect(ops, base_color, "0", "MaterialOutput", "BaseColor")
    add_connect(ops, roughness, "0", "MaterialOutput", "Roughness")
    add_connect(ops, specular, "0", "MaterialOutput", "Specular")

    patch = client.call(
        "graph_patch_apply",
        {
            "asset_path": asset_path,
            "graph_kind": "material",
            "graph_name": None,
            "dry_run": False,
            "compile_after": True,
            "operations": ops,
        },
    )
    compile_data = patch.get("data", {}).get("post_checks", {}).get("compile", {})
    if isinstance(compile_data, dict) and compile_data.get("ok") is False:
        raise RuntimeError(f"material compile failed after patch: {compile_data}")

    client.call("asset_compile", {"asset_path": asset_path})
    client.call("asset_save", {"asset_path": asset_path, "only_if_dirty": True, "fail_if_open_editor_conflict": True})
    client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})

    snapshot = client.call(
        "graph_node_info_get",
        {
            "asset_path": asset_path,
            "graph_kind": "material",
            "graph_name": None,
            "format": "text",
            "id_mode": "alias",
            "max_nodes": 80,
            "section": "links",
        },
    )
    text = str(snapshot.get("data", {}).get("text", ""))
    for expected in ["BaseColor", "Roughness", "Specular"]:
        if expected not in text:
            raise RuntimeError(f"readback missing {expected}: {text}")
    return asset_path


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Create a six-face normal-weighted mapping material through the UE bridge.")
    parser.add_argument("--bridge-url", default=DEFAULT_BRIDGE_URL)
    parser.add_argument("--asset-package", default=DEFAULT_ASSET_PACKAGE)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--replace", action="store_true", default=True)
    parser.add_argument("--log", type=Path, default=REPO_ROOT / "logs" / f"six_face_mapping_{datetime.now().strftime('%Y%m%d_%H%M%S')}.jsonl")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    client = BridgeClient(args.bridge_url, args.timeout, JsonlLog(args.log))
    asset_path = create_six_face_mapping_material(client, args.asset_package, args.replace)
    print(f"Created six-face mapping material: {asset_path}")
    print(f"Log: {args.log}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
