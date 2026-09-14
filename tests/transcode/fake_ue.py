"""In-memory stand-in for the UE side of the transcode ops (writes files like the plugin does)."""

from __future__ import annotations

import copy
import json
from pathlib import Path
from typing import Any

from ue_node_nexus_mcp.transcode.paths import asset_relative, object_path

SCHEMA_KEY = "5.5.4-abcd1234"
FUNCTIONS = {
    "KismetSystemLibrary.PrintString": {
        "path": "/Script/Engine.KismetSystemLibrary.PrintString",
        "params": [{"name": "InString", "type": "string", "dir": "in", "default": "Hello"},
                   {"name": "Duration", "type": "float", "dir": "in", "default": "2.0"}],
        "pure": False,
    },
}


def _prop_schema(kind: str, default: str, **extra: Any) -> dict[str, Any]:
    return {"type": kind, "kind": kind, "default": default, **extra}


class FakeUe:
    """Answers bridge operations the way the transcode plugin does, on disk."""

    def __init__(self, assets: dict[str, dict[str, Any]], project_name: str = "Shadetest") -> None:
        self.assets = {object_path(raw["asset_path"]): copy.deepcopy(raw) for raw in assets.values()}
        self.project_name = project_name
        # Engine version plus plugin hash: a rebuild or Live Coding reload moves it.
        self.schema_key = SCHEMA_KEY
        self.calls: list[tuple[str, dict[str, Any]]] = []
        self.root: str | None = None
        self.applied: list[dict[str, Any]] = []
        self.referencers: dict[str, list[str]] = {}
        self.fail_apply_index: int | None = None
        self.dirty: set[str] = set()

    # -- bridge entry point -------------------------------------------------
    def call(self, operation: str, payload: dict[str, Any], **_: Any) -> dict[str, Any]:
        self.calls.append((operation, copy.deepcopy(payload)))
        handler = getattr(self, f"op_{operation}", None)
        if handler is None:
            return {"ok": False, "operation": operation, "error": {"code": "unknown_operation", "message": operation}, "diagnostics": [], "warnings": []}
        data = handler(payload)
        if isinstance(data, dict) and data.get("__error__"):
            return {"ok": False, "operation": operation, "error": data["__error__"], "diagnostics": [], "warnings": []}
        return {"ok": True, "operation": operation, "data": data, "diagnostics": [], "warnings": []}

    __call__ = call

    # -- context ------------------------------------------------------------
    def op_project_context_get(self, payload: dict[str, Any]) -> dict[str, Any]:
        return {"project_name": self.project_name, "project_file_path": f"D:/UE/{self.project_name}/{self.project_name}.uproject"}

    def op_bridge_capabilities_get(self, payload: dict[str, Any]) -> dict[str, Any]:
        return {"schema_key": self.schema_key, "engine_version": "5.5.4", "modules": {"vfx_available": True}}

    def op_transcode_root_set(self, payload: dict[str, Any]) -> dict[str, Any]:
        self.root = payload["root"]
        return {"root": self.root}

    def op_schema_export(self, payload: dict[str, Any]) -> dict[str, Any]:
        if payload.get("details_only"):
            wanted = [str(item) for item in payload.get("functions") or []]
            return {"schema_key": self.schema_key, "functions": {name: FUNCTIONS[name] for name in wanted if name in FUNCTIONS}}
        out = Path(payload["out_dir"])
        out.mkdir(parents=True, exist_ok=True)
        (out / "key.json").write_text(json.dumps({"key": self.schema_key, "engine_version": "5.5.4", "plugins_hash": self.schema_key.rsplit("-", 1)[-1]}), encoding="utf-8")
        classes = {
            "MaterialExpressionAdd": {"path": "/Script/Engine.MaterialExpressionAdd", "props": {"Desc": _prop_schema("string", ""), "ConstA": _prop_schema("number", "0.0"), "ConstB": _prop_schema("number", "1.0")}, "inputs": ["A", "B"], "outputs": [""]},
            "MaterialExpressionSubtract": {"path": "/Script/Engine.MaterialExpressionSubtract", "props": {"Desc": _prop_schema("string", ""), "ConstA": _prop_schema("number", "0.0"), "ConstB": _prop_schema("number", "1.0")}, "inputs": ["A", "B"], "outputs": [""]},
            "MaterialExpressionMultiply": {"path": "/Script/Engine.MaterialExpressionMultiply", "props": {"Desc": _prop_schema("string", ""), "ConstA": _prop_schema("number", "0.0"), "ConstB": _prop_schema("number", "1.0")}, "inputs": ["A", "B"], "outputs": [""]},
            "MaterialExpressionConstant": {"path": "/Script/Engine.MaterialExpressionConstant", "props": {"Desc": _prop_schema("string", ""), "R": _prop_schema("number", "0.0")}, "inputs": [], "outputs": [""]},
            "MaterialExpressionTextureSample": {"path": "/Script/Engine.MaterialExpressionTextureSample", "props": {"Desc": _prop_schema("string", ""), "Texture": _prop_schema("object", "None")}, "inputs": ["UVs", "Tex", "ApplyViewMipBias", "Level"], "outputs": ["", "", "", "", ""]},
            "MaterialExpressionFunctionInput": {"path": "/Script/Engine.MaterialExpressionFunctionInput", "props": {"Desc": _prop_schema("string", ""), "InputName": _prop_schema("name", "In"), "InputType": _prop_schema("enum", "FunctionInput_Vector3", enum_values=["FunctionInput_Scalar", "FunctionInput_Vector2", "FunctionInput_Vector3"]), "SortPriority": _prop_schema("number", "32")}, "inputs": [""], "outputs": [""]},
            "MaterialExpressionFunctionOutput": {"path": "/Script/Engine.MaterialExpressionFunctionOutput", "props": {"Desc": _prop_schema("string", ""), "OutputName": _prop_schema("name", "Result")}, "inputs": [""], "outputs": []},
        }
        (out / "classes.material_expression.json").write_text(json.dumps(classes), encoding="utf-8")
        (out / "classes.asset.json").write_text(json.dumps({
            "Material": {"path": "/Script/Engine.Material", "props": {"BlendMode": _prop_schema("enum", "BLEND_Opaque", enum_values=["BLEND_Opaque", "BLEND_Translucent"]), "TwoSided": _prop_schema("bool", "False")}},
            "MaterialFunction": {"path": "/Script/Engine.MaterialFunction", "props": {"Description": _prop_schema("string", ""), "bExposeToLibrary": _prop_schema("bool", "False"), "LibraryCategoriesText": _prop_schema("array", "()")}},
        }), encoding="utf-8")
        (out / "classes.k2node.json").write_text(json.dumps({"K2Node_CallFunction": {"path": "/Script/BlueprintGraph.K2Node_CallFunction", "props": {}, "pins": [], "dynamic_pins": True}}), encoding="utf-8")
        (out / "classes.component.json").write_text("{}", encoding="utf-8")
        (out / "classes.niagara_renderer.json").write_text("{}", encoding="utf-8")
        (out / "material_functions.json").write_text("{}", encoding="utf-8")
        (out / "niagara_modules.json").write_text("{}", encoding="utf-8")
        return {"schema_key": self.schema_key, "files": 8}

    # -- status / export --------------------------------------------------
    def op_transcode_status(self, payload: dict[str, Any]) -> dict[str, Any]:
        wanted = [object_path(path) for path in payload.get("asset_paths") or []]
        if payload.get("discover"):
            wanted = sorted(set(wanted) | set(self.assets))
        rows = []
        for path in wanted:
            raw = self.assets.get(path)
            if raw is None:
                continue
            rows.append([path, raw["class"], raw["kind"], raw.get("saved_hash", ""), path in self.dirty])
        return {"assets": rows}

    def _write_raw(self, out_dir: str, raw: dict[str, Any]) -> str:
        relative = asset_relative(raw["asset_path"])
        target = Path(out_dir).joinpath(*relative.parts[:-1]) / f"{relative.name}.json"
        target.parent.mkdir(parents=True, exist_ok=True)
        stored = copy.deepcopy(raw)
        stored["schema_key"] = self.schema_key
        target.write_text(json.dumps(stored), encoding="utf-8")
        return str(target)

    def op_transcode_export(self, payload: dict[str, Any]) -> dict[str, Any]:
        if payload.get("schema_out_dir"):
            Path(payload["schema_out_dir"]).mkdir(parents=True, exist_ok=True)
            (Path(payload["schema_out_dir"]) / "niagara_modules.json").write_text("{}", encoding="utf-8")
        if not payload.get("asset_paths"):
            return {"assets": [], "skipped": [], "count": 0, "schema_written": bool(payload.get("schema_out_dir"))}
        assert self.root and Path(payload["out_dir"]).resolve().is_relative_to(Path(self.root).resolve())
        rows = []
        for path in payload.get("asset_paths") or []:
            raw = self.assets.get(object_path(path))
            if raw is None:
                continue
            rows.append({"asset_path": object_path(path), "class": raw["class"], "file": self._write_raw(payload["out_dir"], raw), "saved_hash": raw.get("saved_hash", ""), "dirty": object_path(path) in self.dirty})
        return {"assets": rows}

    op_vfx_transcode_export = op_transcode_export

    # -- apply ----------------------------------------------------------------
    def op_transcode_apply(self, payload: dict[str, Any]) -> dict[str, Any]:
        self.applied.append(copy.deepcopy(payload))
        asset_path = object_path(payload["asset_path"])
        raw = self.assets.get(asset_path)
        if raw is None:
            if not payload.get("create"):
                return {"__error__": {"code": "asset_not_found", "message": asset_path}}
            raw = {"raw_version": 1, "asset_path": asset_path, "class": f"/Script/Engine.{payload.get('asset_class', 'Material')}", "class_short": payload.get("asset_class", "Material"), "kind": payload["kind"], "saved_hash": "", "dirty": False, "props": [], "graph": {"nodes": [], "links": [], "outputs": []}}
            self.assets[asset_path] = raw
        failed: list[dict[str, Any]] = []
        id_map: dict[str, str] = {}
        applied = 0
        for index, verb in enumerate(payload.get("plan") or []):
            if self.fail_apply_index == index:
                failed.append({"index": index, "code": "simulated_failure", "message": "simulated"})
                break
            self._apply_verb(raw, verb, payload.get("ids") or {}, id_map)
            applied += 1
        raw["saved_hash"] = f"h-{len(self.applied)}"
        file = self._write_raw(payload["out_dir"], raw) if payload.get("out_dir") else ""
        return dict(applied=applied, failed=failed, diagnostics=[], compile=dict(ran=True, ok=not failed, error_count=0),
                    saved=True, file=file, saved_hash=raw["saved_hash"], dirty=False, id_map=id_map)

    op_vfx_transcode_apply = op_transcode_apply

    def _apply_verb(self, raw: dict[str, Any], verb: dict[str, Any], ids: dict[str, str], id_map: dict[str, str]) -> None:
        graph = raw.setdefault("graph", {"nodes": [], "links": [], "outputs": []})
        by_id = dict(ids)
        by_id.update(id_map)
        nodes = {node["guid"]: node for node in graph["nodes"]}
        op = verb["op"]
        if op == "set_asset_prop":
            for prop in raw["props"]:
                if prop["name"] == verb["name"]:
                    prop["value"] = verb["value"]
        elif op == "create_node":
            guid = f"G-NEW-{len(id_map) + 1}"
            id_map[verb["id"]] = guid
            props = [{"name": key, "type": "FString", "value": value, "default": ""} for key, value in (verb.get("params") or {}).items()]
            graph["nodes"].append({"guid": guid, "class": f"/Script/Engine.MaterialExpression{verb['class']}", "class_short": verb["class"], "name": verb["id"], "x": verb.get("x", 0), "y": verb.get("y", 0), "props": props, "inputs": ["A", "B"], "outputs": [""]})
        elif op == "delete_node":
            guid = by_id[verb["id"]]
            graph["nodes"] = [node for node in graph["nodes"] if node["guid"] != guid]
            graph["links"] = [link for link in graph["links"] if guid not in (link["from"], link["to"])]
            graph["outputs"] = [out for out in graph["outputs"] if out["from"] != guid]
        elif op == "set_node_param":
            node = nodes[by_id[verb["id"]]]
            for prop in node["props"]:
                if prop["name"] == verb["name"]:
                    prop["value"] = verb["value"] if verb["value"] is not None else prop["default"]
                    break
            else:
                node["props"].append({"name": verb["name"], "type": "FString", "value": verb["value"], "default": ""})
        elif op == "set_node_position":
            node = nodes[by_id[verb["id"]]]
            node["x"], node["y"] = verb["x"], verb["y"]
        elif op == "connect_pins":
            src = by_id[verb["from"]]
            if verb["to"] == "out":
                graph["outputs"].append({"from": src, "from_out": 0, "property": verb["to_pin"]})
            else:
                dst = nodes[by_id[verb["to"]]]
                to_in = dst["inputs"].index(verb["to_pin"]) if verb.get("to_pin") in dst["inputs"] else 0
                graph["links"].append({"from": src, "from_out": 0, "to": dst["guid"], "to_in": to_in})
        elif op == "disconnect_pins":
            if verb["to"] == "out":
                graph["outputs"] = [out for out in graph["outputs"] if out["property"] != verb["to_pin"]]
            else:
                dst = nodes[by_id[verb["to"]]]
                to_in = dst["inputs"].index(verb["to_pin"]) if verb.get("to_pin") in dst["inputs"] else 0
                graph["links"] = [link for link in graph["links"] if not (link["to"] == dst["guid"] and link["to_in"] == to_in)]
        elif op == "refresh_function_calls":
            raw.setdefault("refreshed", []).append(verb["function"])

    def op_asset_referencers_get(self, payload: dict[str, Any]) -> dict[str, Any]:
        return {"items": [[package, "hard"] for package in self.referencers.get(object_path(payload["asset_path"]), [])]}
