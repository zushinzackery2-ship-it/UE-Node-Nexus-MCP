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
DEFAULT_ROOT = "/Game/MCPDemoTry"


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
        request_id = f"{operation}-{time.time_ns()}"
        envelope = {"operation": operation, "request_id": request_id, "payload": payload}
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


def node_id(response: dict[str, Any], label: str) -> str:
    value = response.get("data", {}).get("node_id")
    if not isinstance(value, str) or not value:
        raise RuntimeError(f"{label} did not return node_id")
    return value


def pins(response: dict[str, Any]) -> list[dict[str, Any]]:
    items = response.get("data", {}).get("pins", [])
    return [item for item in items if isinstance(item, dict)] if isinstance(items, list) else []


def find_pin(response: dict[str, Any], direction: str, names: set[str]) -> str:
    wanted = {name.lower() for name in names}
    for pin in pins(response):
        if str(pin.get("direction", "")).lower() != direction.lower():
            continue
        if str(pin.get("name", "")).lower() not in wanted:
            continue
        pin_id = pin.get("pin_id")
        if isinstance(pin_id, str) and pin_id:
            return pin_id
    raise RuntimeError(f"pin not found direction={direction} names={sorted(names)}")


def assert_compile(response: dict[str, Any], label: str) -> None:
    data = response.get("data", {})
    post = data.get("post_checks", {}) if isinstance(data, dict) else {}
    compile_data = post.get("compile", {}) if isinstance(post, dict) else {}
    if isinstance(compile_data, dict) and compile_data.get("ok") is False:
        raise RuntimeError(f"{label} compile failed: {compile_data}")


def create_material_node(client: BridgeClient, asset_path: str, node_class: str, x: int, y: int, params: dict[str, Any] | None = None) -> str:
    response = client.call(
        "node_create",
        {
            "asset_path": asset_path,
            "graph_kind": "material",
            "graph_name": None,
            "node_class": node_class,
            "position": {"x": x, "y": y},
            "params": params or {},
            "dry_run": False,
        },
    )
    return node_id(response, node_class)


def run_six_axis_material_demo(client: BridgeClient, root: str) -> str:
    package_path = f"{root.rstrip('/')}/M_SixAxisBlendDemo"
    asset_path = object_path(package_path)
    try:
        client.call("asset_delete", {"asset_path": asset_path, "dry_run": False, "allow_referenced": True})
    except RuntimeError as exc:
        if "asset_not_found" not in str(exc):
            raise
    client.call("folder_create", {"folder_path": root, "dry_run": False})
    client.call("asset_create", {"asset_kind": "material", "asset_path": package_path, "dry_run": False, "save": False})

    # Six directional color samples. This is a compact six-way blend core:
    # +X/-X/+Y/-Y/+Z/-Z weighted colors are averaged into BaseColor.
    constants = [
        create_material_node(client, asset_path, "Constant3Vector", -900, -360, {"Constant": "(R=1.0,G=0.05,B=0.04,A=1.0)"}),
        create_material_node(client, asset_path, "Constant3Vector", -900, -220, {"Constant": "(R=0.04,G=0.25,B=1.0,A=1.0)"}),
        create_material_node(client, asset_path, "Constant3Vector", -900, -80, {"Constant": "(R=0.06,G=0.9,B=0.22,A=1.0)"}),
        create_material_node(client, asset_path, "Constant3Vector", -900, 60, {"Constant": "(R=1.0,G=0.75,B=0.04,A=1.0)"}),
        create_material_node(client, asset_path, "Constant3Vector", -900, 200, {"Constant": "(R=0.8,G=0.12,B=1.0,A=1.0)"}),
        create_material_node(client, asset_path, "Constant3Vector", -900, 340, {"Constant": "(R=0.02,G=0.95,B=0.95,A=1.0)"}),
    ]
    add_a = create_material_node(client, asset_path, "Add", -520, -300)
    add_b = create_material_node(client, asset_path, "Add", -520, -120)
    add_c = create_material_node(client, asset_path, "Add", -520, 80)
    add_d = create_material_node(client, asset_path, "Add", -250, -210)
    add_e = create_material_node(client, asset_path, "Add", -250, 10)
    multiply = create_material_node(client, asset_path, "Multiply", 20, -80)
    scalar = create_material_node(client, asset_path, "Constant", -250, 260, {"R": 0.166667})

    ops: list[dict[str, Any]] = [
        {"op": "connect_pins", "from_node_id": constants[0], "from_pin_id": "0", "to_node_id": add_a, "to_pin_id": "A"},
        {"op": "connect_pins", "from_node_id": constants[1], "from_pin_id": "0", "to_node_id": add_a, "to_pin_id": "B"},
        {"op": "connect_pins", "from_node_id": constants[2], "from_pin_id": "0", "to_node_id": add_b, "to_pin_id": "A"},
        {"op": "connect_pins", "from_node_id": constants[3], "from_pin_id": "0", "to_node_id": add_b, "to_pin_id": "B"},
        {"op": "connect_pins", "from_node_id": constants[4], "from_pin_id": "0", "to_node_id": add_c, "to_pin_id": "A"},
        {"op": "connect_pins", "from_node_id": constants[5], "from_pin_id": "0", "to_node_id": add_c, "to_pin_id": "B"},
        {"op": "connect_pins", "from_node_id": add_a, "from_pin_id": "0", "to_node_id": add_d, "to_pin_id": "A"},
        {"op": "connect_pins", "from_node_id": add_b, "from_pin_id": "0", "to_node_id": add_d, "to_pin_id": "B"},
        {"op": "connect_pins", "from_node_id": add_d, "from_pin_id": "0", "to_node_id": add_e, "to_pin_id": "A"},
        {"op": "connect_pins", "from_node_id": add_c, "from_pin_id": "0", "to_node_id": add_e, "to_pin_id": "B"},
        {"op": "connect_pins", "from_node_id": add_e, "from_pin_id": "0", "to_node_id": multiply, "to_pin_id": "A"},
        {"op": "connect_pins", "from_node_id": scalar, "from_pin_id": "0", "to_node_id": multiply, "to_pin_id": "B"},
        {"op": "connect_pins", "from_node_id": multiply, "from_pin_id": "0", "to_node_id": "MaterialOutput", "to_pin_id": "BaseColor"},
    ]
    patch = client.call(
        "graph_patch_apply",
        {"asset_path": asset_path, "graph_kind": "material", "graph_name": None, "dry_run": False, "compile_after": True, "operations": ops},
    )
    assert_compile(patch, "six-axis material")
    client.call("asset_compile", {"asset_path": asset_path})
    client.call("asset_save", {"asset_path": asset_path, "only_if_dirty": True, "fail_if_open_editor_conflict": True})
    snapshot = client.call(
        "graph_node_info_get",
        {"asset_path": asset_path, "graph_kind": "material", "format": "indexed", "id_mode": "both", "max_nodes": 50, "section": "all"},
    )
    text = str(snapshot.get("data", {}).get("text", ""))
    if "MaterialOutput.BaseColor" not in text and "MaterialOutput.BaseColor".lower() not in text.lower():
        raise RuntimeError("six-axis material readback missing BaseColor output connection")
    return asset_path


