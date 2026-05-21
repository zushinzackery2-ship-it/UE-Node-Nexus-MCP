from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path
from typing import Any
from urllib.error import URLError
from urllib.request import Request, urlopen


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BRIDGE_URL = "http://127.0.0.1:8765"
DEFAULT_PROJECT = Path(r"D:\Users\Administrator\Documents\Unreal Projects\我的项目2\我的项目2.uproject")
DEFAULT_UNREAL_EDITOR = Path(r"D:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor.exe")


def call_bridge(base_url: str, operation: str, payload: dict[str, Any], timeout: float) -> dict[str, Any]:
    envelope = {"operation": operation, "request_id": operation, "payload": payload}
    request = Request(
        f"{base_url.rstrip('/')}/mcp",
        data=json.dumps(envelope, separators=(",", ":")).encode("utf-8"),
        headers={"Content-Type": "application/json", "Accept": "application/json"},
        method="POST",
    )
    with urlopen(request, timeout=timeout) as response:
        decoded = json.loads(response.read().decode("utf-8", errors="replace"))
    if not isinstance(decoded, dict) or decoded.get("ok") is False:
        raise RuntimeError(f"{operation} failed: {decoded}")
    return decoded


def run_step(args: list[str], timeout: float) -> None:
    result = subprocess.run(args, cwd=REPO_ROOT, text=True, timeout=timeout)
    if result.returncode != 0:
        raise RuntimeError(f"command failed {result.returncode}: {' '.join(args)}")


def bridge_ready(base_url: str, timeout: float) -> bool:
    try:
        call_bridge(base_url, "level_current_get", {}, timeout)
        return True
    except (RuntimeError, TimeoutError, URLError, OSError):
        return False


def request_editor_exit(base_url: str, timeout: float) -> None:
    if not bridge_ready(base_url, timeout):
        return
    call_bridge(base_url, "editor_save_all", {"save_content_packages": True, "save_map_packages": True}, timeout)
    call_bridge(base_url, "editor_request_exit", {"save_before_exit": True, "force": False}, timeout)


def unreal_editor_running() -> bool:
    result = subprocess.run(
        ["tasklist", "/FI", "IMAGENAME eq UnrealEditor.exe", "/FO", "CSV", "/NH"],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        check=False,
    )
    return "UnrealEditor.exe" in result.stdout


def wait_until_editor_closed(timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not unreal_editor_running():
            return
        time.sleep(0.5)
    raise RuntimeError(f"UnrealEditor.exe still running after {timeout:.0f}s")


def wait_until_bridge_ready(base_url: str, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if bridge_ready(base_url, min(2.0, timeout)):
            return
        time.sleep(0.5)
    raise RuntimeError(f"bridge not ready within {timeout:.0f}s")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Save/close UE, install UE5.5 plugin, relaunch project, run smoke checks.")
    parser.add_argument("--bridge-url", default=DEFAULT_BRIDGE_URL)
    parser.add_argument("--project", type=Path, default=DEFAULT_PROJECT)
    parser.add_argument("--unreal-editor", type=Path, default=DEFAULT_UNREAL_EDITOR)
    parser.add_argument("--timeout", type=float, default=10.0, help="Per bridge call and UE readiness timeout.")
    parser.add_argument("--build-timeout", type=float, default=180.0, help="Plugin build timeout; UBT usually exceeds bridge call timeouts.")
    parser.add_argument("--install-timeout", type=float, default=60.0)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--skip-install", action="store_true")
    parser.add_argument("--skip-launch", action="store_true")
    parser.add_argument("--skip-smoke", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    try:
        request_editor_exit(args.bridge_url, args.timeout)
        wait_until_editor_closed(args.timeout)
        if not args.skip_build:
            run_step([str(REPO_ROOT / "scripts" / "build_plugin_ue55.bat")], args.build_timeout)
        if not args.skip_install:
            run_step([str(REPO_ROOT / "scripts" / "install_ue55_plugin.bat")], args.install_timeout)
        if not args.skip_launch:
            subprocess.Popen([str(args.unreal_editor), str(args.project)], cwd=str(args.project.parent))
            wait_until_bridge_ready(args.bridge_url, args.timeout)
        if not args.skip_smoke:
            run_step([sys.executable, str(REPO_ROOT / "scripts" / "ue_bridge_smoke.py"), "--wait-timeout", str(args.timeout), "--timeout", str(args.timeout)], args.timeout)
            run_step([sys.executable, str(REPO_ROOT / "scripts" / "ue_material_expression_class_matrix.py"), "--wait-timeout", str(args.timeout), "--timeout", str(args.timeout)], args.timeout)
        print("UE5.5 plugin update workflow passed.")
        return 0
    except Exception as exc:
        print(f"UE5.5 plugin update workflow failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
