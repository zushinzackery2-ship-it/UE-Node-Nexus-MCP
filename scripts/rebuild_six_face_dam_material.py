from __future__ import annotations

import argparse
import json
import sys
import uuid
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from scripts.replicate_dam_master_material import (
    BridgeClient,
    JsonlLog,
    assert_no_patch_errors,
    node_id_from_create,
    package_path_to_object_path,
    params_for_node,
    pin_name_by_id,
    wait_for_bridge,
)


BRIDGE_URL = "http://127.0.0.1:8765"
SOURCE_MATERIAL = "/Game/YN/Material/大坝母材质.大坝母材质"
OLD_FUNCTION = "/Game/YN/Material/三维映射材质函数"
FUNCTION_PACKAGE = "/Game/YN/Material/Functions/MF_SixFaceMapping"
MATERIAL_PACKAGE = "/Game/YN/Material/M_DamMaster_SixFaceMapping"
FUNCTION_ASSET = package_path_to_object_path(FUNCTION_PACKAGE)
MATERIAL_ASSET = package_path_to_object_path(MATERIAL_PACKAGE)
APPEND_MANY = "/Engine/Functions/Engine_MaterialFunctions02/Utility/AppendMany.AppendMany"
CUSTOM_ROTATOR = "/Engine/Functions/Engine_MaterialFunctions02/Texturing/CustomRotator.CustomRotator"


def op_connect(src: str, src_pin: str, dst: str, dst_pin: str) -> dict[str, Any]:
    return {"from": {"node": src, "pin": src_pin}, "to": {"node": dst, "pin": dst_pin}}


def scalar_param(name: str, value: float, priority: int) -> dict[str, Any]:
    return {
        "ParameterName": name,
        "DefaultValue": value,
        "Group": "三维映射控制",
        "SortPriority": priority,
    }


def input_node(node_id: str, name: str, input_type: str, x: int, y: int) -> dict[str, Any]:
    return {
        "id": node_id,
        "node_class": "FunctionInput",
        "position": {"x": x, "y": y},
        "params": {"InputName": name, "InputType": input_type},
    }


def append_many(node_id: str, x: int, y: int) -> dict[str, Any]:
    return {
        "id": node_id,
        "node_class": "MaterialFunctionCall",
        "position": {"x": x, "y": y},
        "params": {"MaterialFunction": APPEND_MANY},
    }


def rotator(node_id: str, x: int, y: int) -> dict[str, Any]:
    return {
        "id": node_id,
        "node_class": "MaterialFunctionCall",
        "position": {"x": x, "y": y},
        "params": {"MaterialFunction": CUSTOM_ROTATOR},
    }


def add_weight_nodes(axis: str, pin: str, positive: bool, y: int, nodes: list[dict[str, Any]], links: list[dict[str, Any]]) -> str:
    prefix = f"{'pos' if positive else 'neg'}{axis}"
    nodes.append({"id": f"mask_{prefix}", "node_class": "ComponentMask", "position": {"x": -2100, "y": y}, "params": {pin: True}})
    links.append(op_connect("normal", "0", f"mask_{prefix}", "Input"))
    source = f"mask_{prefix}"
    if not positive:
        nodes.append({"id": f"negate_{prefix}", "node_class": "Multiply", "position": {"x": -1880, "y": y}, "params": {"ConstB": -1.0}})
        links.append(op_connect(source, "0", f"negate_{prefix}", "A"))
        source = f"negate_{prefix}"
    nodes.append({"id": f"max_{prefix}", "node_class": "Max", "position": {"x": -1660, "y": y}, "params": {"ConstB": 0.0}})
    nodes.append({"id": f"pow_{prefix}", "node_class": "Power", "position": {"x": -1440, "y": y}, "params": {}})
    links.append(op_connect(source, "0", f"max_{prefix}", "A"))
    links.append(op_connect(f"max_{prefix}", "0", f"pow_{prefix}", "Base"))
    links.append(op_connect("sharpness", "0", f"pow_{prefix}", "Exponent"))
    return f"pow_{prefix}"