def create_blueprint_node(
    client: BridgeClient,
    asset_path: str,
    node_class: str,
    x: int,
    y: int,
    params: dict[str, Any] | None = None,
    graph_name: str | None = "EventGraph",
) -> dict[str, Any]:
    return client.call(
        "node_create",
        {
            "asset_path": asset_path,
            "graph_kind": "blueprint",
            "graph_name": graph_name,
            "node_class": node_class,
            "position": {"x": x, "y": y},
            "params": params or {},
            "dry_run": False,
        },
    )


def call_function_params(owner: str, name: str) -> dict[str, Any]:
    return {"function_owner": owner, "function_name": name}


def run_3c_blueprint_demo(client: BridgeClient, root: str) -> str:
    run_id = datetime.now().strftime("%H%M%S")
    package_path = f"{root.rstrip('/')}/BP_MCP_3C_Demo_{run_id}"
    asset_path = object_path(package_path)
    try:
        client.call("asset_delete", {"asset_path": asset_path, "dry_run": False, "allow_referenced": True})
    except RuntimeError as exc:
        if "asset_not_found" not in str(exc):
            raise
    client.call(
        "asset_create",
        {
            "asset_kind": "blueprint",
            "asset_path": package_path,
            "parent_class_path": "/Script/Engine.Character",
            "dry_run": False,
            "save": False,
        },
    )

    space = create_blueprint_node(
        client,
        asset_path,
        "/Script/BlueprintGraph.K2Node_InputKey",
        -880,
        -260,
        {"input_key": "SpaceBar", "consume_input": True},
    )
    jump = create_blueprint_node(
        client,
        asset_path,
        "/Script/BlueprintGraph.K2Node_CallFunction",
        -460,
        -300,
        call_function_params("/Script/Engine.Character", "Jump"),
    )
    stop_jump = create_blueprint_node(
        client,
        asset_path,
        "/Script/BlueprintGraph.K2Node_CallFunction",
        -460,
        -120,
        call_function_params("/Script/Engine.Character", "StopJumping"),
    )
    dash_event = create_blueprint_node(
        client,
        asset_path,
        "/Script/BlueprintGraph.K2Node_CustomEvent",
        -880,
        100,
        {"event_name": "MCP_Dash"},
    )
    launch = create_blueprint_node(
        client,
        asset_path,
        "/Script/BlueprintGraph.K2Node_CallFunction",
        -460,
        80,
        call_function_params("/Script/Engine.Character", "LaunchCharacter"),
    )
    branch = create_blueprint_node(client, asset_path, "/Script/BlueprintGraph.K2Node_IfThenElse", -640, 320, {})

    ops: list[dict[str, Any]] = [
        {
            "op": "connect_pins",
            "from_node_id": node_id(space, "Space input"),
            "from_pin_id": find_pin(space, "output", {"Pressed"}),
            "to_node_id": node_id(jump, "Jump"),
            "to_pin_id": find_pin(jump, "input", {"execute", "exec"}),
        },
        {
            "op": "connect_pins",
            "from_node_id": node_id(space, "Space input"),
            "from_pin_id": find_pin(space, "output", {"Released"}),
            "to_node_id": node_id(stop_jump, "StopJumping"),
            "to_pin_id": find_pin(stop_jump, "input", {"execute", "exec"}),
        },
        {
            "op": "connect_pins",
            "from_node_id": node_id(dash_event, "Dash event"),
            "from_pin_id": find_pin(dash_event, "output", {"then", "execute"}),
            "to_node_id": node_id(launch, "LaunchCharacter"),
            "to_pin_id": find_pin(launch, "input", {"execute", "exec"}),
        },
        {
            "op": "set_node_param",
            "node_id": node_id(launch, "LaunchCharacter"),
            "name": "LaunchVelocity",
            "value": "X=900.000 Y=0.000 Z=180.000",
        },
        {
            "op": "set_node_param",
            "node_id": node_id(launch, "LaunchCharacter"),
            "name": "bXYOverride",
            "value": True,
        },
        {
            "op": "set_node_param",
            "node_id": node_id(launch, "LaunchCharacter"),
            "name": "bZOverride",
            "value": False,
        },
        {
            "op": "set_node_param",
            "node_id": node_id(branch, "Branch"),
            "name": "Condition",
            "value": True,
        },
    ]
    patch = client.call(
        "graph_patch_apply",
        {"asset_path": asset_path, "graph_kind": "blueprint", "graph_name": "EventGraph", "dry_run": False, "compile_after": True, "operations": ops},
    )
    assert_compile(patch, "3C blueprint")
    client.call("asset_compile", {"asset_path": asset_path})
    client.call("asset_save", {"asset_path": asset_path, "only_if_dirty": True, "fail_if_open_editor_conflict": True})
    snapshot = client.call(
        "graph_node_info_get",
        {"asset_path": asset_path, "graph_kind": "blueprint", "graph_name": "EventGraph", "format": "indexed", "id_mode": "both", "max_nodes": 80, "section": "all"},
    )
    text = str(snapshot.get("data", {}).get("text", ""))
    required_tokens = ("InputKey", "CallFunction", "CustomEvent", "IfThenElse", "MCP_Dash", "Pressed>", "Released>", "then>")
    for expected in required_tokens:
        if expected.lower() not in text.lower():
            raise RuntimeError(f"3C blueprint readback missing {expected}")
    return asset_path


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Create retained six-axis material and 3C Blueprint demo assets through UE bridge.")
    parser.add_argument("--bridge-url", default=DEFAULT_BRIDGE_URL)
    parser.add_argument("--root", default=DEFAULT_ROOT)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--wait-timeout", type=float, default=10.0)
    parser.add_argument("--skip-material", action="store_true")
    parser.add_argument("--skip-blueprint", action="store_true")
    parser.add_argument("--log", type=Path, default=REPO_ROOT / "logs" / f"ue_demo_try_{datetime.now().strftime('%Y%m%d_%H%M%S')}.jsonl")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    log = JsonlLog(args.log)
    client = BridgeClient(args.bridge_url, args.timeout, log)
    try:
        wait_for_bridge(client, args.wait_timeout)
        material_path = None if args.skip_material else run_six_axis_material_demo(client, args.root)
        blueprint_path = None if args.skip_blueprint else run_3c_blueprint_demo(client, args.root)
        client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})
        summary = {"material": material_path, "blueprint": blueprint_path}
        log.write("summary", {"ok": True, **summary})
        print(f"Demo assets created: material={material_path} blueprint={blueprint_path}. Log: {args.log}")
        return 0
    except Exception as exc:
        log.write("summary", {"ok": False, "error": str(exc)})
        print(f"Demo creation failed: {exc}", file=sys.stderr)
        print(f"Evidence log: {args.log}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
