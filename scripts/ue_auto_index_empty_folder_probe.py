from __future__ import annotations

import argparse
import json
import sys
import uuid
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from scripts.ue_bridge_smoke import BridgeClient, JsonlLog, wait_for_bridge


def call(client: BridgeClient, operation: str, payload: dict[str, object]) -> dict[str, object]:
    response = client.call(operation, payload)
    print(json.dumps(response, ensure_ascii=False, indent=2))
    return response


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bridge-url", default="http://127.0.0.1:8765")
    parser.add_argument("--folder", default="/Game/MCPEmptyFolderIndexProbe")
    parser.add_argument("--depth", type=int, default=5)
    args = parser.parse_args()

    log = JsonlLog(REPO_ROOT / "logs" / f"ue_auto_index_empty_folder_probe_{uuid.uuid4().hex}.jsonl")
    client = BridgeClient(args.bridge_url, 20.0, log)
    wait_for_bridge(client, 15.0)

    child = f"{args.folder}/Child"
    try:
        call(client, "folder_create", {"folder_path": child, "dry_run": False})
        call(client, "auto_index_rebuild", {"root_path": "/Game"})
        tree = call(
            client,
            "auto_index_tree_get",
            {"root_path": args.folder, "depth": args.depth, "limit": 50, "format": "indexed"},
        )
        text = str(tree.get("data", {}).get("text", ""))
        if args.folder not in text or child not in text:
            raise RuntimeError(f"empty folder tree missing expected nodes: {text}")
        call(client, "folder_delete", {"folder_path": args.folder, "dry_run": False, "recursive": True})
        call(client, "auto_index_rebuild", {"root_path": "/Game"})
        call(
            client,
            "auto_index_tree_get",
            {"root_path": args.folder, "depth": args.depth, "limit": 50, "format": "indexed"},
        )
        return 0
    finally:
        try:
            call(client, "folder_delete", {"folder_path": args.folder, "dry_run": False, "recursive": True})
        except Exception:
            pass
        try:
            call(client, "auto_index_rebuild", {"root_path": "/Game"})
        except Exception:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