def build_function_graph(client: BridgeClient) -> None:
    nodes: list[dict[str, Any]] = [
        {"id": "out", "node_class": "FunctionOutput", "position": {"x": 520, "y": 0}, "params": {"OutputName": "Result"}},
        {"id": "world", "node_class": "WorldPosition", "position": {"x": -3400, "y": 420}, "params": {}},
        {"id": "normal", "node_class": "VertexNormalWS", "position": {"x": -2360, "y": -620}, "params": {}},
        input_node("u", "U平铺", "FunctionInput_Scalar", -3400, -220),
        input_node("v", "V平铺", "FunctionInput_Scalar", -3400, -120),
        input_node("rot", "旋转", "FunctionInput_Scalar", -3400, -20),
        {"id": "tiling", **append_many("tiling", -3000, -180)},
        {"id": "sharpness", "node_class": "ScalarParameter", "position": {"x": -2360, "y": -460}, "params": scalar_param("映射混合边缘锐利度", 10.0, 0)},
    ]
    links: list[dict[str, Any]] = [op_connect("u", "0", "tiling", "R"), op_connect("v", "0", "tiling", "G")]
    textures = ["PosX_Texture", "NegX_Texture", "PosY_Texture", "NegY_Texture", "PosZ_Texture", "NegZ_Texture"]
    for index, name in enumerate(textures):
        nodes.append(input_node(name, name, "FunctionInput_Texture2D", -3400, 40 + index * 110))

    axes = [("X", "R", -700), ("Y", "G", -520), ("Z", "B", -340)]
    weights = []
    for axis, pin, base_y in axes:
        weights.append(add_weight_nodes(axis, pin, True, base_y, nodes, links))
        weights.append(add_weight_nodes(axis, pin, False, base_y + 90, nodes, links))

    for index in range(0, 6, 2):
        pair = index // 2
        nodes.append({"id": f"sum_w_{pair}", "node_class": "Add", "position": {"x": -1180 + pair * 180, "y": -540 + pair * 110}, "params": {}})
        links.append(op_connect(weights[index], "0", f"sum_w_{pair}", "A"))
        links.append(op_connect(weights[index + 1], "0", f"sum_w_{pair}", "B"))
    nodes.extend([
        {"id": "sum_w_xy", "node_class": "Add", "position": {"x": -620, "y": -480}, "params": {}},
        {"id": "sum_w_all", "node_class": "Add", "position": {"x": -420, "y": -420}, "params": {}},
        {"id": "safe_w", "node_class": "Max", "position": {"x": -220, "y": -420}, "params": {"ConstB": 0.0001}},
    ])
    links.extend([op_connect("sum_w_0", "0", "sum_w_xy", "A"), op_connect("sum_w_1", "0", "sum_w_xy", "B"), op_connect("sum_w_xy", "0", "sum_w_all", "A"), op_connect("sum_w_2", "0", "sum_w_all", "B"), op_connect("sum_w_all", "0", "safe_w", "A")])

    plane_masks = {
        "X": {"R": False, "G": True, "B": True, "A": False},
        "Y": {"R": True, "G": False, "B": True, "A": False},
        "Z": {"R": True, "G": True, "B": False, "A": False},
    }
    for axis, y in [("X", 80), ("Y", 320), ("Z", 560)]:
        nodes.extend([
            {"id": f"uvmask_{axis}", "node_class": "ComponentMask", "position": {"x": -3000, "y": y}, "params": plane_masks[axis]},
            {"id": f"uvdiv_{axis}", "node_class": "Divide", "position": {"x": -2760, "y": y}, "params": {}},
            {"id": f"rotadd_{axis}", "node_class": "Add", "position": {"x": -2760, "y": y + 100}, "params": {}},
            {"id": f"rotdiv_{axis}", "node_class": "Divide", "position": {"x": -2540, "y": y + 100}, "params": {"ConstB": 360.0}},
            {"id": f"rotparam_{axis}", "node_class": "ScalarParameter", "position": {"x": -3000, "y": y + 120}, "params": scalar_param(f"{axis}方向映射旋转角度", 0.0, {"Z": 2, "Y": 5, "X": 8}[axis])},
            {"id": f"rotator_{axis}", **rotator(f"rotator_{axis}", -2500, y)},
            {"id": f"uoff_{axis}", "node_class": "ScalarParameter", "position": {"x": -2500, "y": y + 130}, "params": scalar_param(f"{axis}方向映射U平移", 0.0, {"Z": 3, "Y": 6, "X": 9}[axis])},
            {"id": f"voff_{axis}", "node_class": "ScalarParameter", "position": {"x": -2500, "y": y + 230}, "params": scalar_param(f"{axis}方向映射V平移", 0.0, {"Z": 4, "Y": 7, "X": 10}[axis])},
            {"id": f"off_{axis}", **append_many(f"off_{axis}", -2260, y + 160)},
            {"id": f"rotmask_{axis}", "node_class": "ComponentMask", "position": {"x": -2260, "y": y}, "params": {"R": True, "G": True, "B": False, "A": False}},
            {"id": f"uvadd_{axis}", "node_class": "Add", "position": {"x": -2040, "y": y}, "params": {}},
        ])
        links.extend([
            op_connect("world", "XYZ", f"uvmask_{axis}", "Input"),
            op_connect(f"uvmask_{axis}", "0", f"uvdiv_{axis}", "A"),
            op_connect("tiling", "RG", f"uvdiv_{axis}", "B"),
            op_connect("rot", "0", f"rotadd_{axis}", "A"),
            op_connect(f"rotparam_{axis}", "0", f"rotadd_{axis}", "B"),
            op_connect(f"rotadd_{axis}", "0", f"rotdiv_{axis}", "A"),
            op_connect(f"uvdiv_{axis}", "0", f"rotator_{axis}", "UVs"),
            op_connect(f"rotdiv_{axis}", "0", f"rotator_{axis}", "Rotation Angle (0-1) (S)"),
            op_connect(f"uoff_{axis}", "0", f"off_{axis}", "R"),
            op_connect(f"voff_{axis}", "0", f"off_{axis}", "G"),
            op_connect(f"rotator_{axis}", "Rotated Values", f"rotmask_{axis}", "Input"),
            op_connect(f"rotmask_{axis}", "0", f"uvadd_{axis}", "A"),
            op_connect(f"off_{axis}", "RG", f"uvadd_{axis}", "B"),
        ])

    face_axis = {"PosX_Texture": "X", "NegX_Texture": "X", "PosY_Texture": "Y", "NegY_Texture": "Y", "PosZ_Texture": "Z", "NegZ_Texture": "Z"}
    terms = []
    for index, tex in enumerate(textures):
        y = 60 + index * 135
        nodes.extend([
            {"id": f"sample_{tex}", "node_class": "TextureSample", "position": {"x": -1580, "y": y}, "params": {"SamplerType": "SAMPLERTYPE_Color"}},
            {"id": f"norm_{tex}", "node_class": "Divide", "position": {"x": -1020, "y": y}, "params": {}},
            {"id": f"term_{tex}", "node_class": "Multiply", "position": {"x": -760, "y": y}, "params": {}},
        ])
        links.extend([
            op_connect(tex, "0", f"sample_{tex}", "TextureObject"),
            op_connect(f"uvadd_{face_axis[tex]}", "0", f"sample_{tex}", "Coordinates"),
            op_connect(weights[index], "0", f"norm_{tex}", "A"),
            op_connect("safe_w", "0", f"norm_{tex}", "B"),
            op_connect(f"sample_{tex}", "RGB", f"term_{tex}", "A"),
            op_connect(f"norm_{tex}", "0", f"term_{tex}", "B"),
        ])
        terms.append(f"term_{tex}")
    for index in range(0, 6, 2):
        pair = index // 2
        nodes.append({"id": f"sum_c_{pair}", "node_class": "Add", "position": {"x": -460, "y": 100 + pair * 220}, "params": {}})
        links.append(op_connect(terms[index], "0", f"sum_c_{pair}", "A"))
        links.append(op_connect(terms[index + 1], "0", f"sum_c_{pair}", "B"))
    nodes.extend([{"id": "sum_c_xy", "node_class": "Add", "position": {"x": -180, "y": 260}, "params": {}}, {"id": "sum_c_all", "node_class": "Add", "position": {"x": 100, "y": 240}, "params": {}}])
    links.extend([op_connect("sum_c_0", "0", "sum_c_xy", "A"), op_connect("sum_c_1", "0", "sum_c_xy", "B"), op_connect("sum_c_xy", "0", "sum_c_all", "A"), op_connect("sum_c_2", "0", "sum_c_all", "B"), op_connect("sum_c_all", "0", "out", "A")])
    response = client.call("graph_build_apply", {"asset_path": FUNCTION_ASSET, "graph_kind": "material_function", "nodes": nodes, "links": links, "dry_run": False, "compile_after": True})
    assert_no_patch_errors(response, "six face function build")


