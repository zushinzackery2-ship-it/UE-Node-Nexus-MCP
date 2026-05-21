from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
import uuid
from datetime import datetime
from pathlib import Path
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from scripts.ue_asset_smoke_matrix import build_asset_matrix, run_write_smoke
from scripts.ue_asset_recreate_matrix import run_asset_recreate_matrix
from scripts.ue_auto_index_matrix import run_auto_index_matrix
from scripts.ue_blueprint_write_matrix import run_blueprint_write_matrix
from scripts.ue_graph_build_custom_matrix import run_graph_build_custom_matrix
from scripts.ue_level_material_matrix import run_level_material_matrix
from scripts.ue_material_function_graph_matrix import run_material_function_graph_matrix
from scripts.ue_material_function_node_matrix import run_material_function_node_matrix
from scripts.ue_material_attribute_link_matrix import run_material_attribute_link_matrix
from scripts.ue_material_property_matrix import run_material_property_matrix


DEFAULT_BRIDGE_URL = "http://127.0.0.1:8765"
DEFAULT_PROJECT = Path(r"D:\Users\Administrator\Documents\Unreal Projects\我的项目2\我的项目2.uproject")
DEFAULT_UNREAL_EDITOR = Path(r"D:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor.exe")

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
        self.last_http_response: str | None = None

    def call(self, operation: str, payload: dict[str, Any]) -> dict[str, Any]:
        envelope = {
            "operation": operation,
            "request_id": str(uuid.uuid4()),
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
            raw = exc.read().decode("utf-8", errors="replace")
            self.last_http_response = raw
            self.log.write("http_error", {"operation": operation, "status": exc.code, "body": raw})
            raise RuntimeError(f"{operation} HTTP {exc.code}") from exc
        except URLError as exc:
            self.log.write("connection_error", {"operation": operation, "reason": str(exc.reason)})
            raise RuntimeError(f"{operation} connection failed: {exc.reason}") from exc

        self.last_http_response = raw
        try:
            decoded = json.loads(raw)
        except json.JSONDecodeError as exc:
            self.log.write("invalid_json", {"operation": operation, "body": raw})
            raise RuntimeError(f"{operation} returned invalid JSON") from exc
        if not isinstance(decoded, dict):
            raise RuntimeError(f"{operation} returned non-object JSON")
        self.log.write("response", {"operation": operation, "response": decoded})
        if decoded.get("ok") is False:
            raise RuntimeError(f"{operation} returned ok=false")
        return decoded


def package_path_to_disk_path(project_file: Path, package_path: str) -> Path:
    if not package_path.startswith("/Game/"):
        raise ValueError(f"Only /Game package paths map to disk: {package_path}")
    relative = package_path.removeprefix("/Game/").replace("/", os.sep)
    return project_file.parent / "Content" / relative


def wait_for_bridge(client: BridgeClient, timeout_seconds: float) -> dict[str, Any]:
    deadline = time.monotonic() + timeout_seconds
    last_error = ""
    while time.monotonic() < deadline:
        try:
            return client.call("level_current_get", {})
        except RuntimeError as exc:
            last_error = str(exc)
            time.sleep(1.0)
    raise RuntimeError(f"bridge did not become ready within {timeout_seconds:.0f}s: {last_error}")


def launch_editor(unreal_editor: Path, project_file: Path, log: JsonlLog) -> subprocess.Popen[Any]:
    if not unreal_editor.exists():
        raise FileNotFoundError(f"UnrealEditor.exe not found: {unreal_editor}")
    if not project_file.exists():
        raise FileNotFoundError(f"uproject not found: {project_file}")
    args = [str(unreal_editor), str(project_file)]
    creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    process = subprocess.Popen(args, cwd=str(project_file.parent), creationflags=creationflags)
    log.write("editor_launched", {"pid": process.pid, "args": args})
    return process


def find_unreal_log(project_file: Path) -> Path | None:
    log_dir = project_file.parent / "Saved" / "Logs"
    if not log_dir.exists():
        return None
    candidates = sorted(log_dir.glob("*.log"), key=lambda item: item.stat().st_mtime, reverse=True)
    return candidates[0] if candidates else None


def tail_text(path: Path | None, max_lines: int) -> list[str]:
    if path is None or not path.exists():
        return []
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    return lines[-max_lines:]


def run_git(args: list[str]) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    return result.stdout.strip()


def collect_disk_residue(project_file: Path, package_root: str) -> list[str]:
    disk_root = package_path_to_disk_path(project_file, package_root)
    if not disk_root.exists():
        return []
    return [str(path) for path in sorted(disk_root.rglob("*")) if path.is_file()]


def collect_failure_evidence(
    client: BridgeClient,
    project_file: Path,
    package_root: str,
    log_tail_lines: int,
) -> dict[str, Any]:
    evidence: dict[str, Any] = {
        "last_http_response": client.last_http_response,
        "asset_registry": None,
        "disk_residue": collect_disk_residue(project_file, package_root),
        "git_status": run_git(["status", "--short", "--untracked-files=all"]),
        "git_diff_stat": run_git(["diff", "--stat"]),
        "ue_log_tail": tail_text(find_unreal_log(project_file), log_tail_lines),
    }
    try:
        evidence["asset_registry"] = client.call(
            "asset_list",
            {"package_paths": [package_root], "recursive": True, "limit": 200, "format": "full", "class_names": [], "cursor": None},
        )
    except RuntimeError as exc:
        evidence["asset_registry_error"] = str(exc)
    return evidence


def run_read_only_smoke(client: BridgeClient) -> None:
    client.call("level_current_get", {})
    client.call("diagnostics_get", {"asset_path": None, "severity": "all"})
    client.call("asset_list", {"class_names": [], "package_paths": ["/Game"], "recursive": False, "limit": 25, "cursor": None, "format": "compact"})


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Direct UE Node Nexus bridge smoke/debug runner.")
    parser.add_argument("--bridge-url", default=DEFAULT_BRIDGE_URL)
    parser.add_argument("--project", type=Path, default=DEFAULT_PROJECT)
    parser.add_argument("--unreal-editor", type=Path, default=DEFAULT_UNREAL_EDITOR)
    parser.add_argument("--launch-editor", action="store_true")
    parser.add_argument("--write", action="store_true", help="Run mutating create/save/node/delete smoke matrix.")
    parser.add_argument("--asset-recreate-matrix", action="store_true", help="Run mutating delete/recreate same package asset matrix.")
    parser.add_argument("--auto-index-matrix", action="store_true", help="Run mutating Auto-Index create/rename/move/delete boundary matrix.")
    parser.add_argument("--material-property-matrix", action="store_true", help="Run mutating material node property schema/write/readback matrix.")
    parser.add_argument("--material-attribute-link-matrix", action="store_true", help="Run mutating material attributes output link validation matrix.")
    parser.add_argument("--graph-build-custom-matrix", action="store_true", help="Run mutating one-shot material graph build and Custom node matrix.")
    parser.add_argument("--level-material-matrix", action="store_true", help="Run mutating current-level mesh/material usage and component MID matrix.")
    parser.add_argument("--material-function-graph-matrix", action="store_true", help="Run mutating material function graph build and material call matrix.")
    parser.add_argument("--material-function-node-matrix", action="store_true", help="Run mutating material function node interface matrix.")
    parser.add_argument("--blueprint-write-matrix", action="store_true", help="Run mutating Blueprint graph create/connect/write/compile/readback matrix.")
    parser.add_argument("--keep-assets", action="store_true")
    parser.add_argument("--package-root", default="/Game/MCPWriteSmoke")
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--wait-timeout", type=float, default=180.0)
    parser.add_argument("--log", type=Path, default=REPO_ROOT / "logs" / f"ue_bridge_smoke_{datetime.now().strftime('%Y%m%d_%H%M%S')}.jsonl")
    parser.add_argument("--log-tail-lines", type=int, default=120)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    log = JsonlLog(args.log)
    client = BridgeClient(args.bridge_url, args.timeout, log)
    if args.launch_editor:
        launch_editor(args.unreal_editor, args.project, log)
    try:
        wait_for_bridge(client, args.wait_timeout)
        if args.asset_recreate_matrix:
            run_asset_recreate_matrix(client, args.project, args.package_root, args.keep_assets, collect_disk_residue)
        elif args.auto_index_matrix:
            run_auto_index_matrix(client, args.project, args.package_root, args.keep_assets, collect_disk_residue)
        elif args.material_attribute_link_matrix:
            run_material_attribute_link_matrix(client, args.project, args.package_root, args.keep_assets, collect_disk_residue)
        elif args.graph_build_custom_matrix:
            run_graph_build_custom_matrix(client, args.project, args.package_root, args.keep_assets, collect_disk_residue)
        elif args.level_material_matrix:
            run_level_material_matrix(client, args.project, args.package_root, args.keep_assets, collect_disk_residue)
        elif args.material_function_graph_matrix:
            run_material_function_graph_matrix(client, args.project, args.package_root, args.keep_assets, collect_disk_residue)
        elif args.material_function_node_matrix:
            run_material_function_node_matrix(client, args.project, args.package_root, args.keep_assets, collect_disk_residue)
        elif args.blueprint_write_matrix:
            run_blueprint_write_matrix(client, args.project, args.package_root, args.keep_assets, collect_disk_residue)
        elif args.material_property_matrix:
            run_material_property_matrix(client, args.project, args.package_root, args.keep_assets, collect_disk_residue)
        elif args.write:
            run_write_smoke(client, args.project, args.package_root, args.keep_assets, collect_disk_residue)
        else:
            run_read_only_smoke(client)
        mode = "asset_recreate_matrix" if args.asset_recreate_matrix else "auto_index_matrix" if args.auto_index_matrix else "material_attribute_link_matrix" if args.material_attribute_link_matrix else "graph_build_custom_matrix" if args.graph_build_custom_matrix else "level_material_matrix" if args.level_material_matrix else "material_function_graph_matrix" if args.material_function_graph_matrix else "material_function_node_matrix" if args.material_function_node_matrix else "blueprint_write_matrix" if args.blueprint_write_matrix else "material_property_matrix" if args.material_property_matrix else "write" if args.write else "read_only"
        log.write("summary", {"ok": True, "mode": mode})
        print(f"UE bridge smoke passed. Log: {args.log}")
        return 0
    except Exception as exc:
        evidence = collect_failure_evidence(client, args.project, args.package_root, args.log_tail_lines)
        log.write("summary", {"ok": False, "error": str(exc), "evidence": evidence})
        print(f"UE bridge smoke failed: {exc}", file=sys.stderr)
        print(f"Evidence log: {args.log}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
