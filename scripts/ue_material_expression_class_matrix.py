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


DEFAULT_BRIDGE_URL = "http://127.0.0.1:8765"
REPO_ROOT = Path(__file__).resolve().parents[1]


class JsonlLog:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.path.parent.mkdir(parents=True, exist_ok=True)

    def write(self, event: str, data: dict[str, Any]) -> None:
        row = {
            "time": datetime.now().isoformat(timespec="seconds"),
            "event": event,
            **data,
        }
        with self.path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")


class BridgeClient:
    def __init__(self, base_url: str, timeout_seconds: float, log: JsonlLog) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout_seconds = timeout_seconds
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
            with urlopen(request, timeout=self.timeout_seconds) as response:
                raw = response.read().decode("utf-8", errors="replace")
        except HTTPError as exc:
            body = exc.read().decode("utf-8", errors="replace")
            self.log.write("http_error", {"operation": operation, "status": exc.code, "body": body})
            raise RuntimeError(f"{operation} HTTP {exc.code}") from exc
        except URLError as exc:
            self.log.write("connection_error", {"operation": operation, "reason": str(exc.reason)})
            raise RuntimeError(f"{operation} connection failed: {exc.reason}") from exc

        decoded = json.loads(raw)
        self.log.write("response", {"operation": operation, "response": decoded})
        if not isinstance(decoded, dict) or decoded.get("ok") is False:
            raise RuntimeError(f"{operation} failed: {raw}")
        return decoded


def wait_for_bridge(client: BridgeClient, timeout_seconds: float) -> None:
    deadline = time.monotonic() + timeout_seconds
    last_error = ""
    while time.monotonic() < deadline:
        try:
            client.call("level_current_get", {})
            return
        except RuntimeError as exc:
            last_error = str(exc)
            time.sleep(1.0)
    raise RuntimeError(f"bridge did not become ready within {timeout_seconds:.0f}s: {last_error}")


def require_schema(params: list[dict[str, Any]], context: str) -> None:
    for param in params:
        for field in ("name", "kind", "type", "cpp_type", "default_value", "editable"):
            if field not in param:
                raise RuntimeError(f"{context} missing {field}: {param}")
        if param.get("kind") == "enum" and not param.get("enum_values"):
            raise RuntimeError(f"{context} enum without enum_values: {param}")
        if param.get("kind") == "object" and "object_class" not in param:
            raise RuntimeError(f"{context} object without object_class: {param}")
        if param.get("kind") == "struct" and "struct_type" not in param:
            raise RuntimeError(f"{context} struct without struct_type: {param}")


def class_items(response: dict[str, Any]) -> list[dict[str, Any]]:
    items = response.get("data", {}).get("classes", [])
    if not isinstance(items, list):
        raise RuntimeError("material_expression_classes_list returned no classes array")
    return [item for item in items if isinstance(item, dict)]


def params(response: dict[str, Any]) -> list[dict[str, Any]]:
    items = response.get("data", {}).get("params", [])
    if not isinstance(items, list):
        raise RuntimeError("node_class_params_get returned no params array")
    return [item for item in items if isinstance(item, dict)]


def run_matrix(
    client: BridgeClient,
    limit: int,
    verify_short_names: bool,
    min_classes: int,
    cross_check_node_class_params: bool,
) -> dict[str, Any]:
    list_response = client.call(
        "material_expression_classes_list",
        {
            "include_abstract": False,
            "include_deprecated": False,
            "include_params": True,
            "limit": limit,
            "cursor": None,
            "format": "full",
        },
    )
    classes = class_items(list_response)
    total = int(list_response.get("data", {}).get("total", 0))
    if total < min_classes or len(classes) < min_classes:
        raise RuntimeError(f"too few material expression classes: total={total} returned={len(classes)}")

    schema_checked = 0
    for item in classes:
        path = str(item.get("path", ""))
        short_name = str(item.get("short_name", ""))
        embedded_params = [param for param in item.get("params", []) if isinstance(param, dict)]
        require_schema(embedded_params, f"{path} embedded")

        if not cross_check_node_class_params:
            schema_checked += 1
            continue

        class_path_params = params(client.call("node_class_params_get", {"graph_kind": "material", "node_class": path}))
        require_schema(class_path_params, f"{path} class_path")
        if len(class_path_params) != int(item.get("param_count", -1)):
            raise RuntimeError(
                f"{path} schema count mismatch: expected={item.get('param_count')} actual={len(class_path_params)}"
            )

        if verify_short_names and short_name:
            short_params = params(client.call("node_class_params_get", {"graph_kind": "material", "node_class": short_name}))
            require_schema(short_params, f"{short_name} short_name")
            if len(short_params) != len(class_path_params):
                raise RuntimeError(f"{short_name} schema count mismatch: expected={len(class_path_params)} actual={len(short_params)}")
        schema_checked += 1

    return {"total": total, "checked": schema_checked}


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Verify all loaded material expression class property schemas.")
    parser.add_argument("--bridge-url", default=DEFAULT_BRIDGE_URL)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--wait-timeout", type=float, default=10.0)
    parser.add_argument("--limit", type=int, default=2000)
    parser.add_argument("--min-classes", type=int, default=100)
    parser.add_argument("--cross-check-node-class-params", action="store_true")
    parser.add_argument("--skip-short-names", action="store_true")
    parser.add_argument("--log", type=Path, default=REPO_ROOT / "logs" / f"ue_material_expression_class_matrix_{datetime.now().strftime('%Y%m%d_%H%M%S')}.jsonl")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    log = JsonlLog(args.log)
    client = BridgeClient(args.bridge_url, args.timeout, log)
    try:
        wait_for_bridge(client, args.wait_timeout)
        summary = run_matrix(
            client,
            args.limit,
            not args.skip_short_names,
            args.min_classes,
            args.cross_check_node_class_params,
        )
        log.write("summary", {"ok": True, **summary})
        print(f"Material expression class matrix passed: {summary['checked']}/{summary['total']} classes. Log: {args.log}")
        return 0
    except Exception as exc:
        log.write("summary", {"ok": False, "error": str(exc)})
        print(f"Material expression class matrix failed: {exc}", file=sys.stderr)
        print(f"Evidence log: {args.log}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