def old_function_value(value: Any) -> bool:
    return isinstance(value, str) and OLD_FUNCTION in value


def build_material_clone(client: BridgeClient) -> None:
    source = client.call("graph_snapshot_get", {"asset_path": SOURCE_MATERIAL, "graph_kind": "material", "format": "full", "include_links": True, "include_node_params": True})["data"]
    nodes = [node for node in source.get("nodes", []) if isinstance(node, dict)]
    links = [link for link in source.get("links", []) if isinstance(link, dict)]
    id_map: dict[str, str] = {}
    replaced: set[str] = set()
    weather_node_id = ""
    for node in nodes:
        params = params_for_node(node)
        if "Surface_Weather_Effects" in str(params.get("MaterialFunction", "")):
            weather_node_id = str(node.get("node_id", ""))
        if node.get("class_name", "").endswith("MaterialExpressionMaterialFunctionCall") and old_function_value(params.get("MaterialFunction")):
            params["MaterialFunction"] = FUNCTION_ASSET
            replaced.add(str(node.get("node_id")))
        pos = node.get("position", {})
        response = client.call("node_create", {"asset_path": MATERIAL_ASSET, "graph_kind": "material", "node_class": node.get("class_name"), "position": {"x": int(pos.get("x", 0)), "y": int(pos.get("y", 0))}, "params": params, "dry_run": False})
        id_map[str(node["node_id"])] = node_id_from_create(response)
    if len(replaced) != 9:
        raise RuntimeError(f"expected 9 old triplanar function calls, found {len(replaced)}")
    ops: list[dict[str, Any]] = []
    for link in links:
        from_id = str(link.get("from_node_id"))
        to_id = str(link.get("to_node_id"))
        from_pin = pin_name_by_id(nodes, str(link.get("from_pin_id")))
        to_pin = pin_name_by_id(nodes, str(link.get("to_pin_id"))) if to_id != "MaterialOutput" else "MaterialAttributes"
        mapped_from = id_map[from_id]
        mapped_to = "MaterialOutput" if to_id == "MaterialOutput" else id_map[to_id]
        if to_id in replaced and to_pin.startswith("三维映射贴图"):
            for pin in ["PosX_Texture", "NegX_Texture", "PosY_Texture", "NegY_Texture", "PosZ_Texture", "NegZ_Texture"]:
                ops.append({"op": "connect_pins", "from_node_id": mapped_from, "from_pin_id": from_pin, "to_node_id": mapped_to, "to_pin_id": pin})
            continue
        ops.append({"op": "connect_pins", "from_node_id": mapped_from, "from_pin_id": from_pin, "to_node_id": mapped_to, "to_pin_id": to_pin})
    for start in range(0, len(ops), 35):
        response = client.call("graph_patch_apply", {"asset_path": MATERIAL_ASSET, "graph_kind": "material", "operations": ops[start:start + 35], "dry_run": False, "compile_after": False})
        assert_no_patch_errors(response, f"clone connect chunk {start // 35 + 1}")
    client.call("node_params_set", {"asset_path": MATERIAL_ASSET, "graph_kind": "material", "node_id": "MaterialOutput", "params": {"bUseMaterialAttributes": "True"}, "dry_run": False, "compile_after": False})
    if not weather_node_id:
        raise RuntimeError("source material weather function call was not found")
    response = client.call("graph_patch_apply", {"asset_path": MATERIAL_ASSET, "graph_kind": "material", "operations": [{"op": "connect_pins", "from_node_id": id_map[weather_node_id], "from_pin_id": "Material Attributes", "to_node_id": "MaterialOutput", "to_pin_id": "MaterialAttributes"}], "dry_run": False, "compile_after": False})
    assert_no_patch_errors(response, "material output weather connection")


def validate(client: BridgeClient) -> dict[str, Any]:
    function = client.call("graph_snapshot_get", {"asset_path": FUNCTION_ASSET, "graph_kind": "material_function", "format": "full", "include_links": True, "include_node_params": True})["data"]
    material = client.call("graph_snapshot_get", {"asset_path": MATERIAL_ASSET, "graph_kind": "material", "format": "full", "include_links": True, "include_node_params": True})["data"]
    fn_nodes = [node for node in function.get("nodes", []) if isinstance(node, dict)]
    mat_nodes = [node for node in material.get("nodes", []) if isinstance(node, dict)]
    texture_inputs = [node for node in fn_nodes if any(p.get("name") == "InputType" and p.get("value") == "FunctionInput_Texture2D" for p in node.get("params", []))]
    texture_samples = [node for node in fn_nodes if str(node.get("class_name", "")).endswith("MaterialExpressionTextureSample")]
    custom_nodes = [node for node in fn_nodes if str(node.get("class_name", "")).endswith("MaterialExpressionCustom")]
    new_calls = [node for node in mat_nodes if any(p.get("name") == "MaterialFunction" and FUNCTION_ASSET in str(p.get("value")) for p in node.get("params", []))]
    old_calls = [node for node in mat_nodes if any(p.get("name") == "MaterialFunction" and OLD_FUNCTION in str(p.get("value")) for p in node.get("params", []))]
    if len(texture_inputs) != 6 or len(texture_samples) != 6 or custom_nodes or len(new_calls) != 9 or old_calls:
        raise RuntimeError({"texture_inputs": len(texture_inputs), "texture_samples": len(texture_samples), "custom_nodes": len(custom_nodes), "new_calls": len(new_calls), "old_calls": len(old_calls)})
    output_info = client.call("node_info_get", {"asset_path": MATERIAL_ASSET, "graph_kind": "material", "node_id": "MaterialOutput", "section": "all", "format": "text"})
    output_text = str(output_info.get("data", {}).get("text", ""))
    if "inpin_00.MaterialAttributes <" not in output_text or " < None" in output_text.split("inpin_00.MaterialAttributes <", 1)[1].splitlines()[0]:
        raise RuntimeError("material output has no MaterialAttributes link")
    client.call("asset_compile", {"asset_path": FUNCTION_ASSET})
    client.call("asset_compile", {"asset_path": MATERIAL_ASSET})
    client.call("asset_save", {"asset_path": FUNCTION_ASSET, "only_if_dirty": True, "fail_if_open_editor_conflict": True})
    client.call("asset_save", {"asset_path": MATERIAL_ASSET, "only_if_dirty": True, "fail_if_open_editor_conflict": True})
    client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})
    return {"function_nodes": len(fn_nodes), "material_nodes": len(mat_nodes), "texture_inputs": 6, "texture_samples": 6, "new_calls": 9}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bridge-url", default=BRIDGE_URL)
    args = parser.parse_args()
    log = JsonlLog(REPO_ROOT / "logs" / f"rebuild_six_face_dam_material_{uuid.uuid4().hex}.jsonl")
    client = BridgeClient(args.bridge_url, 30.0, log)
    wait_for_bridge(client, 10.0)
    client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})
    for asset in [MATERIAL_ASSET, FUNCTION_ASSET]:
        try:
            client.call("asset_delete", {"asset_path": asset, "dry_run": False, "allow_referenced": True, "cleanup_after_delete": True})
        except RuntimeError as exc:
            if "asset_not_found" not in str(exc):
                raise
    client.call("folder_create", {"folder_path": FUNCTION_PACKAGE.rsplit("/", 1)[0], "dry_run": False})
    client.call("asset_create", {"asset_kind": "material_function", "asset_path": FUNCTION_PACKAGE, "dry_run": False, "save": False})
    client.call("asset_create", {"asset_kind": "material", "asset_path": MATERIAL_PACKAGE, "dry_run": False, "save": False})
    build_function_graph(client)
    build_material_clone(client)
    result = validate(client)
    result["log"] = str(log.path)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
